#include "order_book/trading/client.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>

#include "../net/poll_io.hpp"
#include "order_book/core/lfqueue.hpp"

namespace order_book::trading {
namespace {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
void timeout(const net::Socket& socket) {
    const timeval value{2, 0};
    for (auto option : {SO_RCVTIMEO, SO_SNDTIMEO})
        if (::setsockopt(socket.fd(), SOL_SOCKET, option, &value, sizeof(value)) < 0)
            throw std::system_error(errno, std::generic_category(), "Client socket timeout");
}
Message receive(const net::Socket& socket) {
    Frame frame{};
    if (!net::receive_exact(socket, frame))
        throw std::runtime_error("Gateway disconnected; order outcome requires reconciliation");
    const auto m = decode(frame);
    if (!m) throw std::runtime_error("Malformed gateway frame");
    return *m;
}
}  // namespace
struct TradingClient::Transport {
    using Queue = LFQueue<Message, 4096>;
    std::unique_ptr<Queue> requests{std::make_unique<Queue>()};
    std::unique_ptr<Queue> reports{std::make_unique<Queue>()};
    std::unique_ptr<Queue> market{std::make_unique<Queue>()};
    std::atomic<bool> stopped{};
    std::mutex mutex;
    std::exception_ptr error;
    std::thread gateway_thread, subscriber_thread;
    void check() {
        std::lock_guard lock(mutex);
        if (error) std::rethrow_exception(error);
    }
    void fail() {
        {
            std::lock_guard lock(mutex);
            if (!error) error = std::current_exception();
        }
        stopped.store(true);
    }
    bool push(Queue& queue, const Message& m) {
        while (!queue.try_push(m)) {
            if (stopped.load()) return false;
            std::this_thread::yield();
        }
        return true;
    }
    void stop() noexcept {
        stopped.store(true);
        if (gateway_thread.joinable()) gateway_thread.join();
        if (subscriber_thread.joinable()) subscriber_thread.join();
    }
    ~Transport() { stop(); }
    Transport(net::Socket& gateway, net::MulticastSubscriber& subscriber) {
        net::detail::nonblocking(gateway.fd());
        try {
            gateway_thread = std::thread([this, &gateway] {
                try {
                    Frame input{};
                    std::size_t used = 0, sent = 0;
                    std::deque<Frame> output;
                    auto read_started = Clock::now(), write_started = Clock::now();
                    while (!stopped.load()) {
                        Message request;
                        while (output.size() < 4096 && requests->try_pop(request)) {
                            if (output.empty()) write_started = Clock::now();
                            output.push_back(encode(request));
                        }
                        pollfd descriptor{
                            gateway.fd(),
                            static_cast<short>(POLLIN | (output.empty() ? 0 : POLLOUT)), 0};
                        const auto ready = ::poll(&descriptor, 1, 5);
                        if (ready < 0) {
                            if (errno == EINTR) continue;
                            throw std::system_error(errno, std::generic_category(),
                                                    "poll order gateway");
                        }
                        if (descriptor.revents & POLLOUT)
                            net::detail::flush(gateway.fd(), output, sent);
                        if (descriptor.revents & POLLIN) {
                            for (unsigned budget = 0; budget < 64; ++budget) {
                                if (!used) read_started = Clock::now();
                                const auto result =
                                    net::detail::read_frame(gateway.fd(), input, used);
                                if (result == net::detail::Read::Again) break;
                                if (result == net::detail::Read::Eof)
                                    throw std::runtime_error(
                                        "Gateway disconnected; order outcome requires "
                                        "reconciliation");
                                const auto message = decode(input);
                                if (!message || message->kind != Kind::Report)
                                    throw std::runtime_error("Malformed gateway report");
                                if (!push(*reports, *message)) break;
                            }
                        }
                        if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
                            throw std::runtime_error(
                                "Gateway closed; order outcome requires reconciliation");
                        const auto now = Clock::now();
                        if ((used && now - read_started > 2s) ||
                            (!output.empty() && now - write_started > 3s))
                            throw std::runtime_error("Gateway frame deadline expired");
                    }
                } catch (...) {
                    fail();
                }
            });
            subscriber_thread = std::thread([this, &subscriber] {
                try {
                    while (!stopped.load()) {
                        pollfd descriptor{subscriber.fd(), POLLIN, 0};
                        const auto ready = ::poll(&descriptor, 1, 5);
                        if (ready < 0) {
                            if (errno == EINTR) continue;
                            throw std::system_error(errno, std::generic_category(),
                                                    "poll multicast subscriber");
                        }
                        if (!(descriptor.revents & POLLIN)) continue;
                        Frame frame{};
                        const auto packet = subscriber.receive(frame);
                        if (!packet) continue;
                        const auto decoded = !packet->truncated && packet->size == frame.size()
                                                 ? decode(frame)
                                                 : std::nullopt;
                        Message message;
                        if (decoded &&
                            (decoded->kind == Kind::Delta || decoded->kind == Kind::Heartbeat))
                            message = *decoded;
                        else
                            message.kind =
                                static_cast<Kind>(0);  // Strategy thread invalidates and recovers.
                        if (!push(*market, message)) break;
                    }
                } catch (...) {
                    fail();
                }
            });
        } catch (...) {
            stop();
            throw;
        }
    }
    void send(const Message& command) {
        const auto deadline = Clock::now() + 3s;
        while (!requests->try_push(command)) {
            check();
            if (Clock::now() > deadline || stopped.load())
                throw std::runtime_error("Gateway command queue deadline");
            std::this_thread::yield();
        }
        check();
    }
};
TradingClient::TradingClient(ClientConfig config)
    : config_(std::move(config)),
      subscriber_(config_.group.c_str(), config_.multicast_port, config_.interface.c_str(), 1ms),
      gateway_(net::connect_tcp("127.0.0.1", config_.tcp_port)) {
    if (config_.client_id == 0 || config_.max_loss < 0)
        throw std::invalid_argument("Invalid trading client configuration");
    timeout(gateway_);
    Message hello;
    hello.action = Action::Status;
    hello.client_id = config_.client_id;
    net::send_all(gateway_, encode(hello));
    const auto status = receive(gateway_);
    if (status.kind != Kind::Report || status.report != ReportType::Status)
        throw std::runtime_error("Cannot open client session: " +
                                 std::string(to_string(status.reject)));
    // An existing account can have unobserved fills. Never silently reset its local
    // risk state. Raw gateways may reconcile explicitly; the sample app needs a new ID.
    if (status.request_id != 0)
        throw std::runtime_error(
            "Client ID has history; use a fresh ID or reconcile positions before trading");
    request_id_ = status.request_id;
    epoch_ = status.epoch;
    transport_ = std::make_unique<Transport>(gateway_, subscriber_);
    recover();
}
void TradingClient::recover() {
    market_.invalidate();
    const auto deadline = Clock::now() + 3s;
    while (Clock::now() < deadline) {
        auto socket = net::connect_tcp("127.0.0.1", config_.tcp_port);
        timeout(socket);
        Message query;
        query.action = Action::Snapshot;
        net::send_all(socket, encode(query));
        std::vector<Message> snapshot;
        bool complete = false;
        while (Clock::now() < deadline) {
            auto m = receive(socket);
            snapshot.push_back(m);
            if (snapshot.size() > 1'000'000)
                throw std::runtime_error("Snapshot exceeds client resource bound");
            if (m.kind != Kind::SnapshotEnd) continue;
            if (m.epoch != epoch_)
                throw std::runtime_error(
                    "Exchange restarted; reconcile private order state before trading");
            market_.restore(snapshot);
            features_.reset();
            for (const auto& row : snapshot)
                if (row.kind == Kind::SnapshotOrder)
                    features_.refresh(row.instrument_id, market_.book(row.instrument_id));
            complete = true;
            break;
        }
        if (!complete) break;
        // Workers keep receiving during snapshot transfer. Verify the buffered
        // suffix before exposing a synchronized book to the next risk check.
        bool valid = true;
        Message buffered;
        unsigned budget = 0;
        while (!closed_ && budget++ < 4096 && transport_->market->try_pop(buffered)) {
            transport_->check();
            const bool fresh = buffered.kind == Kind::Delta && buffered.epoch == market_.epoch() &&
                               buffered.sequence > market_.sequence();
            if (fresh) features_.before(buffered, market_.book(buffered.instrument_id));
            if (!market_.consume(buffered)) {
                valid = false;
                break;
            }
            if (fresh) features_.after(buffered, market_.book(buffered.instrument_id));
        }
        if (budget > 4096) valid = false;
        if (valid) {
            last_market_ = Clock::now();
            ++recoveries_;
            return;
        }
        market_.invalidate();
    }
    throw std::runtime_error("Snapshot/suffix recovery deadline expired");
}
void TradingClient::drain_market() {
    const auto until = Clock::now() + 5ms;
    bool needs_recovery{};
    while (Clock::now() < until) {
        transport_->check();
        Message message;
        if (!transport_->market->try_pop(message)) break;
        const auto* before = market_.book(message.instrument_id);
        const bool fresh = message.kind == Kind::Delta && message.epoch == market_.epoch() &&
                           message.sequence > market_.sequence();
        if (fresh) features_.before(message, before);
        if (!market_.consume(message)) {
            needs_recovery = true;
            break;
        }
        if (fresh) features_.after(message, market_.book(message.instrument_id));
        last_market_ = Clock::now();
    }
    if (needs_recovery || Clock::now() - last_market_ > 500ms) recover();
}
std::optional<Message> TradingClient::receive_report(std::chrono::milliseconds wait) {
    transport_->check();
    Message m;
    const auto deadline = Clock::now() + wait;
    while (!transport_->reports->try_pop(m)) {
        transport_->check();
        if (Clock::now() >= deadline) return std::nullopt;
        std::this_thread::sleep_for(1ms);
    }
    if (m.kind != Kind::Report || m.client_id != config_.client_id || m.epoch != epoch_)
        throw std::runtime_error("Unexpected gateway/session report");
    if (manager_.consume(m) && handler_) handler_(m);
    return m;
}
void TradingClient::poll(std::chrono::milliseconds duration) {
    const auto deadline = Clock::now() + duration;
    do {
        for (unsigned budget = 0; budget < 4096 && receive_report(0ms); ++budget) {
        }
        drain_market();
        if (Clock::now() < deadline) std::this_thread::sleep_for(1ms);
    } while (Clock::now() < deadline);
}
std::vector<Message> TradingClient::submit(Message command) {
    const bool cancellation =
        command.action == Action::Cancel || command.action == Action::CancelAll;
    if (cancellation) {
        // Risk reduction must not wait for a broken public snapshot/feed channel.
        for (unsigned budget = 0; budget < 4096 && receive_report(0ms); ++budget) {
        }
    } else
        poll(0ms);
    const auto risk = manager_.check(command, config_.limits, market_, config_.max_loss);
    if (risk != Reject::None)
        throw std::runtime_error("Local risk check: " + std::string(to_string(risk)));
    command.kind = Kind::Command;
    command.client_id = config_.client_id;
    command.request_id = ++request_id_;
    if (closed_) throw std::logic_error("Trading client is closed");
    transport_->send(command);
    const auto deadline = Clock::now() + 3s;
    std::vector<Message> result;
    while (Clock::now() < deadline) {
        if (auto m = receive_report(10ms)) {
            if (m->request_id == command.request_id) {
                result.push_back(*m);
                if (m->report == ReportType::Complete) return result;
            }
        }
        if (!cancellation) drain_market();
    }
    throw std::runtime_error("Command deadline; outcome requires reconciliation before retry");
}
TradingClient::~TradingClient() = default;
void TradingClient::close(bool cancel_orders) {
    if (closed_) return;
    if (cancel_orders && !manager_.orders().empty()) {
        Message cancel;
        cancel.action = Action::CancelAll;
        (void)submit(cancel);
    }
    transport_->check();
    transport_->stop();
    gateway_ = net::Socket{};
    closed_ = true;
}
}  // namespace order_book::trading
