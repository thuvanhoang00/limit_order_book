#include "order_book/feed/books.hpp"
#include <limits>
#include "bytes.hpp"

namespace order_book::feed {
namespace {
void applied(ApplyResult result) {
    if (result != ApplyResult::Ok)
        throw Error("LOB apply failed: " + std::string(to_string(result)));
}
AddOrder add(OrderId id, Side side, std::uint64_t price, std::uint64_t quantity) {
    detail::require(price > 0 && price <= std::numeric_limits<Price>::max(),
                    "Feed price cannot be represented by the core LOB");
    detail::require(quantity > 0 && quantity <= std::numeric_limits<Quantity>::max(),
                    "Feed quantity cannot be represented by the core LOB");
    return {id, side, static_cast<Price>(price), static_cast<Quantity>(quantity)};
}
}  // namespace
Books::Books(std::string filter, std::size_t pool_bytes)
    : filter_(std::move(filter)), pool_bytes_(pool_bytes) {
    detail::require(pool_bytes > 0, "Nonzero per-book pool budget required");
}
void Books::consume(const std::string& venue, const Event& event) {
    detail::require(valid_, "Feed books require rebuild after error");
    try { apply(venue, event); }
    catch (...) { valid_ = false; throw; }
}
void Books::apply(const std::string& venue, const Event& e) {
    using detail::require;
    require(!venue.empty(), "Venue namespace required");
    if (e.type == EventType::Metadata || e.type == EventType::Directory || e.type == EventType::Trade)
        return;
    if (e.type == EventType::Clear) {
        std::erase_if(orders_, [&](const auto& p) {
            return std::get<0>(p.first) == venue && std::get<1>(p.first) == e.channel;
        });
        std::erase_if(books_, [&](const auto& p) {
            return p.first.venue == venue && p.first.channel == e.channel;
        });
        return;
    }
    const OrderKey key{venue, e.channel, e.order_id};
    if (e.type == EventType::Add) {
        require(!e.symbol.empty() && !orders_.contains(key), "Missing symbol or duplicate order ID");
        require(e.price > 0 && e.quantity > 0, "Nonpositive feed price/quantity");
        const BookKey book{venue, e.channel, e.symbol};
        if (selected(e.symbol)) {
            const auto order = add(e.order_id, e.side, e.price, e.quantity);
            auto [it, inserted] = books_.try_emplace(book, pool_bytes_);
            (void)inserted;
            applied(it->second.apply(order));
            ++updates_;
        }
        orders_.emplace(key, Tracked{book, e.side, e.price, e.quantity});
        return;
    }
    const auto it = orders_.find(key);
    require(it != orders_.end(), "Unknown feed order; missing history or wrong channel");
    auto old = it->second;
    require(e.symbol.empty() || old.book.symbol == e.symbol, "Order belongs to another symbol");
    const auto found_book = books_.find(old.book);
    auto* book = found_book == books_.end() ? nullptr : &found_book->second;
    if (e.type == EventType::Delete) {
        if (book) applied(book->apply(CancelOrder{e.order_id}));
        orders_.erase(it);
    } else if (e.type == EventType::Reduce || e.type == EventType::Execute) {
        require(e.quantity > 0 && e.quantity <= old.quantity, "Invalid feed decrement");
        if (book) applied(book->apply(ExecuteOrder{e.order_id, static_cast<Quantity>(e.quantity)}));
        it->second.quantity -= e.quantity;
        if (it->second.quantity == 0) orders_.erase(it);
    } else if (e.type == EventType::ExecuteRemaining) {
        // Cboe 0x24 can execute hidden size: Executed != old displayed - remaining.
        require(e.quantity > 0 && e.remaining <= std::numeric_limits<Quantity>::max(),
                "Invalid PITCH execution/remaining quantity");
        if (book) {
            if (e.remaining == 0) applied(book->apply(CancelOrder{e.order_id}));
            else applied(book->replace(e.order_id, add(e.order_id, old.side, old.price, e.remaining), true));
        }
        it->second.quantity = e.remaining;
        if (e.remaining == 0) orders_.erase(it);
    } else if (e.type == EventType::Replace) {
        const OrderKey next{venue, e.channel, e.new_order_id};
        require(next == key || !orders_.contains(next), "Replacement ID already exists");
        require(e.price > 0 && e.quantity > 0, "Nonpositive replacement price/quantity");
        require(!e.maintain_priority || (next == key && e.price == old.price),
                "Maintain-priority replacement changes price/ID");
        if (book) applied(book->replace(e.order_id,
                         add(e.new_order_id, old.side, e.price, e.quantity), e.maintain_priority));
        old.price = e.price;
        old.quantity = e.quantity;
        if (next == key) it->second = std::move(old);
        else { orders_.emplace(next, std::move(old)); orders_.erase(it); }
    } else throw Error("Unsupported normalized book event");
    if (book) ++updates_;
}
bool Books::validate_invariants(std::string* reason) const {
    const auto fail = [&](const char* message) {
        if (reason) *reason = message;
        return false;
    };
    if (!valid_) return fail("Feed stream is invalid");
    std::size_t selected_orders{};
    for (const auto& [key, tracked] : orders_) {
        if (!selected(tracked.book.symbol)) continue;
        ++selected_orders;
        const auto found = books_.find(tracked.book);
        if (found == books_.end()) return fail("Missing reconstructed book");
        const auto order = found->second.order(std::get<2>(key));
        if (!order || order->price != tracked.price || order->quantity != tracked.quantity ||
            order->side != tracked.side) return fail("Routing state and core book disagree");
    }
    std::size_t count{};
    for (const auto& [key, book] : books_) {
        (void)key;
        if (!book.validate_invariants(reason)) return false;
        count += book.order_count();
    }
    return count == selected_orders || fail("Untracked reconstructed orders");
}
}  // namespace order_book::feed
