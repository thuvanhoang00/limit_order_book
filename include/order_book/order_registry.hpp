#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <variant>

#include "order_book/order_book.hpp"

namespace order_book {

class OrderBookRegistry final {
   public:
    [[nodiscard]] ApplyResult apply(const MarketEvent& event) {
        return std::visit(
            [this, &event](const auto& concrete_event) {
                return apply(concrete_event, event.instrument_id);
            },
            event.payload);
    }

    [[nodiscard]] bool seq_valid(const Sequence seq) noexcept {
        if (seq <= sequence_ || seq - sequence_ != 1U) {
            return false;
        }
        sequence_ = seq;
        return true;
    }

    [[nodiscard]] Sequence get_seq() const noexcept { return sequence_; }

    [[nodiscard]] bool validate_invariants(std::string* reason = nullptr) const {
        if (reason != nullptr) {
            reason->clear();
        }
        for (const auto& book : books_) {
            if (!book.second.validate_invariants(reason)) return false;
        }
        return true;
    }

    [[nodiscard]] std::size_t order_count() const noexcept {
        std::size_t total{};

        for (const auto& book : books_) {
            total += book.second.order_count();
        }
        return total;
    }

    [[nodiscard]] std::size_t bid_level_count() const noexcept {
        std::size_t total{};

        for (const auto& book : books_) {
            total += book.second.bid_level_count();
        }
        return total;
    }

    [[nodiscard]] std::size_t ask_level_count() const noexcept {
        std::size_t total{};

        for (const auto& book : books_) {
            total += book.second.ask_level_count();
        }
        return total;
    }

    [[nodiscard]] std::size_t instrument_count() const noexcept { return books_.size(); }

   private:
    OrderBook* find(const InstrumentId id) {
        const auto it = books_.find(id);
        return it == books_.end() ? nullptr : &it->second;
    }

    ApplyResult apply(const AddOrder& event, const InstrumentId instrument_id) {
        auto [book_it, inserted] = books_.try_emplace(instrument_id);
        const ApplyResult result = book_it->second.apply(event);
        if (inserted && result != ApplyResult::Ok) {
            books_.erase(book_it);
        }
        return result;
    }

    ApplyResult apply(const CancelOrder& event, const InstrumentId instrument_id) {
        if (auto book = find(instrument_id)) {
            return book->apply(event);
        }
        return ApplyResult::UnknownInstrument;
    }

    ApplyResult apply(const ExecuteOrder& event, const InstrumentId instrument_id) {
        if (auto book = find(instrument_id)) {
            return book->apply(event);
        }
        return ApplyResult::UnknownInstrument;
    }

    Sequence sequence_{};
    std::unordered_map<InstrumentId, OrderBook> books_;
};

}  // namespace order_book
