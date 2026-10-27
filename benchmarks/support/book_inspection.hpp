#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "order_book/core/types.hpp"

namespace order_book::detail {
struct LevelSnapshot {
    Side side;
    Price price;
    std::uint64_t aggregate;
    std::vector<std::pair<OrderId, Quantity>> orders;
    friend bool operator==(const LevelSnapshot&, const LevelSnapshot&) = default;
};

// Shared inspection for tests and benchmark preflight; never timed.
struct BookInspection {
    template <class Book>
    static std::vector<LevelSnapshot> snapshot(const Book& book) {
        std::vector<LevelSnapshot> result;
        const auto append = [&](const auto& levels, Side side) {
            for (const auto& [price, level] : levels) {
                LevelSnapshot row{side, price, level.aggregate_quantity, {}};
                for (const auto& order : level.orders)
                    row.orders.emplace_back(order.order_id, order.remaining_quantity);
                result.push_back(std::move(row));
            }
        };
        append(book.bids_, Side::Bid);
        append(book.asks_, Side::Ask);
        return result;
    }
    template <class Book>
    static const void* order_address(const Book& book, OrderId id) {
        return std::addressof(*book.orders_by_id_.at(id).iterator);
    }
    template <class Book>
    static std::size_t free_bytes(const Book& book) {
        return book.pool_->count();
    }
    template <class Book>
    static void force_next_index_rehash(Book& book) {
        book.orders_by_id_.max_load_factor(0.001F);
    }
};
}  // namespace order_book::detail
