#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "order_book/event_io.hpp"

namespace {

struct ActiveOrder {
    order_book::OrderId id{};
    order_book::Quantity remaining{};
};

[[nodiscard]] std::size_t parse_size(const char* text, const std::size_t fallback) {
    try {
        return static_cast<std::size_t>(std::stoull(text));
    } catch (...) {
        return fallback;
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string output_path = argc > 1 ? argv[1] : "data/events.csv";
    const std::size_t event_count = argc > 2 ? parse_size(argv[2], 100'000U) : 100'000U;
    const std::uint64_t seed = argc > 3 ? static_cast<std::uint64_t>(parse_size(argv[3], 42U)) : 42U;
    const auto instrument_id = argc > 4
                                   ? static_cast<order_book::InstrumentId>(parse_size(argv[4], 1U))
                                   : order_book::InstrumentId{1U};

    std::ofstream output(output_path);
    if (!output) {
        std::cerr << "Failed to open output file: " << output_path << '\n';
        return 1;
    }

    std::mt19937_64 random(seed);
    std::uniform_int_distribution<int> operation_distribution(0, 99);
    std::uniform_int_distribution<std::uint32_t> price_offset_distribution(0U, 100U);
    std::uniform_int_distribution<std::uint32_t> quantity_distribution(1U, 500U);

    std::vector<ActiveOrder> active_orders;
    active_orders.reserve(event_count / 2U);

    order_book::write_csv_header(output);

    order_book::Sequence sequence = 1U;
    order_book::OrderId next_order_id = 1U;

    for (std::size_t index = 0U; index < event_count; ++index, ++sequence) {
        const int operation = operation_distribution(random);
        const bool must_add = active_orders.empty();

        if (must_add || operation < 65) {
            const order_book::Side side = (next_order_id % 2U == 0U)
                ? order_book::Side::Ask
                : order_book::Side::Bid;
            const auto offset = price_offset_distribution(random);
            const order_book::Price price = side == order_book::Side::Bid
                ? 10'000U - offset
                : 10'001U + offset;
            const order_book::Quantity quantity = quantity_distribution(random);

            const order_book::MarketEvent event{
                .sequence = sequence,
                .instrument_id = instrument_id,
                .payload = order_book::AddOrder{
                    .order_id = next_order_id,
                    .side = side,
                    .price = price,
                    .quantity = quantity}};

            order_book::write_event_csv(output, event);
            active_orders.push_back(ActiveOrder{next_order_id, quantity});
            ++next_order_id;
            continue;
        }

        std::uniform_int_distribution<std::size_t> active_index_distribution(
            0U, active_orders.size() - 1U);
        const std::size_t active_index = active_index_distribution(random);
        ActiveOrder& active = active_orders[active_index];

        if (operation < 82) {
            order_book::write_event_csv(output, order_book::MarketEvent{
                .sequence = sequence,
                .instrument_id = instrument_id,
                .payload = order_book::CancelOrder{.order_id = active.id}});

            active_orders[active_index] = active_orders.back();
            active_orders.pop_back();
            continue;
        }

        std::uniform_int_distribution<std::uint32_t> execute_distribution(1U, active.remaining);
        const order_book::Quantity executed = execute_distribution(random);
        order_book::write_event_csv(output, order_book::MarketEvent{
            .sequence = sequence,
            .instrument_id = instrument_id,
            .payload = order_book::ExecuteOrder{
                .order_id = active.id,
                .quantity = executed}});

        active.remaining -= executed;
        if (active.remaining == 0U) {
            active_orders[active_index] = active_orders.back();
            active_orders.pop_back();
        }
    }

    std::cout << "Generated " << event_count << " events for instrument " << instrument_id
              << " at " << output_path << " with seed " << seed << '\n';
    return 0;
}
