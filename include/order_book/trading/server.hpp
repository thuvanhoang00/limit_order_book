#pragma once
#include <csignal>
#include <string>

#include "order_book/trading/engine.hpp"

namespace order_book::trading {
struct ServerConfig {
    std::uint16_t tcp_port{9100};
    std::uint16_t multicast_port{9101};
    std::string group{"239.255.0.2"};
    std::string interface { "127.0.0.1" };
    std::string journal{"exchange.journal"};  // "-" disables persistence.
    std::string log{"exchange.log"};
    std::string metrics;
    Instruments instruments{default_instruments()};
};
// Blocks until SIGINT/SIGTERM (or a fatal worker error), then drains accepted work.
void run_exchange(const ServerConfig& config, const volatile std::sig_atomic_t& stop);
}  // namespace order_book::trading
