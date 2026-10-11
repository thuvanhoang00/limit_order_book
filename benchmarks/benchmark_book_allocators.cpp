#include <benchmark/benchmark.h>

#include <optional>

#include "order_book/core/order_book.hpp"
#include "support/allocation_probe.hpp"
#include "support/book_inspection.hpp"
#include "support/book_workloads.hpp"
#include "support/heap_order_book.hpp"

namespace order_book {
namespace {
using detail::BookInspection;
using detail::BookWorkload;

template <class Book>
bool replay(Book& book, const std::vector<EventPayload>& events) {
    for (const auto& event : events) {
        auto result = book.apply(event);
        benchmark::DoNotOptimize(result);
        if (result != ApplyResult::Ok) return false;
    }
    return true;
}

bool equivalent(const BookWorkload& workload) {
    OrderBook pooled;
    HeapOrderBook baseline;
    const auto check = [&](const auto& events) {
        for (const auto& event : events) {
            if (pooled.apply(event) != ApplyResult::Ok || baseline.apply(event) != ApplyResult::Ok)
                return false;
            if (BookInspection::snapshot(pooled) != BookInspection::snapshot(baseline) ||
                !pooled.validate_invariants() || !baseline.validate_invariants())
                return false;
        }
        return true;
    };
    return check(workload.seed) && check(workload.events);
}

template <class Book>
bool prepare(Book& book, const BookWorkload& workload, bool warm) {
    if (warm) {
        if (!replay(book, workload.seed) || !replay(book, workload.events)) return false;
        book.reset();
    }
    return replay(book, workload.seed);
}

template <class Book>
void measure(benchmark::State& state, bool churn, bool warm) {
    const auto count = static_cast<std::size_t>(state.range(0));
    const auto workload =
        churn ? detail::churn_workload(count) : detail::fill_clear_workload(count);
    // Full state/FIFO comparison and allocation counting happen outside timed iterations.
    if (!equivalent(workload)) {
        state.SkipWithError("baseline/pooled state differs or workload exceeds capacity");
        return;
    }
    detail::allocation_probe::Counts allocations{};
    {
        Book counting_book;
        if (!prepare(counting_book, workload, warm)) {
            state.SkipWithError("allocation-count setup failed");
            return;
        }
        bool ok;
        {
            detail::allocation_probe::CountScope scope;
            ok = replay(counting_book, workload.events);
            allocations = scope.counts();
        }
        if (!ok) {
            state.SkipWithError("allocation-count replay failed");
            return;
        }
    }
    std::optional<Book> book;
    for (auto _ : state) {
        state.PauseTiming();
        book.emplace();  // constructor/backing storage and prior destruction are excluded
        const bool prepared = prepare(*book, workload, warm);
        state.ResumeTiming();
        if (!prepared || !replay(*book, workload.events)) {
            state.SkipWithError("unexpected apply result");
            return;
        }
    }
    const auto events = static_cast<double>(workload.events.size());
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(workload.events.size()));
    state.counters["heap_allocs/event"] = static_cast<double>(allocations.calls) / events;
    state.counters["heap_requested_bytes/event"] = static_cast<double>(allocations.bytes) / events;
}

template <class Book>
void BM_FillClearFirstUse(benchmark::State& state) {
    measure<Book>(state, false, false);
}
template <class Book>
void BM_FillClearReuse(benchmark::State& state) {
    measure<Book>(state, false, true);
}
template <class Book>
void BM_AddCancelChurn(benchmark::State& state) {
    measure<Book>(state, true, true);
}

BENCHMARK_TEMPLATE(BM_FillClearFirstUse, HeapOrderBook)
    ->Arg(128)
    ->Arg(256)
    ->Unit(benchmark::kNanosecond);
BENCHMARK_TEMPLATE(BM_FillClearFirstUse, OrderBook)
    ->Arg(128)
    ->Arg(256)
    ->Unit(benchmark::kNanosecond);
BENCHMARK_TEMPLATE(BM_FillClearReuse, HeapOrderBook)
    ->Arg(128)
    ->Arg(256)
    ->Unit(benchmark::kNanosecond);
BENCHMARK_TEMPLATE(BM_FillClearReuse, OrderBook)->Arg(128)->Arg(256)->Unit(benchmark::kNanosecond);
BENCHMARK_TEMPLATE(BM_AddCancelChurn, HeapOrderBook)
    ->Arg(128)
    ->Arg(256)
    ->Unit(benchmark::kNanosecond);
BENCHMARK_TEMPLATE(BM_AddCancelChurn, OrderBook)->Arg(128)->Arg(256)->Unit(benchmark::kNanosecond);
}  // namespace
}  // namespace order_book
