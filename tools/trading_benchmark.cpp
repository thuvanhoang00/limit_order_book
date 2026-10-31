#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <vector>

#include "order_book/trading/engine.hpp"
#include "trading_args.hpp"
using namespace order_book;
using namespace order_book::trading;
namespace {
std::vector<Message> workload(std::uint64_t cycles) {
    std::vector<Message> result;
    result.reserve(static_cast<std::size_t>(cycles) * 8);
    std::uint64_t maker_request{}, taker_request{};
    const auto append = [&](ClientId client, OrderId id, Action action, Side side, Price price,
                            Quantity size, TimeInForce tif) {
        Message c;
        c.client_id = client;
        c.order_id = id;
        c.action = action;
        c.side = side;
        c.price = price;
        c.quantity = size;
        c.instrument_id = 1;
        c.tif = tif;
        c.request_id = client == 1 ? ++maker_request : ++taker_request;
        result.push_back(c);
    };
    for (std::uint64_t i = 0; i != cycles; ++i) {
        append(1, 2 * i + 1, Action::New, Side::Ask, 100, 8, TimeInForce::GTC);
        append(1, 2 * i + 1, Action::Replace, Side::Ask, 100, 5, TimeInForce::GTC);
        append(2, 2 * i + 1, Action::New, Side::Bid, 100, 2, TimeInForce::IOC);
        append(1, 2 * i + 1, Action::Cancel, Side::Ask, 0, 0, TimeInForce::GTC);
        append(1, 2 * i + 2, Action::New, Side::Bid, 99, 2, TimeInForce::GTC);
        append(2, 2 * i + 2, Action::New, Side::Ask, 101, 1, TimeInForce::GTC);
        append(2, 2 * i + 2, Action::Cancel, Side::Ask, 0, 0, TimeInForce::GTC);
        append(1, 2 * i + 2, Action::Cancel, Side::Bid, 0, 0, TimeInForce::GTC);
    }
    return result;
}
void verify(const Batch& batch) {
    if (std::any_of(batch.reports.begin(), batch.reports.end(),
                    [](const auto& r) { return r.report == ReportType::Rejected; }))
        throw std::runtime_error("Benchmark workload rejected; refusing performance output");
}
}  // namespace
int main(int argc, char** argv) {
    try {
        TradingArgs args(argc, argv);
        const auto cycles = args.number("--iterations", 1000, 1, 100000);
        const auto output = args.text("--output", "");
        const auto samples = args.text("--samples", "");
        args.finish();
        const auto commands = workload(cycles);
        {
            MatchingEngine warmup;
            for (const auto& c : workload(100)) verify(warmup.process(c));
        }
        MatchingEngine engine;
        std::vector<std::uint64_t> latencies;
        latencies.reserve(commands.size());
        using Clock = std::chrono::steady_clock;
        for (const auto& command : commands) {
            const auto start = Clock::now();
            const auto result = engine.process(command);
            const auto end = Clock::now();
            latencies.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
            verify(result);
        }
        if (!engine.validate_invariants() || engine.book(1).order_count() != 0 ||
            engine.stats().trades != cycles)
            throw std::runtime_error("Benchmark preflight/state verification failed");
        const auto total = std::accumulate(latencies.begin(), latencies.end(), 0ULL);
        if (!samples.empty()) {
            std::ofstream file(samples);
            file << "command_index,latency_ns\n";
            for (std::size_t i = 0; i != latencies.size(); ++i)
                file << i << ',' << latencies[i] << '\n';
            if (!file) throw std::runtime_error("Cannot write latency samples");
        }
        std::sort(latencies.begin(), latencies.end());
        const auto percentile = [&](std::size_t p) {
            return latencies[(latencies.size() - 1) * p / 100];
        };
        const auto json =
            "{\"workload\":\"predecoded_add_reduce_match_cancel\",\"commands\":" +
            std::to_string(commands.size()) +
            ",\"trades\":" + std::to_string(engine.stats().trades) + ",\"mean_ns\":" +
            std::to_string(static_cast<double>(total) / static_cast<double>(commands.size())) +
            ",\"p50_ns\":" + std::to_string(percentile(50)) +
            ",\"p95_ns\":" + std::to_string(percentile(95)) +
            ",\"p99_ns\":" + std::to_string(percentile(99)) +
            ",\"max_ns\":" + std::to_string(latencies.back()) + "}";
        std::cout << json << '\n';
        if (!output.empty()) {
            std::ofstream file(output);
            file << json << '\n';
            if (!file) throw std::runtime_error("Cannot write benchmark output");
        }
    } catch (const std::exception& e) {
        std::cerr << "Benchmark: " << e.what() << '\n';
        return 1;
    }
}
