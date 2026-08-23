#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <map>
#include <list>
#include <unordered_map>

#include "order_book/events.hpp"
#include "order_book/types.hpp"

namespace order_book {

class OrderBook final {
public:
    OrderBook() = default;
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) noexcept = default;
    OrderBook& operator=(OrderBook&&) noexcept = default;

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

    // std::map<Price, PriceLevel> bid_level_;
    // std::map<Price, PriceLevel> ask_level_;

    // std::unordered_map<OrderId, Quantity> order_info_;
    struct OrderEntry{
        OrderId order_id{};
        Quantity remaining_quantity{};
    };
    using OrderQueue = std::list<OrderEntry>;
    using OrderIterator = OrderQueue::iterator;

    struct PriceLevel{
        OrderQueue orders;
        std::uint64_t aggregate_quantity{};
    };

    struct OrderLocation{
        Side side;
        Price price;
        OrderIterator iterator;
    };
    
    using BidLevels = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskLevels = std::map<Price, PriceLevel, std::less<Price>>;

    BidLevels bids_;
    AskLevels asks_;

    std::unordered_map<OrderId, OrderLocation> orders_by_id_;
    
};

}  // namespace order_book
