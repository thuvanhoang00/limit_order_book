#pragma once
#include <map>
#include <optional>
#include <vector>

#include "order_book/trading/engine.hpp"
#include "order_book/trading/replica.hpp"

namespace order_book::trading {
class OrderManager final {
   public:
    // Duplicate execution reports are ignored before positions or reservations change.
    [[nodiscard]] bool consume(const Message& report);
    [[nodiscard]] const std::map<OrderId, Message>& orders() const noexcept { return orders_; }
    [[nodiscard]] Position position(InstrumentId id) const;
    [[nodiscard]] Reject check(const Message& command, const InstrumentRules& limits,
                               const MarketReplica& market, long double max_loss) const;

   private:
    Sequence last_report_{};
    std::map<OrderId, Message> orders_;
    std::map<InstrumentId, Position> positions_;
};
struct Quote {
    Side side;
    Price price;
    Quantity quantity;
};
struct StrategyConfig {
    InstrumentId instrument{1};
    Quantity clip{5};
    Price fair_price{100};
    Price edge{2};
    Price tick{1};
    std::int64_t inventory_limit{50};
};
[[nodiscard]] std::vector<Quote> maker_quotes(const StrategyConfig& config, std::int64_t inventory);
[[nodiscard]] std::optional<Quote> taker_quote(const StrategyConfig& config, const OrderBook* book,
                                               std::int64_t inventory);
}  // namespace order_book::trading
