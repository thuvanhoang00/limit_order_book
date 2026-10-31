#include <csignal>
#include <iostream>
#include <thread>

#include "order_book/exchange/runtime.hpp"
#include "trading_cli.hpp"

namespace {
volatile std::sig_atomic_t stopped = 0;
void signal_handler(int) { stopped = 1; }
}  // namespace
int main(int argc, char** argv) {
    try {
        trading_cli::Args args(argc, argv);
        if (args.help) {
            std::cout << "exchange_server [--tcp-port 9000] [--group 239.255.42.1] [--interface "
                         "127.0.0.1]\n"
                         "  [--incremental-port 9001] [--snapshot-port 9002] [--snapshot-ms 1000]\n"
                         "  [--instruments 1,2] [--tick 1] [--lot 1] [--min-price 1] [--max-price "
                         "4294967295]\n"
                         "  [--max-quantity 4294967295] [--max-notional 18446744073709551615]\n"
                         "  [--io-timeout-ms 3000] [--run-ms 0] [--address 127.0.0.1] "
                         "[--log-prefix PATH]\n";
            return 0;
        }
        order_book::exchange::ExchangeConfig config;
        config.address = args.take("--address", config.address);
        config.tcp_port = args.take_number<std::uint16_t>("--tcp-port", config.tcp_port);
        config.group = args.take("--group", config.group);
        config.interface_ipv4 = args.take("--interface", config.interface_ipv4);
        config.log_prefix = args.take("--log-prefix", "");
        config.incremental_port =
            args.take_number<std::uint16_t>("--incremental-port", config.incremental_port);
        config.snapshot_port =
            args.take_number<std::uint16_t>("--snapshot-port", config.snapshot_port);
        config.snapshot_interval =
            std::chrono::milliseconds(args.take_number<std::uint32_t>("--snapshot-ms", 1000));
        config.io_timeout =
            std::chrono::milliseconds(args.take_number<std::uint32_t>("--io-timeout-ms", 3000));
        order_book::exchange::InstrumentRules rules;
        rules.tick_size = args.take_number<order_book::Price>("--tick", rules.tick_size);
        rules.lot_size = args.take_number<order_book::Quantity>("--lot", rules.lot_size);
        rules.min_price = args.take_number<order_book::Price>("--min-price", rules.min_price);
        rules.max_price = args.take_number<order_book::Price>("--max-price", rules.max_price);
        rules.max_quantity =
            args.take_number<order_book::Quantity>("--max-quantity", rules.max_quantity);
        rules.max_notional = args.take_number<std::uint64_t>("--max-notional", rules.max_notional);
        config.instruments.clear();
        for (const auto id : args.instruments())
            if (!config.instruments.emplace(id, rules).second)
                throw std::invalid_argument("Duplicate instrument");
        const auto run_ms = args.take_number<std::uint64_t>("--run-ms", 0);
        args.finish();
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        order_book::exchange::ExchangeServer server(std::move(config));
        server.start();
        std::cerr << "Ready tcp_port=" << server.tcp_port() << " session=" << server.session()
                  << std::endl;
        const auto start = std::chrono::steady_clock::now();
        while (!stopped && !server.failed()) {
            if (run_ms &&
                std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(run_ms))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        server.stop();
        const auto stats = server.stats();
        std::cout << "requests=" << stats.requests << " reports=" << stats.reports
                  << " updates=" << stats.updates << " snapshots=" << stats.snapshots
                  << " disconnected=" << stats.disconnected << " unroutable=" << stats.unroutable
                  << " log_dropped=" << stats.log_dropped << '\n';
        if (server.failed()) {
            std::cerr << server.error() << '\n';
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
