#pragma once
#include <cstddef>
#include <deque>
#include <vector>

#include "order_book/core/events.hpp"

namespace order_book::detail {
struct BookWorkload {
    std::vector<EventPayload> seed;
    std::vector<EventPayload> events;
};
inline AddOrder make_order(OrderId id) {
    const auto side = id % 2 ? Side::Bid : Side::Ask;
    return {id, side, static_cast<Price>(10'000 + id % 32), 10};
}
inline BookWorkload fill_clear_workload(std::size_t count) {
    BookWorkload result;
    for (OrderId id = 1; id <= count; ++id) result.events.emplace_back(make_order(id));
    for (OrderId id = 1; id <= count; ++id) result.events.emplace_back(CancelOrder{id});
    return result;
}
inline BookWorkload churn_workload(std::size_t count) {
    BookWorkload result;
    std::deque<OrderId> live;
    for (OrderId id = 1; id <= count; ++id) {
        result.seed.emplace_back(make_order(id));
        live.push_back(id);
    }
    OrderId next = static_cast<OrderId>(count) + 1;
    for (std::size_t i = 0; i < count * 4; ++i) {
        result.events.emplace_back(CancelOrder{live.front()});
        live.pop_front();
        result.events.emplace_back(make_order(next));
        live.push_back(next++);
    }
    return result;
}
}  // namespace order_book::detail
