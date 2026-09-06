#include <gtest/gtest.h>

#include <sstream>
#include <string_view>
#include <variant>

#include "order_book/event_io.hpp"

namespace order_book {
namespace {

TEST(EventIo, ParsesAddOrder) {
    const auto result = parse_event_line("1,55,ADD,1001,BID,10100,25");

    ASSERT_TRUE(result.event.has_value()) << result.error;
    EXPECT_EQ(result.event->sequence, 1U);
    EXPECT_EQ(result.event->instrument_id, 55U);
    ASSERT_TRUE(std::holds_alternative<AddOrder>(result.event->payload));
    EXPECT_EQ(std::get<AddOrder>(result.event->payload),
              (AddOrder{1001U, Side::Bid, 10100U, 25U}));
}

TEST(EventIo, ParsesCancelOrder) {
    const auto result = parse_event_line("2,55,CANCEL,1001,,,");

    ASSERT_TRUE(result.event.has_value()) << result.error;
    EXPECT_EQ(result.event->sequence, 2U);
    EXPECT_EQ(result.event->instrument_id, 55U);
    EXPECT_EQ(std::get<CancelOrder>(result.event->payload), (CancelOrder{1001U}));
}

TEST(EventIo, ParsesExecuteOrder) {
    const auto result = parse_event_line("3,55,EXECUTE,1001,,,5");

    ASSERT_TRUE(result.event.has_value()) << result.error;
    EXPECT_EQ(result.event->sequence, 3U);
    EXPECT_EQ(result.event->instrument_id, 55U);
    EXPECT_EQ(std::get<ExecuteOrder>(result.event->payload), (ExecuteOrder{1001U, 5U}));
}

TEST(EventIo, RejectsMalformedInput) {
    const auto result = parse_event_line("1,55,ADD,broken,BID,10100,25");
    EXPECT_FALSE(result.event.has_value());
    EXPECT_FALSE(result.error.empty());
}

TEST(EventIo, WritesRoundTrippableCsv) {
    const MarketEvent input{
        .sequence = 4U,
        .instrument_id = 55U,
        .payload = AddOrder{1002U, Side::Ask, 10200U, 12U}};
    std::ostringstream output;

    write_event_csv(output, input);
    EXPECT_EQ(output.str(), "4,55,ADD,1002,ASK,10200,12\n");
    const auto parsed = parse_event_line(output.str());

    ASSERT_TRUE(parsed.event.has_value()) << parsed.error;
    EXPECT_EQ(*parsed.event, input);
}

TEST(EventIo, ParsesWindowsLineEnding) {
    const auto result = parse_event_line("5,55,EXECUTE,1002,,,7\r\n");

    ASSERT_TRUE(result.event.has_value()) << result.error;
    EXPECT_EQ(*result.event,
              (MarketEvent{.sequence = 5U,
                           .instrument_id = 55U,
                           .payload = ExecuteOrder{1002U, 7U}}));
}

TEST(EventIo, WritesCanonicalHeader) {
    std::ostringstream output;

    write_csv_header(output);

    EXPECT_EQ(output.str(),
              "sequence,instrument_id,event_type,order_id,side,price,quantity\n");
}

TEST(EventIo, SkipsBlankCommentAndHeaderLinesWithoutErrors) {
    for (const std::string_view line : {
             "",
             "# generated fixture",
             "sequence,instrument_id,event_type,order_id,side,price,quantity",
         }) {
        const auto result = parse_event_line(line);
        EXPECT_FALSE(result.event.has_value()) << line;
        EXPECT_TRUE(result.error.empty()) << line;
    }
}

TEST(EventIo, RejectsInvalidCsvFields) {
    for (const std::string_view line : {
             "1,55,ADD,1001,BID,10100",
             "1,55,ADD,1001,BID,10100,25,extra",
             "invalid,55,ADD,1001,BID,10100,25",
             "1,invalid,ADD,1001,BID,10100,25",
             "1,18446744073709551616,ADD,1001,BID,10100,25",
             "1,55,UNKNOWN,1001,BID,10100,25",
             "1,55,ADD,1001,X,10100,25",
             "1,55,ADD,invalid,BID,10100,25",
             "1,55,ADD,1001,BID,invalid,25",
             "1,55,ADD,1001,BID,10100,invalid",
             "1,55,ADD,1001,BID,4294967296,25",
             "1,55,ADD,1001,BID,10100,4294967296",
             "1,55,CANCEL,1001,BID,,",
             "1,55,CANCEL,1001,,,1",
             "1,55,EXECUTE,1001,BID,,1",
             "1,55,EXECUTE,1001,,10000,1",
             "1,55,EXECUTE,1001,,,-1",
         }) {
        const auto result = parse_event_line(line);
        EXPECT_FALSE(result.event.has_value()) << line;
        EXPECT_FALSE(result.error.empty()) << line;
    }
}

TEST(EventIo, RoundTripsEveryEventType) {
    for (const MarketEvent& input : {
             MarketEvent{1U, 55U, AddOrder{1001U, Side::Bid, 10'000U, 20U}},
             MarketEvent{2U, 55U, AddOrder{2001U, Side::Ask, 10'100U, 30U}},
             MarketEvent{3U, 55U, CancelOrder{1001U}},
             MarketEvent{4U, 55U, ExecuteOrder{2001U, 7U}},
         }) {
        std::ostringstream output;
        write_event_csv(output, input);

        const auto parsed = parse_event_line(output.str());
        ASSERT_TRUE(parsed.event.has_value()) << parsed.error;
        EXPECT_EQ(*parsed.event, input);
    }
}

}  // namespace
}  // namespace order_book
