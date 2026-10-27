#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "order_book/core/logger.hpp"
#include "order_book/core/order_registry.hpp"

namespace order_book {
namespace {
class LoggerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        char pattern[] = "/tmp/order-book-logger-XXXXXX";
        const auto* directory = mkdtemp(pattern);
        ASSERT_NE(directory, nullptr);
        directory_ = directory;
        path_ = (directory_ / "events.log").string();
    }
    void TearDown() override {
        if (!directory_.empty()) std::filesystem::remove_all(directory_);
    }
    std::string contents() const {
        std::ifstream input(path_);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
    std::filesystem::path directory_;
    std::string path_;
};

TEST_F(LoggerTest, DestructorDrainsAcceptedRecordsWithSourceAndTypedFields) {
    {
        Logger logger(path_);
        ASSERT_TRUE(logger.try_log({.level = LogLevel::Info,
                                    .event = LogEvent::AddOrder,
                                    .sequence = 3,
                                    .instrument_id = 8,
                                    .order_id = 42,
                                    .price_ticks = 10000,
                                    .quantity = 20}));
    }
    const auto text = contents();
    EXPECT_NE(text.find("[INFO]"), std::string::npos);
    EXPECT_NE(text.find("side=bid"), std::string::npos);
    EXPECT_NE(text.find("test_logger.cpp:"), std::string::npos);
    EXPECT_NE(
        text.find(
            "AddOrder sequence=3 instrument_id=8 order_id=42 price_ticks=10000 qty=20 result=ok"),
        std::string::npos);
}

TEST_F(LoggerTest, FiltersLevelsWithoutCountingDropsAndClosesIdempotently) {
    Logger logger(path_, LogLevel::Warn);
    EXPECT_FALSE(logger.try_log({.level = LogLevel::Debug}));
    EXPECT_FALSE(logger.try_log({.level = LogLevel::Info}));
    EXPECT_FALSE(logger.try_log({.level = LogLevel::Off}));
    EXPECT_TRUE(logger.try_log({.level = LogLevel::Warn, .event = LogEvent::ParseFailed}));
    logger.close();
    logger.close();
    EXPECT_FALSE(logger.try_log({.level = LogLevel::Error}));
    EXPECT_EQ(logger.dropped_count(), 0U);
    EXPECT_FALSE(logger.has_file_write_error());
    EXPECT_EQ(logger.format_failed_count(), 0U);
    EXPECT_EQ(contents().find("[DEBUG]"), std::string::npos);
    EXPECT_NE(contents().find("[WARN]"), std::string::npos);
}

TEST_F(LoggerTest, AppendsAcrossLoggerInstances) {
    for (const auto id : {17U, 18U}) {
        Logger logger(path_);
        ASSERT_TRUE(logger.try_log({.order_id = id}));
    }
    const auto text = contents();
    EXPECT_NE(text.find("order_id=17"), std::string::npos);
    EXPECT_NE(text.find("order_id=18"), std::string::npos);
}

TEST_F(LoggerTest, SaturationAccountsForEveryAttemptAndDrainsAcceptedRecordsInOrder) {
    Logger logger(path_);
    std::vector<OrderId> accepted;
    constexpr OrderId attempts = 20'000;
    for (OrderId id = 0; id < attempts; ++id) {
        if (logger.try_log({.order_id = id})) accepted.push_back(id);
    }
    logger.close();
    EXPECT_EQ(accepted.size() + logger.dropped_count(), attempts);
    EXPECT_FALSE(logger.has_file_write_error());
    EXPECT_EQ(logger.format_failed_count(), 0U);
    std::ifstream input(path_);
    std::string line;
    for (const auto id : accepted) {
        ASSERT_TRUE(static_cast<bool>(std::getline(input, line)));
        EXPECT_NE(line.find("order_id=" + std::to_string(id) + " "), std::string::npos);
    }
    EXPECT_FALSE(static_cast<bool>(std::getline(input, line)));
}

TEST_F(LoggerTest, OffDisablesAllLevels) {
    Logger logger(path_, LogLevel::Off);
    EXPECT_FALSE(logger.try_log({.level = LogLevel::Fatal}));
    logger.close();
    EXPECT_TRUE(contents().empty());
}

TEST_F(LoggerTest, ReportsOpenAndWriteFailures) {
    EXPECT_THROW((Logger{(directory_ / "missing" / "events.log").string()}), std::runtime_error);
    Logger logger("/dev/full");
    (void)logger.try_log({.level = LogLevel::Error});
    logger.close();
    EXPECT_TRUE(logger.has_file_write_error());
    EXPECT_FALSE(logger.try_log({.level = LogLevel::Error}));
}

TEST_F(LoggerTest, RegistryLogsAppliedAndRejectedOperationsWithInstrumentContext) {
    Logger logger(path_);
    OrderBookRegistry registry(&logger);
    EXPECT_EQ(registry.apply({1, 1001, AddOrder{7, Side::Bid, 10000, 20}}), ApplyResult::Ok);
    EXPECT_EQ(registry.apply({2, 1001, AddOrder{7, Side::Bid, 10000, 20}}),
              ApplyResult::DuplicateOrder);
    EXPECT_EQ(registry.apply({3, 1001, ExecuteOrder{7, 5}}), ApplyResult::Ok);
    EXPECT_EQ(registry.apply({4, 1001, CancelOrder{7}}), ApplyResult::Ok);
    EXPECT_EQ(registry.apply({5, 2001, CancelOrder{7}}), ApplyResult::UnknownInstrument);
    EXPECT_EQ(registry.apply({6, 1001, AddOrder{8, Side::Bid, 0, 20}}), ApplyResult::InvalidPrice);
    logger.close();
    const auto text = contents();
    EXPECT_NE(text.find("AddOrder sequence=1 instrument_id=1001 order_id=7"), std::string::npos);
    EXPECT_NE(text.find("result=duplicate_order"), std::string::npos);
    EXPECT_NE(text.find("ExecuteOrder sequence=3 instrument_id=1001 order_id=7"),
              std::string::npos);
    EXPECT_NE(text.find("CancelOrder sequence=4 instrument_id=1001 order_id=7"), std::string::npos);
    EXPECT_NE(text.find("CancelOrder sequence=5 instrument_id=2001"), std::string::npos);
    EXPECT_NE(text.find("result=unknown_instrument"), std::string::npos);
    EXPECT_NE(text.find("result=invalid_price"), std::string::npos);
    EXPECT_TRUE(registry.validate_invariants());
}

TEST_F(LoggerTest, PoolExhaustionIsLoggedAsErrorAndKeepsExistingOrders) {
    Logger logger(path_);
    OrderBookRegistry registry(&logger);
    ApplyResult result = ApplyResult::Ok;
    Sequence sequence = 0;
    while (sequence < 2000 && result == ApplyResult::Ok) {
        ++sequence;
        result = registry.apply({sequence, 1, AddOrder{sequence, Side::Ask, 10000, 10}});
    }
    ASSERT_EQ(result, ApplyResult::AllocationFailed);
    EXPECT_EQ(registry.order_count(), sequence - 1);
    EXPECT_TRUE(registry.validate_invariants());
    logger.close();
    const auto text = contents();
    EXPECT_NE(text.find("[ERROR]"), std::string::npos);
    EXPECT_NE(text.find("result=allocation_failed"), std::string::npos);
    EXPECT_EQ(logger.dropped_count(), 0U);
}
}  // namespace
}  // namespace order_book
