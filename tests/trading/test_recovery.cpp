#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "order_book/trading/engine.hpp"
#include "order_book/trading/journal.hpp"
#include "order_book/trading/replica.hpp"
#include "order_book/trading/strategy.hpp"

namespace order_book::trading {
namespace {
Message add(ClientId client, std::uint64_t request, OrderId id, Side side, Price price,
            Quantity quantity) {
    Message c;
    c.client_id = client;
    c.request_id = request;
    c.order_id = id;
    c.instrument_id = 1;
    c.side = side;
    c.price = price;
    c.quantity = quantity;
    return c;
}
TEST(TradingProtocol, IndependentBigEndianFixtureAndAllFieldsRoundTrip) {
    Message c = add(0x01020304U, 0x0102030405060708ULL, 77, Side::Ask, 123, 5);
    c.kind = Kind::Report;
    c.report = ReportType::Fill;
    c.epoch = 9;
    c.sequence = 11;
    c.exchange_id = 91;
    c.trade_id = 82;
    c.remaining = 2;
    const auto bytes = encode(c);
    EXPECT_EQ(bytes[0], 'L');
    EXPECT_EQ(bytes[1], 'T');
    EXPECT_EQ(bytes[2], 'R');
    EXPECT_EQ(bytes[3], '1');
    EXPECT_EQ(bytes[32], 1);
    EXPECT_EQ(bytes[35], 4);
    EXPECT_EQ(bytes[40], 1);
    EXPECT_EQ(bytes[47], 8);
    EXPECT_EQ(bytes[39], 123);
    EXPECT_EQ(bytes[75], 5);
    EXPECT_EQ(bytes[79], 2);
    EXPECT_EQ(bytes[87], 82);
    EXPECT_EQ(decode(bytes), c);
}
TEST(TradingProtocol, BadVersionsEnumsFlagsAndReservedBytesAreRejected) {
    const auto good = encode(add(1, 1, 1, Side::Bid, 100, 5));
    for (const auto offset : {0U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 88U, 95U}) {
        auto bytes = good;
        bytes[offset] = 255;
        EXPECT_FALSE(decode(bytes)) << offset;
    }
}
TEST(MarketRecovery, GapTailLossAndEpochChangeInvalidateUntilSnapshotRestore) {
    MatchingEngine e;
    MarketReplica r;
    r.restore(e.snapshot());
    const auto first = e.process(add(1, 1, 1, Side::Bid, 90, 5));
    const auto second = e.process(add(2, 1, 1, Side::Ask, 100, 5));
    EXPECT_FALSE(r.consume(second.updates.front()));
    EXPECT_FALSE(r.synchronized());
    EXPECT_FALSE(r.consume(first.updates.front()));
    r.restore(e.snapshot());
    EXPECT_EQ(r.sequence(), e.sequence());
    EXPECT_EQ(r.book(1)->snapshot(), e.book(1).snapshot());
    EXPECT_TRUE(r.consume(first.updates.front()));  // A queued old incremental after restore.
    Message hb;
    hb.kind = Kind::Heartbeat;
    hb.epoch = e.epoch();
    hb.sequence = e.sequence() + 1;
    EXPECT_FALSE(r.consume(hb));  // Detect a lost tail without a following delta.
    r.restore(e.snapshot());
    hb.epoch = e.epoch() + 1;
    hb.sequence = 0;
    EXPECT_FALSE(r.consume(hb));
    EXPECT_THROW((void)r.snapshot(), std::logic_error);
}
TEST(MarketRecovery, IncompleteInconsistentAndDuplicateSnapshotsNeverBecomeVisible) {
    MatchingEngine e;
    (void)e.process(add(1, 1, 1, Side::Bid, 90, 5));
    auto rows = e.snapshot();
    MarketReplica r;
    auto missing = rows;
    missing.pop_back();
    EXPECT_THROW(r.restore(missing), std::runtime_error);
    auto watermark = rows;
    ++watermark[1].sequence;
    EXPECT_THROW(r.restore(watermark), std::runtime_error);
    auto duplicate = rows;
    duplicate.insert(duplicate.begin() + 1, rows[1]);
    ++duplicate.front().quantity;
    ++duplicate.back().quantity;
    EXPECT_THROW(r.restore(duplicate), std::runtime_error);
    r.restore(rows);
    EXPECT_TRUE(r.synchronized());
    EXPECT_EQ(r.order_count(), 1);
}
TEST(MarketRecovery, ReduceCancelAndTradeDeltasMirrorMatchingState) {
    MatchingEngine e;
    MarketReplica r;
    r.restore(e.snapshot());
    const auto consume = [&](const Batch& b) {
        for (const auto& m : b.updates) ASSERT_TRUE(r.consume(m));
    };
    consume(e.process(add(1, 1, 1, Side::Ask, 100, 8)));
    auto reduce = add(1, 2, 1, Side::Ask, 100, 5);
    reduce.action = Action::Replace;
    consume(e.process(reduce));
    EXPECT_EQ(r.book(1)->front(Side::Ask)->quantity, 5);
    consume(e.process(add(2, 1, 1, Side::Bid, 100, 3)));
    reduce.action = Action::Cancel;
    reduce.request_id = 3;
    consume(e.process(reduce));
    EXPECT_EQ(r.book(1)->order_count(), 0);
    EXPECT_EQ(r.book(1)->snapshot(), e.book(1).snapshot());
    EXPECT_EQ(r.sequence(), e.sequence());
}
class JournalTest : public ::testing::Test {
   protected:
    void SetUp() override {
        char pattern[] = "/tmp/lob-trading-journal-XXXXXX";
        const auto* directory = mkdtemp(pattern);
        ASSERT_NE(directory, nullptr);
        directory_ = directory;
        path_ = (directory_ / "commands.journal").string();
    }
    void TearDown() override { std::filesystem::remove_all(directory_); }
    std::filesystem::path directory_;
    std::string path_;
};
TEST_F(JournalTest, DurableReplayReproducesBooksPositionsIdsAndLastRequestCache) {
    MatchingEngine original;
    std::vector<Message> requests;
    requests.push_back(add(1, 1, 1, Side::Ask, 100, 5));
    requests.push_back(add(2, 1, 1, Side::Bid, 100, 3));
    auto replace = add(1, 2, 1, Side::Ask, 102, 4);
    replace.action = Action::Replace;
    requests.push_back(replace);
    {
        Journal journal(path_, config_fingerprint(default_instruments()));
        for (const auto& c : requests) {
            journal.append(c);
            (void)original.process(c);
        }
        EXPECT_THROW((Journal{path_, config_fingerprint(default_instruments())}),
                     std::system_error);
    }
    Journal journal(path_, config_fingerprint(default_instruments()));
    MatchingEngine restored;
    ASSERT_EQ(journal.recovered(), requests);
    for (const auto& c : journal.recovered()) (void)restored.process(c);
    EXPECT_EQ(restored.snapshot(), original.snapshot());
    EXPECT_EQ(restored.position(1, 1).cash, original.position(1, 1).cash);
    EXPECT_EQ(restored.position(2, 1).quantity, 3);
    EXPECT_EQ(restored.last_request(1), 2);
    EXPECT_TRUE(restored.process(replace).updates.empty());
    EXPECT_TRUE(restored.validate_invariants());
}
TEST_F(JournalTest, TornCorruptAndMismatchedConfigurationAreRejected) {
    const auto hash = config_fingerprint(default_instruments());
    {
        Journal journal(path_, hash);
        journal.append(add(1, 1, 1, Side::Bid, 90, 5));
    }
    EXPECT_THROW((Journal{path_, hash + 1}), std::runtime_error);
    {
        std::fstream file(path_, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(30);
        file.put(static_cast<char>(255));
    }
    EXPECT_THROW((Journal{path_, hash}), std::runtime_error);
    std::filesystem::remove(path_);
    {
        Journal journal(path_, hash);
        journal.append(add(1, 1, 1, Side::Bid, 90, 5));
    }
    std::filesystem::resize_file(path_, std::filesystem::file_size(path_) - 1);
    EXPECT_THROW((Journal{path_, hash}), std::runtime_error);
}
TEST(OrderManagement, DuplicateFillsAreIgnoredAndOutstandingQuantityReservesRisk) {
    OrderManager manager;
    MatchingEngine e;
    MarketReplica market;
    market.restore(e.snapshot());
    const auto accepted = e.process(add(1, 1, 1, Side::Bid, 90, 40));
    for (const auto& m : accepted.reports) ASSERT_TRUE(manager.consume(m));
    InstrumentRules limits;
    limits.max_position = 50;
    EXPECT_EQ(manager.check(add(1, 2, 2, Side::Bid, 90, 20), limits, market, 1000),
              Reject::RiskPosition);
    auto replacing = add(1, 2, 1, Side::Bid, 90, 45);
    replacing.action = Action::Replace;
    EXPECT_EQ(manager.check(replacing, limits, market, 1000), Reject::None);
    const auto trade = e.process(add(2, 1, 1, Side::Ask, 90, 10));
    for (const auto& m : trade.reports)
        if (m.client_id == 1) {
            EXPECT_TRUE(manager.consume(m));
            EXPECT_FALSE(manager.consume(m));
        }
    EXPECT_EQ(manager.position(1).quantity, 10);
    EXPECT_EQ(manager.orders().at(1).quantity, 30);
    market.invalidate();
    EXPECT_EQ(manager.check(replacing, limits, market, 1000), Reject::KillSwitch);
    replacing.action = Action::Cancel;
    EXPECT_EQ(manager.check(replacing, limits, market, 1000), Reject::None);
}
TEST(OrderManagement, LossLimitBlocksNewRiskWhileCancellationRemainsAvailable) {
    OrderManager manager;
    Message fill;
    fill.kind = Kind::Report;
    fill.report = ReportType::Fill;
    fill.client_id = 1;
    fill.instrument_id = 1;
    fill.quantity = 10;
    fill.price = 100;
    fill.sequence = 1;
    ASSERT_TRUE(manager.consume(fill));
    MatchingEngine e;
    (void)e.process(add(2, 1, 1, Side::Bid, 80, 1));
    (void)e.process(add(3, 1, 1, Side::Ask, 82, 1));
    MarketReplica market;
    market.restore(e.snapshot());
    EXPECT_EQ(manager.check(add(1, 1, 1, Side::Bid, 80, 1), {}, market, 100), Reject::KillSwitch);
}
}  // namespace
}  // namespace order_book::trading
