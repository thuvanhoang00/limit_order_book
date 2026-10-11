#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace order_book::memory {

template <std::size_t CAPACITY_BYTES>
class Pool {
    static_assert(CAPACITY_BYTES > 0);

   private:
    struct FreeBlock {
        std::size_t start{};
        std::size_t count{};
    };

    struct AllocationInfo {
        std::size_t requested_bytes{};
        std::size_t padding{};
    };

    std::allocator<std::byte> upstream_;
    std::byte* storage_{nullptr};
    std::array<AllocationInfo, CAPACITY_BYTES> allocated_counts_{};  // map pointer-metadata
    std::array<FreeBlock, CAPACITY_BYTES> free_blocks_;
    std::size_t block_count_{1};

   public:
    Pool() {
        free_blocks_[0] = FreeBlock{0, CAPACITY_BYTES};
        storage_ = upstream_.allocate(CAPACITY_BYTES);
    }
    ~Pool() noexcept { upstream_.deallocate(storage_, CAPACITY_BYTES); }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&& other) = delete;
    Pool& operator=(Pool&& other) = delete;

   public:
    // bytes: requested storage size in bytes.
    void* allocate(std::size_t bytes, std::size_t alignment) {
        if (bytes == 0) return nullptr;

        if (bytes > CAPACITY_BYTES || block_count_ < 1) {
            throw std::bad_alloc{};
        }

        for (std::size_t i = 0; i < block_count_; ++i) {
            const auto& block = free_blocks_[i];
            void* ptr = storage_ + block.start;
            std::size_t space = block.count;
            // On success, ptr moves to an aligned address.
            // space decrease by the padding only; bytes are not deducted
            if (!std::align(alignment, bytes, ptr, space)) {
                // this block cannot fit both alignment padding and requested bytes
                continue;
            }

            const auto padding = block.count - space;
            const auto remaining = block.count - padding - bytes;
            if (remaining == 0) {
                if (block_count_ > 1) {
                    std::swap(free_blocks_[i], free_blocks_[block_count_ - 1]);
                    // no need to pop_back here
                }
                block_count_--;
            } else {
                free_blocks_[i].count -= (bytes + padding);
                free_blocks_[i].start += (bytes + padding);
            }
            const auto aligned_start =
                static_cast<std::size_t>(static_cast<std::byte*>(ptr) - storage_);
            allocated_counts_[aligned_start] = {bytes, padding};
            return ptr;
        }

        throw std::bad_alloc{};
    }

    void* try_allocate(std::size_t bytes, std::size_t alignment) noexcept {
        if (bytes == 0) return nullptr;

        if (block_count_ < 1) return nullptr;

        for (std::size_t i = 0; i < block_count_; ++i) {
            const auto& block = free_blocks_[i];
            void* ptr = storage_ + block.start;
            std::size_t space = block.count;
            if (!std::align(alignment, bytes, ptr, space)) {
                continue;
            }

            const auto padding = block.count - space;
            const auto remaining = block.count - padding - bytes;
            if (remaining == 0) {
                if (block_count_ > 1) std::swap(free_blocks_[i], free_blocks_[block_count_ - 1]);
                --block_count_;
            } else {
                free_blocks_[i].count -= (bytes + padding);
                free_blocks_[i].start += (bytes + padding);
            }
            const auto aligned_start =
                static_cast<std::size_t>(static_cast<std::byte*>(ptr) - storage_);
            allocated_counts_[aligned_start] = {bytes, padding};
            return ptr;
        }

        return nullptr;
    }

    // only deallocate has chance to merge
    void deallocate(void* ptr, std::size_t bytes) noexcept {
        if (ptr == nullptr || bytes == 0) return;

        const auto aligned_start =
            static_cast<std::size_t>(static_cast<std::byte*>(ptr) - storage_);
        if (aligned_start >= CAPACITY_BYTES) return;
        const auto& info = allocated_counts_[aligned_start];
        if (info.requested_bytes != bytes) return;

        auto original_start = aligned_start - info.padding;
        auto returned_bytes = info.requested_bytes + info.padding;

        FreeBlock temp{original_start, returned_bytes};

        // Can merge with backward and forward slot
        std::size_t i{0}, j{0};
        bool found_backward{false};
        bool found_forward{false};

        // merge with backward
        for (; i < block_count_; ++i) {
            if (temp.start == (free_blocks_[i].start + free_blocks_[i].count)) {
                found_backward = true;
                temp.start = free_blocks_[i].start;
                temp.count += free_blocks_[i].count;
                free_blocks_[i] = temp;
                break;
            }
        }

        // merge with forward
        for (; j < block_count_; ++j) {
            if ((temp.start + temp.count) == free_blocks_[j].start) {
                found_forward = true;
                temp.count += free_blocks_[j].count;
                free_blocks_[j] = temp;
                if (found_backward) {
                    std::swap(free_blocks_[i], free_blocks_[block_count_ - 1]);
                    --block_count_;
                }
                break;
            }
        }

        if (!found_backward && !found_forward) {
            free_blocks_[block_count_] = temp;
            ++block_count_;
        }

        // mark as free
        allocated_counts_[aligned_start] = {};
    }

    std::size_t count() const noexcept {
        std::size_t total = 0;
        for (std::size_t i = 0; i < block_count_; ++i) {
            total += free_blocks_[i].count;
        }
        return total;
    }
};

}  // namespace order_book::memory
