#include <gtest/gtest.h>

#include <string>

#include "order_book/order_book.hpp"

namespace order_book {
namespace {

TEST(OrderBookScaffold, AddOrderIsImplemented) {
    OrderBook book;
    const auto result = book.apply(AddOrder{
        .sequence = 1U, .order_id = 1001U, .side = Side::Bid, .price = 10'000U, .quantity = 25U});

    EXPECT_EQ(result, ApplyResult::Ok);
    EXPECT_EQ(book.best_bid(), 10'000U);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBookContract, DISABLED_AddFirstBid) {
    OrderBook book;

    EXPECT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    EXPECT_EQ(book.best_bid(), 10'000U);
    EXPECT_EQ(book.order_count(), 1U);
    EXPECT_EQ(book.bid_level_count(), 1U);
    EXPECT_EQ(book.remaining_quantity(101U), 20U);
}

TEST(OrderBookContract, DISABLED_BetterBidBecomesBestBid) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{2U, 102U, Side::Bid, 10'001U, 10U}), ApplyResult::Ok);

    EXPECT_EQ(book.best_bid(), 10'001U);
    EXPECT_EQ(book.bid_level_count(), 2U);
}

TEST(OrderBookContract, DISABLED_LowerAskBecomesBestAsk) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{1U, 201U, Side::Ask, 10'100U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{2U, 202U, Side::Ask, 10'099U, 10U}), ApplyResult::Ok);

    EXPECT_EQ(book.best_ask(), 10'099U);
    EXPECT_EQ(book.ask_level_count(), 2U);
}

TEST(OrderBookContract, DISABLED_OrdersAtSamePriceShareLevel) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{2U, 102U, Side::Bid, 10'000U, 15U}), ApplyResult::Ok);

    EXPECT_EQ(book.best_bid(), 10'000U);
    EXPECT_EQ(book.bid_level_count(), 1U);
    EXPECT_EQ(book.order_count(), 2U);
    EXPECT_EQ(book.remaining_quantity(101U), 20U);
    EXPECT_EQ(book.remaining_quantity(102U), 15U);
}

TEST(OrderBookContract, DISABLED_DuplicateOrderIsRejected) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    EXPECT_EQ(book.apply(AddOrder{2U, 101U, Side::Ask, 10'100U, 30U}), ApplyResult::DuplicateOrder);
    EXPECT_EQ(book.order_count(), 1U);
}

TEST(OrderBookContract, InvalidAddInputsAreRejectedWithoutMutation) {
    OrderBook book;

    EXPECT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 0U, 20U}), ApplyResult::InvalidPrice);
    EXPECT_EQ(book.apply(AddOrder{2U, 102U, Side::Ask, 10'100U, 0U}), ApplyResult::InvalidQuantity);

    EXPECT_EQ(book.order_count(), 0U);
    EXPECT_EQ(book.bid_level_count(), 0U);
    EXPECT_EQ(book.ask_level_count(), 0U);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookContract, MarketEventDispatchesAddOrder) {
    OrderBook book;
    const MarketEvent event = AddOrder{1U, 201U, Side::Ask, 10'100U, 25U};

    ASSERT_EQ(book.apply(event), ApplyResult::Ok);
    EXPECT_EQ(book.best_ask(), 10'100U);
}

TEST(OrderBookContract, DISABLED_CancelRemovesOrderAndEmptyLevel) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(CancelOrder{2U, 101U}), ApplyResult::Ok);

    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.remaining_quantity(101U).has_value());
    EXPECT_EQ(book.order_count(), 0U);
    EXPECT_EQ(book.bid_level_count(), 0U);
}

TEST(OrderBookContract, DISABLED_UnknownCancelIsRejected) {
    OrderBook book;
    EXPECT_EQ(book.apply(CancelOrder{1U, 999U}), ApplyResult::UnknownOrder);
}

TEST(OrderBookContract, DISABLED_PartialExecutionReducesQuantity) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(ExecuteOrder{2U, 101U, 7U}), ApplyResult::Ok);

    EXPECT_EQ(book.remaining_quantity(101U), 13U);
    EXPECT_EQ(book.order_count(), 1U);
}

TEST(OrderBookContract, DISABLED_FullExecutionRemovesOrder) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(ExecuteOrder{2U, 101U, 20U}), ApplyResult::Ok);

    EXPECT_FALSE(book.remaining_quantity(101U).has_value());
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.order_count(), 0U);
}

TEST(OrderBookContract, DISABLED_OverExecutionIsRejectedWithoutMutation) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    EXPECT_EQ(book.apply(ExecuteOrder{2U, 101U, 21U}), ApplyResult::QuantityExceedsRemaining);
    EXPECT_EQ(book.remaining_quantity(101U), 20U);
}

TEST(OrderBookContract, DISABLED_UnknownExecutionIsRejected) {
    OrderBook book;

    EXPECT_EQ(book.apply(ExecuteOrder{1U, 999U, 1U}), ApplyResult::UnknownOrder);
}

TEST(OrderBookContract, DISABLED_InvalidExecutionQuantityIsRejected) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    EXPECT_EQ(book.apply(ExecuteOrder{2U, 101U, 0U}), ApplyResult::InvalidQuantity);
    EXPECT_EQ(book.remaining_quantity(101U), 20U);
}

TEST(OrderBookContract, DISABLED_ResetClearsTheBook) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{2U, 201U, Side::Ask, 10'100U, 20U}), ApplyResult::Ok);

    book.reset();

    EXPECT_EQ(book.order_count(), 0U);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookContract, DISABLED_InvariantsHoldAcrossMixedOperations) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{1U, 101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{2U, 102U, Side::Bid, 10'000U, 15U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{3U, 201U, Side::Ask, 10'100U, 30U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(ExecuteOrder{4U, 101U, 5U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(CancelOrder{5U, 102U}), ApplyResult::Ok);

    std::string reason;
    EXPECT_TRUE(book.validate_invariants(&reason)) << reason;
}

}  // namespace
}  // namespace order_book
