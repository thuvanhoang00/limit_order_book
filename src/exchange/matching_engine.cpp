#include "order_book/exchange/matching_engine.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace order_book::exchange {
void MatchingEngine::add_instrument(InstrumentId id, InstrumentRules rules) {
    if (!id || !rules.tick_size || !rules.lot_size || !rules.min_price ||
        rules.max_price < rules.min_price || !rules.max_quantity || !rules.max_notional)
        throw std::invalid_argument("Invalid instrument rules");
    if (instruments_.contains(id)) throw std::invalid_argument("Instrument already configured");
    auto value = std::make_unique<Instrument>();
    value->rules = rules;
    instruments_.emplace(id, std::move(value));
}
const OrderBook* MatchingEngine::book(InstrumentId id) const noexcept {
    const auto it = instruments_.find(id);
    return it == instruments_.end() ? nullptr : &it->second->book;
}

MatchResult MatchingEngine::process(const Request& r) {
    MatchResult out;
    out.reports.reserve(2);
    const auto response = [&](ReportType type, Status status, OrderId market_id = 0,
                              Price price = 0, Quantity quantity = 0, Quantity leaves = 0) {
        Report report;
        report.type = type;
        report.status = status;
        report.client_id = r.client_id;
        report.request_sequence = r.sequence;
        report.instrument_id = r.instrument_id;
        report.order_id = r.order_id;
        report.market_order_id = market_id;
        report.side = r.side;
        report.price = price;
        report.quantity = quantity;
        report.leaves = leaves;
        out.reports.push_back(report);
    };
    const auto reject = [&](Status status) {
        response(ReportType::Rejected, status);
        response(ReportType::Done, status);
    };
    if (!r.client_id || !r.order_id || !r.sequence ||
        (r.side != Side::Bid && r.side != Side::Ask) ||
        (r.type != RequestType::New && r.type != RequestType::Cancel &&
         r.type != RequestType::Replace) ||
        (r.tif != TimeInForce::Gtc && r.tif != TimeInForce::Ioc && r.tif != TimeInForce::Fok)) {
        reject(Status::InvalidRequest);
        return out;
    }
    const auto instrument = instruments_.find(r.instrument_id);
    if (instrument == instruments_.end()) {
        reject(Status::UnknownInstrument);
        return out;
    }
    auto& state = *instrument->second;
    auto& book = state.book;
    const Key key{r.client_id, r.order_id};
    auto client = state.clients.find(key);
    const auto old_id = client == state.clients.end() ? 0 : client->second;
    const auto old = old_id ? book.order(old_id) : std::nullopt;
    if (r.type == RequestType::New && old) {
        reject(Status::DuplicateOrder);
        return out;
    }
    if (r.type != RequestType::New && !old) {
        reject(Status::UnknownOrder);
        return out;
    }
    // Reserve every possible report/update before touching the book.
    try {
        out.reports.reserve(book.order_count() * 2U + 6U);
        out.updates.reserve(book.order_count() * 2U + 4U);
    } catch (const std::bad_alloc&) {
        reject(Status::Capacity);
        return out;
    }
    if (sequence_ > std::numeric_limits<Sequence>::max() - out.updates.capacity() ||
        next_order_id_ == std::numeric_limits<OrderId>::max() ||
        next_trade_id_ > std::numeric_limits<std::uint64_t>::max() - book.order_count()) {
        reject(Status::Capacity);
        return out;
    }
    const auto update = [&](UpdateType type, const AddOrder& order, std::uint64_t trade = 0) {
        MarketUpdate message;
        message.type = type;
        message.sequence = ++sequence_;
        message.instrument_id = r.instrument_id;
        message.order_id = order.order_id;
        message.trade_id = trade;
        message.side = order.side;
        message.price = order.price;
        message.quantity = order.quantity;
        out.updates.push_back(message);
    };
    const auto remove_identity = [&](OrderId id) {
        const auto owner = state.owners.at(id);
        state.clients.erase({owner.client, owner.order});
        state.owners.erase(id);
    };
    if (r.type == RequestType::Cancel) {
        if (r.price || r.quantity || r.market || r.post_only || r.prevent_self_trade ||
            r.tif != TimeInForce::Gtc) {
            reject(Status::InvalidRequest);
            return out;
        }
        (void)book.apply(CancelOrder{old_id});
        update(UpdateType::Cancel, *old);
        remove_identity(old_id);
        response(ReportType::Canceled, Status::Ok, old_id, old->price, old->quantity, 0);
        out.reports.back().side = old->side;
        response(ReportType::Done, Status::Ok);
        return out;
    }
    const auto& rules = state.rules;
    if (!r.quantity || r.quantity % rules.lot_size) {
        reject(Status::InvalidQuantity);
        return out;
    }
    if (r.quantity > rules.max_quantity) {
        reject(Status::RiskRejected);
        return out;
    }
    if ((!r.market && (!r.price || r.price % rules.tick_size || r.price < rules.min_price ||
                       r.price > rules.max_price)) ||
        (r.market && r.price)) {
        reject(Status::InvalidPrice);
        return out;
    }
    if ((r.market && r.post_only) || (r.post_only && r.tif != TimeInForce::Gtc) ||
        (old && (r.side != old->side || r.market || r.tif != TimeInForce::Gtc))) {
        reject(Status::InvalidRequest);
        return out;
    }
    const auto opposite = r.side == Side::Bid ? Side::Ask : Side::Bid;
    const auto crosses = [&](Price price) {
        return r.market || (r.side == Side::Bid ? r.price >= price : r.price <= price);
    };
    // Market orders use the configured upper band to bound their notional.
    const auto risk_price = r.market ? rules.max_price : r.price;
    if (std::uint64_t{risk_price} * r.quantity > rules.max_notional) {
        reject(Status::RiskRejected);
        return out;
    }
    const auto top = book.top_order(opposite);
    if (r.post_only && top && crosses(top->price)) {
        reject(Status::WouldCross);
        return out;
    }
    if (r.tif == TimeInForce::Fok) {
        std::uint64_t available = 0;
        try {
            const auto snapshot = book.snapshot();
            const auto& levels = opposite == Side::Bid ? snapshot.bids : snapshot.asks;
            bool blocked = false;
            for (const auto& level : levels) {
                if (!crosses(level.price) || blocked || available >= r.quantity) break;
                for (const auto& order : level.orders) {
                    if (r.prevent_self_trade &&
                        state.owners.at(order.order_id).client == r.client_id) {
                        blocked = true;
                        break;
                    }
                    available += order.quantity;
                    if (available >= r.quantity) break;
                }
            }
        } catch (const std::bad_alloc&) {
            reject(Status::Capacity);
            return out;
        }
        if (available < r.quantity) {
            reject(Status::CannotFill);
            return out;
        }
    }
    if (old && r.price == old->price && r.quantity <= old->quantity) {
        if (r.quantity < old->quantity)
            (void)book.apply(ExecuteOrder{old_id, old->quantity - r.quantity});
        update(UpdateType::Modify, AddOrder{old_id, r.side, r.price, r.quantity});
        response(ReportType::Replaced, Status::Ok, old_id, r.price, 0, r.quantity);
        response(ReportType::Done, Status::Ok);
        return out;
    }

    const auto market_id = next_order_id_;
    bool owner_inserted = false;
    bool client_inserted = false;
    const auto rollback_identity = [&] {
        if (owner_inserted) state.owners.erase(market_id);
        if (client_inserted) state.clients.erase(key);
    };
    try {
        state.owners.emplace(market_id, Owner{r.client_id, r.order_id});
        owner_inserted = true;
        if (!old) {
            state.clients.emplace(key, market_id);
            client_inserted = true;
        }
    } catch (const std::bad_alloc&) {
        rollback_identity();
        reject(Status::Capacity);
        return out;
    }
    // Reserve an actual list node and index before matching. It is private to
    // this writer; only the eventual residual is published to market data.
    const Price reservation_price =
        r.market ? (r.side == Side::Bid ? rules.max_price : rules.min_price) : r.price;
    if (book.apply(AddOrder{market_id, r.side, reservation_price, r.quantity}) != ApplyResult::Ok) {
        rollback_identity();
        reject(Status::Capacity);
        return out;
    }
    ++next_order_id_;
    if (old) {
        (void)book.apply(CancelOrder{old_id});
        update(UpdateType::Cancel, *old);
        state.owners.erase(old_id);
        client->second = market_id;
    }
    response(old ? ReportType::Replaced : ReportType::Accepted, Status::Ok, market_id, r.price, 0,
             r.quantity);
    Quantity leaves = r.quantity;
    bool self_trade = false;
    while (leaves) {
        const auto passive = book.top_order(opposite);
        if (!passive || !crosses(passive->price)) break;
        const auto owner = state.owners.at(passive->order_id);
        if (r.prevent_self_trade && owner.client == r.client_id) {
            self_trade = true;
            break;
        }
        const auto fill = std::min(leaves, passive->quantity);
        leaves -= fill;
        (void)book.apply(ExecuteOrder{market_id, fill});
        (void)book.apply(ExecuteOrder{passive->order_id, fill});
        const auto trade = next_trade_id_++;
        response(ReportType::Filled, Status::Ok, market_id, passive->price, fill, leaves);
        out.reports.back().trade_id = trade;
        auto passive_report = out.reports.back();
        passive_report.client_id = owner.client;
        passive_report.order_id = owner.order;
        passive_report.market_order_id = passive->order_id;
        passive_report.request_sequence = 0;
        passive_report.side = opposite;
        passive_report.leaves = passive->quantity - fill;
        out.reports.push_back(passive_report);
        update(UpdateType::Trade, AddOrder{0, r.side, passive->price, fill}, trade);
        update(fill == passive->quantity ? UpdateType::Cancel : UpdateType::Modify,
               AddOrder{passive->order_id, opposite, passive->price, passive->quantity - fill});
        if (fill == passive->quantity) remove_identity(passive->order_id);
    }
    if (leaves && (r.market || r.tif == TimeInForce::Ioc || self_trade)) {
        (void)book.apply(CancelOrder{market_id});
        response(ReportType::Canceled, self_trade ? Status::SelfTrade : Status::Ok, market_id,
                 r.price, leaves, 0);
        leaves = 0;
    }
    if (leaves)
        update(UpdateType::Add, AddOrder{market_id, r.side, r.price, leaves});
    else
        remove_identity(market_id);
    response(ReportType::Done, Status::Ok);
    return out;
}

bool MatchingEngine::validate_invariants(std::string* reason) const {
    const auto fail = [&](const char* message) {
        if (reason) *reason = message;
        return false;
    };
    if (reason) reason->clear();
    for (const auto& [id, pointer] : instruments_) {
        (void)id;
        const auto& state = *pointer;
        if (!state.book.validate_invariants(reason)) return false;
        if (state.book.order_count() != state.owners.size() ||
            state.clients.size() != state.owners.size())
            return fail("Matching identities disagree with resting orders");
        for (const auto& [market, owner] : state.owners) {
            const auto client = state.clients.find({owner.client, owner.order});
            if (!state.book.order(market) || client == state.clients.end() ||
                client->second != market)
                return fail("Missing matching identity");
        }
        const auto bid = state.book.best_bid();
        const auto ask = state.book.best_ask();
        if (bid && ask && *bid >= *ask) return fail("Matching book is crossed");
    }
    return true;
}
}  // namespace order_book::exchange
