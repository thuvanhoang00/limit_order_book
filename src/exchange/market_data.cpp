#include "order_book/exchange/market_data.hpp"

#include <algorithm>

namespace order_book::exchange {
bool FeedBook::apply(const MarketUpdate& u) {
    if (!u.instrument_id || (u.side != Side::Bid && u.side != Side::Ask)) return false;
    if (u.type == UpdateType::Trade) return u.price && u.quantity;
    if (u.type == UpdateType::Add) {
        if (!u.order_id || !u.price || !u.quantity) return false;
        if (const auto* existing = book(u.instrument_id)) {
            const auto opposite = u.side == Side::Bid ? existing->best_ask() : existing->best_bid();
            if (opposite && (u.side == Side::Bid ? u.price >= *opposite : u.price <= *opposite))
                return false;
        }
        auto [it, inserted] = books_.try_emplace(u.instrument_id);
        const auto result = it->second.apply(AddOrder{u.order_id, u.side, u.price, u.quantity});
        if (inserted && result != ApplyResult::Ok) books_.erase(it);
        return result == ApplyResult::Ok;
    }
    const auto it = books_.find(u.instrument_id);
    if (it == books_.end()) return false;
    auto& book = it->second;
    const auto old = book.order(u.order_id);
    if (!old || old->side != u.side || old->price != u.price) return false;
    if (u.type == UpdateType::Cancel) return book.apply(CancelOrder{u.order_id}) == ApplyResult::Ok;
    if (u.type != UpdateType::Modify || !u.quantity || u.quantity > old->quantity) return false;
    return u.quantity == old->quantity ||
           book.apply(ExecuteOrder{u.order_id, old->quantity - u.quantity}) == ApplyResult::Ok;
}
const OrderBook* FeedBook::book(InstrumentId id) const noexcept {
    const auto it = books_.find(id);
    return it == books_.end() ? nullptr : &it->second;
}
std::vector<MarketUpdate> FeedBook::orders() const {
    std::vector<MarketUpdate> result;
    result.reserve(order_count());
    for (const auto& [id, book] : books_) {
        const auto state = book.snapshot();
        for (const auto* levels : {&state.bids, &state.asks}) {
            for (const auto& level : *levels) {
                for (const auto& order : level.orders) {
                    MarketUpdate u;
                    u.type = UpdateType::Add;
                    u.instrument_id = id;
                    u.order_id = order.order_id;
                    u.side = order.side;
                    u.price = order.price;
                    u.quantity = order.quantity;
                    result.push_back(u);
                }
            }
        }
    }
    return result;
}
std::vector<InstrumentId> FeedBook::instruments() const {
    std::vector<InstrumentId> ids;
    for (const auto& [id, book] : books_) {
        (void)book;
        ids.push_back(id);
    }
    return ids;
}
std::size_t FeedBook::order_count() const noexcept {
    std::size_t count = 0;
    for (const auto& [id, book] : books_) {
        (void)id;
        count += book.order_count();
    }
    return count;
}
bool FeedBook::validate_invariants() const {
    for (const auto& [id, book] : books_) {
        (void)id;
        if (!book.validate_invariants()) return false;
    }
    return true;
}
std::vector<MarketUpdate> snapshot_cycle(const FeedBook& book, std::uint64_t session,
                                         std::uint64_t snapshot_id, Sequence watermark) {
    auto orders = book.orders();
    std::vector<MarketUpdate> cycle;
    cycle.reserve(orders.size() + 2);
    MarketUpdate marker;
    marker.type = UpdateType::SnapshotBegin;
    marker.session = session;
    marker.snapshot_id = snapshot_id;
    marker.watermark = watermark;
    marker.count = orders.size();
    cycle.push_back(marker);
    for (auto& order : orders) {
        order.session = session;
        order.snapshot_id = snapshot_id;
        order.watermark = watermark;
        order.sequence = cycle.size();
        cycle.push_back(order);
    }
    marker.type = UpdateType::SnapshotEnd;
    marker.sequence = cycle.size();
    cycle.push_back(marker);
    return cycle;
}
void FeedConsumer::invalidate() noexcept {
    synchronized_ = false;
    can_start_from_one_ = false;
    candidate_.reset();
    ready_ = false;
}
bool FeedConsumer::adopt(std::uint64_t session) {
    if (!session || session < session_) return false;
    if (session > session_) {
        session_ = session;
        sequence_ = observed_ = snapshot_id_ = 0;
        pending_.clear();
        candidate_.reset();
        live_ = FeedBook{};
        ready_ = synchronized_ = false;
        can_start_from_one_ = true;
    }
    return true;
}
FeedResult FeedConsumer::incremental(const MarketUpdate& u) {
    if (!adopt(u.session)) return FeedResult::Dropped;
    if (u.snapshot_id || u.type == UpdateType::SnapshotBegin || u.type == UpdateType::SnapshotEnd) {
        invalidate();
        return FeedResult::Recovering;
    }
    if (u.type == UpdateType::Heartbeat) {
        observed_ = std::max(observed_, u.watermark);
        if (observed_ > sequence_)
            synchronized_ = false;
        else if (can_start_from_one_ && !observed_)
            synchronized_ = true;
        return synchronized_ ? FeedResult::Dropped : finish();
    }
    if (!u.sequence || u.sequence <= sequence_) return FeedResult::Dropped;
    observed_ = std::max(observed_, u.sequence);
    if ((synchronized_ || (can_start_from_one_ && u.sequence == 1 && !sequence_)) &&
        u.sequence == sequence_ + 1) {
        if (!live_.apply(u)) {
            invalidate();
            return FeedResult::Recovering;
        }
        sequence_ = u.sequence;
        synchronized_ = true;
        can_start_from_one_ = false;
        return FeedResult::Applied;
    }
    synchronized_ = false;
    can_start_from_one_ = false;
    if (pending_.size() >= limit_ && !pending_.contains(u.sequence)) {
        pending_.clear();
        candidate_.reset();
        ready_ = false;
    }
    if (limit_) pending_.try_emplace(u.sequence, u);
    return finish();
}
FeedResult FeedConsumer::snapshot(const MarketUpdate& u) {
    if (!adopt(u.session)) return FeedResult::Dropped;
    if (synchronized_) return FeedResult::Dropped;
    if (u.type == UpdateType::SnapshotBegin) {
        if (!u.snapshot_id || u.sequence || u.count > limit_ || u.watermark < sequence_ ||
            u.snapshot_id <= snapshot_id_)
            return FeedResult::Dropped;
        candidate_ = std::make_unique<FeedBook>();
        can_start_from_one_ = false;
        snapshot_id_ = u.snapshot_id;
        watermark_ = u.watermark;
        snapshot_count_ = u.count;
        snapshot_next_ = 1;
        ready_ = false;
        return FeedResult::Recovering;
    }
    if (!candidate_ || ready_ || u.snapshot_id != snapshot_id_ || u.watermark != watermark_)
        return FeedResult::Dropped;
    if (u.sequence != snapshot_next_) {
        candidate_.reset();
        return FeedResult::Recovering;
    }
    ++snapshot_next_;
    if (u.type == UpdateType::Add && u.sequence <= snapshot_count_ && candidate_->apply(u))
        return FeedResult::Recovering;
    if (u.type == UpdateType::SnapshotEnd && u.sequence == snapshot_count_ + 1 &&
        u.count == snapshot_count_ && candidate_->order_count() == snapshot_count_) {
        ready_ = true;
        return finish();
    }
    candidate_.reset();
    return FeedResult::Recovering;
}
FeedResult FeedConsumer::finish() {
    if (!ready_ || !candidate_) return FeedResult::Recovering;
    // Candidate watermark advances only after an incremental was successfully applied.
    pending_.erase(pending_.begin(), pending_.upper_bound(watermark_));
    while (!pending_.empty()) {
        const auto first = pending_.begin();
        if (first->first != watermark_ + 1) return FeedResult::Recovering;
        if (!candidate_->apply(first->second)) {
            candidate_.reset();
            ready_ = false;
            return FeedResult::Recovering;
        }
        watermark_ = first->first;
        pending_.erase(first);
    }
    if (watermark_ < observed_) return FeedResult::Recovering;
    live_ = std::move(*candidate_);
    candidate_.reset();
    sequence_ = watermark_;
    synchronized_ = true;
    can_start_from_one_ = ready_ = false;
    return FeedResult::Recovered;
}
}  // namespace order_book::exchange
