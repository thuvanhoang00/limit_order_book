#pragma once

#include <variant>

#include "order_book/types.hpp"

namespace order_book {

struct AddOrder {
    OrderId order_id{};
    Side side{Side::Bid};
    Price price{};
    Quantity quantity{};

    friend constexpr bool operator==(const AddOrder&, const AddOrder&) = default;
};

struct CancelOrder {
    OrderId order_id{};

    friend constexpr bool operator==(const CancelOrder&, const CancelOrder&) = default;
};

struct ExecuteOrder {
    OrderId order_id{};
    Quantity quantity{};

    friend constexpr bool operator==(const ExecuteOrder&, const ExecuteOrder&) = default;
};

using EventPayload = std::variant<AddOrder, CancelOrder, ExecuteOrder>;

struct MarketEvent {
    Sequence sequence{};
    InstrumentId instrument_id{};
    EventPayload payload;

    friend constexpr bool operator==(const MarketEvent&, const MarketEvent&) = default;
};

}  // namespace order_book
