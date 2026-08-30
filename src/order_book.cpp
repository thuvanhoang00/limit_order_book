#include "order_book/order_book.hpp"

#include <type_traits>
#include <variant>
#include <iterator>

namespace order_book {

ApplyResult OrderBook::apply(const MarketEvent& event) noexcept {
    return std::visit([this](const auto& concrete_event) noexcept { return apply(concrete_event); },
                      event);
}

ApplyResult OrderBook::apply(const AddOrder& event) noexcept {
    if(event.quantity == 0) return ApplyResult::InvalidQuantity;
    if(event.price ==0) return ApplyResult::InvalidPrice;
    if(orders_by_id_.contains(event.order_id)) return ApplyResult::DuplicateOrder;

    if(event.side == Side::Bid){
        if(bids_.contains(event.price)){
            auto& price_lv = bids_.at(event.price);
            price_lv.aggregate_quantity += event.quantity;
            price_lv.orders.emplace_back(event.order_id, event.quantity);

            auto it = std::prev(price_lv.orders.end(), 1);
            orders_by_id_[event.order_id] = {event.side, event.price, it};
        }
        else{
            auto [level_id, inserted] = bids_.try_emplace(event.price);
            auto& level = level_id->second;
            level.aggregate_quantity += event.quantity;
            level.orders.emplace_back(event.order_id, event.quantity);
            auto it = std::prev(level.orders.end(),1);
            orders_by_id_[event.order_id] = {event.side, event.price, it};
        }
    }
    else if(event.side == Side::Ask){
        if(asks_.contains(event.price)){
            auto& price_lv = asks_.at(event.price);
            price_lv.aggregate_quantity += event.quantity;
            price_lv.orders.emplace_back(event.order_id, event.quantity);

            auto it = std::prev(price_lv.orders.end(),1);
            orders_by_id_[event.order_id] = {event.side, event.price, it};
        }
        else{
            auto [level_id, inserted] = asks_.try_emplace(event.price);
            auto& level = level_id->second;
            level.aggregate_quantity += event.quantity;
            level.orders.emplace_back(event.order_id, event.quantity);
            auto it = std::prev(level.orders.end(),1);
            orders_by_id_[event.order_id] = {event.side, event.price, it};
        }
    }
    else{
        return ApplyResult::UnknownOrder;
    }
    return ApplyResult::Ok;
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
    if (bids_.empty()) return std::nullopt;
    return bids_.cbegin()->first;
}

std::optional<Price> OrderBook::best_ask() const noexcept {
    if (asks_.empty()) return std::nullopt;
    return asks_.cbegin()->first;
}

std::optional<Quantity> OrderBook::remaining_quantity(const OrderId order_id) const noexcept {
    const auto index_id = orders_by_id_.find(order_id);
    if (index_id == orders_by_id_.end()) return std::nullopt;

    return index_id->second.iterator->remaining_quantity;
}

std::size_t OrderBook::order_count() const noexcept { return 0U; }

std::size_t OrderBook::bid_level_count() const noexcept { return 0U; }

std::size_t OrderBook::ask_level_count() const noexcept { return 0U; }

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
