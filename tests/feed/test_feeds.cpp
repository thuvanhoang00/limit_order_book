#include <gtest/gtest.h>
#include <algorithm>
#include "order_book/feed/books.hpp"

namespace order_book::feed {
namespace {
using Bytes = std::vector<std::uint8_t>;
void number(Bytes& b, std::size_t pos, std::size_t len, std::uint64_t value, bool le = false) {
    for (std::size_t i = 0; i != len; ++i) {
        b.at(pos + (le ? i : len - 1 - i)) = static_cast<std::uint8_t>(value & 255U);
        value >>= 8U;
    }
}
void text(Bytes& b, std::size_t pos, std::size_t len, const std::string& value) {
    std::fill_n(b.begin() + static_cast<std::ptrdiff_t>(pos), len, ' ');
    std::copy(value.begin(), value.end(), b.begin() + static_cast<std::ptrdiff_t>(pos));
}
Bytes itch(char type, std::size_t len) {
    Bytes b(len); b[0] = static_cast<std::uint8_t>(type); number(b, 1, 2, 1);
    number(b, 5, 6, 45'678'123'456'789ULL); return b;
}
Bytes system(char event) {
    auto b = itch('S', 12); number(b, 1, 2, 0); b[11] = static_cast<std::uint8_t>(event); return b;
}
void initialize(Itch50Decoder& decoder) {
    (void)decoder.decode(system('O'));
    auto b = itch('R', 39); text(b, 11, 8, "AAPL"); (void)decoder.decode(b);
}
Bytes add_itch(OrderId id = 42, char type = 'A') {
    auto b = itch(type, type == 'A' ? 36 : 40); number(b, 11, 8, id); b[19] = 'B';
    number(b, 20, 4, 100); text(b, 24, 8, "AAPL"); number(b, 32, 4, 1'234'567); return b;
}
Bytes pitch(std::uint8_t type, std::size_t size) {
    Bytes b(size); b[0] = static_cast<std::uint8_t>(size); b[1] = type;
    number(b, 2, 4, 123'456'789, true); return b;
}
Bytes time_message() { auto b = pitch(0x20, 6); number(b, 2, 4, 36000, true); return b; }
Bytes add_pitch(OrderId id = 42, std::uint8_t type = 0x21) {
    const bool small = type == 0x22, expanded = type == 0x2f;
    auto b = pitch(type, small ? 26 : expanded ? 41 : 34);
    number(b, 6, 8, id, true); b[14] = 'B'; number(b, 15, small ? 2 : 4, 100, true);
    text(b, small ? 17 : 19, expanded ? 8 : 6, "AAPL");
    number(b, small ? 23 : expanded ? 27 : 25, small ? 2 : 8, small ? 12345 : 1'234'500, true);
    return b;
}
Bytes packet(std::initializer_list<Bytes> messages, std::uint32_t seq = 1, std::uint8_t unit = 1) {
    Bytes b(8); b[2] = static_cast<std::uint8_t>(messages.size()); b[3] = unit;
    number(b, 4, 4, seq, true);
    for (const auto& m : messages) b.insert(b.end(), m.begin(), m.end());
    number(b, 0, 2, b.size(), true); return b;
}
TEST(Itch50, BigEndianDirectoryAddAndAttribution) {
    for (char type : {'A', 'F'}) {
        Itch50Decoder decoder; initialize(decoder);
        const auto e = decoder.decode(add_itch(0x0102030405060708ULL, type));
        EXPECT_EQ(e.type, EventType::Add); EXPECT_EQ(e.symbol, "AAPL");
        EXPECT_EQ(e.order_id, 0x0102030405060708ULL);
        EXPECT_EQ(e.timestamp_ns, 45'678'123'456'789ULL);
        EXPECT_EQ(e.price, 1'234'567U); EXPECT_EQ(e.quantity, 100U); EXPECT_EQ(e.sequence, 3U);
    }
}
TEST(Itch50, CancelExecutionPriceAndNewIdReplacementReconstructBook) {
    Itch50Decoder decoder; initialize(decoder); Books books;
    const auto apply = [&](const Bytes& b) { books.consume("nasdaq", decoder.decode(b)); };
    apply(add_itch());
    auto cancel = itch('X', 23); number(cancel, 11, 8, 42); number(cancel, 19, 4, 10); apply(cancel);
    auto execute = itch('C', 36); number(execute, 11, 8, 42); number(execute, 19, 4, 20);
    execute[31] = 'N'; number(execute, 32, 4, 9999); apply(execute);
    const auto& book = books.books().begin()->second;
    EXPECT_EQ(book.order(42)->quantity, 70U); EXPECT_EQ(book.order(42)->price, 1'234'567U);
    auto replace = itch('U', 35); number(replace, 11, 8, 42); number(replace, 19, 8, 43);
    number(replace, 27, 4, 50); number(replace, 31, 4, 1'240'000); apply(replace);
    EXPECT_FALSE(book.order(42)); EXPECT_EQ(book.order(43)->quantity, 50U);
    auto remove = itch('D', 19); number(remove, 11, 8, 43); apply(remove);
    EXPECT_EQ(book.order_count(), 0U); EXPECT_TRUE(books.validate_invariants());
}
TEST(Itch50, HiddenTradesAndBrokenTradesDoNotMutateBook) {
    Itch50Decoder decoder; initialize(decoder); Books books;
    books.consume("nasdaq", decoder.decode(add_itch()));
    auto trade = itch('P', 44); number(trade, 11, 8, 42); number(trade, 20, 4, 25);
    text(trade, 24, 8, "AAPL"); number(trade, 32, 4, 10000);
    books.consume("nasdaq", decoder.decode(trade));
    books.consume("nasdaq", decoder.decode(itch('B', 19)));
    EXPECT_EQ(books.books().begin()->second.order(42)->quantity, 100U);
}
TEST(Itch50, InvalidLengthUnknownTypeHistoryAndFieldsFailClosed) {
    Itch50Decoder no_start; EXPECT_THROW((void)no_start.decode(add_itch()), Error);
    EXPECT_THROW((void)no_start.decode(system('O')), Error);
    for (unsigned mutation = 0; mutation != 7; ++mutation) {
        Itch50Decoder decoder; initialize(decoder); auto b = add_itch();
        if (mutation == 0) b.clear();
        if (mutation == 1) b.pop_back();
        if (mutation == 2) b[0] = '?';
        if (mutation == 3) b[19] = '?';
        if (mutation == 4) b[24] = 0;
        if (mutation == 5) number(b, 5, 6, 86'400'000'000'000ULL);
        if (mutation == 6) number(b, 1, 2, 99);
        EXPECT_THROW((void)decoder.decode(b), Error);
    }
    Itch50Decoder ended; initialize(ended); (void)ended.decode(system('C'));
    EXPECT_TRUE(ended.complete()); EXPECT_THROW((void)ended.decode(add_itch()), Error);
}
TEST(Pitch, LongShortExpandedAddsNormalizePriceAndEndian) {
    for (auto type : {0x21, 0x22, 0x2f}) {
        PitchDecoder decoder;
        const auto es = decoder.decode(packet({time_message(), add_pitch(42, static_cast<std::uint8_t>(type))}));
        ASSERT_EQ(es.size(), 2U); const auto& e = es[1];
        EXPECT_EQ(e.quantity, 100U); EXPECT_EQ(e.price, 1'234'500U); EXPECT_EQ(e.symbol, "AAPL");
        EXPECT_EQ(e.timestamp_ns, 36'000'123'456'789ULL); EXPECT_EQ(e.sequence, 2U);
        EXPECT_EQ(e.channel, 1U);
    }
}
TEST(Pitch, GapsMalformedCountsAndMissingTimeFailClosed) {
    PitchDecoder gap; (void)gap.decode(packet({time_message()}));
    EXPECT_THROW((void)gap.decode(packet({add_pitch()}, 3)), Error);
    EXPECT_THROW((void)gap.decode(packet({add_pitch()}, 2)), Error);
    for (unsigned mutation = 0; mutation != 5; ++mutation) {
        PitchDecoder decoder; auto p = packet({time_message(), add_pitch()});
        if (mutation == 0) p.pop_back();
        if (mutation == 1) p[2] = 3;
        if (mutation == 2) p[8] = 0;
        if (mutation == 3) p[15] = 0xff;
        if (mutation == 4) p = packet({add_pitch()});
        EXPECT_THROW((void)decoder.decode(p), Error);
    }
}
TEST(Pitch, HeartbeatClearAndEndAreScopedPerUnit) {
    PitchDecoder decoder; Books books;
    for (auto unit : {1U, 2U})
        for (const auto& e : decoder.decode(packet({time_message(), add_pitch()}, 1,
                                                  static_cast<std::uint8_t>(unit)))) books.consume("bzx", e);
    EXPECT_TRUE(decoder.decode(packet({}, 3)).empty());
    for (const auto& e : decoder.decode(packet({pitch(0x97, 6), pitch(0x2d, 6)}, 3))) books.consume("bzx", e);
    ASSERT_EQ(books.books().size(), 1U); EXPECT_EQ(books.books().begin()->first.channel, 2);
    EXPECT_FALSE(decoder.complete());
    (void)decoder.decode(packet({pitch(0x2d, 6)}, 3, 2)); EXPECT_TRUE(decoder.complete());
    EXPECT_THROW((void)decoder.decode(packet({add_pitch()}, 5)), Error);
}
TEST(Pitch, ExecutionWithHiddenSizeUsesExplicitRemainingNotTradeQuantity) {
    PitchDecoder decoder; Books books;
    for (const auto& e : decoder.decode(packet({time_message(), add_pitch()}))) books.consume("bzx", e);
    auto execute = pitch(0x24, 38); number(execute, 6, 8, 42, true);
    number(execute, 14, 4, 500, true); number(execute, 18, 4, 150, true);
    number(execute, 30, 8, 123, true);
    for (const auto& e : decoder.decode(packet({execute}, 3))) books.consume("bzx", e);
    const auto order = books.books().begin()->second.order(42);
    ASSERT_TRUE(order); EXPECT_EQ(order->quantity, 150U); EXPECT_EQ(order->price, 1'234'500U);
    EXPECT_TRUE(books.validate_invariants());
}
TEST(Pitch, ModifyFlagReduceDeleteAndHiddenTradeSemantics) {
    PitchDecoder decoder; Books books;
    for (const auto& e : decoder.decode(packet({time_message(), add_pitch(1), add_pitch(2)})))
        books.consume("bzx", e);
    auto modify = pitch(0x28, 19); number(modify, 6, 8, 1, true);
    number(modify, 14, 2, 50, true); number(modify, 16, 2, 12345, true); modify[18] = 3;
    auto reduce = pitch(0x25, 18); number(reduce, 6, 8, 2, true); number(reduce, 14, 4, 10, true);
    for (const auto& e : decoder.decode(packet({modify, reduce}, 4))) books.consume("bzx", e);
    const auto& book = books.books().begin()->second;
    EXPECT_EQ(book.front(Side::Bid)->order_id, 1U); EXPECT_EQ(book.order(2)->quantity, 90U);
    modify[18] = 1;
    auto hidden = pitch(0x2b, 33); hidden[14] = 'B'; number(hidden, 15, 2, 500, true);
    text(hidden, 17, 6, "AAPL"); number(hidden, 23, 2, 12345, true);
    for (const auto& e : decoder.decode(packet({modify, hidden}, 6))) books.consume("bzx", e);
    EXPECT_EQ(book.front(Side::Bid)->order_id, 2U); EXPECT_EQ(book.order(1)->quantity, 50U);
    auto remove = pitch(0x29, 14); number(remove, 6, 8, 2, true);
    for (const auto& e : decoder.decode(packet({remove}, 8))) books.consume("bzx", e);
    EXPECT_FALSE(book.order(2)); EXPECT_TRUE(books.validate_invariants());
}
TEST(FeedBooks, NamespacesIsolateIdsAndFilteringRetainsFollowupRouting) {
    Books books("AAPL"); Event e; e.type = EventType::Add; e.order_id = 42; e.symbol = "MSFT";
    e.price = 1ULL << 40U; e.quantity = 10; // Unselected symbols need no core-price narrowing.
    books.consume("nasdaq", e); EXPECT_TRUE(books.books().empty());
    e.symbol = "AAPL"; e.price = 10000;
    books.consume("bzx", e); e.channel = 2; books.consume("bzx", e);
    e.type = EventType::Reduce; e.channel = 0; e.symbol.clear(); e.quantity = 5;
    books.consume("nasdaq", e);
    EXPECT_EQ(books.books().size(), 2U); EXPECT_TRUE(books.validate_invariants());
    e.type = EventType::Clear; books.consume("bzx", e); EXPECT_EQ(books.books().size(), 1U);
}
TEST(FeedBooks, UnknownOverCancelAndPriceOverflowPoisonState) {
    for (unsigned mutation = 0; mutation != 3; ++mutation) {
        Books books; Event e; e.type = EventType::Add; e.order_id = 1;
        e.symbol = "AAPL"; e.price = 10000; e.quantity = 10; books.consume("nasdaq", e);
        if (mutation == 0) { e.type = EventType::Delete; e.order_id = 2; }
        if (mutation == 1) { e.type = EventType::Reduce; e.quantity = 11; }
        if (mutation == 2) { e.order_id = 2; e.price = 1ULL << 32U; }
        EXPECT_THROW(books.consume("nasdaq", e), Error); EXPECT_FALSE(books.valid());
        EXPECT_FALSE(books.validate_invariants()); EXPECT_THROW(books.consume("nasdaq", e), Error);
    }
}
TEST(FeedBooks, ReplaceFlagsAndNewIdPreserveOrResetFifo) {
    OrderBook book;
    ASSERT_EQ(book.apply(AddOrder{1, Side::Ask, 100, 10}), ApplyResult::Ok);
    ASSERT_EQ(book.apply(AddOrder{2, Side::Ask, 100, 10}), ApplyResult::Ok);
    ASSERT_EQ(book.replace(1, {1, Side::Ask, 100, 5}, true), ApplyResult::Ok);
    EXPECT_EQ(book.front(Side::Ask)->order_id, 1U);
    ASSERT_EQ(book.replace(1, {1, Side::Ask, 100, 5}, false), ApplyResult::Ok);
    EXPECT_EQ(book.front(Side::Ask)->order_id, 2U);
    ASSERT_EQ(book.replace(2, {3, Side::Ask, 101, 20}), ApplyResult::Ok);
    EXPECT_FALSE(book.order(2)); EXPECT_EQ(book.order(3)->price, 101U);
    EXPECT_EQ(book.replace(1, {3, Side::Ask, 101, 10}), ApplyResult::DuplicateOrder);
    EXPECT_TRUE(book.validate_invariants());
}
TEST(FeedBooks, LargerPoolAndReplacementExhaustionPreserveState) {
    OrderBook larger(1024 * 1024);
    for (OrderId i = 1; i <= 10000; ++i)
        ASSERT_EQ(larger.apply(AddOrder{i, Side::Bid, 100, 10}), ApplyResult::Ok);
    EXPECT_TRUE(larger.validate_invariants());
    OrderBook small; OrderId next = 1;
    while (small.apply(AddOrder{next, Side::Bid, 100, 10}) == ApplyResult::Ok) ++next;
    EXPECT_EQ(small.replace(1, {next, Side::Bid, 101, 5}), ApplyResult::AllocationFailed);
    EXPECT_EQ(small.order(1)->quantity, 10U); EXPECT_FALSE(small.order(next));
    EXPECT_TRUE(small.validate_invariants());
}
}  // namespace
}  // namespace order_book::feed
