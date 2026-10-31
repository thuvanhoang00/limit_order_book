#include <gtest/gtest.h>

#include "order_book/exchange/matching_engine.hpp"
#include "order_book/trading/engine.hpp"

namespace {
using namespace order_book;
using namespace order_book::trading;
Request request(OrderId id, Side side = Side::Bid, Price price = 99, Quantity qty = 5) {
    Request r;
    r.client_id = 1;
    r.sequence = id;
    r.order_id = id;
    r.instrument_id = 1;
    r.side = side;
    r.price = price;
    r.quantity = qty;
    return r;
}
void sync(TradingEngine& engine) {
    FeedBook empty;
    for (const auto& row : snapshot_cycle(empty, 7, 1, 0)) (void)engine.on_market(row);
}
MarketUpdate update(Sequence sequence, OrderId id, Side side, Price price, Quantity qty) {
    MarketUpdate u;
    u.type = UpdateType::Add;
    u.session = 7;
    u.sequence = sequence;
    u.instrument_id = 1;
    u.order_id = id;
    u.side = side;
    u.price = price;
    u.quantity = qty;
    return u;
}
Report report(ReportType type, Sequence sequence, OrderId id, Quantity leaves = 5) {
    Report r;
    r.type = type;
    r.client_id = 1;
    r.sequence = sequence;
    r.request_sequence = 1;
    r.instrument_id = 1;
    r.order_id = id;
    r.market_order_id = id;
    r.side = Side::Bid;
    r.price = 99;
    r.leaves = leaves;
    r.session = 7;
    return r;
}
TEST(TradingPosition, LongShortReversalAndMarkToMarket) {
    Position position;
    position.fill(Side::Bid, 100, 10);
    position.fill(Side::Bid, 110, 10);
    EXPECT_EQ(position.quantity, 20);
    EXPECT_EQ(position.average_price, 105);
    position.mark(108);
    EXPECT_EQ(position.unrealized, 60);
    position.fill(Side::Ask, 115, 25);
    EXPECT_EQ(position.quantity, -5);
    EXPECT_EQ(position.realized, 200);
    EXPECT_EQ(position.average_price, 115);
    position.mark(110);
    EXPECT_EQ(position.pnl(), 225);
    position.fill(Side::Bid, 100, 5);
    EXPECT_EQ(position.quantity, 0);
    EXPECT_EQ(position.realized, 275);
    EXPECT_EQ(position.unrealized, 0);
    EXPECT_EQ(position.average_price, 0);
    EXPECT_EQ(position.volume, 50);
}
TEST(TradingPosition, FullWidthQuantityAndPriceDoNotOverflowMultiplication) {
    Position position;
    position.fill(Side::Bid, 4000000000U, 3000000000U);
    EXPECT_EQ(position.quantity, 3000000000LL);
    position.fill(Side::Ask, 4000000001U, 3000000000U);
    EXPECT_EQ(position.realized, 3000000000.0L);
    EXPECT_EQ(position.volume, 6000000000ULL);
}
TEST(TradingRisk, CountsPendingExposureAndDoesNotReleasePendingCancel) {
    TradingEngine engine(1, {{1, {10, 10, 1000}}});
    EXPECT_EQ(engine.submit(request(1)).status, Status::RiskRejected);
    sync(engine);
    ASSERT_TRUE(engine.submit(request(1, Side::Bid, 99, 6)).request);
    EXPECT_EQ(engine.submit(request(2, Side::Bid, 99, 6)).status, Status::RiskRejected);
    engine.on_report(report(ReportType::Accepted, 1, 1, 6));
    auto cancel = request(1);
    cancel.type = RequestType::Cancel;
    cancel.price = cancel.quantity = 0;
    ASSERT_TRUE(engine.submit(cancel).request);
    EXPECT_EQ(engine.submit(request(2, Side::Bid, 99, 6)).status, Status::RiskRejected);
    engine.on_report(report(ReportType::Canceled, 2, 1, 0));
    EXPECT_TRUE(engine.submit(request(2, Side::Bid, 99, 6)).request);
}
TEST(TradingRisk, ReplacementReservesIncreasedQuantityAndRejectionRestoresRemaining) {
    TradingEngine engine(1, {{1, {20, 10, 1000}}});
    sync(engine);
    ASSERT_TRUE(engine.submit(request(1)).request);
    engine.on_report(report(ReportType::Accepted, 1, 1));
    auto replace = request(1, Side::Bid, 98, 9);
    replace.type = RequestType::Replace;
    ASSERT_TRUE(engine.submit(replace).request);
    EXPECT_EQ(engine.submit(request(2, Side::Bid, 99, 2)).status, Status::RiskRejected);
    auto fill = report(ReportType::Filled, 2, 1, 3);
    fill.quantity = 2;
    engine.on_report(fill);
    auto rejected = report(ReportType::Rejected, 3, 1);
    rejected.status = Status::InvalidPrice;
    engine.on_report(rejected);
    ASSERT_NE(engine.order(1, 1), nullptr);
    EXPECT_EQ(engine.order(1, 1)->state, OrderState::Live);
    EXPECT_EQ(engine.order(1, 1)->leaves, 3);
    EXPECT_EQ(engine.order(1, 1)->order.price, 99);
    EXPECT_TRUE(engine.submit(request(2, Side::Bid, 99, 5)).request);
}
TEST(TradingRisk, LossAndNotionalBlockNewOrdersAndReportsCannotBeReplayed) {
    TradingEngine engine(1, {{1, {10, 20, 10, 1000}}});
    sync(engine);
    EXPECT_EQ(engine.submit(request(1, Side::Bid, 101, 10)).status, Status::RiskRejected);
    ASSERT_TRUE(engine.submit(request(1, Side::Bid, 100, 10)).request);
    auto accepted = report(ReportType::Accepted, 1, 1, 10);
    accepted.price = 100;
    engine.on_report(accepted);
    auto fill = report(ReportType::Filled, 2, 1, 0);
    fill.price = 100;
    fill.quantity = 10;
    engine.on_report(fill);
    EXPECT_THROW(engine.on_report(fill), std::runtime_error);
    EXPECT_EQ(engine.position(1).quantity, 10);
    (void)engine.on_market(update(1, 8, Side::Bid, 90, 5));
    (void)engine.on_market(update(2, 9, Side::Ask, 92, 5));
    EXPECT_LT(engine.position(1).pnl(), -10);
    EXPECT_EQ(engine.submit(request(2, Side::Ask, 92, 1)).status, Status::RiskRejected);
}
TEST(TradingStrategies, MicropriceMakerAndFeedRecoveryGate) {
    TradingEngine engine(1);
    sync(engine);
    (void)engine.on_market(update(1, 8, Side::Bid, 98, 10));
    (void)engine.on_market(update(2, 9, Side::Ask, 102, 30));
    ASSERT_TRUE(engine.features(1).fair_price);
    EXPECT_EQ(*engine.features(1).fair_price, 99);
    StrategyConfig config;
    config.algorithm = Algorithm::MarketMaker;
    config.clip = 3;
    const auto quotes = engine.step(config);
    ASSERT_EQ(quotes.size(), 2);
    EXPECT_EQ(quotes[0].price, 98);
    EXPECT_EQ(quotes[1].price, 100);
    EXPECT_TRUE(quotes[0].post_only);
    EXPECT_TRUE(engine.step(config).empty());  // Pending orders suppress duplicate quotes.
    auto gap = update(4, 10, Side::Bid, 97, 10);
    EXPECT_EQ(engine.on_market(gap), FeedResult::Recovering);
    EXPECT_TRUE(engine.step(config).empty());
}
TEST(TradingRisk, ANewMulticastBootCannotTradeThroughAnOldTcpSession) {
    TradingEngine engine(1);
    sync(engine);
    ASSERT_TRUE(engine.submit(request(1)).request);
    engine.on_report(report(ReportType::Accepted, 1, 1));
    MarketUpdate heartbeat;
    heartbeat.session = 8;
    (void)engine.on_market(heartbeat);
    ASSERT_TRUE(engine.feed().synchronized());
    EXPECT_EQ(engine.submit(request(2)).status, Status::RiskRejected);
    auto cancel = request(1);
    cancel.type = RequestType::Cancel;
    cancel.price = cancel.quantity = 0;
    EXPECT_TRUE(engine.submit(cancel).request);
}
TEST(TradingStrategies, TakerConsumesEachAggressiveSignalAtMostOnce) {
    TradingEngine engine(1);
    sync(engine);
    (void)engine.on_market(update(1, 8, Side::Bid, 98, 10));
    (void)engine.on_market(update(2, 9, Side::Ask, 102, 10));
    auto trade = update(3, 0, Side::Bid, 102, 8);
    trade.type = UpdateType::Trade;
    trade.trade_id = 1;
    (void)engine.on_market(trade);
    StrategyConfig config;
    config.algorithm = Algorithm::LiquidityTaker;
    const auto orders = engine.step(config);
    ASSERT_EQ(orders.size(), 1);
    EXPECT_EQ(orders[0].price, 102);
    EXPECT_EQ(orders[0].side, Side::Bid);
    EXPECT_EQ(orders[0].tif, TimeInForce::Ioc);
    EXPECT_TRUE(engine.step(config).empty());
}
TEST(TradingStrategies, SeededRandomOrdersAreReproducible) {
    TradingEngine one(1), two(1);
    sync(one);
    sync(two);
    StrategyConfig config;
    config.algorithm = Algorithm::Random;
    for (unsigned i = 0; i < 50; ++i) EXPECT_EQ(one.step(config), two.step(config));
}
TEST(TradingLifecycle, ExchangeReportsUpdateOrderManagerAndBothPositions) {
    MatchingEngine exchange;
    exchange.add_instrument(1);
    TradingEngine buyer(1), seller(2);
    sync(buyer);
    sync(seller);
    Sequence buyer_sequence = 0, seller_sequence = 0;
    const auto process = [&](Request r) {
        const auto result = exchange.process(r);
        for (auto u : result.updates) {
            u.session = 7;
            (void)buyer.on_market(u);
            (void)seller.on_market(u);
        }
        for (auto response : result.reports) {
            response.session = 7;
            if (response.client_id == 1) {
                response.sequence = ++buyer_sequence;
                buyer.on_report(response);
            }
            if (response.client_id == 2) {
                response.sequence = ++seller_sequence;
                seller.on_report(response);
            }
        }
    };
    const auto ask = seller.submit(request(1, Side::Ask, 100, 5));
    ASSERT_TRUE(ask.request);
    process(*ask.request);
    const auto bid = buyer.submit(request(1, Side::Bid, 101, 3));
    ASSERT_TRUE(bid.request);
    process(*bid.request);
    EXPECT_EQ(buyer.position(1).quantity, 3);
    EXPECT_EQ(seller.position(1).quantity, -3);
    EXPECT_EQ(buyer.active_orders(), 0);
    EXPECT_EQ(seller.order(1, 1)->leaves, 2);
    for (const auto& cancel : seller.cancel_all()) process(cancel);
    EXPECT_EQ(seller.active_orders(), 0);
    EXPECT_EQ(exchange.book(1)->order_count(), 0);
}
}  // namespace
