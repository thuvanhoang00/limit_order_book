#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "order_book/exchange/matching_engine.hpp"

namespace order_book::exchange {
struct ExchangeConfig {
    std::string address{"127.0.0.1"};
    std::uint16_t tcp_port{9000};
    std::string group{"239.255.42.1"};
    std::string interface_ipv4{"127.0.0.1"};
    std::string log_prefix;  // Optional separate matcher/publisher SPSC logs.
    std::uint16_t incremental_port{9001};
    std::uint16_t snapshot_port{9002};
    std::chrono::milliseconds snapshot_interval{1000};
    std::chrono::milliseconds io_timeout{3000};
    std::map<InstrumentId, InstrumentRules> instruments{{1, {}}};
};
struct ExchangeStats {
    std::uint64_t requests{}, reports{}, updates{}, snapshots{}, disconnected{}, unroutable{},
        log_dropped{};
};
class ExchangeServer final {
   public:
    explicit ExchangeServer(ExchangeConfig config);
    ~ExchangeServer();
    ExchangeServer(const ExchangeServer&) = delete;
    ExchangeServer& operator=(const ExchangeServer&) = delete;
    void start();
    void stop() noexcept;  // Stop ingress, drain matching/output queues, then join.
    [[nodiscard]] std::uint16_t tcp_port() const noexcept;
    [[nodiscard]] std::uint64_t session() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] ExchangeStats stats() const noexcept;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace order_book::exchange
