#include "order_book/trading/runtime.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "../exchange/audit.hpp"
#include "../net/poll_io.hpp"
#include "order_book/exchange/protocol.hpp"
#include "order_book/net/tcp.hpp"
#include "order_book/net/udp.hpp"

namespace order_book::trading {
namespace {
using Clock = std::chrono::steady_clock;
net::Socket connect(const TradingConfig& config) {
    net::Socket socket(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    if (socket.fd() < 0)
        throw std::system_error(errno, std::generic_category(), "socket trading gateway");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config.tcp_port);
    if (::inet_pton(AF_INET, config.address.c_str(), &address.sin_addr) != 1)
        throw std::invalid_argument("Expected numeric gateway IPv4 address");
    if (::connect(socket.fd(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0) {
        if (errno != EINPROGRESS)
            throw std::system_error(errno, std::generic_category(), "connect gateway");
        const auto deadline = Clock::now() + config.io_timeout;
        for (;;) {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now())
                    .count();
            if (remaining <= 0) throw std::runtime_error("Gateway connect deadline expired");
            pollfd fd{socket.fd(), POLLOUT, 0};
            const auto ready =
                ::poll(&fd, 1, static_cast<int>(std::min<std::int64_t>(remaining, 50)));
            if (ready < 0 && errno != EINTR)
                throw std::system_error(errno, std::generic_category(), "poll connect");
            if (ready <= 0) continue;
            int error = 0;
            socklen_t size = sizeof(error);
            if (::getsockopt(socket.fd(), SOL_SOCKET, SO_ERROR, &error, &size) < 0)
                throw std::system_error(errno, std::generic_category(), "getsockopt connect");
            if (error) throw std::system_error(error, std::generic_category(), "connect gateway");
            break;
        }
    }
    net::detail::no_delay(socket.fd());
    return socket;
}
}  // namespace
struct TradingClient::Impl {
    explicit Impl(TradingConfig value)
        : config(std::move(value)), engine(config.client_id, config.limits, config.seed) {
        if (!config.tcp_port || !config.incremental_port || !config.snapshot_port ||
            config.incremental_port == config.snapshot_port || config.io_timeout.count() <= 0 ||
            config.io_timeout > std::chrono::hours(24) || config.feed_timeout.count() <= 0 ||
            config.interval.count() <= 0)
            throw std::invalid_argument("Invalid trading runtime configuration");
        // Subscribe before sending any orders. Snapshot sync also supports late join.
        incremental = std::make_unique<net::MulticastSubscriber>(
            config.group.c_str(), config.incremental_port, config.interface_ipv4.c_str(),
            std::chrono::milliseconds(10));
        snapshots = std::make_unique<net::MulticastSubscriber>(
            config.group.c_str(), config.snapshot_port, config.interface_ipv4.c_str(),
            std::chrono::milliseconds(10));
        socket = connect(config);
        if (!config.log_path.empty()) log = std::make_unique<Logger>(config.log_path);
    }
    TradingConfig config;
    TradingEngine engine;
    std::unique_ptr<Logger> log;
    net::Socket socket;
    std::unique_ptr<net::MulticastSubscriber> incremental, snapshots;
    std::unique_ptr<RequestQueue> requests{std::make_unique<RequestQueue>()};
    std::unique_ptr<ReportQueue> reports{std::make_unique<ReportQueue>()};
    std::unique_ptr<MarketQueue> market{std::make_unique<MarketQueue>()};
    std::atomic<bool> stopping{}, aborting{}, trader_done{};
    std::atomic<std::uint64_t> malformed{};
    std::thread gateway_thread, feed_thread, trader_thread;
    bool started{};
    std::mutex error_mutex;
    std::string failure;
    TradingSummary result;
    void fail(const std::exception& exception) {
        {
            std::lock_guard lock(error_mutex);
            if (failure.empty()) failure = exception.what();
        }
        aborting.store(true);
        stopping.store(true);
    }
    template <class Queue, class Message>
    bool push(Queue& queue, const Message& value) {
        while (!queue.try_push(value)) {
            if (aborting.load() || trader_done.load()) return false;
            std::this_thread::yield();
        }
        return true;
    }
    void gateway() {
        try {
            std::deque<wire::RequestFrame> output;
            wire::ReportFrame input{};
            std::size_t sent = 0, used = 0;
            auto frame_started = Clock::now(), output_started = Clock::now();
            for (;;) {
                if (aborting.load()) break;
                Request request;
                while (output.size() < 4096 && requests->try_pop(request)) {
                    if (output.empty()) output_started = Clock::now();
                    output.push_back(wire::encode(request));
                }
                if (trader_done.load() && output.empty()) {
                    // Synchronize with the strategy's last queue publication.
                    if (!requests->try_pop(request)) break;
                    output.push_back(wire::encode(request));
                    output_started = Clock::now();
                }
                pollfd fd{socket.fd(), static_cast<short>(POLLIN | (output.empty() ? 0 : POLLOUT)),
                          0};
                const auto ready = ::poll(&fd, 1, 5);
                if (ready < 0) {
                    if (errno == EINTR) continue;
                    throw std::system_error(errno, std::generic_category(), "poll gateway");
                }
                if (fd.revents & POLLOUT) net::detail::flush(socket.fd(), output, sent);
                if (fd.revents & POLLIN) {
                    for (unsigned budget = 0; budget < 64; ++budget) {
                        if (!used) frame_started = Clock::now();
                        const auto read = net::detail::read_frame(socket.fd(), input, used);
                        if (read == net::detail::Read::Again) break;
                        if (read == net::detail::Read::Eof)
                            throw std::runtime_error(
                                "Exchange disconnected; order outcome may be unknown");
                        const auto report = wire::decode(input);
                        if (!report)
                            throw std::runtime_error("Malformed exchange execution report");
                        if (!push(*reports, *report)) break;
                    }
                }
                if (fd.revents & (POLLERR | POLLHUP | POLLNVAL))
                    throw std::runtime_error(
                        "Exchange connection closed; order outcome may be unknown");
                const auto now = Clock::now();
                if ((used && now - frame_started > config.io_timeout) ||
                    (!output.empty() && now - output_started > config.io_timeout))
                    throw std::runtime_error("Gateway frame deadline expired");
            }
        } catch (const std::exception& exception) {
            fail(exception);
        }
    }
    void feed() {
        try {
            while (!trader_done.load() && !aborting.load()) {
                pollfd fds[2]{{incremental->fd(), POLLIN, 0}, {snapshots->fd(), POLLIN, 0}};
                const auto ready = ::poll(fds, 2, 10);
                if (ready < 0) {
                    if (errno == EINTR) continue;
                    throw std::system_error(errno, std::generic_category(), "poll market data");
                }
                for (unsigned i = 0; i < 2; ++i) {
                    if (!(fds[i].revents & POLLIN)) continue;
                    wire::MarketFrame frame;
                    const auto packet = (i ? snapshots : incremental)->receive(frame);
                    if (!packet) continue;
                    const auto decoded = packet->truncated || packet->size != frame.size()
                                             ? std::nullopt
                                             : wire::decode(frame);
                    if (!decoded || (i == 0 && decoded->snapshot_id) ||
                        (i == 1 && !decoded->snapshot_id)) {
                        malformed.fetch_add(1);
                        continue;
                    }
                    if (!push(*market, *decoded)) break;
                }
            }
        } catch (const std::exception& exception) {
            fail(exception);
        }
    }
    void trade() {
        try {
            std::size_t script_index = 0;
            Sequence last_submitted = 0;
            auto next_step = Clock::now(), last_market = Clock::now();
            auto stop_started = Clock::time_point::max();
            auto request_deadline = Clock::time_point::max();
            std::uint64_t malformed_seen = 0;
            const auto receive = [&](const Report& report) {
                engine.on_report(report);
                exchange::detail::audit(log.get(), LogEvent::TradingReport, report);
                if (config.on_report) config.on_report(report);
                if (report.type == ReportType::Done)
                    request_deadline = Clock::now() + config.io_timeout;
            };
            const auto send = [&](const Request& request) {
                exchange::detail::audit(log.get(), LogEvent::TradingRequest, request);
                while (!requests->try_push(request)) {
                    if (aborting.load()) return;
                    // Drain the reverse queue while applying backpressure. A
                    // large cancel batch must not deadlock a full report queue.
                    Report report;
                    if (reports->try_pop(report))
                        receive(report);
                    else
                        std::this_thread::yield();
                }
                if (engine.completed_sequence() >= last_submitted)
                    request_deadline = Clock::now() + config.io_timeout;
                last_submitted = request.sequence;
                ++result.submitted;
            };
            while (!aborting.load()) {
                const auto now = Clock::now();
                const auto bad = malformed.load();
                if (bad != malformed_seen) {
                    engine.invalidate_feed();
                    malformed_seen = bad;
                }
                Report report;
                for (unsigned budget = 0; budget < 4096 && reports->try_pop(report); ++budget)
                    receive(report);
                MarketUpdate update;
                for (unsigned budget = 0; budget < 4096 && market->try_pop(update); ++budget) {
                    const auto state = engine.on_market(update);
                    if (state == FeedResult::Recovered) {
                        ++result.recoveries;
                        exchange::detail::audit(log.get(), LogEvent::TradingRecovery, update);
                    }
                    last_market = now;
                }
                if (now - last_market > config.feed_timeout) engine.invalidate_feed();
                if (last_submitted > engine.completed_sequence() && now > request_deadline)
                    throw std::runtime_error(
                        "Order response deadline expired; outcome may be unknown");
                if (stopping.load()) {
                    if (stop_started == Clock::time_point::max()) stop_started = now;
                    for (const auto& cancel : engine.cancel_all()) send(cancel);
                    if (!engine.active_orders() && engine.completed_sequence() >= last_submitted)
                        break;
                    if (now - stop_started > config.io_timeout)
                        throw std::runtime_error(
                            "Cancel/drain deadline expired; outstanding orders may remain");
                } else if (now >= next_step) {
                    next_step = now + config.interval;
                    if (config.strategy.algorithm == Algorithm::Script) {
                        if (script_index < config.script.size() && engine.feed().synchronized() &&
                            engine.completed_sequence() >= last_submitted) {
                            const auto submitted = engine.submit(config.script[script_index++]);
                            if (submitted.request)
                                send(*submitted.request);
                            else
                                ++result.local_rejections;
                        }
                    } else {
                        for (const auto& request : engine.step(config.strategy)) send(request);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            result.synchronized = engine.feed().synchronized();
            result.sequence = engine.feed().sequence();
            result.session = engine.feed().session();
            result.fills = engine.fill_count();
            result.active_orders = engine.active_orders();
            for (const auto& [id, limit] : config.limits) {
                (void)limit;
                result.positions.emplace(id, engine.position(id));
            }
        } catch (const std::exception& exception) {
            fail(exception);
        }
        trader_done.store(true);
    }
};
TradingClient::TradingClient(TradingConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}
TradingClient::~TradingClient() { stop(); }
void TradingClient::start() {
    if (impl_->started) throw std::logic_error("Trading client can only be started once");
    impl_->started = true;
    try {
        impl_->gateway_thread = std::thread([this] { impl_->gateway(); });
        impl_->feed_thread = std::thread([this] { impl_->feed(); });
        impl_->trader_thread = std::thread([this] { impl_->trade(); });
    } catch (...) {
        impl_->aborting.store(true);
        impl_->trader_done.store(true);
        stop();
        throw;
    }
}
void TradingClient::stop() noexcept {
    impl_->stopping.store(true);
    if (impl_->trader_thread.joinable()) impl_->trader_thread.join();
    if (impl_->gateway_thread.joinable()) impl_->gateway_thread.join();
    if (impl_->feed_thread.joinable()) impl_->feed_thread.join();
    if (impl_->log) {
        impl_->log->close();
        impl_->result.log_dropped = impl_->log->dropped_count();
        if (exchange::detail::log_failed(impl_->log.get()))
            impl_->fail(std::runtime_error("Trading asynchronous log write/format failure"));
    }
}
bool TradingClient::failed() const noexcept { return impl_->aborting.load(); }
std::string TradingClient::error() const {
    std::lock_guard lock(impl_->error_mutex);
    return impl_->failure;
}
TradingSummary TradingClient::summary() const {
    if (!impl_->trader_done.load())
        throw std::logic_error("Stop client before reading trading state");
    return impl_->result;
}
}  // namespace order_book::trading
