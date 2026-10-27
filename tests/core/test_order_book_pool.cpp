#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "order_book/core/order_book.hpp"
#include "order_book/core/order_registry.hpp"
#include "support/allocation_probe.hpp"
#include "support/book_inspection.hpp"

namespace order_book {
namespace {
using detail::BookInspection;
using detail::allocation_probe::FailNth;

OrderId fill(OrderBook& book, Side side, Price price, OrderId first = 1) {
    for (OrderId id = first; id < first + 100'000; ++id) {
        const auto result = book.apply(AddOrder{id, side, price, 10});
        if (result == ApplyResult::AllocationFailed) return id;
        if (result != ApplyResult::Ok) throw std::runtime_error("unexpected fill result");
    }
    throw std::runtime_error("expected fixed pool to exhaust");
}

TEST(OrderBookPool, AddAndCancelPreserveFifoOnBothSides) {
    for (const auto side : {Side::Bid, Side::Ask}) {
        OrderBook book;
        ASSERT_EQ(book.apply(AddOrder{1, side, 10'000, 10}), ApplyResult::Ok);
        ASSERT_EQ(book.apply(AddOrder{2, side, 10'000, 20}), ApplyResult::Ok);
        ASSERT_EQ(book.apply(AddOrder{3, side, 10'000, 30}), ApplyResult::Ok);
        const auto before = BookInspection::snapshot(book);
        ASSERT_EQ(before.size(), 1U);
        EXPECT_EQ(before[0].orders,
                  (std::vector<std::pair<OrderId, Quantity>>{{1, 10}, {2, 20}, {3, 30}}));
        ASSERT_EQ(book.apply(CancelOrder{2}), ApplyResult::Ok);
        ASSERT_EQ(book.apply(AddOrder{4, side, 10'000, 40}), ApplyResult::Ok);
        const auto after = BookInspection::snapshot(book);
        EXPECT_EQ(after[0].orders,
                  (std::vector<std::pair<OrderId, Quantity>>{{1, 10}, {3, 30}, {4, 40}}));
        EXPECT_TRUE(book.validate_invariants());
    }
}

TEST(OrderBookPool, CancelReturnsSlotForNextAdd) {
    for (const auto side : {Side::Bid, Side::Ask}) {
        OrderBook book;
        const auto rejected = fill(book, side, 10'000);
        const auto address = BookInspection::order_address(book, 1);
        ASSERT_EQ(BookInspection::free_bytes(book), 0U);
        ASSERT_EQ(book.apply(CancelOrder{1}), ApplyResult::Ok);
        EXPECT_GT(BookInspection::free_bytes(book), 0U);
        ASSERT_EQ(book.apply(AddOrder{rejected, side, 10'000, 10}), ApplyResult::Ok);
        EXPECT_EQ(BookInspection::order_address(book, rejected), address);
        EXPECT_EQ(BookInspection::free_bytes(book), 0U);
        EXPECT_TRUE(book.validate_invariants());
    }
}

TEST(OrderBookPool, PartialExecuteRetainsSlotAndFullExecuteReturnsIt) {
    for (const auto side : {Side::Bid, Side::Ask}) {
        OrderBook book;
        const auto rejected = fill(book, side, 10'000);
        const auto address = BookInspection::order_address(book, 1);
        ASSERT_EQ(book.apply(ExecuteOrder{1, 5}), ApplyResult::Ok);
        EXPECT_EQ(BookInspection::free_bytes(book), 0U);
        const auto partial = BookInspection::snapshot(book);
        EXPECT_EQ(book.apply(AddOrder{rejected, side, 10'000, 10}), ApplyResult::AllocationFailed);
        EXPECT_EQ(BookInspection::snapshot(book), partial);
        ASSERT_EQ(book.apply(ExecuteOrder{1, 5}), ApplyResult::Ok);
        ASSERT_EQ(book.apply(AddOrder{rejected, side, 10'000, 10}), ApplyResult::Ok);
        EXPECT_EQ(BookInspection::order_address(book, rejected), address);
        EXPECT_TRUE(book.validate_invariants());
    }
}

TEST(OrderBookPool, RecreatedPriceLevelReusesReleasedSlot) {
    for (const auto side : {Side::Bid, Side::Ask}) {
        OrderBook book;
        ASSERT_EQ(book.apply(AddOrder{1, side, 9'999, 10}), ApplyResult::Ok);
        const auto address = BookInspection::order_address(book, 1);
        const auto rejected = fill(book, side, 10'000, 2);
        ASSERT_EQ(book.apply(CancelOrder{1}), ApplyResult::Ok);
        EXPECT_EQ(BookInspection::snapshot(book).size(), 1U);
        ASSERT_EQ(book.apply(AddOrder{rejected, side, 9'999, 10}), ApplyResult::Ok);
        EXPECT_EQ(BookInspection::snapshot(book).size(), 2U);
        EXPECT_EQ(BookInspection::order_address(book, rejected), address);
        EXPECT_TRUE(book.validate_invariants());
    }
}

TEST(OrderBookPool, ExhaustedAddLeavesExistingAndNewLevelsUnchanged) {
    for (const auto side : {Side::Bid, Side::Ask}) {
        OrderBook book;
        const auto rejected = fill(book, side, 10'000);
        const auto before = BookInspection::snapshot(book);
        const auto count = book.order_count();
        for (const Price price : {10'000U, 10'001U}) {
            EXPECT_EQ(book.apply(AddOrder{rejected, side, price, 99}),
                      ApplyResult::AllocationFailed);
            EXPECT_EQ(BookInspection::snapshot(book), before);
            EXPECT_EQ(book.order_count(), count);
            EXPECT_FALSE(book.remaining_quantity(rejected).has_value());
            EXPECT_EQ(BookInspection::free_bytes(book), 0U);
            EXPECT_TRUE(book.validate_invariants());
        }
    }
}

// Fail every allocation in one Add, including map nodes, first-use pool metadata,
// index nodes and bucket rehash. Disable the failure guard before assertions.
void check_heap_failures(Side side, bool existing_level, bool rehash, bool fresh_pool) {
    std::size_t failures = 0;
    bool reached_success = false;
    for (std::size_t nth = 0; nth < 16; ++nth) {
        OrderBook book;
        if (!fresh_pool) {
            ASSERT_EQ(book.apply(AddOrder{1, side, 10'000, 10}), ApplyResult::Ok);
        }
        if (rehash) BookInspection::force_next_index_rehash(book);
        const auto before = BookInspection::snapshot(book);
        const auto count = book.order_count();
        const auto free = BookInspection::free_bytes(book);
        const AddOrder event{2, side, existing_level ? 10'000U : 10'001U, 20};
        ApplyResult result;
        {
            FailNth fail(nth);
            result = book.apply(event);
        }
        if (result == ApplyResult::Ok) {
            reached_success = true;
            EXPECT_TRUE(book.validate_invariants());
            break;
        }
        ++failures;
        ASSERT_EQ(result, ApplyResult::AllocationFailed);
        EXPECT_EQ(BookInspection::snapshot(book), before);
        EXPECT_EQ(book.order_count(), count);
        EXPECT_FALSE(book.remaining_quantity(event.order_id).has_value());
        if (!fresh_pool) {
            EXPECT_EQ(BookInspection::free_bytes(book), free);
        }
        EXPECT_TRUE(book.validate_invariants());
        ASSERT_EQ(book.apply(event), ApplyResult::Ok);  // rolled-back slot is usable
        EXPECT_TRUE(book.validate_invariants());
    }
    EXPECT_TRUE(reached_success);
    const std::size_t minimum = fresh_pool ? 5U : (existing_level ? 1U : 2U) + (rehash ? 1U : 0U);
    EXPECT_GE(failures, minimum);
}

TEST(OrderBookPool, IndexFailureRollsBackExistingLevel) {
    for (const auto side : {Side::Bid, Side::Ask}) check_heap_failures(side, true, false, false);
}
TEST(OrderBookPool, MapAndIndexFailuresRollBackNewLevel) {
    for (const auto side : {Side::Bid, Side::Ask}) check_heap_failures(side, false, false, false);
}
TEST(OrderBookPool, BucketRehashFailuresRollBackNewAndExistingLevels) {
    for (const auto side : {Side::Bid, Side::Ask}) {
        check_heap_failures(side, true, true, false);
        check_heap_failures(side, false, true, false);
    }
}
TEST(OrderBookPool, FirstUseMetadataAndIndexFailuresLeaveBookEmpty) {
    for (const auto side : {Side::Bid, Side::Ask}) check_heap_failures(side, false, false, true);
}
TEST(OrderBookPool, MovedFromBookDoesNotUseFormerPool) {
    OrderBook source;
    ASSERT_EQ(source.apply(AddOrder{1, Side::Bid, 10'000, 10}), ApplyResult::Ok);
    OrderBook destination(std::move(source));
    EXPECT_EQ(source.apply(AddOrder{2, Side::Bid, 10'000, 10}), ApplyResult::AllocationFailed);
    EXPECT_EQ(destination.order_count(), 1U);
    EXPECT_TRUE(destination.validate_invariants());
}
}  // namespace
}  // namespace order_book

#include "support/book_workloads.hpp"
#include "support/heap_order_book.hpp"
namespace order_book {
namespace {
TEST(OrderBookPool, MatchesPrePoolBaselineAfterEveryBenchmarkEvent) {
    for (const std::size_t count : {64U, 128U, 256U}) {
        for (const bool churn : {false, true}) {
            const auto workload =
                churn ? detail::churn_workload(count) : detail::fill_clear_workload(count);
            OrderBook pooled;
            HeapOrderBook baseline;
            const auto replay = [&](const auto& events) {
                for (const auto& event : events) {
                    ASSERT_EQ(pooled.apply(event), ApplyResult::Ok);
                    ASSERT_EQ(baseline.apply(event), ApplyResult::Ok);
                    ASSERT_EQ(detail::BookInspection::snapshot(pooled),
                              detail::BookInspection::snapshot(baseline));
                    ASSERT_TRUE(pooled.validate_invariants());
                    ASSERT_TRUE(baseline.validate_invariants());
                }
            };
            replay(workload.seed);
            replay(workload.events);
        }
    }
}
}  // namespace
}  // namespace order_book

namespace order_book {
namespace {
TEST(OrderBookRegistry, AllocationFailureCreatingInstrumentReturnsResultAndRollsBack) {
    bool reached_success = false;
    std::size_t failures = 0;
    for (std::size_t nth = 0; nth < 20; ++nth) {
        OrderBookRegistry registry;
        ApplyResult result;
        {
            FailNth fail(nth);
            result = registry.apply({1, 1001, AddOrder{1, Side::Bid, 10000, 20}});
        }
        if (result == ApplyResult::Ok) {
            reached_success = true;
            EXPECT_EQ(registry.instrument_count(), 1U);
            EXPECT_EQ(registry.order_count(), 1U);
            break;
        }
        ++failures;
        ASSERT_EQ(result, ApplyResult::AllocationFailed);
        EXPECT_EQ(registry.instrument_count(), 0U);
        EXPECT_EQ(registry.order_count(), 0U);
        EXPECT_TRUE(registry.validate_invariants());
        ASSERT_EQ(registry.apply({1, 1001, AddOrder{1, Side::Bid, 10000, 20}}), ApplyResult::Ok);
        EXPECT_TRUE(registry.validate_invariants());
    }
    EXPECT_TRUE(reached_success);
    EXPECT_GT(failures, 0U);
}
}  // namespace
}  // namespace order_book
