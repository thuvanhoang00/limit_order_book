#include <algorithm>
#include <charconv>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "order_book/exchange/matching_engine.hpp"

int main(int argc, char** argv) {
    using namespace order_book;
    using namespace order_book::exchange;
    try {
        std::size_t count = 100000;
        if (argc > 2) throw std::invalid_argument("Usage: benchmark_matching [even-event-count]");
        if (argc == 2) {
            const std::string_view value(argv[1]);
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), count);
            if (error != std::errc{} || end != value.data() + value.size())
                throw std::invalid_argument("Invalid count");
        }
        if (!count || count % 2)
            throw std::invalid_argument("Event count must be positive and even");
        // Predecode alternating adds/cancels and add/match pairs. Reserve all
        // measurements before timing; process() output allocations are included.
        std::vector<Request> events;
        events.reserve(count);
        for (std::size_t i = 0; i < count / 2; ++i) {
            Request add;
            add.client_id = 1;
            add.sequence = i * 2 + 1;
            add.instrument_id = 1;
            add.order_id = i + 1;
            add.side = Side::Ask;
            add.price = 100;
            add.quantity = 10;
            events.push_back(add);
            auto remove = add;
            remove.sequence++;
            if (i % 2) {
                remove.client_id = 2;
                remove.side = Side::Bid;
            } else {
                remove.type = RequestType::Cancel;
                remove.price = remove.quantity = 0;
            }
            events.push_back(remove);
        }
        MatchingEngine engine;
        engine.add_instrument(1);
        std::vector<std::uint64_t> times;
        times.reserve(count);
        std::uint64_t checksum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (const auto& event : events) {
            const auto before = std::chrono::steady_clock::now();
            auto result = engine.process(event);
            const auto after = std::chrono::steady_clock::now();
            times.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count()));
            if (result.reports[0].status != Status::Ok)
                throw std::runtime_error("Benchmark command rejected");
            checksum += result.reports.size() + result.updates.size();
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (!engine.validate_invariants() || engine.book(1)->order_count())
            throw std::runtime_error("Benchmark state invalid");
        std::sort(times.begin(), times.end());
        const auto percentile = [&](std::size_t percent) {
            return times[(count - 1) * percent / 100];
        };
        const auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        std::cout << "events=" << count
                  << " ns_per_event=" << static_cast<double>(total_ns) / static_cast<double>(count)
                  << " events_per_second="
                  << static_cast<double>(count) * 1e9 / static_cast<double>(total_ns)
                  << " p50_ns=" << percentile(50) << " p95_ns=" << percentile(95)
                  << " p99_ns=" << percentile(99) << " checksum=" << checksum << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
