#pragma once

#include <cstddef>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "order_book/core/events.hpp"
#include "order_book/core/types.hpp"
#include "order_book/memory/fixed_slot_pool.hpp"
#include "order_book/memory/allocator_adaptor.hpp"

namespace order_book {

namespace detail { struct BookInspection; }

struct OrderView {
    OrderId order_id{};
    Side side{Side::Bid};
    Price price{};
    Quantity quantity{};
    friend bool operator==(const OrderView&, const OrderView&) = default;
};

struct LevelView {
    Side side{Side::Bid};
    Price price{};
    std::uint64_t quantity{};
    std::vector<OrderView> orders;
    friend bool operator==(const LevelView&, const LevelView&) = default;
};

class OrderBook final {
   public:
    OrderBook();
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&& other) noexcept // can be default, no need to call reset 
        : pool_(std::move(other.pool_)), order_alloc_(std::move(other.order_alloc_)) 
        {
            bids_ = std::move(other.bids_);
            asks_ = std::move(other.asks_);
            orders_by_id_ = std::move(other.orders_by_id_);
        }
    OrderBook& operator=(OrderBook&& other) noexcept{
        if(this != &other){
            // move assignment need to manually written due to this call reset
            reset();
            pool_ = std::move(other.pool_);
            order_alloc_ = std::move(other.order_alloc_);
            bids_ = std::move(other.bids_);
            asks_ = std::move(other.asks_);
            orders_by_id_ = std::move(other.orders_by_id_);
        }
        return *this;
    }

    [[nodiscard]] ApplyResult apply(const EventPayload& event) noexcept;
    [[nodiscard]] ApplyResult apply(const AddOrder& event) noexcept;
    [[nodiscard]] ApplyResult apply(const CancelOrder& event) noexcept;
    [[nodiscard]] ApplyResult apply(const ExecuteOrder& event) noexcept;

    [[nodiscard]] std::uint64_t best_quantity(Side side) const noexcept;
    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;
    [[nodiscard]] std::optional<Quantity> remaining_quantity(OrderId order_id) const noexcept;

    [[nodiscard]] std::optional<OrderView> front(Side side) const noexcept;
    [[nodiscard]] std::optional<OrderView> order(OrderId order_id) const noexcept;
    [[nodiscard]] std::vector<LevelView> snapshot() const;
    // Visits copies in price-time order. Return false to stop at the price limit.
    template <class Visitor>
    void for_each_order(Side side, Visitor&& visitor) const {
        const auto visit = [&](const auto& levels) {
            for (const auto& [price, level] : levels)
                for (const auto& entry : level.orders)
                    if (!visitor(OrderView{entry.order_id, side, price, entry.remaining_quantity})) return;
        };
        if (side == Side::Bid) visit(bids_);
        else if (side == Side::Ask) visit(asks_);
    }

    [[nodiscard]] std::size_t order_count() const noexcept;
    [[nodiscard]] std::size_t bid_level_count() const noexcept;
    [[nodiscard]] std::size_t ask_level_count() const noexcept;

    void reset() noexcept;

    // This function is intentionally allowed to be slower than the hot path.
    // Use it in tests and debug builds to validate internal consistency.
    [[nodiscard]] bool validate_invariants(std::string* reason = nullptr) const;

   private:
    friend struct detail::BookInspection;
    struct OrderEntry {
        OrderId order_id{};
        Quantity remaining_quantity{};
    };
    static constexpr size_t CAPACITY_BYTES = sizeof(OrderEntry) * 1024;

    std::unique_ptr<memory::FixedSlotPool<CAPACITY_BYTES>> pool_;
    using OrderAllocator = memory::AllocatorAdaptor<OrderEntry, CAPACITY_BYTES>;
    OrderAllocator order_alloc_;

    using OrderQueue = std::list<OrderEntry, OrderAllocator>;
    using OrderIterator = OrderQueue::iterator;

    struct PriceLevel {
        explicit PriceLevel(const OrderAllocator& alloc) : orders(alloc) {}
        OrderQueue orders;
        std::uint64_t aggregate_quantity{};
    };

    struct OrderLocation {
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
