#pragma once
#include <array>
#include <optional>

#include "order_book/exchange/messages.hpp"

namespace order_book::exchange::wire {
using RequestFrame = std::array<std::uint8_t, 64>;
using ReportFrame = std::array<std::uint8_t, 96>;
using MarketFrame = std::array<std::uint8_t, 80>;
[[nodiscard]] RequestFrame encode(const Request& request);
[[nodiscard]] ReportFrame encode(const Report& report);
[[nodiscard]] MarketFrame encode(const MarketUpdate& update);
[[nodiscard]] std::optional<Request> decode(const RequestFrame& frame) noexcept;
[[nodiscard]] std::optional<Report> decode(const ReportFrame& frame) noexcept;
[[nodiscard]] std::optional<MarketUpdate> decode(const MarketFrame& frame) noexcept;
}  // namespace order_book::exchange::wire
