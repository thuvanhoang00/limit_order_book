#include "order_book/order_book.hpp"

#include <iterator>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <variant>

namespace order_book {

ApplyResult OrderBook::apply(const EventPayload& event) noexcept {
    return std::visit([this](const auto& concrete_event) noexcept { return apply(concrete_event); },
                      event);
}

// This function marked as noexcept but some action can throw bad alloc
ApplyResult OrderBook::apply(const AddOrder& event) noexcept {
    if (event.quantity == 0) return ApplyResult::InvalidQuantity;
    if (event.price == 0) return ApplyResult::InvalidPrice;
    if (orders_by_id_.contains(event.order_id)) return ApplyResult::DuplicateOrder;

    if (event.side == Side::Bid) {
        const auto& price_lv_it = bids_.find(event.price);
        if (price_lv_it != bids_.end()) {
            price_lv_it->second.aggregate_quantity += event.quantity;
            price_lv_it->second.orders.emplace_back(event.order_id, event.quantity); // can be thrown-becareful

            auto it = std::prev(price_lv_it->second.orders.end(), 1);
            orders_by_id_[event.order_id] = {event.side, event.price, it};
        } else {
            auto [level_id, inserted] = bids_.try_emplace(event.price);// can be thrown-becareful
            auto& level = level_id->second;
            level.aggregate_quantity += event.quantity;
            level.orders.emplace_back(event.order_id, event.quantity);
            auto it = std::prev(level.orders.end(), 1);
            orders_by_id_[event.order_id] = {event.side, event.price, it};
        }
    } else if (event.side == Side::Ask) {
        const auto& price_lv_it = asks_.find(event.price);
        if (price_lv_it != asks_.end()) {
            price_lv_it->second.aggregate_quantity += event.quantity;
            price_lv_it->second.orders.emplace_back(event.order_id, event.quantity);// can be thrown-becareful

            auto it = std::prev(price_lv_it->second.orders.end(), 1);
            orders_by_id_[event.order_id] = {event.side, event.price, it};
        } else {
            auto [level_id, inserted] = asks_.try_emplace(event.price);// can be thrown-becareful
            auto& level = level_id->second;
            level.aggregate_quantity += event.quantity;
            level.orders.emplace_back(event.order_id, event.quantity);// can be thrown-becareful
            auto it = std::prev(level.orders.end(), 1);
            orders_by_id_[event.order_id] = {event.side, event.price, it};
        }
    } else {
        return ApplyResult::UnknownOrder;
    }
    return ApplyResult::Ok;
}

ApplyResult OrderBook::apply(const CancelOrder& event) noexcept {
    const auto order_loc_it = orders_by_id_.find(event.order_id);
    if(order_loc_it != orders_by_id_.end()){
        const auto& price = order_loc_it->second.price;
        const auto& side = order_loc_it->second.side;
        const auto& iter = order_loc_it->second.iterator;
        if(side == Side::Bid){
            const auto bid_it = bids_.find(price);
            if(bid_it != bids_.end()){
                bid_it->second.aggregate_quantity -= iter->remaining_quantity;
                bid_it->second.orders.erase(iter);
                if(bid_it->second.orders.size() == 0){
                    bids_.erase(bid_it);
                } 
            }
            else return ApplyResult::UnknownOrder;
        }
        else if(side == Side::Ask){
            const auto ask_it = asks_.find(price);
            if(ask_it != asks_.end()){
                ask_it->second.aggregate_quantity -= iter->remaining_quantity;
                ask_it->second.orders.erase(iter);
                if(ask_it->second.orders.size() == 0){
                    asks_.erase(ask_it);
                } 
            }
            else return ApplyResult::UnknownOrder;
        }
        else{
            return ApplyResult::NotImplemented;
        }

        orders_by_id_.erase(order_loc_it); 
    }
    else{
        return ApplyResult::UnknownOrder;
    }   
    return ApplyResult::Ok;
}

ApplyResult OrderBook::apply(const ExecuteOrder& event) noexcept {
    const auto order_loc_it = orders_by_id_.find(event.order_id);
    if(order_loc_it == orders_by_id_.end()) return ApplyResult::UnknownOrder;
    if(event.quantity == 0) return ApplyResult::InvalidQuantity;

    const auto& iter = order_loc_it->second.iterator;
    auto& quantity = iter->remaining_quantity;
    const auto& side = order_loc_it->second.side;
    const auto& price = order_loc_it->second.price;

    if(quantity < event.quantity) return ApplyResult::QuantityExceedsRemaining;

    if(side == Side::Bid){
        const auto bid_it = bids_.find(price);
        if(bid_it == bids_.end()) return ApplyResult::UnknownOrder;
        bid_it->second.aggregate_quantity -= event.quantity;
        quantity -= event.quantity;
        if(quantity == 0){
            bid_it->second.orders.erase(iter);
            if(bid_it->second.orders.empty()){
                bids_.erase(bid_it);
            } 
            orders_by_id_.erase(order_loc_it); 
        }
    }
    else if(side == Side::Ask){
        const auto ask_it = asks_.find(price);
        if(ask_it == asks_.end()) return ApplyResult::UnknownOrder;
        ask_it->second.aggregate_quantity -= event.quantity;
        quantity -= event.quantity;
        if(quantity == 0){
            ask_it->second.orders.erase(iter);
            if(ask_it->second.orders.empty()){
                asks_.erase(ask_it);
            } 
            orders_by_id_.erase(order_loc_it); 
        }
    }
    else{
        return ApplyResult::NotImplemented;
    }

    return ApplyResult::Ok;
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

std::size_t OrderBook::order_count() const noexcept { 
    return orders_by_id_.size();
}

std::size_t OrderBook::bid_level_count() const noexcept { 
    return bids_.size();
}

std::size_t OrderBook::ask_level_count() const noexcept { 
    return asks_.size();
}

void OrderBook::reset() noexcept {
    bids_.clear();
    asks_.clear();
    orders_by_id_.clear();
}

bool OrderBook::validate_invariants(std::string* reason) const {
    if (reason != nullptr) {
        reason->clear();
    }

    const auto fail = [reason](const std::string& message) {
        if (reason != nullptr) {
            *reason = message;
        }
        return false;
    };

    std::unordered_set<OrderId> seen_order_ids;
    seen_order_ids.reserve(orders_by_id_.size());

    std::size_t level_order_count = 0U;
    std::optional<Price> expected_best_bid;
    std::optional<Price> expected_best_ask;

    const auto validate_levels = [&](const auto& levels, const Side expected_side,
                                     std::optional<Price>& expected_best) {
        const std::string side_name{to_string(expected_side)};

        for (const auto& [price, level] : levels) {
            if (price == 0U) {
                return fail(side_name + " level has an invalid zero price");
            }
            if (level.orders.empty()) {
                return fail(side_name + " level " + std::to_string(price) + " is empty");
            }

            if (!expected_best.has_value() ||
                (expected_side == Side::Bid ? price > *expected_best : price < *expected_best)) {
                expected_best = price;
            }

            std::uint64_t computed_aggregate = 0U;
            for (auto order_it = level.orders.cbegin(); order_it != level.orders.cend();
                 ++order_it) {
                const auto& order = *order_it;

                if (order.remaining_quantity == 0U) {
                    return fail("order " + std::to_string(order.order_id) +
                                " has zero remaining quantity");
                }
                if (!seen_order_ids.insert(order.order_id).second) {
                    return fail("order " + std::to_string(order.order_id) +
                                " appears in more than one price level");
                }
                if (computed_aggregate >
                    std::numeric_limits<std::uint64_t>::max() - order.remaining_quantity) {
                    return fail(side_name + " level " + std::to_string(price) +
                                " aggregate quantity overflows");
                }
                computed_aggregate += order.remaining_quantity;
                ++level_order_count;

                const auto location_it = orders_by_id_.find(order.order_id);
                if (location_it == orders_by_id_.end()) {
                    return fail("order " + std::to_string(order.order_id) +
                                " exists in a price level but not in the order index");
                }

                const auto& location = location_it->second;
                if (location.side != expected_side) {
                    return fail("order " + std::to_string(order.order_id) +
                                " has the wrong side in the order index");
                }
                if (location.price != price) {
                    return fail("order " + std::to_string(order.order_id) +
                                " has the wrong price in the order index");
                }
                if (OrderQueue::const_iterator{location.iterator} != order_it) {
                    return fail("order " + std::to_string(order.order_id) +
                                " has the wrong queue iterator in the order index");
                }
            }

            if (computed_aggregate != level.aggregate_quantity) {
                return fail(side_name + " level " + std::to_string(price) +
                            " aggregate quantity mismatch: stored=" +
                            std::to_string(level.aggregate_quantity) +
                            ", computed=" + std::to_string(computed_aggregate));
            }
        }

        return true;
    };

    if (!validate_levels(bids_, Side::Bid, expected_best_bid)) {
        return false;
    }
    if (!validate_levels(asks_, Side::Ask, expected_best_ask)) {
        return false;
    }

    for (const auto& index_entry : orders_by_id_) {
        const auto order_id = index_entry.first;
        if (!seen_order_ids.contains(order_id)) {
            return fail("order " + std::to_string(order_id) +
                        " exists in the order index but not in a price level");
        }
    }

    if (level_order_count != orders_by_id_.size()) {
        return fail("order count mismatch: levels=" + std::to_string(level_order_count) +
                    ", index=" + std::to_string(orders_by_id_.size()));
    }
    if (best_bid() != expected_best_bid) {
        return fail("best bid does not match the highest live bid level");
    }
    if (best_ask() != expected_best_ask) {
        return fail("best ask does not match the lowest live ask level");
    }

    return true;
}

}  // namespace order_book
