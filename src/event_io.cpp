#include "order_book/event_io.hpp"

#include <charconv>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace order_book {
namespace {

[[nodiscard]] std::vector<std::string_view> split_csv(const std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0U;

    while (start <= line.size()) {
        const std::size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }

        fields.push_back(line.substr(start, comma - start));
        start = comma + 1U;
    }

    return fields;
}

template <typename Integer>
[[nodiscard]] bool parse_integer(const std::string_view text, Integer& value) {
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    return error == std::errc{} && ptr == end;
}

[[nodiscard]] std::optional<Side> parse_side(const std::string_view text) {
    if (text == "B" || text == "bid") {
        return Side::Bid;
    }
    if (text == "A" || text == "ask") {
        return Side::Ask;
    }
    return std::nullopt;
}

}  // namespace

ParseResult parse_event_line(const std::string_view line) {
    if (line.empty() || line.starts_with('#') || line.starts_with("sequence,")) {
        return {};
    }

    const auto fields = split_csv(line);
    if (fields.size() != 6U) {
        return {.event = std::nullopt, .error = "expected 6 CSV fields"};
    }

    Sequence sequence{};
    OrderId order_id{};
    Price price{};
    Quantity quantity{};

    if (!parse_integer(fields[0], sequence)) {
        return {.event = std::nullopt, .error = "invalid sequence"};
    }
    if (!parse_integer(fields[3], order_id)) {
        return {.event = std::nullopt, .error = "invalid order_id"};
    }

    const std::string_view type = fields[1];
    if (type == "A") {
        const auto side = parse_side(fields[2]);
        if (!side.has_value()) {
            return {.event = std::nullopt, .error = "invalid side"};
        }
        if (!parse_integer(fields[4], price)) {
            return {.event = std::nullopt, .error = "invalid price"};
        }
        if (!parse_integer(fields[5], quantity)) {
            return {.event = std::nullopt, .error = "invalid quantity"};
        }

        return {
            .event = AddOrder{
                .sequence = sequence,
                .order_id = order_id,
                .side = *side,
                .price = price,
                .quantity = quantity},
            .error = {}};
    }

    if (type == "C") {
        return {
            .event = CancelOrder{
                .sequence = sequence,
                .order_id = order_id},
            .error = {}};
    }

    if (type == "E") {
        if (!parse_integer(fields[5], quantity)) {
            return {.event = std::nullopt, .error = "invalid quantity"};
        }

        return {
            .event = ExecuteOrder{
                .sequence = sequence,
                .order_id = order_id,
                .quantity = quantity},
            .error = {}};
    }

    return {.event = std::nullopt, .error = "unknown event type"};
}

void write_csv_header(std::ostream& output) {
    output << "sequence,type,side,order_id,price,quantity\n";
}

void write_event_csv(std::ostream& output, const MarketEvent& event) {
    std::visit(
        [&output](const auto& concrete_event) {
            using Event = std::decay_t<decltype(concrete_event)>;

            if constexpr (std::is_same_v<Event, AddOrder>) {
                output << concrete_event.sequence << ",A,"
                       << (concrete_event.side == Side::Bid ? 'B' : 'A') << ','
                       << concrete_event.order_id << ','
                       << concrete_event.price << ','
                       << concrete_event.quantity << '\n';
            } else if constexpr (std::is_same_v<Event, CancelOrder>) {
                output << concrete_event.sequence << ",C,,"
                       << concrete_event.order_id << ",0,0\n";
            } else {
                output << concrete_event.sequence << ",E,,"
                       << concrete_event.order_id << ",0,"
                       << concrete_event.quantity << '\n';
            }
        },
        event);
}

}  // namespace order_book
