#include "order_book/trading/replica.hpp"

#include <set>
#include <stdexcept>

namespace order_book::trading {
void MarketReplica::restore(std::span<const Message> rows) {
    synchronized_ = false;
    if (rows.size() < 2 || rows.front().kind != Kind::SnapshotBegin ||
        rows.back().kind != Kind::SnapshotEnd || rows.front().epoch == 0 ||
        rows.size() - 2 != rows.front().quantity || rows.back().quantity != rows.front().quantity)
        throw std::runtime_error("Invalid snapshot envelope/count");
    std::map<InstrumentId, OrderBook> books;
    std::set<OrderId> ids;
    for (std::size_t i = 0; i != rows.size(); ++i) {
        const auto& m = rows[i];
        if (m.epoch != rows.front().epoch || m.sequence != rows.front().sequence)
            throw std::runtime_error("Inconsistent snapshot watermark");
        if (i == 0 || i == rows.size() - 1) continue;
        if (m.kind != Kind::SnapshotOrder || m.instrument_id == 0 || m.exchange_id == 0 ||
            !ids.insert(m.exchange_id).second ||
            books[m.instrument_id].apply(AddOrder{m.exchange_id, m.side, m.price, m.quantity}) !=
                ApplyResult::Ok)
            throw std::runtime_error("Invalid snapshot order");
    }
    for (const auto& [id, book] : books) {
        (void)id;
        if (!book.validate_invariants() ||
            (book.best_bid() && book.best_ask() && *book.best_bid() >= *book.best_ask()))
            throw std::runtime_error("Invalid snapshot book");
    }
    books_.swap(books);
    epoch_ = rows.front().epoch;
    sequence_ = rows.front().sequence;
    synchronized_ = true;
}
bool MarketReplica::consume(const Message& m) {
    if (!synchronized_) return false;
    if (m.epoch != epoch_) {
        synchronized_ = false;
        return false;
    }
    if (m.kind == Kind::Heartbeat) {
        if (m.sequence > sequence_) synchronized_ = false;
        return synchronized_;
    }
    if (m.kind != Kind::Delta) {
        synchronized_ = false;
        return false;
    }
    if (m.sequence <= sequence_) return true;
    if (m.sequence - sequence_ != 1) {
        synchronized_ = false;
        return false;
    }
    ApplyResult result = ApplyResult::Ok;
    if (m.update != UpdateType::Trade) {
        auto it = books_.find(m.instrument_id);
        if (m.update == UpdateType::Add) {
            result =
                books_[m.instrument_id].apply(AddOrder{m.exchange_id, m.side, m.price, m.quantity});
        } else if (it == books_.end())
            result = ApplyResult::UnknownInstrument;
        else if (m.update == UpdateType::Cancel)
            result = it->second.apply(CancelOrder{m.exchange_id});
        else if (m.update == UpdateType::Execute)
            result = it->second.apply(ExecuteOrder{m.exchange_id, m.quantity});
        else if (m.update == UpdateType::Reduce) {
            const auto old = it->second.remaining_quantity(m.exchange_id);
            if (!old || m.quantity == 0 || m.quantity >= *old)
                result = ApplyResult::InvalidQuantity;
            else
                result = it->second.apply(ExecuteOrder{m.exchange_id, *old - m.quantity});
        } else
            result = ApplyResult::ParseError;
    }
    if (result != ApplyResult::Ok) {
        synchronized_ = false;
        return false;
    }
    sequence_ = m.sequence;
    return true;
}
const OrderBook* MarketReplica::book(InstrumentId id) const noexcept {
    const auto it = books_.find(id);
    return it == books_.end() ? nullptr : &it->second;
}
std::size_t MarketReplica::order_count() const noexcept {
    std::size_t count{};
    for (const auto& [id, book] : books_) {
        (void)id;
        count += book.order_count();
    }
    return count;
}
std::vector<Message> MarketReplica::snapshot() const {
    if (!synchronized_) throw std::logic_error("Unsynchronized book is unavailable");
    std::vector<Message> result;
    Message m;
    m.kind = Kind::SnapshotBegin;
    m.epoch = epoch_;
    m.sequence = sequence_;
    m.quantity = static_cast<Quantity>(order_count());
    result.push_back(m);
    for (const auto& [instrument, book] : books_)
        for (const auto& level : book.snapshot())
            for (const auto& order : level.orders) {
                auto row = m;
                row.kind = Kind::SnapshotOrder;
                row.instrument_id = instrument;
                row.exchange_id = order.order_id;
                row.side = order.side;
                row.price = order.price;
                row.quantity = order.quantity;
                result.push_back(row);
            }
    m.kind = Kind::SnapshotEnd;
    result.push_back(m);
    return result;
}
}  // namespace order_book::trading
