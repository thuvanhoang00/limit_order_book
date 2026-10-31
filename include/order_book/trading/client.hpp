#pragma once
#include <chrono>
#include <functional>
#include <string>

#include "order_book/net/tcp.hpp"
#include "order_book/net/udp.hpp"
#include "order_book/trading/strategy.hpp"

namespace order_book::trading {
struct ClientConfig {
    ClientId client_id{1};
    std::uint16_t tcp_port{9100}, multicast_port{9101};
    std::string group{"239.255.0.2"}, interface { "127.0.0.1" };
    InstrumentRules limits;
    long double max_loss{100'000};
};
// One application thread multiplexes order reports and multicast data. A snapshot
// connection is separate, so recovery cannot consume private execution reports.
class TradingClient final {
   public:
    explicit TradingClient(ClientConfig config);
    void recover();
    [[nodiscard]] std::vector<Message> submit(Message command);
    void poll(std::chrono::milliseconds duration);
    void set_report_handler(std::function<void(const Message&)> handler) {
        handler_ = std::move(handler);
    }
    [[nodiscard]] const MarketReplica& market() const noexcept { return market_; }
    [[nodiscard]] const OrderManager& orders() const noexcept { return manager_; }
    [[nodiscard]] std::uint64_t recoveries() const noexcept { return recoveries_; }

   private:
    ClientConfig config_;
    net::MulticastSubscriber subscriber_;
    net::Socket gateway_;
    MarketReplica market_;
    OrderManager manager_;
    std::uint64_t request_id_{}, epoch_{}, recoveries_{};
    std::function<void(const Message&)> handler_;
    std::chrono::steady_clock::time_point last_market_{std::chrono::steady_clock::now()};
    std::optional<Message> receive_report(std::chrono::milliseconds wait);
    void drain_market();
};
}  // namespace order_book::trading
