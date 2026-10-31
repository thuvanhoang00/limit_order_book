#pragma once

#include <limits>
#include <map>
#include <optional>
#include <random>
#include <vector>

#include "order_book/exchange/market_data.hpp"

namespace order_book::trading {
using namespace order_book::exchange;
struct Position {
    std::int64_t quantity{};
    std::uint64_t volume{};
    long double average_price{}, realized{}, unrealized{};
    void fill(Side side, Price price, Quantity quantity);
    void mark(long double price) noexcept;
    [[nodiscard]] long double pnl() const noexcept { return realized + unrealized; }
};
struct RiskLimits {
    Quantity max_order_quantity{1000};
    std::int64_t max_position{10000};
    long double max_loss{1000000};  // Nonnegative loss budget.
    std::uint64_t max_notional{std::numeric_limits<std::uint64_t>::max()};
};
struct Features {
    std::optional<long double> fair_price;
    std::optional<long double> aggressive_ratio;
    std::uint64_t trade_id{};
    Side aggressor{Side::Bid};
    void book(const OrderBook* book) noexcept;
    void trade(const MarketUpdate& trade, const OrderBook* book) noexcept;
};
enum class OrderState { PendingNew, Live, PendingCancel, PendingReplace, Dead };
struct ManagedOrder {
    Request order;
    OrderId market_order_id{};
    Quantity leaves{};
    OrderState state{OrderState::PendingNew};
    std::optional<Request> pending;
    [[nodiscard]] Quantity reserved() const noexcept;
};
enum class Algorithm { Script, MarketMaker, LiquidityTaker, Random };
struct StrategyConfig {
    Algorithm algorithm{Algorithm::Script};
    Quantity clip{10};
    Price tick_size{1};
    Price edge{1};
    long double trade_ratio{0.5};
};
struct SubmitResult {
    Status status{Status::Ok};
    std::optional<Request> request;
};

// The strategy thread owns all mutable trading state. Gateways only transport
// messages through queues; risk includes outstanding and pending order exposure.
class TradingEngine final {
   public:
    explicit TradingEngine(ClientId client, std::map<InstrumentId, RiskLimits> limits = {{1, {}}},
                           std::uint64_t seed = 42);
    [[nodiscard]] SubmitResult submit(Request request);
    void on_report(const Report& report);
    [[nodiscard]] FeedResult on_market(const MarketUpdate& update);
    void invalidate_feed() noexcept { feed_.invalidate(); }
    [[nodiscard]] std::vector<Request> step(const StrategyConfig& config);
    [[nodiscard]] std::vector<Request> cancel_all();
    [[nodiscard]] const FeedConsumer& feed() const noexcept { return feed_; }
    [[nodiscard]] const Position& position(InstrumentId id) const;
    [[nodiscard]] const Features& features(InstrumentId id) const;
    [[nodiscard]] const ManagedOrder* order(InstrumentId id, OrderId order) const noexcept;
    [[nodiscard]] std::size_t active_orders() const noexcept;
    [[nodiscard]] Sequence completed_sequence() const noexcept { return completed_; }
    [[nodiscard]] std::uint64_t fill_count() const noexcept { return fills_; }

   private:
    using Key = std::pair<InstrumentId, OrderId>;
    Status risk(const Request& request, const ManagedOrder* replacing) const;
    void refresh(InstrumentId id);
    ClientId client_;
    std::map<InstrumentId, RiskLimits> limits_;
    std::map<InstrumentId, Position> positions_;
    std::map<InstrumentId, Features> features_;
    std::map<Key, ManagedOrder> orders_;
    std::map<InstrumentId, std::uint64_t> consumed_trades_;
    FeedConsumer feed_;
    Sequence next_sequence_{1}, response_sequence_{}, completed_{};
    std::uint64_t report_session_{}, fills_{};
    OrderId next_order_{1};
    std::mt19937_64 random_;
};
}  // namespace order_book::trading
