#include <gtest/gtest.h>

#include <string>

#include "order_book/order_registry.hpp"

namespace order_book {
namespace {

TEST(OrderBookRegistry, RoutesEventsAndKeepsInstrumentStateIndependent) {
    OrderBookRegistry registry;

    EXPECT_EQ(registry.apply(MarketEvent{1U, 1001U, AddOrder{7U, Side::Bid, 10'000U, 20U}}),
              ApplyResult::Ok);
    EXPECT_EQ(registry.apply(MarketEvent{2U, 2001U, AddOrder{7U, Side::Ask, 20'000U, 30U}}),
              ApplyResult::Ok);

    EXPECT_EQ(registry.instrument_count(), 2U);
    EXPECT_EQ(registry.order_count(), 2U);
    EXPECT_EQ(registry.bid_level_count(), 1U);
    EXPECT_EQ(registry.ask_level_count(), 1U);

    EXPECT_EQ(registry.apply(MarketEvent{3U, 1001U, CancelOrder{7U}}), ApplyResult::Ok);
    EXPECT_EQ(registry.order_count(), 1U);
    EXPECT_EQ(registry.bid_level_count(), 0U);
    EXPECT_EQ(registry.ask_level_count(), 1U);

    EXPECT_EQ(registry.apply(MarketEvent{4U, 2001U, ExecuteOrder{7U, 30U}}), ApplyResult::Ok);
    EXPECT_EQ(registry.order_count(), 0U);

    std::string reason;
    EXPECT_TRUE(registry.validate_invariants(&reason)) << reason;
}

TEST(OrderBookRegistry, RejectsEventsForUnknownInstrumentWithoutCreatingBook) {
    OrderBookRegistry registry;

    EXPECT_EQ(registry.apply(MarketEvent{1U, 9999U, CancelOrder{1U}}),
              ApplyResult::UnknownInstrument);
    EXPECT_EQ(registry.apply(MarketEvent{2U, 9999U, ExecuteOrder{1U, 1U}}),
              ApplyResult::UnknownInstrument);
    EXPECT_EQ(registry.instrument_count(), 0U);
    EXPECT_EQ(registry.order_count(), 0U);
}

TEST(OrderBookRegistry, DistinguishesUnknownOrderFromUnknownInstrument) {
    OrderBookRegistry registry;

    ASSERT_EQ(registry.apply(MarketEvent{1U, 1001U, AddOrder{1U, Side::Bid, 10'000U, 20U}}),
              ApplyResult::Ok);

    EXPECT_EQ(registry.apply(MarketEvent{2U, 1001U, CancelOrder{999U}}), ApplyResult::UnknownOrder);
    EXPECT_EQ(registry.apply(MarketEvent{3U, 2001U, CancelOrder{999U}}),
              ApplyResult::UnknownInstrument);
}

TEST(OrderBookRegistry, InvalidAddDoesNotKeepAnEmptyInstrument) {
    OrderBookRegistry registry;

    EXPECT_EQ(registry.apply(MarketEvent{1U, 1001U, AddOrder{1U, Side::Bid, 0U, 20U}}),
              ApplyResult::InvalidPrice);
    EXPECT_EQ(registry.apply(MarketEvent{2U, 2001U, AddOrder{2U, Side::Ask, 10'000U, 0U}}),
              ApplyResult::InvalidQuantity);
    EXPECT_EQ(registry.instrument_count(), 0U);
    EXPECT_EQ(registry.order_count(), 0U);
}

TEST(OrderBookRegistry, RejectedAddDoesNotRemoveAnExistingInstrument) {
    OrderBookRegistry registry;

    ASSERT_EQ(registry.apply(MarketEvent{1U, 1001U, AddOrder{1U, Side::Bid, 10'000U, 20U}}),
              ApplyResult::Ok);
    EXPECT_EQ(registry.apply(MarketEvent{2U, 1001U, AddOrder{2U, Side::Ask, 0U, 10U}}),
              ApplyResult::InvalidPrice);
    EXPECT_EQ(registry.apply(MarketEvent{3U, 1001U, AddOrder{1U, Side::Ask, 10'100U, 10U}}),
              ApplyResult::DuplicateOrder);

    EXPECT_EQ(registry.instrument_count(), 1U);
    EXPECT_EQ(registry.order_count(), 1U);
    EXPECT_EQ(registry.bid_level_count(), 1U);
    EXPECT_EQ(registry.ask_level_count(), 0U);
}

TEST(OrderBookRegistry, PartialAndFullExecutionUpdateAggregateCounts) {
    OrderBookRegistry registry;

    ASSERT_EQ(registry.apply(MarketEvent{1U, 1001U, AddOrder{1U, Side::Bid, 10'000U, 20U}}),
              ApplyResult::Ok);
    EXPECT_EQ(registry.apply(MarketEvent{2U, 1001U, ExecuteOrder{1U, 5U}}), ApplyResult::Ok);
    EXPECT_EQ(registry.order_count(), 1U);
    EXPECT_EQ(registry.bid_level_count(), 1U);

    EXPECT_EQ(registry.apply(MarketEvent{3U, 1001U, ExecuteOrder{1U, 15U}}), ApplyResult::Ok);
    EXPECT_EQ(registry.order_count(), 0U);
    EXPECT_EQ(registry.bid_level_count(), 0U);
}

TEST(OrderBookRegistry, AcceptsOnlyTheNextGlobalSequence) {
    OrderBookRegistry registry;

    EXPECT_TRUE(registry.seq_valid(1U));
    EXPECT_TRUE(registry.seq_valid(2U));
    EXPECT_FALSE(registry.seq_valid(2U));
    EXPECT_EQ(registry.get_seq(), 2U);
    EXPECT_FALSE(registry.seq_valid(1U));
    EXPECT_EQ(registry.get_seq(), 2U);
    EXPECT_FALSE(registry.seq_valid(4U));
    EXPECT_EQ(registry.get_seq(), 2U);
    EXPECT_TRUE(registry.seq_valid(3U));
    EXPECT_EQ(registry.get_seq(), 3U);
}

}  // namespace
}  // namespace order_book
