#include <chrono>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

#include "order_book/feed/books.hpp"
#include "order_book/feed/input.hpp"
#include "trading_args.hpp"
#include "latency_histogram.hpp"
#include "feed_outliers.hpp"

using namespace order_book;
using namespace order_book::feed;
namespace {
using Clock = std::chrono::steady_clock;
using order_book::measurement::LatencyHistogram;
struct Latencies {
    LatencyHistogram read_decode_apply, decode_apply, selected_update_decode_apply, clock_pair_overhead;
    static std::uint64_t ns(Clock::duration duration) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
    }
    Latencies() {
        for (unsigned i = 0; i != 10000; ++i) {
            const auto start = Clock::now();
            const auto end = Clock::now();
            clock_pair_overhead.observe(ns(end - start));
        }
    }
    std::string summary() const {
        return "{\"unit\":\"input_record\",\"method\":\"exact_nanosecond_frequencies_nearest_rank\","
               "\"read_decode_apply\":" + read_decode_apply.summary() +
               ",\"decode_apply\":" + decode_apply.summary() +
               ",\"selected_update_decode_apply\":" + selected_update_decode_apply.summary() +
               ",\"clock_pair_overhead\":" + clock_pair_overhead.summary() + "}";
    }
    std::string frequencies() const {
        std::ostringstream out;
        out << "{\"read_decode_apply\":"; read_decode_apply.frequencies(out);
        out << ",\"decode_apply\":"; decode_apply.frequencies(out);
        out << ",\"selected_update_decode_apply\":"; selected_update_decode_apply.frequencies(out);
        out << ",\"clock_pair_overhead\":"; clock_pair_overhead.frequencies(out);
        out << '}';
        return out.str();
    }
};
std::string quote(const std::string& text) {
    std::ostringstream out;
    out << std::quoted(text);
    return out.str();
}
std::string optional_price(std::optional<Price> price) {
    return price ? std::to_string(*price) : "null";
}
void write(const std::string& path, const std::string& data) {
    if (path.empty()) return;
    std::ofstream file(path);
    file << data << '\n';
    if (!file) throw Error("Cannot write replay output: " + path);
}
}  // namespace
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "replay_market_data --input file[.gz] [--format itch50|pitch] "
                     "[--venue nasdaq|cboe-bzx|cboe-byx|cboe-edga|cboe-edgx] "
                     "[--symbol AAPL] [--pool-bytes 1048576] [--max-records 0] "
                     "[--output summary.json] [--snapshot book.json] [--latency 0|1] "
                     "[--histogram frequencies.json] [--outlier-trace trace.json] "
                     "[--outlier-threshold-ns 100000] [--outlier-capacity 256]\n";
        return 0;
    }
    std::uint64_t records{};
    try {
        TradingArgs args(argc, argv);
        const auto input = args.text("--input", "");
        const auto format = args.text("--format", "itch50");
        if (format != "itch50" && format != "pitch") throw Error("Unknown feed format: " + format);
        const auto venue = args.text("--venue", format == "itch50" ? "nasdaq" : "cboe-bzx");
        if ((format == "itch50" && venue != "nasdaq") ||
            (format == "pitch" && venue != "cboe-bzx" && venue != "cboe-byx" &&
             venue != "cboe-edga" && venue != "cboe-edgx"))
            throw Error("Unsupported venue/format combination; HOSE/HNX need their own feed specs");
        const auto symbol = args.text("--symbol", "");
        for (char value : symbol) {
            const auto c = static_cast<unsigned char>(value);
            if (c < 33 || c > 126) throw Error("Invalid symbol filter");
        }
        const auto budget = args.number("--pool-bytes", 1024 * 1024, 1,
                                        std::numeric_limits<std::size_t>::max());
        const auto limit = args.number("--max-records", 0, 0,
                                       std::numeric_limits<std::uint64_t>::max());
        const auto output = args.text("--output", "");
        const auto snapshot_path = args.text("--snapshot", "");
        const bool measure_latency = args.number("--latency", 0, 0, 1) != 0;
        const auto histogram_path = args.text("--histogram", "");
        const auto outlier_path = args.text("--outlier-trace", "");
        const auto outlier_threshold = args.number("--outlier-threshold-ns", 100000, 1,
                                                   std::numeric_limits<std::uint64_t>::max());
        const auto outlier_capacity = args.number("--outlier-capacity", 256, 1, 100000);
        args.finish();
        if (input.empty()) throw Error("--input is required");
        if (!histogram_path.empty() && !measure_latency)
            throw Error("--histogram requires --latency 1");
        if (!outlier_path.empty() && (format != "itch50" || symbol.empty() || measure_latency))
            throw Error("--outlier-trace requires ITCH, one --symbol and --latency 0 (separate diagnostic run)");
        const auto same_path = [](const std::string& a, const std::string& b) {
            return !a.empty() && !b.empty() &&
                   std::filesystem::weakly_canonical(a) == std::filesystem::weakly_canonical(b);
        };
        const std::string paths[]{input, output, snapshot_path, histogram_path, outlier_path};
        for (unsigned i = 0; i != 5; ++i)
            for (unsigned j = i + 1; j != 5; ++j)
                if (same_path(paths[i], paths[j])) throw Error("Input and output paths must be distinct");
        Input stream(input, format == "itch50" ? Format::Itch50 : Format::Pitch);
        Itch50Decoder itch;
        PitchDecoder pitch;
        Books books(symbol, static_cast<std::size_t>(budget));
        std::vector<std::uint8_t> record;
        std::uint64_t messages{}, trade_notices{};
        auto latencies = measure_latency ? std::make_unique<Latencies>() : nullptr;
        auto outliers = outlier_path.empty() ? nullptr : std::make_unique<measurement::FeedOutliers>(
            symbol, outlier_threshold, static_cast<std::size_t>(outlier_capacity));
        const auto start = Clock::now();
        while (limit == 0 || records < limit) {
            const auto read_start = latencies ? Clock::now() : Clock::time_point{};
            if (!stream.next(record)) break;
            const auto decode_start = latencies ? Clock::now() : Clock::time_point{};
            const auto updates_before = books.updates();
            ++records;
            const auto consume = [&](const Event& event) {
                ++messages;
                if (event.type == EventType::Trade) ++trade_notices;
                books.consume(venue, event);
            };
            if (format == "itch50") {
                std::optional<measurement::FeedOutliers::Start> profile;
                if (outliers && outliers->selected(record)) profile = measurement::FeedOutliers::start();
                const auto event = itch.decode(record);
                consume(event);
                if (profile) {
                    const auto end = Clock::now();
                    outliers->finish(*profile, end, records, static_cast<char>(record[0]), event);
                }
                if (outliers) outliers->mapping(record, event);
            }
            else for (const auto& event : pitch.decode(record)) consume(event);
            if (latencies) {
                const auto end = Clock::now();
                const auto decode_ns = Latencies::ns(end - decode_start);
                latencies->read_decode_apply.observe(Latencies::ns(end - read_start));
                latencies->decode_apply.observe(decode_ns);
                if (books.updates() != updates_before)
                    latencies->selected_update_decode_apply.observe(decode_ns);
            }
        }
        const auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        const bool limited = limit != 0 && records == limit;
        const bool ended = format == "itch50" ? itch.complete() : pitch.complete();
        if (!limited && !ended) throw Error("Input ended before exchange end-of-session marker(s)");
        std::string reason;
        if (!books.validate_invariants(&reason)) throw Error("Book invariant: " + reason);
        if (!symbol.empty() && books.updates() == 0) throw Error("No orders found for requested symbol");
        std::ostringstream summary, snapshot;
        summary << std::setprecision(12)
                << "{\"format\":" << quote(format) << ",\"venue\":" << quote(venue)
                << ",\"symbol_filter\":" << quote(symbol) << ",\"records\":" << records
                << ",\"messages\":" << messages << ",\"book_updates\":" << books.updates()
                << ",\"trade_notices\":" << trade_notices << ",\"elapsed_seconds\":" << elapsed
                << ",\"messages_per_second\":" << (elapsed > 0 ? static_cast<double>(messages) / elapsed : 0)
                << ",\"limited\":" << (limited ? "true" : "false")
                << ",\"complete_input\":" << (!limited ? "true" : "false")
                << ",\"session_ended\":" << (ended ? "true" : "false")
                << ",\"price_scale\":10000,\"invariants_valid\":true,\"books\":[";
        snapshot << "{\"price_scale\":10000,\"books\":[";
        bool first = true;
        for (const auto& [key, book] : books.books()) {
            if (!first) { summary << ','; snapshot << ','; }
            first = false;
            const auto identity = "\"venue\":" + quote(key.venue) + ",\"channel\":" +
                                  std::to_string(key.channel) + ",\"symbol\":" + quote(key.symbol);
            summary << '{' << identity << ",\"orders\":" << book.order_count()
                    << ",\"best_bid\":" << optional_price(book.best_bid())
                    << ",\"best_ask\":" << optional_price(book.best_ask()) << '}';
            if (snapshot_path.empty()) continue;
            snapshot << '{' << identity << ",\"levels\":[";
            bool first_level = true;
            for (const auto& level : book.snapshot()) {
                if (!first_level) snapshot << ',';
                first_level = false;
                snapshot << "{\"side\":" << quote(std::string(to_string(level.side)))
                         << ",\"price\":" << level.price << ",\"quantity\":" << level.quantity
                         << ",\"orders\":[";
                bool first_order = true;
                for (const auto& order : level.orders) {
                    if (!first_order) snapshot << ',';
                    first_order = false;
                    snapshot << "{\"id\":" << order.order_id << ",\"quantity\":" << order.quantity << '}';
                }
                snapshot << "]}";
            }
            snapshot << "]}";
        }
        summary << ']';
        if (latencies) summary << ",\"latency\":" << latencies->summary();
        summary << ",\"instrumentation\":" << quote(outliers ? "outlier_trace" : latencies ? "latency" : "off");
        summary << '}';
        snapshot << "]}";
        write(output, summary.str());
        write(snapshot_path, snapshot.str());
        if (latencies) write(histogram_path, latencies->frequencies());
        if (outliers) write(outlier_path, outliers->json());
        std::cout << summary.str() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Market-data replay after record " << records << ": " << error.what() << '\n';
        return 1;
    }
}
