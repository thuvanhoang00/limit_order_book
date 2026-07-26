#include <chrono>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "order_book/event_io.hpp"
#include "order_book/order_book.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: replay_order_book <events.csv>\n";
        return 1;
    }

    const std::string input_path = argv[1];
    std::ifstream input(input_path);
    if (!input) {
        std::cerr << "Failed to open input file: " << input_path << '\n';
        return 1;
    }

    order_book::OrderBook book;
    std::string line;
    std::size_t line_number = 0U;
    std::size_t event_count = 0U;

    const auto start = std::chrono::steady_clock::now();

    while (std::getline(input, line)) {
        ++line_number;
        const auto parsed = order_book::parse_event_line(line);

        if (!parsed.event.has_value()) {
            if (parsed.error.empty()) {
                continue;
            }

            std::cerr << "Parse error at line " << line_number << ": "
                      << parsed.error << '\n';
            return 1;
        }

        const auto result = book.apply(*parsed.event);
        if (result == order_book::ApplyResult::NotImplemented) {
            std::cerr << "OrderBook core is not implemented yet. Start with "
                         "tests/test_order_book.cpp.\n";
            return 2;
        }
        if (result != order_book::ApplyResult::Ok) {
            std::cerr << "Apply error at line " << line_number << ": "
                      << order_book::to_string(result) << '\n';
            return 1;
        }

        ++event_count;
    }

    const auto end = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(end - start).count();
    const double ns_per_event = event_count == 0U
        ? 0.0
        : (elapsed * 1'000'000'000.0) / static_cast<double>(event_count);
    const double million_events_per_second = elapsed == 0.0
        ? 0.0
        : static_cast<double>(event_count) / elapsed / 1'000'000.0;

    std::string reason;
    if (!book.validate_invariants(&reason)) {
        std::cerr << "Invariant failure after replay: " << reason << '\n';
        return 1;
    }

    std::cout << std::fixed << std::setprecision(2)
              << "events=" << event_count << '\n'
              << "elapsed_seconds=" << elapsed << '\n'
              << "ns_per_event=" << ns_per_event << '\n'
              << "million_events_per_second=" << million_events_per_second << '\n'
              << "remaining_orders=" << book.order_count() << '\n'
              << "bid_levels=" << book.bid_level_count() << '\n'
              << "ask_levels=" << book.ask_level_count() << '\n';

    return 0;
}
