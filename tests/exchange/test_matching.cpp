#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <tuple>

#include "order_book/exchange/market_data.hpp"
#include "order_book/exchange/matching_engine.hpp"

namespace {
using namespace order_book;
using namespace order_book::exchange;
Request make_order(ClientId client, OrderId id, Side side, Price price, Quantity qty,
                   InstrumentId instrument = 1) {
    Request r;
    r.client_id = client;
    r.sequence = id;
    r.order_id = id;
    r.instrument_id = instrument;
    r.side = side;
    r.price = price;
    r.quantity = qty;
    return r;
}
Request cancel(ClientId client, OrderId id, InstrumentId instrument = 1) {
    auto r = make_order(client, id, Side::Bid, 0, 0, instrument);
    r.type = RequestType::Cancel;
    return r;
}
class Matching : public ::testing::Test {
   protected:
    Matching() {
        engine.add_instrument(1);
        engine.add_instrument(2);
    }
    MatchResult apply(const Request& r) {
        auto result = engine.process(r);
        for (auto u : result.updates) {
            EXPECT_EQ(u.sequence, ++sequence);
            EXPECT_TRUE(shadow.apply(u));
        }
        EXPECT_TRUE(engine.validate_invariants());
        for (const auto id : {1U, 2U}) {
            const auto* replica = shadow.book(id);
            EXPECT_EQ(engine.book(id)->snapshot(), replica ? replica->snapshot() : BookSnapshot{});
        }
        EXPECT_EQ(result.reports.back().type, ReportType::Done);
        return result;
    }
    MatchingEngine engine;
    FeedBook shadow;
    Sequence sequence{};
};
TEST_F(Matching, PriceTimeAndPairedFillsAcrossLevels) {
    (void)apply(make_order(7, 1, Side::Ask, 101, 4));
    (void)apply(make_order(8, 1, Side::Ask, 101, 6));
    (void)apply(make_order(9, 1, Side::Ask, 102, 7));
    const auto result = apply(make_order(10, 1, Side::Bid, 103, 12));
    ASSERT_EQ(result.reports.size(), 8);
    EXPECT_EQ(result.reports[1].quantity, 4);
    EXPECT_EQ(result.reports[2].client_id, 7);
    EXPECT_EQ(result.reports[3].quantity, 6);
    EXPECT_EQ(result.reports[4].client_id, 8);
    EXPECT_EQ(result.reports[5].quantity, 2);
    EXPECT_EQ(result.reports[5].price, 102);
    EXPECT_EQ(result.reports[6].leaves, 5);
    for (unsigned i : {1U, 3U, 5U}) {
        EXPECT_EQ(result.reports[i].trade_id, result.reports[i + 1].trade_id);
        EXPECT_EQ(result.reports[i + 1].request_sequence, 0);
    }
    EXPECT_FALSE(engine.book(1)->best_bid());
    EXPECT_EQ(engine.book(1)->best_ask(), 102);
    EXPECT_EQ(engine.book(1)->best_quantity(Side::Ask), 5);
}
TEST_F(Matching, PartialAggressorRestsOnlyItsResidual) {
    (void)apply(make_order(1, 1, Side::Ask, 99, 4));
    const auto result = apply(make_order(2, 1, Side::Bid, 100, 9));
    EXPECT_EQ(engine.book(1)->best_bid(), 100);
    EXPECT_EQ(engine.book(1)->best_quantity(Side::Bid), 5);
    EXPECT_EQ(result.updates.back().type, UpdateType::Add);
    EXPECT_EQ(result.updates.back().quantity, 5);
}
TEST_F(Matching, PublicSnapshotsOwnTheirStorageAndExposeFifoWithoutMutatingTheBook) {
    (void)apply(make_order(1, 1, Side::Bid, 99, 4));
    (void)apply(make_order(2, 1, Side::Bid, 99, 6));
    auto snapshot = engine.book(1)->snapshot();
    ASSERT_EQ(snapshot.bids.size(), 1);
    EXPECT_EQ(snapshot.bids[0].quantity, 10);
    ASSERT_EQ(snapshot.bids[0].orders.size(), 2);
    EXPECT_LT(snapshot.bids[0].orders[0].order_id, snapshot.bids[0].orders[1].order_id);
    snapshot.bids[0].orders[0].quantity = 0;
    EXPECT_EQ(engine.book(1)->best_quantity(Side::Bid), 10);
    EXPECT_FALSE(engine.book(1)->top_order(static_cast<Side>(99)));
    EXPECT_EQ(engine.book(1)->best_quantity(static_cast<Side>(99)), 0);
}
TEST_F(Matching, ClientAndInstrumentIdentitiesAreIndependentAndCancellationIsOwned) {
    (void)apply(make_order(1, 1, Side::Bid, 99, 4));
    (void)apply(make_order(2, 1, Side::Bid, 98, 3));
    (void)apply(make_order(1, 1, Side::Bid, 97, 2, 2));
    EXPECT_EQ(apply(cancel(3, 1)).reports[0].status, Status::UnknownOrder);
    EXPECT_EQ(apply(make_order(1, 1, Side::Bid, 95, 1)).reports[0].status, Status::DuplicateOrder);
    (void)apply(cancel(1, 1));
    EXPECT_EQ(engine.book(1)->best_bid(), 98);
    EXPECT_EQ(engine.book(2)->best_bid(), 97);
}
TEST_F(Matching, ReplaceReductionPreservesPriorityAndIncreaseLosesIt) {
    const auto first = apply(make_order(1, 1, Side::Ask, 101, 8)).reports[0].market_order_id;
    (void)apply(make_order(2, 1, Side::Ask, 101, 8));
    auto replace = make_order(1, 1, Side::Ask, 101, 5);
    replace.type = RequestType::Replace;
    EXPECT_EQ(apply(replace).reports[0].market_order_id, first);
    EXPECT_EQ(apply(make_order(3, 1, Side::Bid, 101, 2)).reports[2].client_id, 1);
    replace.quantity = 9;
    EXPECT_NE(apply(replace).reports[0].market_order_id, first);
    EXPECT_EQ(apply(make_order(3, 2, Side::Bid, 101, 2)).reports[2].client_id, 2);
}
TEST_F(Matching, RepriceCanMatchAndInvalidReplacementPreservesOriginal) {
    (void)apply(make_order(1, 1, Side::Bid, 99, 4));
    (void)apply(make_order(2, 1, Side::Ask, 101, 4));
    auto replace = make_order(1, 1, Side::Bid, 0, 2);
    replace.type = RequestType::Replace;
    const auto before = engine.book(1)->snapshot();
    EXPECT_EQ(apply(replace).reports[0].status, Status::InvalidPrice);
    EXPECT_EQ(engine.book(1)->snapshot(), before);
    replace.price = 101;
    const auto result = apply(replace);
    EXPECT_EQ(result.reports[0].type, ReportType::Replaced);
    EXPECT_EQ(result.reports[1].type, ReportType::Filled);
    EXPECT_FALSE(engine.book(1)->best_bid());
    EXPECT_EQ(engine.book(1)->best_quantity(Side::Ask), 2);
}
TEST_F(Matching, IocMarketAndFokAreAtomicAndNeverRestUnpricedOrders) {
    (void)apply(make_order(1, 1, Side::Ask, 101, 5));
    auto request = make_order(2, 1, Side::Bid, 102, 6);
    request.tif = TimeInForce::Fok;
    const auto before = engine.book(1)->snapshot();
    EXPECT_EQ(apply(request).reports[0].status, Status::CannotFill);
    EXPECT_EQ(engine.book(1)->snapshot(), before);
    request.quantity = 3;
    const auto fok = apply(request);
    EXPECT_EQ(fok.reports[1].quantity, 3);
    request.order_id = 2;
    request.tif = TimeInForce::Ioc;
    request.quantity = 6;
    const auto ioc = apply(request);
    EXPECT_EQ(ioc.reports[3].type, ReportType::Canceled);
    EXPECT_EQ(ioc.reports[3].quantity, 4);
    EXPECT_EQ(engine.book(1)->order_count(), 0);
    (void)apply(make_order(1, 2, Side::Bid, 100, 3));
    request = make_order(2, 3, Side::Ask, 0, 5);
    request.market = true;
    const auto market = apply(request);
    EXPECT_EQ(market.reports[1].price, 100);
    EXPECT_EQ(market.reports[3].quantity, 2);
    EXPECT_EQ(engine.book(1)->order_count(), 0);
}
TEST_F(Matching, PostOnlyAndSelfTradePreventionDoNotExecuteForbiddenLiquidity) {
    (void)apply(make_order(1, 1, Side::Ask, 101, 5));
    auto request = make_order(2, 1, Side::Bid, 101, 3);
    request.post_only = true;
    EXPECT_EQ(apply(request).reports[0].status, Status::WouldCross);
    request.post_only = false;
    request.client_id = 1;
    request.order_id = 2;
    request.prevent_self_trade = true;
    const auto stp = apply(request);
    EXPECT_EQ(stp.reports[1].type, ReportType::Canceled);
    EXPECT_EQ(stp.reports[1].status, Status::SelfTrade);
    EXPECT_EQ(engine.book(1)->best_quantity(Side::Ask), 5);
    request.tif = TimeInForce::Fok;
    EXPECT_EQ(apply(request).reports[0].status, Status::CannotFill);
}
TEST(MatchingRules, TickLotBandsQuantityAndNotional) {
    MatchingEngine engine;
    engine.add_instrument(1, {5, 2, 90, 110, 10, 800});
    auto request = make_order(1, 1, Side::Bid, 100, 4);
    EXPECT_EQ(engine.process(request).reports[0].status, Status::Ok);
    request.order_id = 2;
    request.price = 101;
    EXPECT_EQ(engine.process(request).reports[0].status, Status::InvalidPrice);
    request.price = 115;
    EXPECT_EQ(engine.process(request).reports[0].status, Status::InvalidPrice);
    request.price = 100;
    request.quantity = 3;
    EXPECT_EQ(engine.process(request).reports[0].status, Status::InvalidQuantity);
    request.quantity = 12;
    EXPECT_EQ(engine.process(request).reports[0].status, Status::RiskRejected);
    request.quantity = 10;
    EXPECT_EQ(engine.process(request).reports[0].status, Status::RiskRejected);
    request.instrument_id = 2;
    EXPECT_EQ(engine.process(request).reports[0].status, Status::UnknownInstrument);
    request.side = static_cast<Side>(77);
    EXPECT_EQ(engine.process(request).reports[0].status, Status::InvalidRequest);
    EXPECT_TRUE(engine.validate_invariants());
    EXPECT_THROW(engine.add_instrument(1), std::invalid_argument);
    EXPECT_THROW(engine.add_instrument(3, {0}), std::invalid_argument);
}
TEST_F(Matching, CapacityRejectionRollsBackIdentitiesAndLeavesExistingOrdersIntact) {
    OrderId id = 1;
    for (; id < 2000; ++id) {
        const auto before = engine.book(1)->snapshot();
        const auto result = apply(make_order(1, id, Side::Bid, 99, 1));
        if (result.reports[0].status == Status::Capacity) {
            EXPECT_EQ(engine.book(1)->snapshot(), before);
            EXPECT_EQ(result.updates.size(), 0);
            break;
        }
    }
    ASSERT_LT(id, 2000);
    auto replace = make_order(1, 1, Side::Bid, 98, 3);
    replace.type = RequestType::Replace;
    EXPECT_EQ(apply(replace).reports[0].status, Status::Capacity);
    EXPECT_EQ(engine.book(1)->best_bid(), 99);
    (void)apply(cancel(1, 1));
    EXPECT_EQ(apply(make_order(1, id, Side::Bid, 99, 1)).reports[0].status, Status::Ok);
}
TEST_F(Matching, SeededMixedCommandsAgreeWithIndependentPriceTimeModel) {
    struct Rest {
        ClientId client;
        OrderId client_order;
        AddOrder order;
    };
    std::vector<Rest> reference;
    std::mt19937 rng(1234);
    for (OrderId id = 1; id <= 2000; ++id) {
        if (!reference.empty() && (rng() % 4 == 0 || reference.size() > 90)) {
            const auto index = static_cast<std::size_t>(rng()) % reference.size();
            const auto entry = reference[index];
            (void)apply(cancel(entry.client, entry.client_order));
            reference.erase(reference.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            const auto side = rng() % 2 ? Side::Bid : Side::Ask;
            const auto client = ClientId{1} + rng() % 5;
            const auto price = static_cast<Price>(95 + rng() % 11);
            const auto qty = static_cast<Quantity>(1 + rng() % 20);
            const auto result = apply(make_order(client, id, side, price, qty));
            ASSERT_EQ(result.reports[0].type, ReportType::Accepted);
            Quantity remaining = qty;
            std::size_t report_index = 1;
            while (remaining) {
                auto best = reference.end();
                for (auto it = reference.begin(); it != reference.end(); ++it) {
                    if (it->order.side == side ||
                        (side == Side::Bid ? it->order.price > price : it->order.price < price))
                        continue;
                    if (best == reference.end() ||
                        (side == Side::Bid ? it->order.price < best->order.price
                                           : it->order.price > best->order.price) ||
                        (it->order.price == best->order.price &&
                         it->order.order_id < best->order.order_id))
                        best = it;
                }
                if (best == reference.end()) break;
                const auto size = std::min(remaining, best->order.quantity);
                ASSERT_LT(report_index + 1, result.reports.size());
                EXPECT_EQ(result.reports[report_index].price, best->order.price);
                EXPECT_EQ(result.reports[report_index].quantity, size);
                EXPECT_EQ(result.reports[report_index + 1].client_id, best->client);
                EXPECT_EQ(result.reports[report_index + 1].order_id, best->client_order);
                report_index += 2;
                remaining -= size;
                best->order.quantity -= size;
                if (!best->order.quantity) reference.erase(best);
            }
            EXPECT_EQ(report_index, result.reports.size() - 1);
            if (remaining)
                reference.push_back(
                    {client, id, {result.reports[0].market_order_id, side, price, remaining}});
        }
        ASSERT_EQ(engine.book(1)->order_count(), reference.size());
        for (const auto& rest : reference)
            EXPECT_EQ(engine.book(1)->order(rest.order.order_id), rest.order);
    }
}
}  // namespace
