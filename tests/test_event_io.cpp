#include <gtest/gtest.h>

#include <sstream>
#include <string_view>
#include <variant>

#include "order_book/event_io.hpp"

namespace order_book {
namespace {

TEST(EventIo, ParsesAddOrder) {
    const auto result = parse_event_line("1,A,B,1001,10100,25");

    ASSERT_TRUE(result.event.has_value()) << result.error;
    ASSERT_TRUE(std::holds_alternative<AddOrder>(*result.event));
    EXPECT_EQ(std::get<AddOrder>(*result.event), (AddOrder{1U, 1001U, Side::Bid, 10100U, 25U}));
}

TEST(EventIo, ParsesCancelOrder) {
    const auto result = parse_event_line("2,C,,1001,0,0");

    ASSERT_TRUE(result.event.has_value()) << result.error;
    EXPECT_EQ(std::get<CancelOrder>(*result.event), (CancelOrder{2U, 1001U}));
}

TEST(EventIo, ParsesExecuteOrder) {
    const auto result = parse_event_line("3,E,,1001,0,5");

    ASSERT_TRUE(result.event.has_value()) << result.error;
    EXPECT_EQ(std::get<ExecuteOrder>(*result.event), (ExecuteOrder{3U, 1001U, 5U}));
}

TEST(EventIo, RejectsMalformedInput) {
    const auto result = parse_event_line("1,A,B,broken,10100,25");
    EXPECT_FALSE(result.event.has_value());
    EXPECT_FALSE(result.error.empty());
}

TEST(EventIo, WritesRoundTrippableCsv) {
    const MarketEvent input = AddOrder{4U, 1002U, Side::Ask, 10200U, 12U};
    std::ostringstream output;

    write_event_csv(output, input);
    const auto parsed = parse_event_line(output.str());

    ASSERT_TRUE(parsed.event.has_value()) << parsed.error;
    EXPECT_EQ(*parsed.event, input);
}

TEST(EventIo, ParsesWindowsLineEnding) {
    const auto result = parse_event_line("5,E,,1002,0,7\r\n");

    ASSERT_TRUE(result.event.has_value()) << result.error;
    EXPECT_EQ(*result.event, MarketEvent(ExecuteOrder{5U, 1002U, 7U}));
}

TEST(EventIo, SkipsBlankCommentAndHeaderLinesWithoutErrors) {
    for (const std::string_view line : {
             "",
             "# generated fixture",
             "sequence,type,side,order_id,price,quantity",
         }) {
        const auto result = parse_event_line(line);
        EXPECT_FALSE(result.event.has_value()) << line;
        EXPECT_TRUE(result.error.empty()) << line;
    }
}

TEST(EventIo, RejectsInvalidCsvFields) {
    for (const std::string_view line : {
             "1,A,B,1001,10100",
             "1,A,B,1001,10100,25,extra",
             "invalid,A,B,1001,10100,25",
             "1,X,B,1001,10100,25",
             "1,A,X,1001,10100,25",
             "1,A,B,invalid,10100,25",
             "1,A,B,1001,invalid,25",
             "1,A,B,1001,10100,invalid",
             "1,A,B,1001,4294967296,25",
             "1,A,B,1001,10100,4294967296",
             "1,E,,1001,0,-1",
         }) {
        const auto result = parse_event_line(line);
        EXPECT_FALSE(result.event.has_value()) << line;
        EXPECT_FALSE(result.error.empty()) << line;
    }
}

TEST(EventIo, RoundTripsEveryEventType) {
    for (const MarketEvent& input : {
             MarketEvent{AddOrder{1U, 1001U, Side::Bid, 10'000U, 20U}},
             MarketEvent{AddOrder{2U, 2001U, Side::Ask, 10'100U, 30U}},
             MarketEvent{CancelOrder{3U, 1001U}},
             MarketEvent{ExecuteOrder{4U, 2001U, 7U}},
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
