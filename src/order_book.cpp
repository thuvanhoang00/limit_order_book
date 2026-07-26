#include "order_book/order_book.hpp"

#include <type_traits>
#include <variant>

namespace order_book {

ApplyResult OrderBook::apply(const MarketEvent& event) noexcept {
    return std::visit(
        [this](const auto& concrete_event) noexcept {
            return apply(concrete_event);
        },
        event);
}

ApplyResult OrderBook::apply(const AddOrder& /*event*/) noexcept {
    // TODO(core): validate the event, insert the order, update best bid/ask.
    return ApplyResult::NotImplemented;
}

ApplyResult OrderBook::apply(const CancelOrder& /*event*/) noexcept {
    // TODO(core): locate the order, erase it, and remove empty price levels.
    return ApplyResult::NotImplemented;
}

ApplyResult OrderBook::apply(const ExecuteOrder& /*event*/) noexcept {
    // TODO(core): decrement remaining quantity and erase fully executed orders.
    return ApplyResult::NotImplemented;
}

std::optional<Price> OrderBook::best_bid() const noexcept {
    return std::nullopt;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    return std::nullopt;
}

std::optional<Quantity> OrderBook::remaining_quantity(const OrderId /*order_id*/) const noexcept {
    return std::nullopt;
}

std::size_t OrderBook::order_count() const noexcept {
    return 0U;
}

std::size_t OrderBook::bid_level_count() const noexcept {
    return 0U;
}

std::size_t OrderBook::ask_level_count() const noexcept {
    return 0U;
}

void OrderBook::reset() noexcept {
    // TODO(core): clear all storage while preserving reusable capacity where possible.
}

bool OrderBook::validate_invariants(std::string* reason) const {
    if (reason != nullptr) {
        reason->clear();
    }

    // TODO(core): validate lookup consistency, aggregate quantities, FIFO links,
    // empty-level removal, and best bid/ask correctness.
    return true;
}

}  // namespace order_book
