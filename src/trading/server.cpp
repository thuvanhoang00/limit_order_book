#include "order_book/trading/server.hpp"

#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <system_error>
#include <thread>
#include <type_traits>

#include "../net/poll_io.hpp"
#include "order_book/core/lfqueue.hpp"
#include "order_book/core/logger.hpp"
#include "order_book/net/tcp.hpp"
#include "order_book/net/udp.hpp"
#include "order_book/trading/journal.hpp"

namespace order_book::trading {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
struct Envelope {
    std::uint64_t connection{};
    Message message;
};
template <class T>
using Queue = LFQueue<T, 4096>;
[[noreturn]] void fail(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}
void nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) fail("nonblocking socket");
}
std::uint16_t port_of(int fd) {
    sockaddr_in address{};
    socklen_t size = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) < 0) fail("getsockname");
    return ntohs(address.sin_port);
}
struct Connection {
    explicit Connection(net::Socket descriptor) : socket(std::move(descriptor)) {}
    net::Socket socket;
    Frame input{};
    std::size_t used{}, offset{};
    std::deque<Frame> output;
    ClientId client{};
    bool closing{}, read_closed{};
    std::size_t admitted{};
    Clock::time_point progress{Clock::now()}, read_progress{Clock::now()};
};
}  // namespace
void run_exchange(const ServerConfig& config, const volatile std::sig_atomic_t& stop) {
    const auto epoch = (static_cast<std::uint64_t>(std::random_device{}()) << 32U) |
                       static_cast<std::uint64_t>(std::random_device{}() | 1U);
    MatchingEngine engine(config.instruments, epoch);
    std::unique_ptr<Journal> journal;
    if (config.journal != "-") {
        journal = std::make_unique<Journal>(config.journal, config_fingerprint(config.instruments));
        for (const auto& command : journal->recovered()) (void)engine.process(command);
    }
    std::string reason;
    if (!engine.validate_invariants(&reason))
        throw std::runtime_error("Recovery invariant: " + reason);
    Logger logger(config.log);
    auto requests = std::make_unique<Queue<Envelope>>();
    auto responses = std::make_unique<Queue<Envelope>>();
    auto updates = std::make_unique<Queue<Message>>();
    auto listener = net::listen_tcp("127.0.0.1", config.tcp_port);
    if (::listen(listener.fd(), 128) < 0) fail("listen backlog");
    nonblocking(listener.fd());
    net::MulticastPublisher publisher(config.group.c_str(), config.multicast_port,
                                      config.interface.c_str());
    std::atomic<bool> ingress_done{}, engine_done{}, fatal{};
    std::atomic<std::uint64_t> datagrams{};
    std::exception_ptr engine_error, publisher_error;
    net::Socket response_ready(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
    if (response_ready.fd() < 0) fail("eventfd response queue");
    const auto push = [&](auto& queue, const auto& value) {
        while (!queue.try_push(value)) {
            if (fatal.load(std::memory_order_acquire)) return false;
            std::this_thread::yield();
        }
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Envelope>) {
            const std::uint64_t one = 1;
            ssize_t n;
            do {
                n = ::write(response_ready.fd(), &one, sizeof(one));
            } while (n < 0 && errno == EINTR);
            if (n < 0 && errno != EAGAIN) fail("wake response queue");
        }
        return true;
    };
    const auto restored_sequence = engine.sequence();
    std::jthread engine_thread([&] {
        try {
            for (;;) {
                if (fatal.load(std::memory_order_acquire)) break;
                const bool input_finished = ingress_done.load(std::memory_order_acquire);
                Envelope envelope;
                if (!requests->try_pop(envelope)) {
                    if (input_finished) break;
                    if (fatal.load(std::memory_order_acquire)) break;
                    std::this_thread::sleep_for(50us);
                    continue;
                }
                const auto& c = envelope.message;
                if (c.action == Action::Snapshot) {
                    for (const auto& m : engine.snapshot())
                        if (!push(*responses, Envelope{envelope.connection, m})) break;
                    continue;
                }
                if (journal && c.action != Action::Status) journal->append(c);
                const auto batch = engine.process(c);
                (void)logger.try_log({.level = LogLevel::Info,
                                      .event = LogEvent::TradingCommand,
                                      .sequence = c.request_id,
                                      .instrument_id = c.instrument_id,
                                      .order_id = c.order_id,
                                      .price_ticks = c.price,
                                      .quantity = c.quantity});
                for (const auto& m : batch.reports) {
                    if (!push(*responses,
                              Envelope{m.client_id == c.client_id ? envelope.connection : 0, m}))
                        break;
                }
                for (const auto& m : batch.updates)
                    if (!push(*updates, m)) break;
            }
            if (!engine.validate_invariants(&reason))
                throw std::runtime_error("Engine invariant: " + reason);
        } catch (...) {
            engine_error = std::current_exception();
            fatal.store(true, std::memory_order_release);
        }
        engine_done.store(true, std::memory_order_release);
    });
    std::jthread publisher_thread;
    try {
        publisher_thread = std::jthread([&] {
            try {
                auto heartbeat_at = Clock::now();
                Message heartbeat;
                heartbeat.kind = Kind::Heartbeat;
                heartbeat.epoch = epoch;
                heartbeat.sequence = restored_sequence;
                for (;;) {
                    const bool matching_finished = engine_done.load(std::memory_order_acquire);
                    Message m;
                    if (updates->try_pop(m)) {
                        publisher.send(encode(m));
                        ++datagrams;
                        heartbeat.sequence = m.sequence;
                    } else if (matching_finished)
                        break;
                    else
                        std::this_thread::sleep_for(50us);
                    if (Clock::now() >= heartbeat_at) {
                        publisher.send(encode(heartbeat));
                        ++datagrams;
                        heartbeat_at = Clock::now() + 100ms;
                    }
                    if (fatal.load(std::memory_order_acquire)) break;
                }
            } catch (...) {
                publisher_error = std::current_exception();
                fatal.store(true, std::memory_order_release);
            }
        });
    } catch (...) {
        fatal.store(true, std::memory_order_release);
        ingress_done.store(true, std::memory_order_release);
        engine_thread.join();
        throw;
    }
    struct WorkerGuard {
        std::atomic<bool>& abort;
        std::atomic<bool>& ingress;
        std::jthread& engine;
        std::jthread& publisher;
        ~WorkerGuard() {
            abort.store(true, std::memory_order_release);
            ingress.store(true, std::memory_order_release);
            if (engine.joinable()) engine.join();
            if (publisher.joinable()) publisher.join();
        }
    } guard{fatal, ingress_done, engine_thread, publisher_thread};
    std::map<std::uint64_t, Connection> connections;
    std::map<ClientId, std::uint64_t> routes;
    std::uint64_t next_connection{1}, accepted{}, slow_clients{}, malformed{};
    bool draining{};
    auto drain_until = Clock::time_point::max();
    const auto append = [&](Connection& c, const Message& m) {
        if (c.output.size() >= 4096) {
            c.closing = true;
            c.output.clear();
            ++slow_clients;
            return;
        }
        if (c.output.empty()) c.progress = Clock::now();
        if (((m.kind == Kind::Report &&
              (m.report == ReportType::Complete || m.report == ReportType::Status)) ||
             m.kind == Kind::SnapshotEnd) &&
            c.admitted)
            --c.admitted;
        c.output.push_back(encode(m));
    };
    std::cout << "READY tcp_port=" << port_of(listener.fd()) << " md_port=" << config.multicast_port
              << " group=" << config.group << " epoch=" << epoch
              << " sequence=" << restored_sequence << std::endl;
    std::exception_ptr io_error;
    try {
        while (true) {
            if (!draining && (stop != 0 || fatal.load(std::memory_order_acquire))) {
                draining = true;
                ingress_done.store(true, std::memory_order_release);
                drain_until = Clock::now() + 5s;
            }
            const bool matching_finished = engine_done.load(std::memory_order_acquire);
            Envelope response;
            bool responses_empty = true;
            for (unsigned i = 0; i != 4096 && responses->try_pop(response); ++i) {
                responses_empty = false;
                auto target = response.connection;
                if (target == 0) {
                    const auto route = routes.find(response.message.client_id);
                    if (route == routes.end()) continue;
                    target = route->second;
                }
                const auto it = connections.find(target);
                if (it != connections.end() && !it->second.closing)
                    append(it->second, response.message);
            }
            if (draining && matching_finished && responses_empty) {
                const bool pending =
                    std::any_of(connections.begin(), connections.end(),
                                [](const auto& pair) { return !pair.second.output.empty(); });
                if (!pending) break;
            }
            if (draining && Clock::now() >= drain_until) {
                fatal.store(true, std::memory_order_release);
                break;
            }
            std::vector<pollfd> polls{{listener.fd(), static_cast<short>(draining ? 0 : POLLIN), 0},
                                      {response_ready.fd(), POLLIN, 0}};
            std::vector<std::uint64_t> ids{0, 0};
            for (auto& [id, c] : connections) {
                short events = 0;
                if (!draining && !c.closing && !c.read_closed) events |= POLLIN;
                if (!c.output.empty()) events |= POLLOUT;
                polls.push_back({c.socket.fd(), events, 0});
                ids.push_back(id);
            }
            const auto polled = ::poll(polls.data(), polls.size(), 10);
            if (polled < 0) {
                if (errno == EINTR) continue;
                fail("poll order server");
            }
            if ((polls[0].revents & POLLIN) != 0 && !draining) {
                for (unsigned i = 0; i != 64; ++i) {
                    const int fd =
                        ::accept4(listener.fd(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (fd < 0) {
                        if (errno == EINTR) continue;
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        fail("accept order client");
                    }
                    net::Socket socket(fd);
                    if (connections.size() >= 64) continue;
                    const int enabled = 1;
                    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) < 0)
                        fail("TCP_NODELAY");
                    connections.try_emplace(next_connection++, std::move(socket));
                    ++accepted;
                }
            }
            if ((polls[1].revents & POLLIN) != 0) {
                std::uint64_t count{};
                const auto drained = ::read(response_ready.fd(), &count, sizeof(count));
                if (drained < 0 && errno != EAGAIN && errno != EINTR)
                    fail("read response notification");
            }
            std::vector<std::uint64_t> remove;
            for (std::size_t i = 2; i != polls.size(); ++i) {
                const auto id = ids[i];
                auto& c = connections.at(id);
                const auto events = polls[i].revents;
                bool dead = (events & (POLLERR | POLLNVAL)) != 0;
                if ((events & POLLOUT) != 0 && !c.output.empty()) {
                    const auto before_size = c.output.size();
                    const auto before_offset = c.offset;
                    try {
                        net::detail::flush(c.socket.fd(), c.output, c.offset);
                    } catch (const std::runtime_error&) {
                        dead = true;
                    }
                    if (c.output.size() != before_size || c.offset != before_offset)
                        c.progress = Clock::now();
                }
                if (!draining && !c.closing && !c.read_closed &&
                    ((events & (POLLIN | POLLHUP)) != 0 || c.used == frame_size)) {
                    if (c.used < frame_size) {
                        const auto n =
                            ::recv(c.socket.fd(), c.input.data() + c.used, frame_size - c.used, 0);
                        if (n > 0) {
                            if (!c.used) c.read_progress = Clock::now();
                            c.used += static_cast<std::size_t>(n);
                        } else if (n == 0)
                            c.read_closed = true;
                        else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                            dead = true;
                    }
                    if (c.used == frame_size && !dead) {
                        const auto m = decode(c.input);
                        if (!m || m->kind != Kind::Command ||
                            (m->action != Action::Snapshot && m->client_id == 0) ||
                            (c.client != 0 && m->client_id != c.client)) {
                            dead = true;
                            ++malformed;
                        } else {
                            bool allowed = true;
                            if (c.client == 0 && m->action != Action::Snapshot) {
                                if (routes.contains(m->client_id)) {
                                    auto busy = *m;
                                    busy.kind = Kind::Report;
                                    busy.report = ReportType::Rejected;
                                    busy.reject = Reject::SessionBusy;
                                    busy.epoch = epoch;
                                    append(c, busy);
                                    c.closing = true;
                                    allowed = false;
                                } else {
                                    c.client = m->client_id;
                                    routes.emplace(c.client, id);
                                }
                            }
                            if (allowed && requests->try_push({id, *m})) {
                                c.used = 0;
                                ++c.admitted;
                            }
                        }
                    }
                }
                if ((c.used != 0 && c.used != frame_size && Clock::now() - c.read_progress > 5s) ||
                    (!c.output.empty() && Clock::now() - c.progress > 5s)) {
                    dead = true;
                    ++slow_clients;
                }
                if ((c.closing || (c.read_closed && !c.admitted)) && c.output.empty()) dead = true;
                if (dead) remove.push_back(id);
            }
            for (auto id : remove) {
                routes.erase(connections.at(id).client);
                connections.erase(id);
            }
        }
    } catch (...) {
        io_error = std::current_exception();
        fatal.store(true, std::memory_order_release);
    }
    ingress_done.store(true, std::memory_order_release);
    engine_thread.join();
    publisher_thread.join();
    logger.close();
    const auto& stats = engine.stats();
    const std::string metrics = "{\"commands\":" + std::to_string(stats.commands) +
                                ",\"rejected\":" + std::to_string(stats.rejected) +
                                ",\"trades\":" + std::to_string(stats.trades) +
                                ",\"volume\":" + std::to_string(stats.volume) +
                                ",\"market_sequence\":" + std::to_string(engine.sequence()) +
                                ",\"datagrams\":" + std::to_string(datagrams.load()) +
                                ",\"connections\":" + std::to_string(accepted) +
                                ",\"slow_clients\":" + std::to_string(slow_clients) +
                                ",\"malformed\":" + std::to_string(malformed) + "}";
    std::cout << metrics << std::endl;
    if (!config.metrics.empty()) {
        std::ofstream output(config.metrics);
        output << metrics << '\n';
        if (!output) throw std::runtime_error("Cannot write exchange metrics");
    }
    if (io_error) std::rethrow_exception(io_error);
    if (engine_error) std::rethrow_exception(engine_error);
    if (publisher_error) std::rethrow_exception(publisher_error);
    if (fatal.load() || logger.has_file_write_error() || logger.format_failed_count() != 0)
        throw std::runtime_error("Exchange stopped with an I/O, drain or logging failure");
}
}  // namespace order_book::trading
