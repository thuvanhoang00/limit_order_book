#pragma once
#include <compare>
#include <map>
#include <string>
#include <tuple>
#include "order_book/core/order_book.hpp"
#include "order_book/feed/decoder.hpp"

namespace order_book::feed {
struct BookKey {
    std::string venue;
    std::uint16_t channel{};
    std::string symbol;
    auto operator<=>(const BookKey&) const = default;
};
// Feed-derived displayed books, not an order-entry matching engine. A failed
// stream poisons this object; callers must restart/rebuild instead of skipping.
class Books final {
   public:
    explicit Books(std::string symbol_filter = {}, std::size_t pool_bytes = 1024 * 1024);
    void consume(const std::string& venue, const Event& event);
    [[nodiscard]] const std::map<BookKey, OrderBook>& books() const noexcept { return books_; }
    [[nodiscard]] bool validate_invariants(std::string* reason = nullptr) const;
    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] std::uint64_t updates() const noexcept { return updates_; }
   private:
    using OrderKey = std::tuple<std::string, std::uint16_t, OrderId>;
    struct Tracked { BookKey book; Side side; std::uint64_t price, quantity; };
    std::string filter_;
    std::size_t pool_bytes_;
    bool valid_{true};
    std::uint64_t updates_{};
    std::map<BookKey, OrderBook> books_;
    // Retain routing/quantities even for filtered symbols: a PITCH followup has no symbol.
    std::map<OrderKey, Tracked> orders_;
    bool selected(const std::string& symbol) const { return filter_.empty() || filter_ == symbol; }
    void apply(const std::string& venue, const Event& event);
};
}  // namespace order_book::feed
