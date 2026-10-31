#include "order_book/exchange/runtime.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>

#include "../net/poll_io.hpp"
#include "audit.hpp"
#include "order_book/exchange/market_data.hpp"
#include "order_book/exchange/protocol.hpp"
#include "order_book/net/tcp.hpp"
#include "order_book/net/udp.hpp"

namespace order_book::exchange {
namespace {
using Clock = std::chrono::steady_clock;
std::uint16_t port_of(int fd) {
    sockaddr_in address{};
    socklen_t size = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) < 0)
        throw std::system_error(errno, std::generic_category(), "getsockname exchange");
    return ntohs(address.sin_port);
}
}  // namespace
struct ExchangeServer::Impl {
    explicit Impl(ExchangeConfig value) : config(std::move(value)) {
        if (config.snapshot_interval.count() <= 0 || config.io_timeout.count() <= 0 ||
            !config.incremental_port || !config.snapshot_port ||
            config.incremental_port == config.snapshot_port || config.instruments.empty())
            throw std::invalid_argument("Invalid exchange configuration");
        for (const auto& [id, rules] : config.instruments) engine.add_instrument(id, rules);
        if (!config.log_prefix.empty()) {
            matcher_log = std::make_unique<Logger>(config.log_prefix + ".matcher.log");
            publisher_log = std::make_unique<Logger>(config.log_prefix + ".publisher.log");
        }
        listener = net::listen_tcp(config.address.c_str(), config.tcp_port);
        if (::listen(listener.fd(), 128) < 0)
            throw std::system_error(errno, std::generic_category(), "listen exchange");
        net::detail::nonblocking(listener.fd());
        port = port_of(listener.fd());
        epoch = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count());
        incremental = std::make_unique<net::MulticastPublisher>(
            config.group.c_str(), config.incremental_port, config.interface_ipv4.c_str());
        snapshots = std::make_unique<net::MulticastPublisher>(
            config.group.c_str(), config.snapshot_port, config.interface_ipv4.c_str());
    }
    ExchangeConfig config;
    MatchingEngine engine;
    net::Socket listener;
    std::unique_ptr<net::MulticastPublisher> incremental, snapshots;
    std::unique_ptr<Logger> matcher_log, publisher_log;
    std::uint16_t port{};
    std::uint64_t epoch{};
    std::unique_ptr<RequestQueue> requests{std::make_unique<RequestQueue>()};
    std::unique_ptr<ReportQueue> responses{std::make_unique<ReportQueue>()};
    std::unique_ptr<MarketQueue> updates{std::make_unique<MarketQueue>()};
    std::atomic<bool> stopping{}, aborting{}, ingress_done{}, matcher_done{};
    std::thread io_thread, matcher_thread, publisher_thread;
    std::mutex error_mutex;
    std::string failure;
    bool started{};
    std::atomic<std::uint64_t> request_count{}, report_count{}, update_count{}, snapshot_count{},
        disconnect_count{}, unroutable_count{};

    void fail(const std::exception& exception) {
        {
            std::lock_guard lock(error_mutex);
            if (failure.empty()) failure = exception.what();
        }
        aborting.store(true);
        stopping.store(true);
    }
    template <class Queue, class Message>
    bool push(Queue& queue, const Message& message) {
        while (!queue.try_push(message)) {
            if (aborting.load()) return false;
            std::this_thread::yield();
        }
        return true;
    }
    void match() {
        try {
            for (;;) {
                Request request;
                if (!requests->try_pop(request)) {
                    if (aborting.load()) break;
                    if (ingress_done.load()) {
                        if (!requests->try_pop(request)) break;
                    } else {
                        std::this_thread::yield();
                        continue;
                    }
                }
                detail::audit(matcher_log.get(), LogEvent::ExchangeRequest, request);
                auto result = engine.process(request);
                for (const auto& u : result.updates)
                    if (!push(*updates, u)) break;
                for (const auto& r : result.reports) {
                    detail::audit(matcher_log.get(), LogEvent::ExchangeReport, r);
                    if (!push(*responses, r)) break;
                }
                if (aborting.load()) break;
            }
            if (!aborting.load() && !engine.validate_invariants())
                throw std::runtime_error("Matching invariants failed at shutdown");
        } catch (const std::exception& exception) {
            fail(exception);
        }
        matcher_done.store(true);
    }
    void publish() {
        try {
            FeedBook shadow;
            Sequence last = 0;
            std::uint64_t cycle = 0;
            auto next_snapshot = Clock::now();
            auto next_heartbeat = Clock::now();
            for (;;) {
                bool work = false;
                MarketUpdate u;
                for (unsigned budget = 0; budget < 1024 && updates->try_pop(u); ++budget) {
                    if (u.sequence != last + 1 || !shadow.apply(u))
                        throw std::runtime_error(
                            "Publisher book or sequence rejected matching update");
                    u.session = epoch;
                    detail::audit(publisher_log.get(), LogEvent::ExchangeMarket, u);
                    incremental->send(wire::encode(u));
                    last = u.sequence;
                    update_count.fetch_add(1);
                    work = true;
                }
                const auto now = Clock::now();
                if (now >= next_snapshot) {
                    for (const auto& row : snapshot_cycle(shadow, epoch, ++cycle, last))
                        snapshots->send(wire::encode(row));
                    snapshot_count.fetch_add(1);
                    next_snapshot = now + config.snapshot_interval;
                }
                if (now >= next_heartbeat) {
                    MarketUpdate heartbeat;
                    heartbeat.session = epoch;
                    heartbeat.sequence = heartbeat.watermark = last;
                    incremental->send(wire::encode(heartbeat));
                    next_heartbeat = now + std::chrono::milliseconds(100);
                }
                if (aborting.load()) break;
                if (!work && matcher_done.load()) {
                    // Recheck after observing completion to cover the final enqueue.
                    if (!updates->try_pop(u)) break;
                    if (u.sequence != last + 1 || !shadow.apply(u))
                        throw std::runtime_error("Publisher final update rejected");
                    u.session = epoch;
                    detail::audit(publisher_log.get(), LogEvent::ExchangeMarket, u);
                    incremental->send(wire::encode(u));
                    last = u.sequence;
                    update_count.fetch_add(1);
                    work = true;
                }
                if (!work) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            // A final complete cycle makes a drained shutdown observable.
            if (!aborting.load()) {
                for (const auto& row : snapshot_cycle(shadow, epoch, ++cycle, last))
                    snapshots->send(wire::encode(row));
                snapshot_count.fetch_add(1);
            }
        } catch (const std::exception& exception) {
            fail(exception);
        }
    }
    struct Peer {
        explicit Peer(net::Socket value) : socket(std::move(value)) {}
        net::Socket socket;
        ClientId client{};
        Sequence received{}, sent_sequence{};
        std::size_t pending_requests{};
        bool read_closed{};
        wire::RequestFrame input{};
        std::size_t used{}, sent{};
        std::deque<wire::ReportFrame> output;
        Clock::time_point frame_started{Clock::now()}, output_started{Clock::now()};
    };
    void io() {
        try {
            std::map<int, Peer> peers;
            std::map<ClientId, int> routes;
            std::set<ClientId> seen;
            std::optional<Request> waiting;
            auto stop_started = Clock::time_point::max();
            const auto drop = [&](int fd) {
                const auto it = peers.find(fd);
                if (it != peers.end()) {
                    if (it->second.client) routes.erase(it->second.client);
                    peers.erase(it);
                    disconnect_count.fetch_add(1);
                }
            };
            const auto deliver = [&](Report report) {
                const auto route = routes.find(report.client_id);
                if (route == routes.end()) {
                    unroutable_count.fetch_add(1);
                    return;
                }
                auto& peer = peers.at(route->second);
                if (report.type == ReportType::Done && peer.pending_requests)
                    --peer.pending_requests;
                if (peer.output.size() >= 4096) {
                    drop(route->second);
                    unroutable_count.fetch_add(1);
                    return;
                }
                report.sequence = ++peer.sent_sequence;
                report.session = epoch;
                if (peer.output.empty()) peer.output_started = Clock::now();
                peer.output.push_back(wire::encode(report));
                report_count.fetch_add(1);
            };
            for (;;) {
                const auto now = Clock::now();
                if (aborting.load()) break;
                if (waiting && requests->try_push(*waiting)) {
                    request_count.fetch_add(1);
                    waiting.reset();
                }
                if (stopping.load() && stop_started == Clock::time_point::max()) stop_started = now;
                if (stopping.load() && !waiting) ingress_done.store(true);
                // Drain reports before reading more commands. Each client has a
                // bounded output backlog; a slow/disconnected peer cannot stall others.
                Report report;
                for (unsigned budget = 0; budget < 4096 && responses->try_pop(report); ++budget)
                    deliver(report);
                bool pending_output = false;
                for (const auto& [fd, peer] : peers) {
                    (void)fd;
                    pending_output |= !peer.output.empty();
                }
                if (ingress_done.load() && matcher_done.load() && !pending_output) {
                    // matcher_done is published after the last report enqueue.
                    // Recheck the queue after observing it before concluding drain.
                    if (!responses->try_pop(report)) break;
                    deliver(report);
                }
                if (stop_started != Clock::time_point::max() &&
                    now - stop_started > config.io_timeout) {
                    if (!matcher_done.load()) {
                        // Keep draining the queue even if peers exceeded the I/O
                        // deadline, allowing accepted commands to finish.
                        while (!peers.empty()) drop(peers.begin()->first);
                    } else
                        break;
                }
                std::vector<pollfd> fds;
                fds.push_back({listener.fd(), static_cast<short>(stopping.load() ? 0 : POLLIN), 0});
                for (const auto& [fd, peer] : peers) {
                    short events = (!stopping.load() && !waiting && !peer.read_closed) ? POLLIN : 0;
                    if (!peer.output.empty()) events = static_cast<short>(events | POLLOUT);
                    fds.push_back({fd, events, 0});
                }
                const auto polled = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), 2);
                if (polled < 0) {
                    if (errno == EINTR) continue;
                    throw std::system_error(errno, std::generic_category(), "poll exchange");
                }
                if (fds[0].revents & POLLIN) {
                    const auto fd =
                        ::accept4(listener.fd(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (fd >= 0) {
                        net::Socket accepted(fd);
                        if (peers.size() < 256) {
                            net::detail::no_delay(fd);
                            peers.try_emplace(fd, std::move(accepted));
                        }
                    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                        throw std::system_error(errno, std::generic_category(), "accept exchange");
                }
                for (std::size_t i = 1; i < fds.size(); ++i) {
                    const auto it = peers.find(fds[i].fd);
                    if (it == peers.end()) continue;
                    auto& peer = it->second;
                    bool closed = false;
                    try {
                        if (fds[i].revents & POLLOUT)
                            net::detail::flush(peer.socket.fd(), peer.output, peer.sent);
                        if ((fds[i].revents & POLLIN) && !stopping.load() && !waiting) {
                            for (unsigned budget = 0; budget < 16; ++budget) {
                                if (!peer.used) peer.frame_started = Clock::now();
                                const auto result = net::detail::read_frame(peer.socket.fd(),
                                                                            peer.input, peer.used);
                                if (result == net::detail::Read::Again) break;
                                if (result == net::detail::Read::Eof) {
                                    peer.read_closed = true;
                                    break;
                                }
                                const auto decoded = wire::decode(peer.input);
                                if (!decoded || !decoded->client_id ||
                                    decoded->sequence != peer.received + 1 ||
                                    (peer.client && decoded->client_id != peer.client)) {
                                    closed = true;
                                    break;
                                }
                                if (!peer.client) {
                                    if (!seen.insert(decoded->client_id).second) {
                                        closed = true;
                                        break;
                                    }
                                    peer.client = decoded->client_id;
                                    routes.emplace(peer.client, peer.socket.fd());
                                }
                                ++peer.received;
                                ++peer.pending_requests;
                                if (requests->try_push(*decoded))
                                    request_count.fetch_add(1);
                                else {
                                    waiting = *decoded;
                                    break;
                                }
                            }
                        }
                        if ((peer.used && now - peer.frame_started > config.io_timeout) ||
                            (!peer.output.empty() && now - peer.output_started > config.io_timeout))
                            closed = true;
                        if (peer.read_closed && !peer.pending_requests && peer.output.empty())
                            closed = true;
                    } catch (const std::system_error&) {
                        closed = true;
                    } catch (const std::runtime_error&) {
                        closed = true;
                    }
                    if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) closed = true;
                    if (closed) drop(fds[i].fd);
                }
            }
        } catch (const std::exception& exception) {
            fail(exception);
        }
        ingress_done.store(true);
    }
};
ExchangeServer::ExchangeServer(ExchangeConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
ExchangeServer::~ExchangeServer() { stop(); }
void ExchangeServer::start() {
    if (impl_->started) throw std::logic_error("Exchange can only be started once");
    impl_->started = true;
    try {
        impl_->publisher_thread = std::thread([this] { impl_->publish(); });
        impl_->matcher_thread = std::thread([this] { impl_->match(); });
        impl_->io_thread = std::thread([this] { impl_->io(); });
    } catch (...) {
        impl_->aborting.store(true);
        impl_->stopping.store(true);
        stop();
        throw;
    }
}
void ExchangeServer::stop() noexcept {
    impl_->stopping.store(true);
    if (impl_->io_thread.joinable()) impl_->io_thread.join();
    if (impl_->matcher_thread.joinable()) impl_->matcher_thread.join();
    if (impl_->publisher_thread.joinable()) impl_->publisher_thread.join();
    if (impl_->matcher_log) impl_->matcher_log->close();
    if (impl_->publisher_log) impl_->publisher_log->close();
    if (detail::log_failed(impl_->matcher_log.get()) ||
        detail::log_failed(impl_->publisher_log.get()))
        impl_->fail(std::runtime_error("Exchange asynchronous log write/format failure"));
}
std::uint16_t ExchangeServer::tcp_port() const noexcept { return impl_->port; }
std::uint64_t ExchangeServer::session() const noexcept { return impl_->epoch; }
bool ExchangeServer::failed() const noexcept { return impl_->aborting.load(); }
std::string ExchangeServer::error() const {
    std::lock_guard lock(impl_->error_mutex);
    return impl_->failure;
}
ExchangeStats ExchangeServer::stats() const noexcept {
    return {impl_->request_count.load(),
            impl_->report_count.load(),
            impl_->update_count.load(),
            impl_->snapshot_count.load(),
            impl_->disconnect_count.load(),
            impl_->unroutable_count.load(),
            (impl_->matcher_log ? impl_->matcher_log->dropped_count() : 0) +
                (impl_->publisher_log ? impl_->publisher_log->dropped_count() : 0)};
}
}  // namespace order_book::exchange
