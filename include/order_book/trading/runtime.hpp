#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "order_book/trading/engine.hpp"

namespace order_book::trading {
struct TradingConfig {
    ClientId client_id{1};
    std::string address{"127.0.0.1"};
    std::uint16_t tcp_port{9000};
    std::string group{"239.255.42.1"};
    std::string interface_ipv4{"127.0.0.1"};
    std::string log_path;
    std::uint16_t incremental_port{9001}, snapshot_port{9002};
    std::chrono::milliseconds io_timeout{3000}, feed_timeout{1000}, interval{50};
    std::map<InstrumentId, RiskLimits> limits{{1, {}}};
    StrategyConfig strategy;
    std::vector<Request> script;
    std::uint64_t seed{42};
    std::function<void(const Report&)> on_report;  // Runs on the strategy thread.
};
struct TradingSummary {
    bool synchronized{};
    Sequence sequence{};
    std::uint64_t session{}, fills{}, submitted{}, local_rejections{}, recoveries{}, log_dropped{};
    std::size_t active_orders{};
    std::map<InstrumentId, Position> positions;
};
class TradingClient final {
   public:
    explicit TradingClient(TradingConfig config);
    ~TradingClient();
    TradingClient(const TradingClient&) = delete;
    TradingClient& operator=(const TradingClient&) = delete;
    void start();
    void stop() noexcept;  // Cancel live orders, await Done, drain, and join.
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] TradingSummary summary() const;  // Only after stop().
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace order_book::trading
