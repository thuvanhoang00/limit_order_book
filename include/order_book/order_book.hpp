#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "order_book/events.hpp"
#include "order_book/types.hpp"

namespace order_book {

class OrderBook final {
public:
    OrderBook() = default;

    [[nodiscard]] ApplyResult apply(const MarketEvent& event) noexcept;
    [[nodiscard]] ApplyResult apply(const AddOrder& event) noexcept;
    [[nodiscard]] ApplyResult apply(const CancelOrder& event) noexcept;
    [[nodiscard]] ApplyResult apply(const ExecuteOrder& event) noexcept;

    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;
    [[nodiscard]] std::optional<Quantity> remaining_quantity(OrderId order_id) const noexcept;

    [[nodiscard]] std::size_t order_count() const noexcept;
    [[nodiscard]] std::size_t bid_level_count() const noexcept;
    [[nodiscard]] std::size_t ask_level_count() const noexcept;

    void reset() noexcept;

    // This function is intentionally allowed to be slower than the hot path.
    // Use it in tests and debug builds to validate internal consistency.
    [[nodiscard]] bool validate_invariants(std::string* reason = nullptr) const;

private:
    // TODO(core): choose and implement the storage layout.
    //
    // Suggested V1 baseline:
    // - std::map<Price, PriceLevel> for bid/ask levels
    // - std::unordered_map<OrderId, OrderLocation> for O(1)-average lookup
    // - stable per-level order storage preserving FIFO
    //
    // Do not optimize before the correctness contract passes.
};

}  // namespace order_book
