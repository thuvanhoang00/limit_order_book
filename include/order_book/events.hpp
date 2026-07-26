#pragma once

#include <variant>

#include "order_book/types.hpp"

namespace order_book {

struct AddOrder {
    Sequence sequence{};
    OrderId order_id{};
    Side side{Side::Bid};
    Price price{};
    Quantity quantity{};

    friend constexpr bool operator==(const AddOrder&, const AddOrder&) = default;
};

struct CancelOrder {
    Sequence sequence{};
    OrderId order_id{};

    friend constexpr bool operator==(const CancelOrder&, const CancelOrder&) = default;
};

struct ExecuteOrder {
    Sequence sequence{};
    OrderId order_id{};
    Quantity quantity{};

    friend constexpr bool operator==(const ExecuteOrder&, const ExecuteOrder&) = default;
};

using MarketEvent = std::variant<AddOrder, CancelOrder, ExecuteOrder>;

}  // namespace order_book
