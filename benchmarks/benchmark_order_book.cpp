#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "order_book/order_book.hpp"

namespace order_book {
namespace {

[[nodiscard]] std::vector<EventPayload> make_add_only_events(const std::size_t count) {
    std::vector<EventPayload> events;
    events.reserve(count);

    for (std::size_t index = 0U; index < count; ++index) {
        const auto id = static_cast<OrderId>(index + 1U);
        const auto price_offset = static_cast<Price>(index % 200U);
        const Side side = (index % 2U == 0U) ? Side::Bid : Side::Ask;
        const Price price = side == Side::Bid ? 10'000U - price_offset : 10'001U + price_offset;

        events.emplace_back(AddOrder{
            .order_id = id,
            .side = side,
            .price = price,
            .quantity = 100U});
    }

    return events;
}

void BM_AddOrders(benchmark::State& state) {
    const auto event_count = static_cast<std::size_t>(state.range(0));
    const auto events = make_add_only_events(event_count);
    OrderBook book;

    for (auto _ : state) {
        state.PauseTiming();
        book.reset();
        state.ResumeTiming();

        for (const auto& event : events) {
            ApplyResult result = book.apply(event);
            if (result == ApplyResult::NotImplemented) {
                state.SkipWithError("Implement OrderBook core before benchmarking");
                return;
            }
            benchmark::DoNotOptimize(result);
        }
    }

    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(event_count));
}

BENCHMARK(BM_AddOrders)
    ->Arg(1'000)
    ->Arg(10'000)
    ->Unit(benchmark::kNanosecond);

}  // namespace
}  // namespace order_book
