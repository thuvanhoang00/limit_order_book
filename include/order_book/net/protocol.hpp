#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "order_book/core/events.hpp"

namespace order_book::net {
using EventFrame = std::array<std::uint8_t, 40>;
using AckFrame = std::array<std::uint8_t, 16>;

// Explicit big-endian encoding, independent of struct padding and host ABI.
[[nodiscard]] EventFrame encode_event(const MarketEvent& event);
[[nodiscard]] std::optional<MarketEvent> decode_event(const EventFrame& frame) noexcept;
// ACK means decoded/received only, not applied, persisted or executed.
[[nodiscard]] AckFrame encode_received_ack(Sequence sequence) noexcept;
[[nodiscard]] std::optional<Sequence> decode_received_ack(const AckFrame& frame) noexcept;

struct ApplyAck {
    Sequence sequence{};
    ApplyResult result{};
    friend constexpr bool operator==(const ApplyAck&, const ApplyAck&) = default;
};
// Distinct message kind: result of applying an event to the in-memory book.
[[nodiscard]] AckFrame encode_apply_ack(ApplyAck ack);
[[nodiscard]] std::optional<ApplyAck> decode_apply_ack(const AckFrame& frame) noexcept;
}  // namespace order_book::net
