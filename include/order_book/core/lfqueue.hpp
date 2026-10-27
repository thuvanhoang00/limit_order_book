#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace order_book {

// Single producer, single consumer. Stop both before destroying the queue.
// T must be default-constructible and copy-assignable for this implementation.
template <typename T, std::size_t SIZE>
class LFQueue {
   private:
    static constexpr std::size_t CACHE_LINE_SIZE = 64;
    using Index = std::size_t;

    static_assert(std::is_unsigned_v<Index>);
    static_assert(std::atomic<Index>::is_always_lock_free);
    static_assert(std::is_default_constructible_v<T>);
    static_assert(std::is_copy_assignable_v<T>);
    static_assert(std::has_single_bit(SIZE));
    static_assert(std::numeric_limits<Index>::max() >= SIZE);

    alignas(CACHE_LINE_SIZE) std::atomic<Index> prod_idx_{};
    alignas(CACHE_LINE_SIZE) std::atomic<Index> cons_idx_{};
    alignas(CACHE_LINE_SIZE) std::array<T, SIZE> buffers_{};
    static constexpr std::size_t MASK = SIZE - 1;

    bool is_empty(Index prod_idx, Index cons_idx) const noexcept { return prod_idx == cons_idx; }

    bool is_full(Index prod_idx, Index cons_idx) const noexcept {
        return prod_idx - cons_idx == SIZE;
    }

   public:
    [[nodiscard]] bool try_push(const T& data) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        const auto prod_idx = prod_idx_.load(std::memory_order_relaxed);
        const auto cons_idx = cons_idx_.load(std::memory_order_acquire);
        if (is_full(prod_idx, cons_idx)) return false;

        buffers_[prod_idx & MASK] = data;
        prod_idx_.store(prod_idx + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& data) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        const auto prod_idx = prod_idx_.load(std::memory_order_acquire);
        const auto cons_idx = cons_idx_.load(std::memory_order_relaxed);
        if (is_empty(prod_idx, cons_idx)) return false;

        data = buffers_[cons_idx & MASK];
        cons_idx_.store(cons_idx + 1, std::memory_order_release);
        return true;
    }
};

}  // namespace order_book
