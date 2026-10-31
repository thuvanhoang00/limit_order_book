#include <csignal>
#include <exception>
#include <iostream>
#include <string_view>

#include "order_book/trading/server.hpp"
#include "trading_args.hpp"
namespace {
volatile std::sig_atomic_t stop_requested{};
void stop(int) { stop_requested = 1; }
}  // namespace
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "exchange_server [--tcp-port 9100] [--md-port 9101] [--group 239.255.0.2] "
                     "[--interface 127.0.0.1] [--rules configs/instruments.csv] [--journal "
                     "exchange.journal|-] "
                     "[--log exchange.log] [--metrics metrics.json]\n";
        return 0;
    }
    try {
        TradingArgs args(argc, argv);
        order_book::trading::ServerConfig c;
        c.tcp_port = static_cast<std::uint16_t>(args.number("--tcp-port", 9100, 0, 65535));
        c.multicast_port = static_cast<std::uint16_t>(args.number("--md-port", 9101, 1, 65535));
        c.group = args.text("--group", c.group);
        c.interface = args.text("--interface", c.interface);
        c.journal = args.text("--journal", c.journal);
        c.log = args.text("--log", c.log);
        c.metrics = args.text("--metrics", "");
        const auto rules = args.text("--rules", "");
        if (!rules.empty()) c.instruments = order_book::trading::load_instruments(rules);
        args.finish();
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        order_book::trading::run_exchange(c, stop_requested);
    } catch (const std::exception& e) {
        std::cerr << "Exchange: " << e.what() << '\n';
        return 1;
    }
}
