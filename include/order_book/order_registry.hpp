#pragma once
#include "order_book.hpp"
namespace order_book {

class OrderBookRegistry final {
   public:
    OrderBook& get_or_create(const order_book::InstrumentId instrument_id) {
        return books_.try_emplace(instrument_id).first->second;
    }
    OrderBook* find(const InstrumentId id) {
        const auto it = books_.find(id);
        return it == books_.end() ? nullptr : &it->second;
    }

   private:
    std::unordered_map<InstrumentId, OrderBook> books_;
};

}  // namespace order_book
