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
    if(bids_.empty())
        return std::nullopt;
    return bids_.cbegin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    if(asks_.empty())
        return std::nullopt;
    return asks_.cbegin()->first;
}

std::optional<Quantity> OrderBook::remaining_quantity(const OrderId order_id) const noexcept {
    /* No need to check Side here
    if(orders_by_id_.contains(order_id)){
        auto loc = orders_by_id_.at(order_id);
        if(loc.side == Side::Ask){
            if(asks_.contains(loc.price)){
                const auto& pricelv = asks_.at(loc.price);
                for(auto it = pricelv.orders.begin(); it != pricelv.orders.end(); ++it){
                    if(it == loc.iterator) return it->remaining_quantity;
                }
            }
        }
        else if(loc.side ==Side::Bid){
            if(bids_.contains(loc.price)){
                const auto& pricelv = bids_.at(loc.price);
                for(auto it = pricelv.orders.begin(); it != pricelv.orders.end(); ++it){
                    if(it == loc.iterator) return it->remaining_quantity;
                }
            }
        }
    }
    return std::nullopt;
    */

    const auto index_id = orders_by_id_.find(order_id);
    if(index_id == orders_by_id_.end()) return std::nullopt;

    return index_id->second.iterator->remaining_quantity;
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
