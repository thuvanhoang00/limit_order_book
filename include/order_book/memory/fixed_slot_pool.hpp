#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <vector>

namespace order_book::memory {

template <std::size_t CAPACITY_BYTES>
class FixedSlotPool {
    static_assert(CAPACITY_BYTES > 0);

   private:
    struct FreeBlock {
        std::size_t start{};
    };
    bool first_init{false};
    std::allocator<std::byte> upstream_;
    std::byte* storage_;

    std::vector<FreeBlock> free_slots_;
    std::vector<bool> in_uses_;
    std::size_t free_count_{0};
    std::size_t slot_sz_{};
    std::size_t slot_alignment_{};
    std::size_t last_valid_start_{};
    std::size_t first_valid_start_{};  // also is padding
   public:
    FixedSlotPool() { storage_ = upstream_.allocate(CAPACITY_BYTES); }

    ~FixedSlotPool() { 
        if(storage_)
            upstream_.deallocate(storage_, CAPACITY_BYTES);
    }

    // delete copy/move here
    FixedSlotPool(const FixedSlotPool&) = delete;
    FixedSlotPool& operator=(const FixedSlotPool&) = delete;
    FixedSlotPool(FixedSlotPool&& other) = delete;

    FixedSlotPool& operator=(FixedSlotPool&&) = delete;

    // only support number of element = 1
    void* allocate(std::size_t bytes, std::size_t alignment) {
        if (bytes == 0) return nullptr;

        if (!first_init) {
            //
            void* aligned_storage_ = storage_;
            auto space = CAPACITY_BYTES;
            if (!std::align(alignment, bytes, aligned_storage_, space)) {
                throw std::bad_alloc();
            }
            const std::size_t count = space / bytes;
            const auto padding = CAPACITY_BYTES - space;
            free_slots_.reserve(count);
            in_uses_.reserve(count);
            free_count_ = count;
            for (std::size_t i = 0; i < count; ++i) {
                free_slots_.emplace_back(padding + i * bytes);
                in_uses_.emplace_back(false);
            }
            slot_sz_ = bytes;
            slot_alignment_ = alignment;
            last_valid_start_ = free_slots_[count - 1].start;
            first_valid_start_ = padding;
            first_init = true;
        }

        if (free_count_ == 0) throw std::bad_alloc();
        if (slot_sz_ != bytes || slot_alignment_ != alignment) throw std::bad_alloc();

        const auto block = free_slots_[--free_count_];
        in_uses_[(block.start - first_valid_start_) / bytes] = true;
        return storage_ + block.start;  // return aligned address
    }

    // only support number of element = 1
    void* try_allocate(std::size_t bytes, std::size_t alignment) noexcept {
        if (bytes == 0) return nullptr;

        if (!first_init) {
            //
            void* aligned_storage_ = storage_;
            auto space = CAPACITY_BYTES;
            if (!std::align(alignment, bytes, aligned_storage_, space)) {
                return nullptr;
            }
            const std::size_t count = space / bytes;
            const auto padding = CAPACITY_BYTES - space;
            try {
                free_slots_.reserve(count);
                in_uses_.reserve(count);
            } catch (const std::bad_alloc&) {
                return nullptr;
            }
            free_count_ = count;
            for (std::size_t i = 0; i < count; ++i) {
                free_slots_.emplace_back(padding + i * bytes);
                in_uses_.emplace_back(false);
            }
            slot_sz_ = bytes;
            slot_alignment_ = alignment;
            last_valid_start_ = free_slots_[count - 1].start;
            first_valid_start_ = padding;
            first_init = true;
        }

        if (free_count_ == 0) return nullptr;
        if (slot_sz_ != bytes || slot_alignment_ != alignment) return nullptr;

        const auto block = free_slots_[--free_count_];
        in_uses_[(block.start - first_valid_start_) / bytes] = true;
        return storage_ + block.start;  // return aligned address
    }

    void deallocate(void* ptr, std::size_t bytes) noexcept {
        if (ptr == nullptr || bytes == 0 || !first_init || bytes != slot_sz_) return;

        const auto start = static_cast<std::size_t>(static_cast<std::byte*>(ptr) - storage_);
        if (start > last_valid_start_ || start < first_valid_start_) return;

        const auto offset = start - first_valid_start_;
        if (offset % slot_sz_ != 0) return;

        auto space = slot_sz_;
        if (!std::align(slot_alignment_, slot_sz_, ptr, space))  // if ptr is not aligned
            return;

        auto idx = (start - first_valid_start_) / bytes;
        if (!in_uses_[idx]) return;

        in_uses_[idx] = false;
        free_slots_[free_count_++] = {start};
    }

    std::size_t count() const { return free_count_ * slot_sz_; }
};

}  // namespace order_book::memory
