#include <cmath>
#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "order_book/trading/runtime.hpp"
#include "trading_cli.hpp"

namespace {
volatile std::sig_atomic_t stopped = 0;
void signal_handler(int) { stopped = 1; }
std::vector<order_book::exchange::Request> read_script(const std::string& path) {
    using namespace order_book;
    using namespace order_book::exchange;
    if (path.empty()) return {};
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open order script: " + path);
    std::vector<Request> result;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        line = line.substr(0, line.find('#'));
        std::istringstream in(line);
        std::vector<std::string> columns;
        for (std::string word; in >> word;) columns.push_back(word);
        if (columns.empty()) continue;
        try {
            if (columns.size() < 4)
                throw std::invalid_argument(
                    "Expected TYPE INSTRUMENT ORDER SIDE [PRICE QTY TIF FLAGS]");
            Request r;
            if (columns[0] == "NEW")
                r.type = RequestType::New;
            else if (columns[0] == "CANCEL")
                r.type = RequestType::Cancel;
            else if (columns[0] == "REPLACE")
                r.type = RequestType::Replace;
            else
                throw std::invalid_argument("Expected NEW, CANCEL or REPLACE");
            r.instrument_id = trading_cli::Args::number<InstrumentId>(columns[1]);
            r.order_id = trading_cli::Args::number<OrderId>(columns[2]);
            if (columns[3] != "BID" && columns[3] != "ASK")
                throw std::invalid_argument("Expected BID or ASK");
            r.side = columns[3] == "BID" ? Side::Bid : Side::Ask;
            if (r.type == RequestType::Cancel) {
                if (columns.size() != 4) throw std::invalid_argument("CANCEL takes four fields");
            } else {
                if (columns.size() < 7) throw std::invalid_argument("Expected PRICE QTY TIF");
                r.market = columns[4] == "MKT";
                if (!r.market) r.price = trading_cli::Args::number<Price>(columns[4]);
                r.quantity = trading_cli::Args::number<Quantity>(columns[5]);
                if (columns[6] == "GTC")
                    r.tif = TimeInForce::Gtc;
                else if (columns[6] == "IOC")
                    r.tif = TimeInForce::Ioc;
                else if (columns[6] == "FOK")
                    r.tif = TimeInForce::Fok;
                else
                    throw std::invalid_argument("Expected GTC, IOC or FOK");
                for (std::size_t i = 7; i < columns.size(); ++i) {
                    if (columns[i] == "POST_ONLY")
                        r.post_only = true;
                    else if (columns[i] == "STP")
                        r.prevent_self_trade = true;
                    else
                        throw std::invalid_argument("Unknown order flag");
                }
            }
            result.push_back(r);
        } catch (const std::exception& error) {
            throw std::runtime_error("Order script line " + std::to_string(line_number) + ": " +
                                     error.what());
        }
    }
    if (!file.eof()) throw std::runtime_error("Failed reading order script");
    return result;
}
}  // namespace
int main(int argc, char** argv) {
    using namespace order_book;
    using namespace order_book::trading;
    try {
        trading_cli::Args args(argc, argv);
        if (args.help) {
            std::cout
                << "trading_client --client-id N [--strategy script|maker|taker|random] [--orders "
                   "FILE]\n"
                   "  [--tcp-port 9000] [--group 239.255.42.1] [--interface 127.0.0.1]\n"
                   "  [--incremental-port 9001] [--snapshot-port 9002] [--address 127.0.0.1]\n"
                   "  [--instruments 1,2] [--clip 10] [--tick 1] [--edge 1] [--trade-ratio 0.5]\n"
                   "  [--max-order 1000] [--max-position 10000] [--max-loss 1000000]\n"
                   "  [--max-notional 18446744073709551615] [--seed 42] [--run-ms 10000]\n"
                   "  [--interval-ms 50] [--io-timeout-ms 3000] [--feed-timeout-ms 1000] [--log "
                   "FILE]\n";
            return 0;
        }
        TradingConfig config;
        config.client_id = args.take_number<ClientId>("--client-id", 1);
        config.address = args.take("--address", config.address);
        config.tcp_port = args.take_number<std::uint16_t>("--tcp-port", config.tcp_port);
        config.group = args.take("--group", config.group);
        config.interface_ipv4 = args.take("--interface", config.interface_ipv4);
        config.log_path = args.take("--log", "");
        config.incremental_port =
            args.take_number<std::uint16_t>("--incremental-port", config.incremental_port);
        config.snapshot_port =
            args.take_number<std::uint16_t>("--snapshot-port", config.snapshot_port);
        config.io_timeout =
            std::chrono::milliseconds(args.take_number<std::uint32_t>("--io-timeout-ms", 3000));
        config.feed_timeout =
            std::chrono::milliseconds(args.take_number<std::uint32_t>("--feed-timeout-ms", 1000));
        config.interval =
            std::chrono::milliseconds(args.take_number<std::uint32_t>("--interval-ms", 50));
        config.seed = args.take_number<std::uint64_t>("--seed", 42);
        config.strategy.clip = args.take_number<Quantity>("--clip", 10);
        config.strategy.tick_size = args.take_number<Price>("--tick", 1);
        config.strategy.edge = args.take_number<Price>("--edge", 1);
        const auto ratio = args.take("--trade-ratio", "0.5");
        std::size_t parsed = 0;
        config.strategy.trade_ratio = std::stold(ratio, &parsed);
        if (parsed != ratio.size() || !std::isfinite(config.strategy.trade_ratio) ||
            config.strategy.trade_ratio < 0)
            throw std::invalid_argument("Invalid trade ratio");
        const auto strategy = args.take("--strategy", "script");
        if (strategy == "script")
            config.strategy.algorithm = Algorithm::Script;
        else if (strategy == "maker")
            config.strategy.algorithm = Algorithm::MarketMaker;
        else if (strategy == "taker")
            config.strategy.algorithm = Algorithm::LiquidityTaker;
        else if (strategy == "random")
            config.strategy.algorithm = Algorithm::Random;
        else
            throw std::invalid_argument("Unknown strategy");
        config.script = read_script(args.take("--orders", ""));
        if (!config.script.empty() && config.strategy.algorithm != Algorithm::Script)
            throw std::invalid_argument("--orders requires --strategy script");
        RiskLimits limit;
        limit.max_order_quantity =
            args.take_number<Quantity>("--max-order", limit.max_order_quantity);
        limit.max_position = args.take_number<std::int64_t>("--max-position", limit.max_position);
        limit.max_loss = args.take_number<std::uint64_t>("--max-loss", 1000000);
        limit.max_notional = args.take_number<std::uint64_t>("--max-notional", limit.max_notional);
        config.limits.clear();
        for (const auto id : args.instruments())
            if (!config.limits.emplace(id, limit).second)
                throw std::invalid_argument("Duplicate instrument");
        const auto run_ms = args.take_number<std::uint64_t>("--run-ms", 10000);
        args.finish();
        if (!config.strategy.clip || !config.strategy.tick_size || !config.strategy.edge)
            throw std::invalid_argument("Clip, tick and edge must be positive");
        config.on_report = [](const Report& r) {
            std::cout << "report=" << to_string(r.type) << " status=" << to_string(r.status)
                      << " sequence=" << r.sequence << " request=" << r.request_sequence
                      << " instrument=" << r.instrument_id << " order=" << r.order_id
                      << " market_order=" << r.market_order_id << " trade=" << r.trade_id
                      << " price=" << r.price << " quantity=" << r.quantity
                      << " leaves=" << r.leaves << std::endl;
        };
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        TradingClient client(std::move(config));
        client.start();
        std::cerr << "Trading client ready\n";
        const auto start = std::chrono::steady_clock::now();
        while (!stopped && !client.failed()) {
            if (run_ms &&
                std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(run_ms))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        client.stop();
        const auto state = client.summary();
        std::cout << "summary synchronized=" << state.synchronized << " sequence=" << state.sequence
                  << " session=" << state.session << " fills=" << state.fills
                  << " submitted=" << state.submitted
                  << " local_rejections=" << state.local_rejections
                  << " recoveries=" << state.recoveries << " active_orders=" << state.active_orders
                  << " log_dropped=" << state.log_dropped << '\n';
        for (const auto& [id, position] : state.positions)
            std::cout << "position instrument=" << id << " quantity=" << position.quantity
                      << " volume=" << position.volume << " realized=" << position.realized
                      << " unrealized=" << position.unrealized << " pnl=" << position.pnl() << '\n';
        if (client.failed()) {
            std::cerr << client.error() << '\n';
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
