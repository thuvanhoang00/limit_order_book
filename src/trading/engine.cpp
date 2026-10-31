#include "order_book/trading/engine.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace order_book::trading {
namespace {
void require(bool condition, const char* text) {
    if (!condition) throw std::logic_error(text);
}
bool crosses(const Message& c, Price price) {
    return c.order_type == OrderType::Market ||
           (c.side == Side::Bid ? c.price >= price : c.price <= price);
}
std::uint64_t config_number(const std::string& s) {
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (error != std::errc{} || end != s.data() + s.size())
        throw std::invalid_argument("Invalid rule number");
    return value;
}
}  // namespace
Instruments default_instruments() { return {{1, {}}, {2, {}}}; }
Instruments load_instruments(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open instrument rules");
    Instruments result;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#' || line.starts_with("instrument,")) continue;
        std::istringstream stream(line);
        std::string field;
        std::vector<std::uint64_t> values;
        while (std::getline(stream, field, ',')) values.push_back(config_number(field));
        if (values.size() != 9 || values[0] == 0)
            throw std::invalid_argument("Expected nine instrument rule fields");
        for (std::size_t i = 1; i <= 5; ++i)
            if (values[i] > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("Instrument field overflow");
        if (values[7] > 1'000'000'000'000ULL || values[8] > 4096)
            throw std::invalid_argument("Instrument limit overflow");
        InstrumentRules r{
            static_cast<Price>(values[1]),        static_cast<Quantity>(values[2]),
            static_cast<Price>(values[3]),        static_cast<Price>(values[4]),
            static_cast<Quantity>(values[5]),     values[6],
            static_cast<std::int64_t>(values[7]), static_cast<std::size_t>(values[8])};
        if (!result.emplace(values[0], r).second)
            throw std::invalid_argument("Duplicate instrument");
    }
    if (!input.eof()) throw std::runtime_error("Instrument rules read failed");
    return result;
}
std::uint64_t config_fingerprint(const Instruments& instruments) {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&](std::uint64_t value) {
        for (unsigned i = 0; i != 8; ++i) {
            hash ^= value & 0xffU;
            hash *= 1099511628211ULL;
            value >>= 8U;
        }
    };
    mix(1);
    for (const auto& [id, r] : instruments) {
        mix(id);
        mix(r.tick_size);
        mix(r.lot_size);
        mix(r.min_price);
        mix(r.max_price);
        mix(r.max_order_quantity);
        mix(r.max_notional);
        mix(static_cast<std::uint64_t>(r.max_position));
        mix(r.max_open_orders);
    }
    return hash;
}
void Position::fill(Side side, Price price, Quantity size) noexcept {
    const auto signed_size =
        side == Side::Bid ? static_cast<std::int64_t>(size) : -static_cast<std::int64_t>(size);
    const auto old = quantity;
    const auto old_abs = std::abs(old);
    const auto next = old + signed_size;
    if (old == 0 || (old > 0) == (signed_size > 0))
        average_price =
            (average_price * old_abs + static_cast<long double>(price) * size) / std::abs(next);
    else {
        const auto closed = std::min(old_abs, static_cast<std::int64_t>(size));
        realized_pnl +=
            (static_cast<long double>(price) - average_price) * closed * (old > 0 ? 1 : -1);
        if (next == 0)
            average_price = 0;
        else if ((next > 0) != (old > 0))
            average_price = price;
    }
    quantity = next;
    volume += size;
    cash -= static_cast<long double>(price) * signed_size;
}
long double Position::pnl(long double mark) const noexcept { return cash + quantity * mark; }
MatchingEngine::MatchingEngine(Instruments instruments, std::uint64_t epoch)
    : instruments_(std::move(instruments)), epoch_(epoch) {
    if (instruments_.empty() || epoch == 0)
        throw std::invalid_argument("Nonempty instruments/nonzero epoch required");
    for (const auto& [id, r] : instruments_) {
        if (id == 0 || r.tick_size == 0 || r.lot_size == 0 || r.min_price == 0 ||
            r.max_price < r.min_price || r.max_order_quantity == 0 || r.max_notional == 0 ||
            r.max_position <= 0 || r.max_position > 1'000'000'000'000LL || r.max_open_orders == 0 ||
            r.max_open_orders > 4096)
            throw std::invalid_argument("Invalid instrument rules");
        books_.try_emplace(id);
    }
}
const OrderBook& MatchingEngine::book(InstrumentId id) const { return books_.at(id); }
Position MatchingEngine::position(ClientId client, InstrumentId instrument) const {
    const auto it = positions_.find({client, instrument});
    return it == positions_.end() ? Position{} : it->second;
}
std::uint64_t MatchingEngine::last_request(ClientId client) const {
    const auto it = clients_.find(client);
    return it == clients_.end() ? 0 : it->second.last_request;
}
void MatchingEngine::report(Batch& b, Message m, ReportType type, Reject reason) {
    m.kind = Kind::Report;
    m.report = type;
    m.reject = reason;
    m.epoch = epoch_;
    m.sequence = ++response_sequence_;
    b.reports.push_back(m);
}
void MatchingEngine::update(Batch& b, Message m, UpdateType type) {
    m.kind = Kind::Delta;
    m.update = type;
    m.epoch = epoch_;
    m.sequence = ++sequence_;
    m.client_id = 0;
    m.request_id = 0;
    m.order_id = 0;
    b.updates.push_back(m);
}
void MatchingEngine::reject(Batch& b, const Message& c, Reject reason) {
    ++stats_.rejected;
    report(b, c, ReportType::Rejected, reason);
}
void MatchingEngine::cancel(Batch& b, OrderId id, std::uint64_t request) {
    auto m = resting_.at(id).order;
    m.request_id = request;
    m.remaining = 0;
    require(books_.at(m.instrument_id).apply(CancelOrder{id}) == ApplyResult::Ok,
            "Cancel invariant");
    update(b, m, UpdateType::Cancel);
    report(b, m, ReportType::Cancelled);
    by_client_.erase({m.client_id, m.order_id});
    resting_.erase(id);
}
void MatchingEngine::submit(Batch& b, const Message& c, OrderId old_id) {
    const auto instrument = instruments_.find(c.instrument_id);
    if (instrument == instruments_.end()) return reject(b, c, Reject::UnknownInstrument);
    const auto& r = instrument->second;
    if ((c.side != Side::Bid && c.side != Side::Ask) || c.order_id == 0 ||
        (c.order_type != OrderType::Limit && c.order_type != OrderType::Market) ||
        (c.tif != TimeInForce::GTC && c.tif != TimeInForce::IOC && c.tif != TimeInForce::FOK) ||
        (c.order_type == OrderType::Market &&
         (c.price != 0 || c.tif == TimeInForce::GTC || c.post_only)) ||
        (c.post_only && c.tif != TimeInForce::GTC) ||
        (old_id != 0 && (c.order_type != OrderType::Limit || c.tif != TimeInForce::GTC)))
        return reject(b, c, Reject::InvalidCommand);
    if (c.quantity == 0 || c.quantity % r.lot_size != 0)
        return reject(b, c, Reject::InvalidQuantity);
    if (c.order_type == OrderType::Limit &&
        (c.price < r.min_price || c.price > r.max_price || c.price % r.tick_size != 0))
        return reject(b, c, Reject::InvalidPrice);
    if (c.quantity > r.max_order_quantity) return reject(b, c, Reject::RiskQuantity);
    if (static_cast<std::uint64_t>(c.order_type == OrderType::Market ? r.max_price : c.price) *
            c.quantity >
        r.max_notional)
        return reject(b, c, Reject::RiskNotional);
    auto& state = clients_.at(c.client_id);
    if (old_id == 0 && c.order_id <= state.max_order_id)
        return reject(b, c, Reject::DuplicateOrder);
    std::int64_t reserved{};
    std::size_t open{};
    for (const auto& [id, entry] : resting_) {
        const auto& order = entry.order;
        if (id == old_id || order.client_id != c.client_id) continue;
        ++open;
        if (order.instrument_id == c.instrument_id && order.side == c.side)
            reserved += order.quantity;
    }
    if (open >= r.max_open_orders) return reject(b, c, Reject::RiskOpenOrders);
    const auto worst = position(c.client_id, c.instrument_id).quantity +
                       (c.side == Side::Bid ? 1 : -1) * (reserved + c.quantity);
    if (std::abs(worst) > r.max_position) return reject(b, c, Reject::RiskPosition);
    auto& book = books_.at(c.instrument_id);
    if (old_id != 0) {
        const auto& old = resting_.at(old_id).order;
        if (old.instrument_id != c.instrument_id || old.side != c.side)
            return reject(b, c, Reject::InvalidCommand);
        if (old.price == c.price && c.quantity <= old.quantity) {
            const auto reduction = old.quantity - c.quantity;
            auto m = c;
            m.exchange_id = old_id;
            m.remaining = c.quantity;
            if (reduction != 0) {
                require(book.apply(ExecuteOrder{old_id, reduction}) == ApplyResult::Ok,
                        "Reduce invariant");
                update(b, m, UpdateType::Reduce);
            }
            resting_.at(old_id).order.quantity = c.quantity;
            report(b, m, ReportType::Replaced);
            return;
        }
    }
    struct Fill {
        OrderId id;
        Price price;
        Quantity size;
    };
    std::vector<Fill> planned;
    auto leaves = c.quantity;
    Reject plan_rejection = Reject::None;
    book.for_each_order(c.side == Side::Bid ? Side::Ask : Side::Bid, [&](const OrderView& order) {
        if (leaves == 0 || !crosses(c, order.price)) return false;
        if (c.post_only) {
            plan_rejection = Reject::PostOnly;
            return false;
        }
        if (resting_.at(order.order_id).order.client_id == c.client_id) {
            plan_rejection = Reject::SelfTrade;
            return false;
        }
        const auto size = std::min(leaves, order.quantity);
        planned.push_back({order.order_id, order.price, size});
        leaves -= size;
        return leaves != 0;
    });
    if (plan_rejection != Reject::None) return reject(b, c, plan_rejection);
    if (c.tif == TimeInForce::FOK && leaves != 0) return reject(b, c, Reject::Fok);
    const bool rests = leaves != 0 && c.tif == TimeInForce::GTC && c.order_type == OrderType::Limit;
    b.reports.reserve(b.reports.size() + 2 * planned.size() + 5);
    b.updates.reserve(b.updates.size() + 2 * planned.size() + 2);
    positions_.try_emplace({c.client_id, c.instrument_id});
    const auto new_id = next_order_;
    auto incoming = c;
    incoming.exchange_id = new_id;
    incoming.remaining = c.quantity;
    if (rests) {
        auto rest = incoming;
        rest.quantity = leaves;
        rest.remaining = leaves;
        resting_.emplace(new_id, Resting{rest});
        try {
            if (old_id == 0) by_client_.emplace(Key{c.client_id, c.order_id}, new_id);
        } catch (...) {
            resting_.erase(new_id);
            throw;
        }
        if (book.apply(AddOrder{new_id, c.side, c.price, leaves}) != ApplyResult::Ok) {
            resting_.erase(new_id);
            if (old_id == 0) by_client_.erase({c.client_id, c.order_id});
            return reject(b, c, Reject::Capacity);
        }
    }
    ++next_order_;
    if (old_id != 0) {
        const auto old = resting_.at(old_id).order;
        require(book.apply(CancelOrder{old_id}) == ApplyResult::Ok, "Replace invariant");
        update(b, old, UpdateType::Cancel);
        resting_.erase(old_id);
        if (rests)
            by_client_.at({c.client_id, c.order_id}) = new_id;
        else
            by_client_.erase({c.client_id, c.order_id});
    }
    state.max_order_id = std::max(state.max_order_id, c.order_id);
    report(b, incoming, old_id == 0 ? ReportType::Accepted : ReportType::Replaced);
    auto taker_leaves = c.quantity;
    for (const auto& fill : planned) {
        auto& maker = resting_.at(fill.id).order;
        maker.quantity -= fill.size;
        taker_leaves -= fill.size;
        require(book.apply(ExecuteOrder{fill.id, fill.size}) == ApplyResult::Ok, "Fill invariant");
        auto execution = maker;
        execution.quantity = fill.size;
        execution.remaining = maker.quantity;
        execution.trade_id = next_trade_;
        report(b, execution, ReportType::Fill);
        update(b, execution, UpdateType::Execute);
        auto taker = incoming;
        taker.price = fill.price;
        taker.quantity = fill.size;
        taker.remaining = taker_leaves;
        taker.trade_id = next_trade_++;
        report(b, taker, ReportType::Fill);
        update(b, taker, UpdateType::Trade);
        positions_.at({maker.client_id, c.instrument_id}).fill(maker.side, fill.price, fill.size);
        positions_.at({c.client_id, c.instrument_id}).fill(c.side, fill.price, fill.size);
        if (maker.quantity == 0) {
            by_client_.erase({maker.client_id, maker.order_id});
            resting_.erase(fill.id);
        }
        ++stats_.trades;
        stats_.volume += fill.size;
    }
    if (rests) {
        auto m = incoming;
        m.quantity = leaves;
        m.remaining = leaves;
        update(b, m, UpdateType::Add);
    } else if (leaves != 0) {
        auto m = incoming;
        m.quantity = leaves;
        m.remaining = 0;
        report(b, m, ReportType::Cancelled);
    }
}
Batch MatchingEngine::process(const Message& c) {
    Batch b;
    b.reports.reserve(resting_.size() + 4);
    b.updates.reserve(resting_.size() + 2);
    if (c.kind != Kind::Command || c.action == Action::Snapshot) {
        reject(b, c, Reject::InvalidCommand);
        return b;
    }
    if (c.action == Action::Status) {
        auto m = c;
        m.request_id = last_request(c.client_id);
        report(b, m, ReportType::Status);
        return b;
    }
    if (c.client_id == 0 || c.request_id == 0) {
        reject(b, c, Reject::InvalidCommand);
        report(b, c, ReportType::Complete);
        return b;
    }
    auto& state = clients_[c.client_id];
    if (c.request_id == state.last_request && c == state.last_command) {
        b.reports = state.cached;
        return b;
    }
    if (c.request_id <= state.last_request || c.request_id - state.last_request != 1) {
        reject(b, c, Reject::Sequence);
        report(b, c, ReportType::Complete);
        return b;
    }
    ++stats_.commands;
    if (c.action == Action::New)
        submit(b, c);
    else if (c.action == Action::Cancel || c.action == Action::Replace) {
        const auto it = by_client_.find({c.client_id, c.order_id});
        if (it == by_client_.end() ||
            resting_.at(it->second).order.instrument_id != c.instrument_id)
            reject(b, c, Reject::UnknownOrder);
        else if (c.action == Action::Cancel)
            cancel(b, it->second, c.request_id);
        else
            submit(b, c, it->second);
    } else if (c.action == Action::CancelAll) {
        std::vector<OrderId> ids;
        for (const auto& [id, entry] : resting_)
            if (entry.order.client_id == c.client_id &&
                (c.instrument_id == 0 || entry.order.instrument_id == c.instrument_id))
                ids.push_back(id);
        for (auto id : ids) cancel(b, id, c.request_id);
    } else
        reject(b, c, Reject::InvalidCommand);
    auto complete = c;
    if (const auto it = by_client_.find({c.client_id, c.order_id}); it != by_client_.end()) {
        complete.remaining = resting_.at(it->second).order.quantity;
        complete.exchange_id = it->second;
    }
    report(b, complete, ReportType::Complete);
    state.last_request = c.request_id;
    state.last_command = c;
    state.cached.clear();
    for (const auto& m : b.reports)
        if (m.client_id == c.client_id) state.cached.push_back(m);
    return b;
}
std::vector<Message> MatchingEngine::snapshot() const {
    std::vector<Message> rows;
    rows.reserve(resting_.size() + 2);
    Message m;
    m.kind = Kind::SnapshotBegin;
    m.epoch = epoch_;
    m.sequence = sequence_;
    m.quantity = static_cast<Quantity>(resting_.size());
    rows.push_back(m);
    for (const auto& [id, book] : books_)
        for (const auto& level : book.snapshot())
            for (const auto& order : level.orders) {
                Message row;
                row.kind = Kind::SnapshotOrder;
                row.epoch = epoch_;
                row.sequence = sequence_;
                row.instrument_id = id;
                row.exchange_id = order.order_id;
                row.side = order.side;
                row.price = order.price;
                row.quantity = order.quantity;
                rows.push_back(row);
            }
    m.kind = Kind::SnapshotEnd;
    rows.push_back(m);
    return rows;
}
bool MatchingEngine::validate_invariants(std::string* reason) const {
    const auto fail = [&](const char* s) {
        if (reason) *reason = s;
        return false;
    };
    std::size_t count{};
    for (const auto& [id, book] : books_) {
        if (!book.validate_invariants(reason)) return false;
        if (book.best_bid() && book.best_ask() && *book.best_bid() >= *book.best_ask())
            return fail("Crossed book");
        count += book.order_count();
        for (const auto& level : book.snapshot())
            for (const auto& order : level.orders) {
                const auto it = resting_.find(order.order_id);
                if (it == resting_.end()) return fail("Missing owner metadata");
                const auto& m = it->second.order;
                if (m.instrument_id != id || m.side != order.side || m.price != order.price ||
                    m.quantity != order.quantity)
                    return fail("Book/owner mismatch");
                const auto key = by_client_.find({m.client_id, m.order_id});
                if (key == by_client_.end() || key->second != order.order_id)
                    return fail("Client index mismatch");
            }
    }
    if (count != resting_.size() || count != by_client_.size())
        return fail("Order counts mismatch");
    std::map<InstrumentId, std::int64_t> total;
    std::map<InstrumentId, long double> cash;
    for (const auto& [key, p] : positions_) {
        total[key.second] += p.quantity;
        cash[key.second] += p.cash;
    }
    for (const auto& [id, net] : total)
        if (net != 0 || cash[id] != 0) return fail("Position/cash conservation");
    return true;
}
}  // namespace order_book::trading
