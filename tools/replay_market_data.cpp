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

using namespace order_book;
using namespace order_book::feed;
namespace {
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
                     "[--output summary.json] [--snapshot book.json]\n";
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
        args.finish();
        if (input.empty()) throw Error("--input is required");
        const auto same_path = [](const std::string& a, const std::string& b) {
            return !a.empty() && !b.empty() &&
                   std::filesystem::weakly_canonical(a) == std::filesystem::weakly_canonical(b);
        };
        if (same_path(input, output) || same_path(input, snapshot_path) || same_path(output, snapshot_path))
            throw Error("Input, summary and snapshot paths must be distinct");
        Input stream(input, format == "itch50" ? Format::Itch50 : Format::Pitch);
        Itch50Decoder itch;
        PitchDecoder pitch;
        Books books(symbol, static_cast<std::size_t>(budget));
        std::vector<std::uint8_t> record;
        std::uint64_t messages{}, trade_notices{};
        const auto start = std::chrono::steady_clock::now();
        while (limit == 0 || records < limit) {
            if (!stream.next(record)) break;
            ++records;
            const auto consume = [&](const Event& event) {
                ++messages;
                if (event.type == EventType::Trade) ++trade_notices;
                books.consume(venue, event);
            };
            if (format == "itch50") consume(itch.decode(record));
            else for (const auto& event : pitch.decode(record)) consume(event);
        }
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
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
        summary << "]}";
        snapshot << "]}";
        write(output, summary.str());
        write(snapshot_path, snapshot.str());
        std::cout << summary.str() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "Market-data replay after record " << records << ": " << error.what() << '\n';
        return 1;
    }
}
