#pragma once

#include <cstddef>
#include <new>
#include <utility>

#include "order_book/memory/fixed_slot_pool.hpp"

namespace order_book::memory {

template <typename T, std::size_t CAPACITY_BYTES>
class AllocatorAdaptor {
   private:
    FixedSlotPool<CAPACITY_BYTES>* pool_;

    // allow different specializations to access each other's pool
    template <typename, std::size_t>
    friend class AllocatorAdaptor;

   public:
    template <typename U>
    struct rebind {
        using other = AllocatorAdaptor<U, CAPACITY_BYTES>;
    };

    using value_type = T;

    template <typename U>
    AllocatorAdaptor(const AllocatorAdaptor<U, CAPACITY_BYTES>& other) noexcept
        : pool_(other.pool_) {}

    AllocatorAdaptor(FixedSlotPool<CAPACITY_BYTES>& pool) : pool_(&pool) {}
    AllocatorAdaptor(const AllocatorAdaptor& other) : pool_(other.pool_) {}
    AllocatorAdaptor& operator=(const AllocatorAdaptor& other) {
        if (this != &other) {
            pool_ = other.pool_;
        }
        return *this;
    }
    AllocatorAdaptor(AllocatorAdaptor&& other) noexcept : pool_(std::move(other.pool_)){};
    AllocatorAdaptor& operator=(AllocatorAdaptor&& other) noexcept {
        if (this != &other) {
            pool_ = std::move(other.pool_);
        }
        return *this;
    }

    template <typename U>
    bool operator==(const AllocatorAdaptor<U, CAPACITY_BYTES>& other) const noexcept {
        return pool_ == other.pool_;
    }

    std::size_t count() const noexcept { return pool_->count(); }
    T* try_allocate(std::size_t n) noexcept {
        if (n > CAPACITY_BYTES / sizeof(T)) return nullptr;

        if (n > 1) return nullptr;

        return static_cast<T*>(pool_->try_allocate(n * sizeof(T), alignof(T)));
    }
    T* allocate(std::size_t n) {
        if (n > CAPACITY_BYTES / sizeof(T)) throw std::bad_alloc{};

        if (n > 1) throw std::bad_alloc{};

        return static_cast<T*>(pool_->allocate(n * sizeof(T), alignof(T)));
    }
    void deallocate(T* ptr, std::size_t n) noexcept {
        if (n > 1) return;
        return pool_->deallocate(ptr, n * sizeof(T));
    }
};

}  // namespace order_book::memory
