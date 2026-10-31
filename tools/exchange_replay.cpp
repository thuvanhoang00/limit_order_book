#include <exception>
#include <iostream>

#include "order_book/trading/engine.hpp"
#include "order_book/trading/journal.hpp"
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "exchange_replay <journal> [instrument_rules.csv]\n";
        return 1;
    }
    try {
        using namespace order_book::trading;
        auto rules = argc == 3 ? load_instruments(argv[2]) : default_instruments();
        Journal journal(argv[1], config_fingerprint(rules));
        MatchingEngine engine(rules);
        for (const auto& command : journal.recovered()) (void)engine.process(command);
        std::string reason;
        if (!engine.validate_invariants(&reason)) throw std::runtime_error(reason);
        const auto& stats = engine.stats();
        std::cout << "{\"commands\":" << stats.commands << ",\"trades\":" << stats.trades
                  << ",\"volume\":" << stats.volume << ",\"sequence\":" << engine.sequence()
                  << ",\"open_orders\":" << engine.snapshot().size() - 2 << "}\n";
    } catch (const std::exception& e) {
        std::cerr << "Replay: " << e.what() << '\n';
        return 1;
    }
}
