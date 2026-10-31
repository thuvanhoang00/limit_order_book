#include "order_book/trading/client.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <chrono>
#include <stdexcept>
#include <system_error>
#include <thread>

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
    recover();
}
void TradingClient::recover() {
    market_.invalidate();
    auto socket = net::connect_tcp("127.0.0.1", config_.tcp_port);
    timeout(socket);
    Message query;
    query.action = Action::Snapshot;
    net::send_all(socket, encode(query));
    std::vector<Message> snapshot;
    const auto deadline = Clock::now() + 3s;
    while (Clock::now() < deadline) {
        auto m = receive(socket);
        snapshot.push_back(m);
        if (snapshot.size() > 1'000'000)
            throw std::runtime_error("Snapshot exceeds client resource bound");
        if (m.kind == Kind::SnapshotEnd) {
            if (m.epoch != epoch_)
                throw std::runtime_error(
                    "Exchange restarted; reconcile private order state before trading");
            market_.restore(snapshot);
            last_market_ = Clock::now();
            ++recoveries_;
            return;
        }
    }
    throw std::runtime_error("Snapshot deadline expired");
}
void TradingClient::drain_market() {
    const auto until = Clock::now() + 5ms;
    bool needs_recovery{};
    while (Clock::now() < until) {
        Frame frame{};
        const auto packet = subscriber_.receive(frame);
        if (!packet) break;
        if (packet->truncated || packet->size != frame.size()) {
            needs_recovery = true;
            break;
        }
        const auto m = decode(frame);
        if (!m || !market_.consume(*m)) {
            needs_recovery = true;
            break;
        }
        last_market_ = Clock::now();
    }
    if (needs_recovery || Clock::now() - last_market_ > 500ms) recover();
}
std::optional<Message> TradingClient::receive_report(std::chrono::milliseconds wait) {
    pollfd descriptor{gateway_.fd(), POLLIN, 0};
    int count;
    do {
        count = ::poll(&descriptor, 1, static_cast<int>(wait.count()));
    } while (count < 0 && errno == EINTR);
    if (count < 0) throw std::system_error(errno, std::generic_category(), "poll gateway");
    if (count == 0) return std::nullopt;
    auto m = receive(gateway_);
    if (m.kind != Kind::Report || m.client_id != config_.client_id || m.epoch != epoch_)
        throw std::runtime_error("Unexpected gateway/session report");
    if (manager_.consume(m) && handler_) handler_(m);
    return m;
}
void TradingClient::poll(std::chrono::milliseconds duration) {
    const auto deadline = Clock::now() + duration;
    do {
        while (receive_report(0ms)) {
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
        while (receive_report(0ms)) {
        }
    } else
        poll(0ms);
    const auto risk = manager_.check(command, config_.limits, market_, config_.max_loss);
    if (risk != Reject::None)
        throw std::runtime_error("Local risk check: " + std::string(to_string(risk)));
    command.kind = Kind::Command;
    command.client_id = config_.client_id;
    command.request_id = ++request_id_;
    net::send_all(gateway_, encode(command));
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
}  // namespace order_book::trading
