#pragma once
#include <map>
#include <optional>

#include "order_book/core/order_book.hpp"
#include "order_book/trading/messages.hpp"

namespace order_book::trading {
struct Features {
    std::optional<long double> microprice, aggressive_ratio;
    std::uint64_t trade_id{};
    Side aggressor{Side::Bid};
};
// Strategy-thread ownership. Capture the passive BBO before Execute, since this
// protocol publishes Execute before Trade (unlike PR #17's update ordering).
class FeatureEngine final {
   public:
    void before(const Message& message, const OrderBook* book);
    void after(const Message& message, const OrderBook* book);
    void refresh(InstrumentId id, const OrderBook* book);
    void reset() noexcept {
        values_.clear();
        pending_.clear();
    }
    [[nodiscard]] const Features& get(InstrumentId id) const noexcept;

   private:
    std::map<InstrumentId, Features> values_;
    struct Pending {
        std::uint64_t trade{}, quantity{};
    };
    std::map<InstrumentId, Pending> pending_;
};
}  // namespace order_book::trading
