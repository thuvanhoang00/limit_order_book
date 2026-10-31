#include "order_book/trading/strategy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace order_book::trading {
bool OrderManager::consume(const Message& m) {
    if (m.kind != Kind::Report || m.report == ReportType::Status || m.sequence <= last_report_)
        return false;
    last_report_ = m.sequence;
    if (m.report == ReportType::Accepted || m.report == ReportType::Replaced) {
        auto order = m;
        order.quantity = m.remaining;
        orders_[m.order_id] = order;
    } else if (m.report == ReportType::Fill) {
        positions_[m.instrument_id].fill(m.side, m.price, m.quantity);
        if (auto it = orders_.find(m.order_id); it != orders_.end()) {
            it->second.quantity = m.remaining;
            if (m.remaining == 0) orders_.erase(it);
        }
    } else if (m.report == ReportType::Cancelled)
        orders_.erase(m.order_id);
    else if (m.report == ReportType::Complete && m.reject == Reject::None && m.order_id != 0) {
        if (auto it = orders_.find(m.order_id); it != orders_.end()) {
            it->second.quantity = m.remaining;
            if (m.remaining == 0) orders_.erase(it);
        }
    }
    return true;
}
Position OrderManager::position(InstrumentId id) const {
    const auto it = positions_.find(id);
    return it == positions_.end() ? Position{} : it->second;
}
Reject OrderManager::check(const Message& c, const InstrumentRules& limits,
                           const MarketReplica& market, long double max_loss) const {
    if (c.action == Action::Cancel || c.action == Action::CancelAll) return Reject::None;
    if (!market.synchronized()) return Reject::KillSwitch;
    if (c.quantity == 0 || c.quantity > limits.max_order_quantity) return Reject::RiskQuantity;
    const auto* book = market.book(c.instrument_id);
    const auto p = position(c.instrument_id);
    auto mark = c.price != 0 ? static_cast<long double>(c.price) : p.average_price;
    if (book && book->best_bid() && book->best_ask())
        mark = (static_cast<long double>(*book->best_bid()) + *book->best_ask()) / 2;
    if (p.pnl(mark) < -max_loss) return Reject::KillSwitch;
    const auto risk_price = c.order_type == OrderType::Market ? limits.max_price : c.price;
    if (static_cast<std::uint64_t>(risk_price) * c.quantity > limits.max_notional)
        return Reject::RiskNotional;
    std::int64_t reserved{};
    std::size_t open{};
    for (const auto& [id, order] : orders_) {
        if (c.action == Action::Replace && id == c.order_id) continue;
        ++open;
        if (order.instrument_id == c.instrument_id && order.side == c.side)
            reserved += order.quantity;
    }
    if (open >= limits.max_open_orders) return Reject::RiskOpenOrders;
    if (std::abs(p.quantity + (c.side == Side::Bid ? 1 : -1) * (reserved + c.quantity)) >
        limits.max_position)
        return Reject::RiskPosition;
    return Reject::None;
}
std::vector<Quote> maker_quotes(const StrategyConfig& c, std::int64_t inventory) {
    std::vector<Quote> quotes;
    if (c.tick == 0 || c.edge == 0 || c.clip == 0 || c.inventory_limit <= 0) return quotes;
    // One tick of skew per clip: long inventory lowers both desired quotes.
    const auto fair = static_cast<std::int64_t>(c.fair_price) - (inventory / c.clip) * c.tick;
    const auto bid = ((fair - c.edge) / c.tick) * c.tick;
    const auto ask = ((fair + c.edge + c.tick - 1) / c.tick) * c.tick;
    if (bid > 0 && bid < ask && bid <= std::numeric_limits<Price>::max() &&
        inventory + c.clip <= c.inventory_limit)
        quotes.push_back({Side::Bid, static_cast<Price>(bid), c.clip});
    if (ask > bid && ask > 0 && ask <= std::numeric_limits<Price>::max() &&
        inventory - c.clip >= -c.inventory_limit)
        quotes.push_back({Side::Ask, static_cast<Price>(ask), c.clip});
    return quotes;
}
std::optional<Quote> taker_quote(const StrategyConfig& c, const OrderBook* book,
                                 std::int64_t inventory) {
    if (!book || c.clip == 0 || c.inventory_limit <= 0) return std::nullopt;
    if (book->best_ask() &&
        static_cast<std::uint64_t>(*book->best_ask()) + c.edge <= c.fair_price &&
        inventory + c.clip <= c.inventory_limit)
        return Quote{Side::Bid, *book->best_ask(), c.clip};
    if (book->best_bid() &&
        static_cast<std::uint64_t>(c.fair_price) + c.edge <= *book->best_bid() &&
        inventory - c.clip >= -c.inventory_limit)
        return Quote{Side::Ask, *book->best_bid(), c.clip};
    return std::nullopt;
}
}  // namespace order_book::trading
