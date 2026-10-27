#include <chrono>
#include <cstddef>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

#include "order_book/core/logger.hpp"
#include "order_book/core/order_registry.hpp"
#include "order_book/io/event_io.hpp"

namespace {

int replay(const std::string& input_path, order_book::Logger& logger) {
    std::ifstream input(input_path);
    if (!input) {
        (void)logger.try_log(
            {.level = order_book::LogLevel::Error, .event = order_book::LogEvent::InputOpenFailed});
        std::cerr << "Failed to open input file: " << input_path << '\n';
        return 1;
    }

    (void)logger.try_log(
        {.level = order_book::LogLevel::Info, .event = order_book::LogEvent::ReplayStarted});
    order_book::OrderBookRegistry book_registry(&logger);

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

            (void)logger.try_log({.level = order_book::LogLevel::Error,
                                  .event = order_book::LogEvent::ParseFailed,
                                  .result = order_book::ApplyResult::ParseError,
                                  .line_number = line_number});
            std::cerr << "Parse error at line " << line_number << ": " << parsed.error << '\n';
            return 1;
        }

        if (!book_registry.seq_valid(parsed.event->sequence)) {
            (void)logger.try_log({.level = order_book::LogLevel::Error,
                                  .event = order_book::LogEvent::InvalidSequence,
                                  .sequence = parsed.event->sequence,
                                  .instrument_id = parsed.event->instrument_id,
                                  .line_number = line_number});
            std::cerr << "Invalid sequence: last accepted=" << book_registry.get_seq()
                      << ", received=" << parsed.event->sequence << '\n';
            return 1;
        }

        const auto result = book_registry.apply(*parsed.event);
        if (result == order_book::ApplyResult::NotImplemented) {
            std::cerr << "OrderBook core is not implemented yet. Start with "
                         "tests/core/test_order_book.cpp.\n";
            return 2;
        }
        if (result != order_book::ApplyResult::Ok) {
            std::cerr << "Apply error at line " << line_number << ": "
                      << order_book::to_string(result) << '\n';
            return 1;
        }

        ++event_count;
    }

    if (input.bad() || (!input.eof() && input.fail())) {
        (void)logger.try_log({.level = order_book::LogLevel::Error,
                              .event = order_book::LogEvent::InputReadFailed,
                              .line_number = line_number});
        std::cerr << "Failed to read input file: " << input_path << '\n';
        return 1;
    }

    const auto end = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(end - start).count();
    const double ns_per_event =
        event_count == 0U ? 0.0 : (elapsed * 1'000'000'000.0) / static_cast<double>(event_count);
    const double million_events_per_second =
        elapsed == 0.0 ? 0.0 : static_cast<double>(event_count) / elapsed / 1'000'000.0;

    std::string reason;
    if (!book_registry.validate_invariants(&reason)) {
        (void)logger.try_log(
            {.level = order_book::LogLevel::Fatal, .event = order_book::LogEvent::InvariantFailed});
        std::cerr << "Invariant failure after replay: " << reason << '\n';
        return 1;
    }

    (void)logger.try_log({.level = order_book::LogLevel::Info,
                          .event = order_book::LogEvent::ReplayFinished,
                          .sequence = book_registry.get_seq(),
                          .line_number = line_number});

    std::cout << std::fixed << std::setprecision(2) << "events=" << event_count << '\n'
              << "elapsed_seconds=" << elapsed << '\n'
              << "ns_per_event=" << ns_per_event << '\n'
              << "million_events_per_second=" << million_events_per_second << '\n'
              << "instruments=" << book_registry.instrument_count() << '\n'
              << "remaining_orders=" << book_registry.order_count() << '\n'
              << "bid_levels=" << book_registry.bid_level_count() << '\n'
              << "ask_levels=" << book_registry.ask_level_count() << '\n';

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: replay_order_book <events.csv> [log_file]\n";
        return 1;
    }

    std::unique_ptr<order_book::Logger> logger;
    int status = 1;
    try {
        logger = std::make_unique<order_book::Logger>(argc == 3 ? argv[2] : "order_book.log");
        status = replay(argv[1], *logger);
    } catch (const std::exception& error) {
        std::cerr << "Replay failed: " << error.what() << '\n';
    }
    if (logger) {
        logger->close();
        std::cout << "log_dropped=" << logger->dropped_count() << '\n'
                  << "log_format_failures=" << logger->format_failed_count() << '\n'
                  << "log_write_failed=" << logger->has_file_write_error() << '\n';
        if (logger->has_file_write_error() || logger->format_failed_count() != 0U) status = 1;
    }
    return status;
}
