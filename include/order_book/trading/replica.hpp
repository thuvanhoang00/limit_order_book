#pragma once
#include <map>
#include <memory>
#include <span>

#include "order_book/core/order_book.hpp"
#include "order_book/trading/messages.hpp"

namespace order_book::trading {
class MarketReplica final {
   public:
    // Build privately, validate the entire snapshot, then swap atomically on this thread.
    void restore(std::span<const Message> snapshot);
    // false means no strategy may trade until restore succeeds.
    [[nodiscard]] bool consume(const Message& message);
    void invalidate() noexcept { synchronized_ = false; }
    [[nodiscard]] bool synchronized() const noexcept { return synchronized_; }
    [[nodiscard]] Sequence sequence() const noexcept { return sequence_; }
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] const OrderBook* book(InstrumentId instrument) const noexcept;
    [[nodiscard]] std::vector<Message> snapshot() const;
    [[nodiscard]] std::size_t order_count() const noexcept;

   private:
    std::map<InstrumentId, OrderBook> books_;
    std::uint64_t epoch_{};
    Sequence sequence_{};
    bool synchronized_{};
};
}  // namespace order_book::trading
