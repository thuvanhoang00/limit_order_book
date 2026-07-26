#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

#include "order_book/events.hpp"

namespace order_book {

struct ParseResult {
    std::optional<MarketEvent> event;
    std::string error;
};

[[nodiscard]] ParseResult parse_event_line(std::string_view line);
void write_event_csv(std::ostream& output, const MarketEvent& event);
void write_csv_header(std::ostream& output);

}  // namespace order_book
