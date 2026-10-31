#include "order_book/trading/engine.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace order_book::trading {
void Position::fill(Side side, Price price, Quantity size) {
    const auto old = quantity;
    const auto delta = side == Side::Bid ? std::int64_t{size} : -std::int64_t{size};
    quantity += delta;
    volume += size;
    if (!old || (old > 0) == (delta > 0)) {
        average_price = (average_price * std::abs(old) + static_cast<long double>(price) * size) /
                        std::abs(quantity);
    } else {
        const auto closed = std::min<std::int64_t>(std::abs(old), size);
        realized += closed * (static_cast<long double>(price) - average_price) * (old > 0 ? 1 : -1);
        if (!quantity)
            average_price = 0;
        else if ((old > 0) != (quantity > 0))
            average_price = price;
    }
    mark(price);
}
void Position::mark(long double price) noexcept { unrealized = quantity * (price - average_price); }
void Features::book(const OrderBook* value) noexcept {
    fair_price.reset();
    if (!value) {
        aggressive_ratio.reset();
        return;
    }
    const auto bid = value->best_bid();
    const auto ask = value->best_ask();
    if (!bid || !ask) {
        aggressive_ratio.reset();
        return;
    }
    const auto bq = value->best_quantity(Side::Bid);
    const auto aq = value->best_quantity(Side::Ask);
    fair_price = (static_cast<long double>(*bid) * aq + static_cast<long double>(*ask) * bq) /
                 (static_cast<long double>(bq) + aq);
}
void Features::trade(const MarketUpdate& update, const OrderBook* value) noexcept {
    trade_id = update.trade_id;
    aggressor = update.side;
    aggressive_ratio.reset();
    if (!value) return;
    const auto opposite = update.side == Side::Bid ? Side::Ask : Side::Bid;
    const auto quantity = value->best_quantity(opposite);
    if (quantity) aggressive_ratio = static_cast<long double>(update.quantity) / quantity;
}
Quantity ManagedOrder::reserved() const noexcept {
    if (state == OrderState::Dead) return 0;
    return pending && pending->type == RequestType::Replace ? std::max(leaves, pending->quantity)
                                                            : leaves;
}
TradingEngine::TradingEngine(ClientId client, std::map<InstrumentId, RiskLimits> limits,
                             std::uint64_t seed)
    : client_(client), limits_(std::move(limits)), random_(seed) {
    if (!client_ || limits_.empty())
        throw std::invalid_argument("Client and risk limits are required");
    for (const auto& [id, limit] : limits_) {
        if (!id || !limit.max_order_quantity || limit.max_position <= 0 ||
            !std::isfinite(limit.max_loss) || limit.max_loss < 0 || !limit.max_notional)
            throw std::invalid_argument("Invalid risk limits");
        positions_.try_emplace(id);
        features_.try_emplace(id);
    }
}
const Position& TradingEngine::position(InstrumentId id) const { return positions_.at(id); }
const Features& TradingEngine::features(InstrumentId id) const { return features_.at(id); }
const ManagedOrder* TradingEngine::order(InstrumentId id, OrderId order_id) const noexcept {
    const auto it = orders_.find({id, order_id});
    return it == orders_.end() ? nullptr : &it->second;
}
std::size_t TradingEngine::active_orders() const noexcept {
    std::size_t count = 0;
    for (const auto& [key, order] : orders_) {
        (void)key;
        count += order.state != OrderState::Dead;
    }
    return count;
}
Status TradingEngine::risk(const Request& r, const ManagedOrder* replacing) const {
    const auto limits = limits_.find(r.instrument_id);
    if (limits == limits_.end()) return Status::UnknownInstrument;
    const auto& limit = limits->second;
    const auto& position = positions_.at(r.instrument_id);
    if (!r.quantity || r.quantity > limit.max_order_quantity || position.pnl() < -limit.max_loss)
        return Status::RiskRejected;
    Price price = r.price;
    if (r.market) {
        // No finite notional bound can be guaranteed for an unpriced market
        // order. A configured notional limit requires a priced IOC instead.
        if (limit.max_notional != std::numeric_limits<std::uint64_t>::max())
            return Status::RiskRejected;
        price = 0;
    }
    if (std::uint64_t{price} * r.quantity > limit.max_notional) return Status::RiskRejected;
    long double reserved = r.quantity;
    for (const auto& [key, order] : orders_) {
        if (key.first == r.instrument_id && &order != replacing && order.order.side == r.side)
            reserved += order.reserved();
    }
    const auto projected =
        static_cast<long double>(position.quantity) + (r.side == Side::Bid ? reserved : -reserved);
    return std::abs(projected) > limit.max_position ? Status::RiskRejected : Status::Ok;
}
SubmitResult TradingEngine::submit(Request r) {
    if (r.type != RequestType::Cancel && !feed_.synchronized())
        return {Status::RiskRejected, std::nullopt};
    if (r.type != RequestType::Cancel && report_session_ && feed_.session() != report_session_)
        return {Status::RiskRejected, std::nullopt};
    if ((r.side != Side::Bid && r.side != Side::Ask) || !r.order_id ||
        (r.type != RequestType::New && r.type != RequestType::Cancel &&
         r.type != RequestType::Replace))
        return {Status::InvalidRequest, std::nullopt};
    const Key key{r.instrument_id, r.order_id};
    auto it = orders_.find(key);
    auto* old = it == orders_.end() ? nullptr : &it->second;
    if (r.type == RequestType::New) {
        if (old && old->state != OrderState::Dead) return {Status::DuplicateOrder, std::nullopt};
    } else {
        if (!old || old->state == OrderState::Dead) return {Status::UnknownOrder, std::nullopt};
        if (old->state != OrderState::Live || old->order.side != r.side)
            return {Status::InvalidRequest, std::nullopt};
    }
    if (r.type != RequestType::Cancel) {
        const auto status = risk(r, old);
        if (status != Status::Ok) return {status, std::nullopt};
    }
    r.client_id = client_;
    r.sequence = next_sequence_;
    if (r.type == RequestType::New) {
        ManagedOrder value;
        value.order = r;
        value.leaves = r.quantity;
        orders_.insert_or_assign(key, value);
    } else {
        old->state =
            r.type == RequestType::Cancel ? OrderState::PendingCancel : OrderState::PendingReplace;
        old->pending = r;
    }
    ++next_sequence_;
    if (r.order_id >= next_order_ && r.order_id < std::numeric_limits<OrderId>::max())
        next_order_ = r.order_id + 1;
    return {Status::Ok, r};
}
void TradingEngine::on_report(const Report& r) {
    if (r.client_id != client_ || !r.session || r.sequence != response_sequence_ + 1 ||
        (report_session_ && r.session != report_session_))
        throw std::runtime_error("Invalid trading report sequence/session");
    report_session_ = r.session;
    response_sequence_ = r.sequence;
    if (r.type == ReportType::Done) {
        completed_ = std::max(completed_, r.request_sequence);
        return;
    }
    auto it = orders_.find({r.instrument_id, r.order_id});
    if (it == orders_.end()) throw std::runtime_error("Report refers to an untracked client order");
    auto& order = it->second;
    switch (r.type) {
        case ReportType::Accepted:
        case ReportType::Replaced:
            if (order.pending) order.order = *order.pending;
            order.pending.reset();
            order.market_order_id = r.market_order_id;
            order.order.price = r.price;
            order.leaves = r.leaves;
            order.state = r.leaves ? OrderState::Live : OrderState::Dead;
            break;
        case ReportType::Filled:
            if (!r.quantity || r.market_order_id != order.market_order_id)
                throw std::runtime_error("Invalid execution report identity");
            positions_.at(r.instrument_id).fill(r.side, r.price, r.quantity);
            ++fills_;
            order.leaves = r.leaves;
            if (!order.pending) order.state = r.leaves ? OrderState::Live : OrderState::Dead;
            break;
        case ReportType::Canceled:
            order.leaves = 0;
            order.pending.reset();
            order.state = OrderState::Dead;
            break;
        case ReportType::Rejected:
            if (order.pending) {
                order.pending.reset();
                order.state = order.leaves ? OrderState::Live : OrderState::Dead;
            } else {
                order.leaves = 0;
                order.state = OrderState::Dead;
            }
            break;
        case ReportType::Done:
            break;
    }
    if (order.state == OrderState::Dead) orders_.erase(it);
}
void TradingEngine::refresh(InstrumentId id) {
    if (!limits_.contains(id)) return;
    const auto* book = feed_.books().book(id);
    features_.at(id).book(book);
    if (book && book->best_bid() && book->best_ask())
        positions_.at(id).mark((static_cast<long double>(*book->best_bid()) + *book->best_ask()) /
                               2);
}
FeedResult TradingEngine::on_market(const MarketUpdate& u) {
    const auto result = u.snapshot_id ? feed_.snapshot(u) : feed_.incremental(u);
    if (result == FeedResult::Applied) {
        if (u.type == UpdateType::Trade && limits_.contains(u.instrument_id))
            features_.at(u.instrument_id).trade(u, feed_.books().book(u.instrument_id));
        refresh(u.instrument_id);
    } else if (result == FeedResult::Recovered) {
        for (auto& [id, feature] : features_) {
            feature = Features{};
            refresh(id);
        }
    }
    return result;
}
std::vector<Request> TradingEngine::step(const StrategyConfig& config) {
    std::vector<Request> requests;
    if (!feed_.synchronized() || config.algorithm == Algorithm::Script) return requests;
    if (!config.clip || !config.tick_size || !config.edge || !std::isfinite(config.trade_ratio) ||
        config.trade_ratio < 0)
        throw std::invalid_argument("Invalid strategy configuration");
    const auto send = [&](Request r) {
        if (auto result = submit(r); result.request) requests.push_back(*result.request);
    };
    const auto quote = [&](InstrumentId id, Side side, Price price) {
        Request request;
        request.instrument_id = id;
        request.side = side;
        request.price = price;
        request.quantity = config.clip;
        request.order_id = next_order_;
        request.prevent_self_trade = true;
        for (const auto& [key, order] : orders_) {
            if (key.first != id || order.order.side != side || order.state == OrderState::Dead)
                continue;
            if (order.state != OrderState::Live) return;
            if (order.order.price == price && order.leaves == config.clip) return;
            request.type = RequestType::Replace;
            request.order_id = order.order.order_id;
            break;
        }
        request.post_only = true;
        send(request);
    };
    for (const auto& [id, limits] : limits_) {
        (void)limits;
        const auto* book = feed_.books().book(id);
        const auto& feature = features_.at(id);
        if (config.algorithm == Algorithm::MarketMaker) {
            if (!book || !feature.fair_price) continue;
            const auto bid = std::floor((*feature.fair_price - config.edge) / config.tick_size) *
                             config.tick_size;
            const auto ask = std::ceil((*feature.fair_price + config.edge) / config.tick_size) *
                             config.tick_size;
            if (bid >= 1 && ask <= std::numeric_limits<Price>::max()) {
                quote(id, Side::Bid, static_cast<Price>(bid));
                quote(id, Side::Ask, static_cast<Price>(ask));
            }
        } else if (config.algorithm == Algorithm::LiquidityTaker) {
            if (!book || !feature.aggressive_ratio ||
                *feature.aggressive_ratio < config.trade_ratio ||
                feature.trade_id <= consumed_trades_[id])
                continue;
            consumed_trades_[id] = feature.trade_id;
            const auto price = feature.aggressor == Side::Bid ? book->best_ask() : book->best_bid();
            if (!price) continue;
            Request r;
            r.instrument_id = id;
            r.order_id = next_order_;
            r.side = feature.aggressor;
            r.price = *price;
            r.quantity = config.clip;
            r.tif = TimeInForce::Ioc;
            r.prevent_self_trade = true;
            send(r);
        } else {
            if (random_() % 3 == 0) {
                for (const auto& [key, order] : orders_) {
                    if (key.first == id && order.state == OrderState::Live) {
                        Request cancel;
                        cancel.type = RequestType::Cancel;
                        cancel.instrument_id = id;
                        cancel.order_id = key.second;
                        cancel.side = order.order.side;
                        send(cancel);
                        break;
                    }
                }
            } else {
                Request r;
                r.instrument_id = id;
                r.order_id = next_order_;
                r.side = random_() % 2 ? Side::Bid : Side::Ask;
                const auto top = book ? (r.side == Side::Bid ? book->best_bid() : book->best_ask())
                                      : std::nullopt;
                const auto base = top.value_or(r.side == Side::Bid ? 9990U : 10010U);
                r.price = std::max(config.tick_size, (base / config.tick_size) * config.tick_size);
                r.quantity = config.clip;
                r.prevent_self_trade = true;
                send(r);
            }
        }
    }
    return requests;
}
std::vector<Request> TradingEngine::cancel_all() {
    std::vector<Request> requests;
    for (const auto& [key, order] : orders_) {
        if (order.state != OrderState::Live) continue;
        Request cancel;
        cancel.type = RequestType::Cancel;
        cancel.instrument_id = key.first;
        cancel.order_id = key.second;
        cancel.side = order.order.side;
        if (auto result = submit(cancel); result.request) requests.push_back(*result.request);
    }
    return requests;
}
}  // namespace order_book::trading
