#include "order_book/trading/features.hpp"
namespace order_book::trading {
const Features& FeatureEngine::get(InstrumentId id) const noexcept {
    static const Features empty;
    const auto it = values_.find(id);
    return it == values_.end() ? empty : it->second;
}
void FeatureEngine::refresh(InstrumentId id, const OrderBook* book) {
    auto& feature = values_[id];
    feature.microprice.reset();
    if (!book || !book->best_bid() || !book->best_ask()) return;
    const auto bid_quantity = book->best_quantity(Side::Bid);
    const auto ask_quantity = book->best_quantity(Side::Ask);
    feature.microprice = (static_cast<long double>(*book->best_bid()) * ask_quantity +
                          static_cast<long double>(*book->best_ask()) * bid_quantity) /
                         (static_cast<long double>(bid_quantity) + ask_quantity);
}
void FeatureEngine::before(const Message& m, const OrderBook* book) {
    if (m.kind == Kind::Delta && m.update == UpdateType::Execute)
        pending_[m.instrument_id] = {m.trade_id, book ? book->best_quantity(m.side) : 0};
}
void FeatureEngine::after(const Message& m, const OrderBook* book) {
    refresh(m.instrument_id, book);
    if (m.kind != Kind::Delta || m.update != UpdateType::Trade) return;
    auto& feature = values_[m.instrument_id];
    feature.trade_id = m.trade_id;
    feature.aggressor = m.side;
    feature.aggressive_ratio.reset();
    const auto it = pending_.find(m.instrument_id);
    if (it != pending_.end() && it->second.trade == m.trade_id) {
        if (it->second.quantity)
            feature.aggressive_ratio = static_cast<long double>(m.quantity) / it->second.quantity;
        pending_.erase(it);
    }
}
}  // namespace order_book::trading
