#include <gtest/gtest.h>

#include <algorithm>
#include <random>

#include "order_book/trading/engine.hpp"
#include "order_book/trading/replica.hpp"
#include "order_book/trading/strategy.hpp"

namespace order_book::trading {
namespace {
Message command(ClientId client, std::uint64_t request, OrderId order, Side side, Price price,
                Quantity quantity, TimeInForce tif = TimeInForce::GTC) {
    Message c;
    c.client_id = client;
    c.request_id = request;
    c.order_id = order;
    c.instrument_id = 1;
    c.side = side;
    c.price = price;
    c.quantity = quantity;
    c.tif = tif;
    return c;
}
Reject rejection(const Batch& b) {
    const auto it = std::find_if(b.reports.begin(), b.reports.end(),
                                 [](const auto& m) { return m.report == ReportType::Rejected; });
    return it == b.reports.end() ? Reject::None : it->reject;
}
std::vector<Message> fills(const Batch& b, ClientId id) {
    std::vector<Message> result;
    for (const auto& m : b.reports)
        if (m.report == ReportType::Fill && m.client_id == id) result.push_back(m);
    return result;
}
TEST(Matching, PriceTimePriorityAndBothSidesReceiveFillsAtMakerPrice) {
    MatchingEngine e;
    ASSERT_EQ(rejection(e.process(command(1, 1, 1, Side::Ask, 102, 7))), Reject::None);
    ASSERT_EQ(rejection(e.process(command(2, 1, 1, Side::Ask, 101, 3))), Reject::None);
    ASSERT_EQ(rejection(e.process(command(3, 1, 1, Side::Ask, 101, 4))), Reject::None);
    const auto batch = e.process(command(4, 1, 1, Side::Bid, 103, 9));
    const auto executions = fills(batch, 4);
    ASSERT_EQ(executions.size(), 3);
    EXPECT_EQ(executions[0].price, 101);
    EXPECT_EQ(executions[0].quantity, 3);
    EXPECT_EQ(executions[1].price, 101);
    EXPECT_EQ(executions[1].quantity, 4);
    EXPECT_EQ(executions[2].price, 102);
    EXPECT_EQ(executions[2].quantity, 2);
    EXPECT_EQ(fills(batch, 2).front().trade_id, executions[0].trade_id);
    EXPECT_EQ(fills(batch, 3).front().trade_id, executions[1].trade_id);
    EXPECT_EQ(e.book(1).order_count(), 1);
    EXPECT_EQ(e.book(1).front(Side::Ask)->quantity, 5);
    EXPECT_EQ(e.position(4, 1).quantity, 9);
    EXPECT_EQ(e.position(4, 1).cash, -911);
    EXPECT_TRUE(e.validate_invariants());
}
TEST(Matching, PartialFillRestsOnlyResidualAndProducesReconstructibleUpdates) {
    MatchingEngine e;
    MarketReplica replica;
    replica.restore(e.snapshot());
    auto one = e.process(command(1, 1, 1, Side::Ask, 100, 2));
    for (const auto& m : one.updates) ASSERT_TRUE(replica.consume(m));
    const auto two = e.process(command(2, 1, 1, Side::Bid, 101, 5));
    for (const auto& m : two.updates) ASSERT_TRUE(replica.consume(m));
    EXPECT_EQ(e.book(1).front(Side::Bid)->quantity, 3);
    ASSERT_NE(replica.book(1), nullptr);
    EXPECT_EQ(replica.book(1)->snapshot(), e.book(1).snapshot());
    EXPECT_EQ(replica.sequence(), e.sequence());
    EXPECT_TRUE(e.validate_invariants());
}
TEST(Matching, IocMarketAndFokNeverRestUnfilledQuantity) {
    MatchingEngine e;
    (void)e.process(command(1, 1, 1, Side::Ask, 100, 5));
    const auto before = e.snapshot();
    EXPECT_EQ(rejection(e.process(command(2, 1, 1, Side::Bid, 100, 6, TimeInForce::FOK))),
              Reject::Fok);
    EXPECT_EQ(e.snapshot(), before);
    auto market = command(2, 2, 1, Side::Bid, 0, 8, TimeInForce::IOC);
    market.order_type = OrderType::Market;
    const auto b = e.process(market);
    ASSERT_EQ(fills(b, 2).size(), 1);
    EXPECT_EQ(fills(b, 2)[0].quantity, 5);
    EXPECT_EQ(e.book(1).order_count(), 0);
    EXPECT_EQ(b.reports.back().remaining, 0);
    market.request_id = 3;
    market.order_id = 2;
    market.tif = TimeInForce::GTC;
    EXPECT_EQ(rejection(e.process(market)), Reject::InvalidCommand);
}
TEST(Matching, FokSweepsAllAvailableLevelsAtomically) {
    MatchingEngine e;
    (void)e.process(command(1, 1, 1, Side::Ask, 100, 2));
    (void)e.process(command(1, 2, 2, Side::Ask, 101, 3));
    EXPECT_EQ(rejection(e.process(command(2, 1, 1, Side::Bid, 101, 5, TimeInForce::FOK))),
              Reject::None);
    EXPECT_EQ(e.book(1).order_count(), 0);
    EXPECT_EQ(e.stats().trades, 2);
    EXPECT_TRUE(e.validate_invariants());
}
TEST(Matching, PostOnlyAndSelfTradeRejectionsDoNotTouchEitherBookOrPositions) {
    MatchingEngine e;
    (void)e.process(command(1, 1, 1, Side::Ask, 100, 5));
    auto post = command(2, 1, 1, Side::Bid, 100, 2);
    post.post_only = true;
    const auto before = e.snapshot();
    EXPECT_EQ(rejection(e.process(post)), Reject::PostOnly);
    EXPECT_EQ(rejection(e.process(command(1, 2, 2, Side::Bid, 101, 6))), Reject::SelfTrade);
    EXPECT_EQ(before, e.snapshot());
    EXPECT_EQ(e.stats().trades, 0);
}
TEST(Matching, ReplaceReductionKeepsPriorityIncreaseLosesItAndRejectPreservesOriginal) {
    MatchingEngine e;
    (void)e.process(command(1, 1, 1, Side::Ask, 100, 5));
    (void)e.process(command(2, 1, 1, Side::Ask, 100, 5));
    auto replace = command(1, 2, 1, Side::Ask, 100, 3);
    replace.action = Action::Replace;
    EXPECT_EQ(rejection(e.process(replace)), Reject::None);
    EXPECT_EQ(e.book(1).front(Side::Ask)->quantity, 3);
    auto bad = replace;
    bad.request_id = 3;
    bad.price = 0;
    const auto before = e.snapshot();
    EXPECT_EQ(rejection(e.process(bad)), Reject::InvalidPrice);
    EXPECT_EQ(before, e.snapshot());
    replace.request_id = 4;
    replace.quantity = 6;
    EXPECT_EQ(rejection(e.process(replace)), Reject::None);
    const auto result = e.process(command(3, 1, 1, Side::Bid, 100, 1));
    EXPECT_EQ(fills(result, 2).size(), 1);
    EXPECT_TRUE(fills(result, 1).empty());
    EXPECT_TRUE(e.validate_invariants());
}
TEST(Matching, CancellationOwnershipAndClientScopedOrderIds) {
    MatchingEngine e;
    (void)e.process(command(1, 1, 1, Side::Bid, 90, 3));
    (void)e.process(command(2, 1, 1, Side::Bid, 90, 3));
    auto cancel = command(3, 1, 1, Side::Bid, 0, 0);
    cancel.action = Action::Cancel;
    EXPECT_EQ(rejection(e.process(cancel)), Reject::UnknownOrder);
    cancel.client_id = 1;
    cancel.request_id = 2;
    EXPECT_EQ(rejection(e.process(cancel)), Reject::None);
    EXPECT_EQ(e.book(1).order_count(), 1);
    EXPECT_EQ(rejection(e.process(command(1, 3, 1, Side::Bid, 90, 3))), Reject::DuplicateOrder);
    EXPECT_TRUE(e.validate_invariants());
}
TEST(Matching, MultipleInstrumentsAndCancelAllStayIndependent) {
    MatchingEngine e;
    (void)e.process(command(1, 1, 1, Side::Bid, 90, 3));
    auto other = command(1, 2, 2, Side::Ask, 80, 4);
    other.instrument_id = 2;
    (void)e.process(other);
    EXPECT_EQ(e.book(1).order_count(), 1);
    EXPECT_EQ(e.book(2).order_count(), 1);
    Message c;
    c.client_id = 1;
    c.request_id = 3;
    c.action = Action::CancelAll;
    c.instrument_id = 1;
    (void)e.process(c);
    EXPECT_EQ(e.book(1).order_count(), 0);
    EXPECT_EQ(e.book(2).order_count(), 1);
    c.request_id = 4;
    c.instrument_id = 0;
    (void)e.process(c);
    EXPECT_EQ(e.book(2).order_count(), 0);
    EXPECT_TRUE(e.validate_invariants());
}
TEST(Matching, IdempotentLastRequestDoesNotRepeatTradesOrMarketData) {
    MatchingEngine e;
    (void)e.process(command(1, 1, 1, Side::Ask, 100, 2));
    const auto c = command(2, 1, 1, Side::Bid, 100, 1);
    const auto first = e.process(c);
    const auto seq = e.sequence();
    const auto repeated = e.process(c);
    EXPECT_EQ(repeated.updates.size(), 0);
    EXPECT_EQ(e.sequence(), seq);
    EXPECT_EQ(e.stats().trades, 1);
    std::vector<Message> own;
    for (const auto& m : first.reports)
        if (m.client_id == 2) own.push_back(m);
    EXPECT_EQ(repeated.reports, own);
    auto changed = c;
    changed.quantity = 2;
    EXPECT_EQ(rejection(e.process(changed)), Reject::Sequence);
    changed.request_id = 3;
    EXPECT_EQ(rejection(e.process(changed)), Reject::Sequence);
    changed.request_id = 2;
    changed.order_id = 2;
    EXPECT_EQ(rejection(e.process(changed)), Reject::None);
    EXPECT_TRUE(e.validate_invariants());
}
TEST(Matching, RiskIncludesOutstandingOrdersAndValidatesTickLotAndBands) {
    InstrumentRules r;
    r.tick_size = 5;
    r.lot_size = 10;
    r.max_order_quantity = 50;
    r.max_position = 60;
    r.max_notional = 10'000;
    MatchingEngine e({{1, r}});
    EXPECT_EQ(rejection(e.process(command(1, 1, 1, Side::Bid, 101, 10))), Reject::InvalidPrice);
    EXPECT_EQ(rejection(e.process(command(1, 2, 1, Side::Bid, 100, 11))), Reject::InvalidQuantity);
    EXPECT_EQ(rejection(e.process(command(1, 3, 1, Side::Bid, 100, 60))), Reject::RiskQuantity);
    EXPECT_EQ(rejection(e.process(command(1, 4, 1, Side::Bid, 1000, 20))), Reject::RiskNotional);
    EXPECT_EQ(rejection(e.process(command(1, 5, 1, Side::Bid, 100, 40))), Reject::None);
    EXPECT_EQ(rejection(e.process(command(1, 6, 2, Side::Bid, 100, 30))), Reject::RiskPosition);
    EXPECT_EQ(e.book(1).order_count(), 1);
    auto replace = command(1, 7, 1, Side::Bid, 100, 50);
    replace.action = Action::Replace;
    EXPECT_EQ(rejection(e.process(replace)), Reject::None);
}
TEST(Matching, CapacityRejectionDoesNotApplyPlannedFillsOrLoseAReplacement) {
    auto r = InstrumentRules{};
    r.max_open_orders = 4096;
    MatchingEngine e({{1, r}});
    std::uint64_t request = 1;
    OrderId id = 1;
    for (; id < 2000; ++id, ++request) {
        if (rejection(e.process(command(1, request, id, Side::Bid, 90, 1))) == Reject::Capacity)
            break;
    }
    ASSERT_LT(id, 2000);
    const auto before = e.snapshot();
    auto replace = command(1, ++request, 1, Side::Bid, 91, 2);
    replace.action = Action::Replace;
    EXPECT_EQ(rejection(e.process(replace)), Reject::Capacity);
    EXPECT_EQ(e.snapshot(), before);
    EXPECT_TRUE(e.validate_invariants());
    const auto aggressive = command(2, 1, 1, Side::Ask, 89, static_cast<Quantity>(id + 1));
    EXPECT_EQ(rejection(e.process(aggressive)), Reject::Capacity);
    EXPECT_EQ(e.snapshot(), before);
    EXPECT_EQ(e.stats().trades, 0);
}
TEST(Matching, RandomizedMatchingAgreesWithIndependentPriceTimeReference) {
    struct Order {
        ClientId owner;
        OrderId id;
        Side side;
        Price price;
        Quantity leaves;
        std::uint64_t time;
    };
    std::vector<Order> reference;
    std::map<ClientId, std::uint64_t> requests;
    MatchingEngine e;
    std::mt19937 random(42);
    std::uint64_t time{};
    for (OrderId id = 1; id <= 2000; ++id) {
        const auto owner = static_cast<ClientId>(id);  // Distinct owners isolate matching from STP.
        const auto side = random() % 2 == 0 ? Side::Bid : Side::Ask;
        const auto price = static_cast<Price>(95 + random() % 11);
        auto leaves = static_cast<Quantity>(1 + random() % 10);
        const auto b = e.process(command(owner, 1, id, side, price, leaves));
        ASSERT_EQ(rejection(b), Reject::None);
        std::vector<std::pair<Price, Quantity>> expected;
        while (leaves != 0) {
            auto best = reference.end();
            for (auto it = reference.begin(); it != reference.end(); ++it) {
                if (it->side == side || (side == Side::Bid ? it->price > price : it->price < price))
                    continue;
                if (best == reference.end() ||
                    (side == Side::Bid ? it->price < best->price : it->price > best->price) ||
                    (it->price == best->price && it->time < best->time))
                    best = it;
            }
            if (best == reference.end()) break;
            const auto size = std::min(leaves, best->leaves);
            leaves -= size;
            best->leaves -= size;
            expected.emplace_back(best->price, size);
            if (best->leaves == 0) reference.erase(best);
        }
        if (leaves) reference.push_back({owner, id, side, price, leaves, time++});
        const auto actual = fills(b, owner);
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i != actual.size(); ++i) {
            EXPECT_EQ(actual[i].price, expected[i].first);
            EXPECT_EQ(actual[i].quantity, expected[i].second);
        }
        ASSERT_EQ(e.book(1).order_count(), reference.size());
        ASSERT_TRUE(e.validate_invariants());
    }
}
TEST(Positions, AverageCostRealizedMarkToMarketAndDirectionFlip) {
    Position p;
    p.fill(Side::Bid, 100, 10);
    p.fill(Side::Bid, 110, 10);
    EXPECT_EQ(p.average_price, 105);
    EXPECT_EQ(p.pnl(107), 40);
    p.fill(Side::Ask, 120, 5);
    EXPECT_EQ(p.quantity, 15);
    EXPECT_EQ(p.realized_pnl, 75);
    p.fill(Side::Ask, 90, 20);
    EXPECT_EQ(p.quantity, -5);
    EXPECT_EQ(p.average_price, 90);
    EXPECT_EQ(p.realized_pnl, -150);
    EXPECT_EQ(p.pnl(80), -100);
    p.fill(Side::Bid, 80, 5);
    EXPECT_EQ(p.quantity, 0);
    EXPECT_EQ(p.average_price, 0);
    EXPECT_EQ(p.realized_pnl, -100);
    EXPECT_EQ(p.cash, -100);
}
TEST(TradingStrategies, MakerSkewsWithInventoryAndTakerNeedsEdgeAndCapacity) {
    StrategyConfig c;
    const auto quotes = maker_quotes(c, 0);
    ASSERT_EQ(quotes.size(), 2);
    EXPECT_EQ(quotes[0].price, 98);
    EXPECT_EQ(quotes[1].price, 102);
    EXPECT_LT(maker_quotes(c, 10)[0].price, quotes[0].price);
    EXPECT_EQ(maker_quotes(c, c.inventory_limit).size(), 1);
    OrderBook book;
    ASSERT_EQ(book.apply(AddOrder{1, Side::Ask, 95, 5}), ApplyResult::Ok);
    ASSERT_TRUE(taker_quote(c, &book, 0));
    EXPECT_EQ(taker_quote(c, &book, 0)->side, Side::Bid);
    EXPECT_FALSE(taker_quote(c, &book, c.inventory_limit));
}
}  // namespace
}  // namespace order_book::trading
