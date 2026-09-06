#include <gtest/gtest.h>

#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <utility>

#include "order_book/order_book.hpp"

namespace order_book {
namespace {

TEST(OrderBookScaffold, AddOrderIsImplemented) {
    OrderBook book;
    const auto result = book.apply(AddOrder{
        .order_id = 1001U, .side = Side::Bid, .price = 10'000U, .quantity = 25U});

    EXPECT_EQ(result, ApplyResult::Ok);
    EXPECT_EQ(book.best_bid(), 10'000U);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBookContract, AddFirstBid) {
    OrderBook book;

    EXPECT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    EXPECT_EQ(book.best_bid(), 10'000U);
    EXPECT_EQ(book.order_count(), 1U);
    EXPECT_EQ(book.bid_level_count(), 1U);
    EXPECT_EQ(book.remaining_quantity(101U), 20U);
}

TEST(OrderBookContract, BetterBidBecomesBestBid) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{102U, Side::Bid, 10'001U, 10U}), ApplyResult::Ok);

    EXPECT_EQ(book.best_bid(), 10'001U);
    EXPECT_EQ(book.bid_level_count(), 2U);
}

TEST(OrderBookContract, LowerAskBecomesBestAsk) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{201U, Side::Ask, 10'100U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{202U, Side::Ask, 10'099U, 10U}), ApplyResult::Ok);

    EXPECT_EQ(book.best_ask(), 10'099U);
    EXPECT_EQ(book.ask_level_count(), 2U);
}

TEST(OrderBookContract, OrdersAtSamePriceShareLevel) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{102U, Side::Bid, 10'000U, 15U}), ApplyResult::Ok);

    EXPECT_EQ(book.best_bid(), 10'000U);
    EXPECT_EQ(book.bid_level_count(), 1U);
    EXPECT_EQ(book.order_count(), 2U);
    EXPECT_EQ(book.remaining_quantity(101U), 20U);
    EXPECT_EQ(book.remaining_quantity(102U), 15U);
}

TEST(OrderBookContract, DuplicateOrderIsRejected) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    EXPECT_EQ(book.apply(AddOrder{101U, Side::Ask, 10'100U, 30U}), ApplyResult::DuplicateOrder);
    EXPECT_EQ(book.order_count(), 1U);
}

TEST(OrderBookContract, InvalidAddInputsAreRejectedWithoutMutation) {
    OrderBook book;

    EXPECT_EQ(book.apply(AddOrder{101U, Side::Bid, 0U, 20U}), ApplyResult::InvalidPrice);
    EXPECT_EQ(book.apply(AddOrder{102U, Side::Ask, 10'100U, 0U}), ApplyResult::InvalidQuantity);

    EXPECT_EQ(book.order_count(), 0U);
    EXPECT_EQ(book.bid_level_count(), 0U);
    EXPECT_EQ(book.ask_level_count(), 0U);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookContract, EventPayloadDispatchesAddOrder) {
    OrderBook book;
    const EventPayload event = AddOrder{201U, Side::Ask, 10'100U, 25U};

    ASSERT_EQ(book.apply(event), ApplyResult::Ok);
    EXPECT_EQ(book.best_ask(), 10'100U);
}

TEST(OrderBookContract, CancelRemovesOrderAndEmptyLevel) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(CancelOrder{101U}), ApplyResult::Ok);

    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.remaining_quantity(101U).has_value());
    EXPECT_EQ(book.order_count(), 0U);
    EXPECT_EQ(book.bid_level_count(), 0U);
}

TEST(OrderBookContract, UnknownCancelIsRejected) {
    OrderBook book;
    EXPECT_EQ(book.apply(CancelOrder{999U}), ApplyResult::UnknownOrder);
}

TEST(OrderBookContract, PartialExecutionReducesQuantity) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(ExecuteOrder{101U, 7U}), ApplyResult::Ok);

    EXPECT_EQ(book.remaining_quantity(101U), 13U);
    EXPECT_EQ(book.order_count(), 1U);
}

TEST(OrderBookContract, FullExecutionRemovesOrder) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(ExecuteOrder{101U, 20U}), ApplyResult::Ok);

    EXPECT_FALSE(book.remaining_quantity(101U).has_value());
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.order_count(), 0U);
}

TEST(OrderBookContract, OverExecutionIsRejectedWithoutMutation) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    EXPECT_EQ(book.apply(ExecuteOrder{101U, 21U}), ApplyResult::QuantityExceedsRemaining);
    EXPECT_EQ(book.remaining_quantity(101U), 20U);
}

TEST(OrderBookContract, UnknownExecutionIsRejected) {
    OrderBook book;

    EXPECT_EQ(book.apply(ExecuteOrder{999U, 1U}), ApplyResult::UnknownOrder);
}

TEST(OrderBookContract, InvalidExecutionQuantityIsRejected) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    EXPECT_EQ(book.apply(ExecuteOrder{101U, 0U}), ApplyResult::InvalidQuantity);
    EXPECT_EQ(book.remaining_quantity(101U), 20U);
}

TEST(OrderBookContract, ResetClearsTheBook) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{201U, Side::Ask, 10'100U, 20U}), ApplyResult::Ok);

    book.reset();

    EXPECT_EQ(book.order_count(), 0U);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_FALSE(book.best_ask().has_value());
}

TEST(OrderBookContract, InvariantsHoldAcrossMixedOperations) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{102U, Side::Bid, 10'000U, 15U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{201U, Side::Ask, 10'100U, 30U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(ExecuteOrder{101U, 5U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(CancelOrder{102U}), ApplyResult::Ok);

    std::string reason;
    EXPECT_TRUE(book.validate_invariants(&reason)) << reason;
}

TEST(OrderBookContract, AskOrdersSupportPartialExecutionCancellationAndFullExecution) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{201U, Side::Ask, 10'100U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{202U, Side::Ask, 10'100U, 15U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(ExecuteOrder{201U, 7U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(CancelOrder{201U}), ApplyResult::Ok);

    EXPECT_EQ(book.best_ask(), 10'100U);
    EXPECT_EQ(book.ask_level_count(), 1U);
    EXPECT_EQ(book.order_count(), 1U);
    EXPECT_FALSE(book.remaining_quantity(201U).has_value());
    EXPECT_EQ(book.remaining_quantity(202U), 15U);
    EXPECT_TRUE(book.validate_invariants());

    ASSERT_EQ(book.apply(ExecuteOrder{202U, 15U}), ApplyResult::Ok);
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_EQ(book.ask_level_count(), 0U);
    EXPECT_EQ(book.order_count(), 0U);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBookContract, RemovingBestLevelsRevealsTheNextBestPrices) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 10U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{102U, Side::Bid, 10'001U, 10U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{201U, Side::Ask, 10'101U, 10U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{202U, Side::Ask, 10'100U, 10U}), ApplyResult::Ok);

    ASSERT_EQ(book.apply(CancelOrder{102U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(ExecuteOrder{202U, 10U}), ApplyResult::Ok);

    EXPECT_EQ(book.best_bid(), 10'000U);
    EXPECT_EQ(book.best_ask(), 10'101U);
    EXPECT_EQ(book.bid_level_count(), 1U);
    EXPECT_EQ(book.ask_level_count(), 1U);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBookContract, RemovingOneOrderKeepsOtherOrdersAtTheSamePrice) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 10U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{102U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{103U, Side::Bid, 10'000U, 30U}), ApplyResult::Ok);

    ASSERT_EQ(book.apply(CancelOrder{102U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(ExecuteOrder{101U, 10U}), ApplyResult::Ok);

    EXPECT_EQ(book.best_bid(), 10'000U);
    EXPECT_EQ(book.bid_level_count(), 1U);
    EXPECT_EQ(book.order_count(), 1U);
    EXPECT_EQ(book.remaining_quantity(103U), 30U);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBookContract, RemovedOrderIdsCanBeReused) {
    OrderBook book;

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 10U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(CancelOrder{101U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{101U, Side::Ask, 10'100U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(ExecuteOrder{101U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 9'999U, 30U}), ApplyResult::Ok);

    EXPECT_EQ(book.order_count(), 1U);
    EXPECT_EQ(book.remaining_quantity(101U), 30U);
    EXPECT_EQ(book.best_bid(), 9'999U);
    EXPECT_FALSE(book.best_ask().has_value());
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBookContract, AggregateQuantityHandlesMultipleMaximumSizedOrders) {
    OrderBook book;
    constexpr Quantity max_quantity = std::numeric_limits<Quantity>::max();

    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, max_quantity}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{102U, Side::Bid, 10'000U, max_quantity}), ApplyResult::Ok);
    EXPECT_TRUE(book.validate_invariants());

    ASSERT_EQ(book.apply(ExecuteOrder{101U, max_quantity}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(CancelOrder{102U}), ApplyResult::Ok);

    EXPECT_EQ(book.order_count(), 0U);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBookContract, MovingABookPreservesOrderIndexIterators) {
    OrderBook source;
    ASSERT_EQ(source.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(source.apply(AddOrder{201U, Side::Ask, 10'100U, 30U}), ApplyResult::Ok);
    ASSERT_EQ(source.apply(ExecuteOrder{101U, 5U}), ApplyResult::Ok);

    OrderBook moved{std::move(source)};
    EXPECT_EQ(moved.remaining_quantity(101U), 15U);
    EXPECT_EQ(moved.remaining_quantity(201U), 30U);
    EXPECT_TRUE(moved.validate_invariants());

    OrderBook destination;
    ASSERT_EQ(destination.apply(AddOrder{999U, Side::Bid, 9'000U, 1U}), ApplyResult::Ok);
    destination = std::move(moved);

    ASSERT_EQ(destination.apply(CancelOrder{101U}), ApplyResult::Ok);
    ASSERT_EQ(destination.apply(ExecuteOrder{201U, 30U}), ApplyResult::Ok);
    EXPECT_EQ(destination.order_count(), 0U);
    EXPECT_TRUE(destination.validate_invariants());
}

TEST(OrderBookContract, ResetBookCanBeReused) {
    OrderBook book;
    ASSERT_EQ(book.apply(AddOrder{101U, Side::Bid, 10'000U, 20U}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{201U, Side::Ask, 10'100U, 30U}), ApplyResult::Ok);

    book.reset();

    ASSERT_EQ(book.apply(AddOrder{301U, Side::Ask, 10'200U, 40U}), ApplyResult::Ok);
    EXPECT_EQ(book.order_count(), 1U);
    EXPECT_FALSE(book.best_bid().has_value());
    EXPECT_EQ(book.best_ask(), 10'200U);
    EXPECT_EQ(book.remaining_quantity(301U), 40U);
    EXPECT_TRUE(book.validate_invariants());
}

TEST(OrderBookContract, DeterministicMixedWorkloadMatchesReferenceModel) {
    struct ModelOrder {
        Side side;
        Price price;
        Quantity remaining;
    };

    using ModelOrders = std::map<OrderId, ModelOrder>;
    using ModelBidLevels = std::map<Price, std::size_t, std::greater<Price>>;
    using ModelAskLevels = std::map<Price, std::size_t, std::less<Price>>;

    OrderBook book;
    ModelOrders orders;
    ModelBidLevels bids;
    ModelAskLevels asks;
    std::mt19937_64 random{0xC0FFEEU};
    OrderId next_order_id = 1U;

    const auto apply_event = [&book](const EventPayload& event) { return book.apply(event); };

    const auto remove_model_order = [&](const ModelOrders::iterator order_it) {
        auto remove_level_order = [](auto& levels, const Price price) {
            const auto level_it = levels.find(price);
            ASSERT_NE(level_it, levels.end());
            if (level_it->second == 1U) {
                levels.erase(level_it);
            } else {
                --level_it->second;
            }
        };

        if (order_it->second.side == Side::Bid) {
            remove_level_order(bids, order_it->second.price);
        } else {
            remove_level_order(asks, order_it->second.price);
        }
        orders.erase(order_it);
    };

    const auto expect_matches_model = [&] {
        EXPECT_EQ(book.order_count(), orders.size());
        EXPECT_EQ(book.bid_level_count(), bids.size());
        EXPECT_EQ(book.ask_level_count(), asks.size());

        const std::optional<Price> expected_best_bid =
            bids.empty() ? std::nullopt : std::optional<Price>{bids.begin()->first};
        const std::optional<Price> expected_best_ask =
            asks.empty() ? std::nullopt : std::optional<Price>{asks.begin()->first};
        EXPECT_EQ(book.best_bid(), expected_best_bid);
        EXPECT_EQ(book.best_ask(), expected_best_ask);

        for (const auto& [order_id, order] : orders) {
            EXPECT_EQ(book.remaining_quantity(order_id), order.remaining);
        }

        std::string reason;
        EXPECT_TRUE(book.validate_invariants(&reason)) << reason;
    };

    for (std::size_t step = 0U; step < 2'000U; ++step) {
        SCOPED_TRACE(step);
        const auto operation = static_cast<unsigned>(random() % 100U);

        if (orders.empty() || operation < 45U) {
            const OrderId order_id = next_order_id++;
            const Side side = random() % 2U == 0U ? Side::Bid : Side::Ask;
            const Price price = static_cast<Price>(9'900U + random() % 201U);
            const Quantity quantity = static_cast<Quantity>(1U + random() % 500U);

            ASSERT_EQ(apply_event(AddOrder{order_id, side, price, quantity}),
                      ApplyResult::Ok);
            orders.emplace(order_id, ModelOrder{side, price, quantity});
            if (side == Side::Bid) {
                ++bids[price];
            } else {
                ++asks[price];
            }
        } else {
            auto selected = orders.begin();
            std::advance(selected, static_cast<std::ptrdiff_t>(random() % orders.size()));

            if (operation < 68U) {
                const unsigned execution_kind = static_cast<unsigned>(random() % 4U);
                if (execution_kind == 0U) {
                    ASSERT_EQ(apply_event(ExecuteOrder{selected->first, 0U}),
                              ApplyResult::InvalidQuantity);
                } else if (execution_kind == 1U) {
                    ASSERT_EQ(apply_event(ExecuteOrder{selected->first,
                                                       selected->second.remaining + 1U}),
                              ApplyResult::QuantityExceedsRemaining);
                } else if (execution_kind == 2U || selected->second.remaining == 1U) {
                    ASSERT_EQ(apply_event(ExecuteOrder{selected->first,
                                                       selected->second.remaining}),
                              ApplyResult::Ok);
                    remove_model_order(selected);
                } else {
                    const Quantity executed =
                        static_cast<Quantity>(1U + random() % (selected->second.remaining - 1U));
                    ASSERT_EQ(apply_event(ExecuteOrder{selected->first, executed}),
                              ApplyResult::Ok);
                    selected->second.remaining -= executed;
                }
            } else if (operation < 82U) {
                ASSERT_EQ(apply_event(CancelOrder{selected->first}), ApplyResult::Ok);
                remove_model_order(selected);
            } else if (operation < 91U) {
                ASSERT_EQ(apply_event(AddOrder{selected->first, selected->second.side,
                                               selected->second.price, selected->second.remaining}),
                          ApplyResult::DuplicateOrder);
            } else {
                const OrderId unknown_order_id = next_order_id + step + 1'000U;
                if (operation % 2U == 0U) {
                    ASSERT_EQ(apply_event(CancelOrder{unknown_order_id}),
                              ApplyResult::UnknownOrder);
                } else {
                    ASSERT_EQ(apply_event(ExecuteOrder{unknown_order_id, 1U}),
                              ApplyResult::UnknownOrder);
                }
            }
        }

        expect_matches_model();
    }
}

}  // namespace
}  // namespace order_book
