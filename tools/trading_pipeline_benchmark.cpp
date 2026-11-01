#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>

#include "order_book/trading/client.hpp"
#include "trading_args.hpp"

using namespace order_book;
using namespace order_book::trading;
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
struct Step {
    Message command;
    std::vector<Message> expected;
    Sequence watermark{};
};
struct Sample {
    std::uint64_t complete_ns{}, market_ns{};
};
std::uint64_t ns(Clock::duration duration) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
}
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
// Balanced round trips leave both accounts flat and the book empty every cycle.
std::vector<Step> workload(std::uint64_t cycles) {
    MatchingEngine reference;
    std::vector<Step> steps;
    steps.reserve(static_cast<std::size_t>(cycles) * 8);
    std::uint64_t requests[2]{};
    for (std::uint64_t cycle = 0; cycle != cycles; ++cycle) {
        for (unsigned leg = 0; leg != 2; ++leg) {
            const auto side = leg == 0 ? Side::Ask : Side::Bid;
            for (unsigned operation = 0; operation != 4; ++operation) {
                Message c;
                c.client_id = operation == 2 ? 2 : 1;
                c.request_id = ++requests[c.client_id - 1];
                c.order_id = 2 * cycle + leg + 1;
                c.instrument_id = 1;
                c.price = 100;
                c.side = side;
                c.quantity = operation == 0 ? 8 : 5;
                if (operation == 1) c.action = Action::Replace;
                if (operation == 2) {
                    c.side = side == Side::Ask ? Side::Bid : Side::Ask;
                    c.tif = TimeInForce::IOC;
                    c.quantity = 2;
                }
                if (operation == 3) {
                    c.action = Action::Cancel;
                    c.price = c.quantity = 0;
                }
                auto batch = reference.process(c);
                std::vector<Message> own;
                for (const auto& report : batch.reports) {
                    require(report.report != ReportType::Rejected, "Reference workload rejected");
                    if (report.client_id == c.client_id && report.request_id == c.request_id)
                        own.push_back(report);
                }
                steps.push_back({c, std::move(own), reference.sequence()});
            }
        }
    }
    require(reference.validate_invariants() && reference.book(1).order_count() == 0 &&
                reference.stats().trades == cycles * 2 && reference.stats().volume == cycles * 4 &&
                reference.position(1, 1).quantity == 0 && reference.position(2, 1).quantity == 0,
            "Reference state verification failed");
    return steps;
}
void verify(std::vector<Message> actual, const std::vector<Message>& expected) {
    require(actual.size() == expected.size(), "Unexpected private report count");
    for (std::size_t i = 0; i != actual.size(); ++i) {
        auto want = expected[i];
        // Session handshakes and random exchange epochs differ from the reference.
        actual[i].sequence = want.sequence = 0;
        actual[i].epoch = want.epoch = 0;
        require(actual[i] == want, "Private report differs from reference matching result");
    }
}
void wait_market(TradingClient& client, Sequence target) {
    const auto deadline = Clock::now() + 3s;
    do {
        client.poll(0ms);
        require(client.market().synchronized(), "Market replica lost synchronization");
        if (client.market().sequence() == target) return;
        require(client.market().sequence() < target, "Market watermark exceeds reference");
        std::this_thread::yield();
    } while (Clock::now() < deadline);
    throw std::runtime_error("Market update deadline");
}
std::string distribution(std::vector<std::uint64_t> values) {
    std::sort(values.begin(), values.end());
    // Nearest-rank percentiles, including p99.9 (permille 999).
    const auto percentile = [&](std::size_t permille) {
        return values[(values.size() * permille + 999) / 1000 - 1];
    };
    const auto total = std::accumulate(values.begin(), values.end(), 0.0L);
    return "{\"mean_ns\":" + std::to_string(total / values.size()) +
           ",\"p50_ns\":" + std::to_string(percentile(500)) +
           ",\"p99_ns\":" + std::to_string(percentile(990)) +
           ",\"p999_ns\":" + std::to_string(percentile(999)) +
           ",\"max_ns\":" + std::to_string(values.back()) + "}";
}
}  // namespace

int main(int argc, char** argv) {
    try {
        TradingArgs args(argc, argv);
        ClientConfig config;
        config.tcp_port = static_cast<std::uint16_t>(args.number("--tcp-port", 9100, 1, 65535));
        config.multicast_port = static_cast<std::uint16_t>(args.number("--md-port", 9101, 1, 65535));
        config.group = args.text("--group", config.group);
        const auto cycles = args.number("--iterations", 5000, 1, 100000);
        const auto warmup = args.number("--warmup", 100, 0, 10000);
        const auto output = args.text("--output", "");
        const auto samples_path = args.text("--samples", "");
        args.finish();
        const auto steps = workload(cycles + warmup);
        config.client_id = 1;
        TradingClient maker(config);
        config.client_id = 2;
        TradingClient taker(config);
        require(maker.market().sequence() == 0 && taker.market().sequence() == 0,
                "Benchmark requires a fresh exchange");
        const auto maker_recoveries = maker.recoveries(), taker_recoveries = taker.recoveries();
        std::vector<Sample> samples;
        samples.reserve(static_cast<std::size_t>(cycles) * 8);
        Clock::time_point measured_start;
        for (std::size_t i = 0; i != steps.size(); ++i) {
            const auto& step = steps[i];
            auto& client = step.command.client_id == 1 ? maker : taker;
            if (i == warmup * 8) measured_start = Clock::now();
            const auto start = Clock::now();
            auto reports = client.submit(step.command);
            const auto complete = Clock::now();
            wait_market(client, step.watermark);
            const auto market = Clock::now();
            if (i >= warmup * 8) samples.push_back({ns(complete - start), ns(market - start)});
            verify(std::move(reports), step.expected);
        }
        const auto elapsed = ns(Clock::now() - measured_start);
        const auto target = steps.back().watermark;
        wait_market(maker, target);
        wait_market(taker, target);
        for (auto* client : {&maker, &taker}) {
            const auto p = client->orders().position(1);
            require(client->orders().orders().empty() && client->market().order_count() == 0 &&
                        p.quantity == 0 && p.cash == 0 && p.volume == (cycles + warmup) * 4,
                    "Final local book/order/position/cash verification failed");
        }
        require(maker.recoveries() == maker_recoveries && taker.recoveries() == taker_recoveries,
                "Feed recovered during benchmark; refusing clean-run performance output");
        maker.close(false);
        taker.close(false);
        if (!samples_path.empty()) {
            std::ofstream file(samples_path);
            file << "command_index,action,client_id,complete_ns,market_ns\n";
            for (std::size_t i = 0; i != samples.size(); ++i) {
                const auto& command = steps[static_cast<std::size_t>(warmup) * 8 + i].command;
                file << i << ',' << static_cast<unsigned>(command.action) << ',' << command.client_id
                     << ',' << samples[i].complete_ns << ',' << samples[i].market_ns << '\n';
            }
            if (!file) throw std::runtime_error("Cannot write pipeline samples");
        }
        std::vector<std::uint64_t> complete, market;
        complete.reserve(samples.size());
        market.reserve(samples.size());
        for (const auto& sample : samples) {
            complete.push_back(sample.complete_ns);
            market.push_back(sample.market_ns);
        }
        const auto json = "{\"workload\":\"balanced_add_reduce_match_cancel\","
                          "\"load_model\":\"closed_loop_one_global_inflight\",\"commands\":" +
                          std::to_string(samples.size()) + ",\"warmup_commands\":" +
                          std::to_string(warmup * 8) + ",\"total_commands\":" +
                          std::to_string(steps.size()) + ",\"expected_trades\":" +
                          std::to_string((cycles + warmup) * 2) + ",\"expected_volume\":" +
                          std::to_string((cycles + warmup) * 4) + ",\"market_sequence\":" +
                          std::to_string(target) + ",\"elapsed_ns\":" + std::to_string(elapsed) +
                          ",\"commands_per_second\":" +
                          std::to_string(static_cast<double>(samples.size()) * 1e9 /
                                         static_cast<double>(elapsed)) +
                          ",\"complete\":" + distribution(std::move(complete)) +
                          ",\"market_applied\":" + distribution(std::move(market)) +
                          ",\"verified\":true}";
        std::cout << json << '\n';
        if (!output.empty()) {
            std::ofstream file(output);
            file << json << '\n';
            if (!file) throw std::runtime_error("Cannot write pipeline benchmark output");
        }
    } catch (const std::exception& e) {
        std::cerr << "Pipeline benchmark: " << e.what() << '\n';
        return 1;
    }
}
