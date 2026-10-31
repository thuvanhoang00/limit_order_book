#include <gtest/gtest.h>

#include "order_book/exchange/market_data.hpp"
#include "order_book/exchange/protocol.hpp"

namespace {
using namespace order_book;
using namespace order_book::exchange;
MarketUpdate add(Sequence seq, OrderId order, Side side = Side::Bid, Price price = 99,
                 Quantity qty = 5) {
    MarketUpdate u;
    u.type = UpdateType::Add;
    u.session = 7;
    u.sequence = seq;
    u.instrument_id = 1;
    u.order_id = order;
    u.side = side;
    u.price = price;
    u.quantity = qty;
    return u;
}
TEST(MarketRecovery, CompleteSnapshotPlusBufferedIncrementalsCommitsAtomicallyInFifoOrder) {
    FeedBook source;
    ASSERT_TRUE(source.apply(add(1, 1)));
    ASSERT_TRUE(source.apply(add(2, 2)));
    auto cycle = snapshot_cycle(source, 7, 1, 2);
    FeedConsumer consumer;
    auto modify = add(3, 1);
    modify.type = UpdateType::Modify;
    modify.quantity = 2;
    auto cancel = add(4, 2);
    cancel.type = UpdateType::Cancel;
    EXPECT_EQ(consumer.incremental(cancel), FeedResult::Recovering);
    EXPECT_EQ(consumer.snapshot(cycle[0]), FeedResult::Recovering);
    EXPECT_EQ(consumer.snapshot(cycle[1]), FeedResult::Recovering);
    EXPECT_EQ(consumer.books().order_count(), 0);
    EXPECT_EQ(consumer.incremental(modify), FeedResult::Recovering);
    EXPECT_EQ(consumer.snapshot(cycle[2]), FeedResult::Recovering);
    EXPECT_EQ(consumer.snapshot(cycle[3]), FeedResult::Recovered);
    EXPECT_TRUE(consumer.synchronized());
    EXPECT_EQ(consumer.sequence(), 4);
    EXPECT_EQ(consumer.books().book(1)->top_order(Side::Bid)->order_id, 1);
    EXPECT_EQ(consumer.books().book(1)->best_quantity(Side::Bid), 2);
    EXPECT_EQ(consumer.incremental(cancel), FeedResult::Dropped);
}
TEST(MarketRecovery, MissingSnapshotPacketCannotExposePartialStateAndNewCycleRecovers) {
    FeedBook source;
    ASSERT_TRUE(source.apply(add(1, 1)));
    ASSERT_TRUE(source.apply(add(2, 2)));
    FeedConsumer consumer;
    auto cycle = snapshot_cycle(source, 7, 1, 2);
    (void)consumer.snapshot(cycle[0]);
    (void)consumer.snapshot(cycle[2]);
    (void)consumer.snapshot(cycle[3]);
    EXPECT_FALSE(consumer.synchronized());
    EXPECT_EQ(consumer.books().order_count(), 0);
    cycle = snapshot_cycle(source, 7, 2, 2);
    for (const auto& row : cycle) (void)consumer.snapshot(row);
    ASSERT_TRUE(consumer.synchronized());
    EXPECT_EQ(consumer.books().book(1)->snapshot(), source.book(1)->snapshot());
    EXPECT_EQ(consumer.books().book(1)->top_order(Side::Bid)->order_id, 1);
}
TEST(MarketRecovery, HeartbeatDetectsLostTailAndBootEpochRejectsOldFeed) {
    FeedConsumer consumer;
    EXPECT_EQ(consumer.incremental(add(1, 1)), FeedResult::Applied);
    MarketUpdate heartbeat;
    heartbeat.session = 7;
    heartbeat.sequence = heartbeat.watermark = 2;
    EXPECT_EQ(consumer.incremental(heartbeat), FeedResult::Recovering);
    EXPECT_FALSE(consumer.synchronized());
    EXPECT_EQ(consumer.sequence(), 1);
    FeedBook source;
    ASSERT_TRUE(source.apply(add(1, 1)));
    ASSERT_TRUE(source.apply(add(2, 2)));
    for (const auto& u : snapshot_cycle(source, 7, 1, 2)) (void)consumer.snapshot(u);
    ASSERT_TRUE(consumer.synchronized());
    heartbeat.session = 8;
    heartbeat.sequence = heartbeat.watermark = 0;
    (void)consumer.incremental(heartbeat);
    EXPECT_TRUE(consumer.synchronized());
    EXPECT_EQ(consumer.books().order_count(), 0);
    EXPECT_EQ(consumer.incremental(add(3, 3)), FeedResult::Dropped);
    EXPECT_EQ(consumer.session(), 8);
}
TEST(MarketRecovery, BoundedBuffersAndInvalidUpdatesRequireFreshSnapshot) {
    FeedConsumer consumer(2);
    (void)consumer.incremental(add(3, 1));
    (void)consumer.incremental(add(4, 2));
    (void)consumer.incremental(add(5, 3));
    EXPECT_LE(consumer.buffered(), 2);
    EXPECT_FALSE(consumer.synchronized());
    FeedBook empty;
    for (const auto& u : snapshot_cycle(empty, 7, 1, 5)) (void)consumer.snapshot(u);
    ASSERT_TRUE(consumer.synchronized());
    auto bad = add(6, 1);
    bad.type = UpdateType::Modify;
    EXPECT_EQ(consumer.incremental(bad), FeedResult::Recovering);
    EXPECT_EQ(consumer.sequence(), 5);
    EXPECT_FALSE(consumer.synchronized());
}
TEST(MarketRecovery, SnapshotCannotDeclareSyncAheadOfAnObservedGap) {
    FeedConsumer consumer;
    (void)consumer.incremental(add(4, 4));
    FeedBook source;
    ASSERT_TRUE(source.apply(add(1, 1)));
    for (const auto& row : snapshot_cycle(source, 7, 1, 1)) (void)consumer.snapshot(row);
    EXPECT_FALSE(consumer.synchronized());
    (void)consumer.incremental(add(2, 2));
    EXPECT_FALSE(consumer.synchronized());
    EXPECT_EQ(consumer.incremental(add(3, 3)), FeedResult::Recovered);
    EXPECT_EQ(consumer.sequence(), 4);
    EXPECT_EQ(consumer.books().order_count(), 4);
}
TEST(MarketRecovery, CrossedSnapshotCannotBecomeATradableBook) {
    FeedConsumer consumer;
    auto begin = add(0, 0);
    begin.type = UpdateType::SnapshotBegin;
    begin.instrument_id = begin.price = begin.quantity = 0;
    begin.snapshot_id = 1;
    begin.watermark = 2;
    begin.count = 2;
    (void)consumer.snapshot(begin);
    auto bid = add(1, 1, Side::Bid, 101);
    bid.snapshot_id = 1;
    bid.watermark = 2;
    auto ask = add(2, 2, Side::Ask, 100);
    ask.snapshot_id = 1;
    ask.watermark = 2;
    (void)consumer.snapshot(bid);
    (void)consumer.snapshot(ask);
    auto end = begin;
    end.type = UpdateType::SnapshotEnd;
    end.sequence = 3;
    (void)consumer.snapshot(end);
    EXPECT_FALSE(consumer.synchronized());
    EXPECT_EQ(consumer.books().order_count(), 0);
}

TEST(MarketRecovery, GapAfterIgnoredBeginWaitsForTheNextCompletePeriodicCycle) {
    FeedConsumer consumer;
    EXPECT_EQ(consumer.incremental(add(1, 1)), FeedResult::Applied);
    FeedBook source;
    ASSERT_TRUE(source.apply(add(1, 1)));
    ASSERT_TRUE(source.apply(add(2, 2)));
    ASSERT_TRUE(source.apply(add(3, 3)));
    const auto cycle = snapshot_cycle(source, 7, 1, 3);
    EXPECT_EQ(consumer.snapshot(cycle.front()), FeedResult::Dropped);
    EXPECT_EQ(consumer.incremental(add(3, 3)), FeedResult::Recovering);
    for (std::size_t i = 1; i < cycle.size(); ++i) (void)consumer.snapshot(cycle[i]);
    EXPECT_FALSE(consumer.synchronized());
    for (const auto& row : snapshot_cycle(source, 7, 2, 3)) (void)consumer.snapshot(row);
    EXPECT_TRUE(consumer.synchronized());
    EXPECT_EQ(consumer.sequence(), 3);
    EXPECT_EQ(consumer.books().order_count(), 3);
}
TEST(TradingWire, RequestReportsAndMarketUpdatesRoundTripWithoutHostPadding) {
    Request request;
    request.type = RequestType::Replace;
    request.client_id = 0x0102030405060708ULL;
    request.sequence = 9;
    request.instrument_id = 2;
    request.order_id = 99;
    request.side = Side::Ask;
    request.price = 0xffffffffU;
    request.quantity = 0xfffffffeU;
    request.tif = TimeInForce::Fok;
    request.prevent_self_trade = true;
    const auto frame = wire::encode(request);
    EXPECT_EQ(wire::decode(frame), request);
    EXPECT_EQ(frame[16], 1);
    EXPECT_EQ(frame[23], 8);
    EXPECT_EQ(frame[48], 255);
    Report report;
    report.type = ReportType::Filled;
    report.client_id = 99;
    report.sequence = 5;
    report.price = 100;
    report.quantity = 7;
    report.leaves = 8;
    report.session = 9;
    EXPECT_EQ(wire::decode(wire::encode(report)), report);
    auto update = add(1, 1);
    EXPECT_EQ(wire::decode(wire::encode(update)), update);
    FeedBook source;
    ASSERT_TRUE(source.apply(update));
    for (const auto& row : snapshot_cycle(source, 7, 5, 1))
        EXPECT_EQ(wire::decode(wire::encode(row)), row);
}
TEST(TradingWire, RejectsMalformedHeadersReservedBitsEnumsAndMarketFields) {
    Request r;
    const auto valid = wire::encode(r);
    for (auto offset : {0U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 56U}) {
        auto broken = valid;
        broken[offset] = 255;
        EXPECT_FALSE(wire::decode(broken)) << offset;
    }
    auto report = wire::encode(Report{});
    report[84] = 1;
    EXPECT_FALSE(wire::decode(report));
    auto market = wire::encode(add(1, 1));
    market[72] = 1;
    EXPECT_FALSE(wire::decode(market));
    market = wire::encode(add(1, 1));
    market[6] = 99;
    EXPECT_FALSE(wire::decode(market));
    MarketUpdate invalid;
    EXPECT_THROW((void)wire::encode(invalid), std::invalid_argument);
}
}  // namespace
