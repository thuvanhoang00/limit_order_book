#include <gtest/gtest.h>

#include "order_book/trading/engine.hpp"
#include "order_book/trading/features.hpp"
#include "order_book/trading/replica.hpp"

namespace order_book::trading {
namespace {
Message order(ClientId client, Side side, Price price, Quantity quantity) {
    Message m;
    m.client_id = client;
    m.request_id = 1;
    m.order_id = 1;
    m.instrument_id = 1;
    m.side = side;
    m.price = price;
    m.quantity = quantity;
    return m;
}
TEST(Features, WeightedMicropriceAndMissingQuotes) {
    OrderBook book;
    ASSERT_EQ(book.apply(AddOrder{1, Side::Bid, 98, 10}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{2, Side::Ask, 102, 30}), ApplyResult::Ok);
    FeatureEngine features;
    features.refresh(1, &book);
    ASSERT_TRUE(features.get(1).microprice);
    EXPECT_EQ(*features.get(1).microprice, 99);
    ASSERT_EQ(book.apply(CancelOrder{2}), ApplyResult::Ok);
    features.refresh(1, &book);
    EXPECT_FALSE(features.get(1).microprice);
}
TEST(Features, TradeRatioUsesLiquidityBeforeExecuteRatherThanReducedBook) {
    MatchingEngine engine;
    (void)engine.process(order(1, Side::Bid, 90, 10));
    (void)engine.process(order(2, Side::Ask, 100, 20));
    MarketReplica replica;
    replica.restore(engine.snapshot());
    FeatureEngine features;
    const auto result = engine.process(order(3, Side::Bid, 100, 5));
    for (const auto& m : result.updates) {
        features.before(m, replica.book(m.instrument_id));
        ASSERT_TRUE(replica.consume(m));
        features.after(m, replica.book(m.instrument_id));
    }
    ASSERT_TRUE(features.get(1).aggressive_ratio);
    EXPECT_EQ(*features.get(1).aggressive_ratio, 0.25L);
    EXPECT_EQ(features.get(1).aggressor, Side::Bid);
    ASSERT_TRUE(features.get(1).microprice);
    EXPECT_EQ(*features.get(1).microprice, 94);
}
TEST(Features, ResetDropsTradeSignalsAndInstrumentsStayIndependent) {
    FeatureEngine features;
    OrderBook book;
    ASSERT_EQ(book.apply(AddOrder{1, Side::Bid, 90, 10}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{2, Side::Ask, 100, 10}), ApplyResult::Ok);
    features.refresh(1, &book);
    EXPECT_FALSE(features.get(2).microprice);
    features.reset();
    EXPECT_FALSE(features.get(1).microprice);
    EXPECT_FALSE(features.get(1).aggressive_ratio);
    EXPECT_EQ(features.get(1).trade_id, 0);
}
TEST(Features, PriceTimesAggregatedQuantityDoesNotOverflow) {
    OrderBook book;
    ASSERT_EQ(book.apply(AddOrder{1, Side::Bid, 4000000000U, 4000000000U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{2, Side::Bid, 4000000000U, 4000000000U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{3, Side::Ask, 4000000002U, 4000000000U}), ApplyResult::Ok);
    FeatureEngine features;
    features.refresh(1, &book);
    ASSERT_TRUE(features.get(1).microprice);
    EXPECT_NEAR(static_cast<double>(*features.get(1).microprice), 4000000001.333333, 0.00001);
}
}  // namespace
}  // namespace order_book::trading
