#pragma once

#include <limits>
#include <map>
#include <memory>
#include <utility>

#include "order_book/core/order_book.hpp"
#include "order_book/exchange/messages.hpp"

namespace order_book::exchange {
struct InstrumentRules {
    Price tick_size{1};
    Quantity lot_size{1};
    Price min_price{1};
    Price max_price{std::numeric_limits<Price>::max()};
    Quantity max_quantity{std::numeric_limits<Quantity>::max()};
    std::uint64_t max_notional{std::numeric_limits<std::uint64_t>::max()};
};

// Single writer. Output storage and resting-order resources are reserved before
// any executions, so an allocation rejection cannot partially execute a command.
class MatchingEngine final {
   public:
    void add_instrument(InstrumentId instrument, InstrumentRules rules = {});
    [[nodiscard]] MatchResult process(const Request& request);
    [[nodiscard]] const OrderBook* book(InstrumentId instrument) const noexcept;
    [[nodiscard]] bool validate_invariants(std::string* reason = nullptr) const;
    [[nodiscard]] Sequence sequence() const noexcept { return sequence_; }

   private:
    struct Owner {
        ClientId client;
        OrderId order;
    };
    using Key = std::pair<ClientId, OrderId>;
    struct Instrument {
        InstrumentRules rules;
        OrderBook book;
        std::map<OrderId, Owner> owners;
        std::map<Key, OrderId> clients;
    };
    std::map<InstrumentId, std::unique_ptr<Instrument>> instruments_;
    OrderId next_order_id_{1};
    std::uint64_t next_trade_id_{1};
    Sequence sequence_{};
};
}  // namespace order_book::exchange
