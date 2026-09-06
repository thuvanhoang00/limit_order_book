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
    if (text == "BID" || text == "B" || text == "bid") {
        return Side::Bid;
    }
    if (text == "ASK" || text == "A" || text == "ask") {
        return Side::Ask;
    }
    return std::nullopt;
}

}  // namespace

ParseResult parse_event_line(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1U);
    }

    if (line.empty() || line.starts_with('#') || line.starts_with("sequence,")) {
        return {};
    }

    const auto fields = split_csv(line);
    if (fields.size() != 7U) {
        return {.event = std::nullopt, .error = "expected 7 CSV fields"};
    }

    Sequence sequence{};
    InstrumentId instrument_id{};
    OrderId order_id{};
    Price price{};
    Quantity quantity{};

    if (!parse_integer(fields[0], sequence)) {
        return {.event = std::nullopt, .error = "invalid sequence"};
    }
    if (!parse_integer(fields[1], instrument_id)) {
        return {.event = std::nullopt, .error = "invalid instrument_id"};
    }
    if (!parse_integer(fields[3], order_id)) {
        return {.event = std::nullopt, .error = "invalid order_id"};
    }

    const std::string_view type = fields[2];
    if (type == "ADD") {
        const auto side = parse_side(fields[4]);
        if (!side.has_value()) {
            return {.event = std::nullopt, .error = "invalid side"};
        }
        if (!parse_integer(fields[5], price)) {
            return {.event = std::nullopt, .error = "invalid price"};
        }
        if (!parse_integer(fields[6], quantity)) {
            return {.event = std::nullopt, .error = "invalid quantity"};
        }

        return {.event = MarketEvent{.sequence = sequence,
                                     .instrument_id = instrument_id,
                                     .payload =
                                         AddOrder{
                                             .order_id = order_id,
                                             .side = *side,
                                             .price = price,
                                             .quantity = quantity,
                                         }},
                .error = {}};
    }

    if (type == "CANCEL") {
        if (!fields[4].empty() || !fields[5].empty() || !fields[6].empty()) {
            return {.event = std::nullopt,
                    .error = "cancel fields side, price, and quantity must be empty"};
        }
        return {.event = MarketEvent{.sequence = sequence,
                                     .instrument_id = instrument_id,
                                     .payload = CancelOrder{.order_id = order_id}},
                .error = {}};
    }

    if (type == "EXECUTE") {
        if (!fields[4].empty() || !fields[5].empty()) {
            return {.event = std::nullopt, .error = "execute fields side and price must be empty"};
        }
        if (!parse_integer(fields[6], quantity)) {
            return {.event = std::nullopt, .error = "invalid quantity"};
        }

        return {.event = MarketEvent{.sequence = sequence,
                                     .instrument_id = instrument_id,
                                     .payload =
                                         ExecuteOrder{.order_id = order_id, .quantity = quantity}},
                .error = {}};
    }

    return {.event = std::nullopt, .error = "unknown event type"};
}

void write_csv_header(std::ostream& output) {
    output << "sequence,instrument_id,event_type,order_id,side,price,quantity\n";
}

void write_event_csv(std::ostream& output, const MarketEvent& event) {
    output << event.sequence << ',' << event.instrument_id << ',';
    std::visit(
        [&output](const auto& concrete_event) {
            using Event = std::decay_t<decltype(concrete_event)>;

            if constexpr (std::is_same_v<Event, AddOrder>) {
                output << "ADD," << concrete_event.order_id << ','
                       << (concrete_event.side == Side::Bid ? "BID" : "ASK") << ','
                       << concrete_event.price << ',' << concrete_event.quantity << '\n';
            } else if constexpr (std::is_same_v<Event, CancelOrder>) {
                output << "CANCEL," << concrete_event.order_id << ",,,\n";
            } else {
                output << "EXECUTE," << concrete_event.order_id << ",,," << concrete_event.quantity
                       << '\n';
            }
        },
        event.payload);
}

}  // namespace order_book
