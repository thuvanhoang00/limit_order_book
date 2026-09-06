#pragma once
#include <cstdint>
#include <string_view>

namespace order_book {

using Sequence = std::uint64_t;
using OrderId = std::uint64_t;
using InstrumentId = std::uint64_t;
using Price = std::uint32_t;
using Quantity = std::uint32_t;

enum class Side : std::uint8_t {
    Bid,
    Ask,
};

[[nodiscard]] constexpr std::string_view to_string(const Side side) noexcept {
    return side == Side::Bid ? "bid" : "ask";
}

enum class ApplyResult : std::uint8_t {
    Ok,
    DuplicateOrder,
    UnknownOrder,
    InvalidPrice,
    InvalidQuantity,
    UnknownInstrument,
    QuantityExceedsRemaining,
    ParseError,
    NotImplemented,
};

[[nodiscard]] constexpr std::string_view to_string(const ApplyResult result) noexcept {
    switch (result) {
        case ApplyResult::Ok:
            return "ok";
        case ApplyResult::DuplicateOrder:
            return "duplicate_order";
        case ApplyResult::UnknownOrder:
            return "unknown_order";
        case ApplyResult::InvalidPrice:
            return "invalid_price";
        case ApplyResult::InvalidQuantity:
            return "invalid_quantity";
        case ApplyResult::QuantityExceedsRemaining:
            return "quantity_exceeds_remaining";
        case ApplyResult::ParseError:
            return "parse_error";
        case ApplyResult::NotImplemented:
            return "not_implemented";
        case ApplyResult::UnknownInstrument:
            return "unknown_instrument";
    }

    return "unknown";
}

}  // namespace order_book
