#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "order_book/core/types.hpp"

namespace order_book::feed {
enum class EventType { Metadata, Directory, Add, Reduce, Execute, Delete, Replace,
                       ExecuteRemaining, Trade, Clear };
// Prices are unsigned integer units of 1/10,000 currency units. Timestamps are
// exchange-local nanoseconds since midnight, NOT latency or UTC timestamps.
struct Event {
    EventType type{EventType::Metadata};
    std::uint64_t sequence{}, timestamp_ns{};
    std::uint16_t channel{};
    std::string symbol;
    OrderId order_id{}, new_order_id{};
    Side side{Side::Bid};
    std::uint64_t price{}, quantity{}, remaining{}, match_id{};
    bool maintain_priority{};
};
class Error : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};
// Exactly one decoder per source/session. Errors poison it; recreate to restart.
// Input excludes the archive's two-byte big-endian length prefix. ITCH payloads
// have no transport sequence; sequence below is the archive record ordinal.
class Itch50Decoder final {
   public:
    [[nodiscard]] Event decode(std::span<const std::uint8_t> message);
    [[nodiscard]] bool complete() const noexcept { return complete_ && !failed_; }
   private:
    std::uint64_t record_{};
    bool failed_{};
    bool complete_{};
    std::unordered_map<std::uint16_t, std::string> symbols_;
};
// Input is a complete Sequenced Unit Header + messages (not Ethernet/IP/PCAP).
// Supports U.S. EQUITIES PITCH 2.x; other PITCH families fail explicitly.
class PitchDecoder final {
   public:
    [[nodiscard]] std::vector<Event> decode(std::span<const std::uint8_t> packet);
    [[nodiscard]] bool complete() const noexcept;
   private:
    struct Unit { std::uint64_t next{1}, seconds{}; bool timed{}, ended{}; };
    std::array<Unit, 256> units_{};
    bool failed_{};
};
}  // namespace order_book::feed
