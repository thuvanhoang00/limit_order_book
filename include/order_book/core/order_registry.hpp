#pragma once
#include <new>
#include <unordered_map>

#include "order_book/core/logger.hpp"
#include "order_book/core/order_book.hpp"
namespace order_book {

class OrderBookRegistry final {
   public:
    // The optional logger must outlive this registry; all calls share one writer.
    explicit OrderBookRegistry(Logger* logger = nullptr) noexcept : logger_(logger) {}

    [[nodiscard]] ApplyResult apply(const MarketEvent& event) {
        ApplyResult result;
        try {
            result = std::visit(
                [this, &event](const auto& concrete_event) {
                    return apply(concrete_event, event.instrument_id);
                },
                event.payload);
        } catch (const std::bad_alloc&) {
            result = ApplyResult::AllocationFailed;
        }
        if (logger_) logger_->log_event(event, result);
        return result;
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

    Logger* logger_{};
    Sequence sequence_{};
    std::unordered_map<InstrumentId, OrderBook> books_;
};

}  // namespace order_book
