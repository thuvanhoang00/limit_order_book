#pragma once
#include <cstddef>
#include <array>
#include <new>
#include <utility>
#include <type_traits>
#include <algorithm>
#include <unordered_map>
#include <set>
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

template<std::size_t CAPACITY_BYTES>
class Pool{
    static_assert(CAPACITY_BYTES>0);
private:
    struct FreeBlock{
        std::size_t start{};
        std::size_t count{};
    };

    struct AllocationInfo{
        std::size_t requested_bytes{};
        std::size_t padding{};
    };

    std::allocator<std::byte> upstream_;
    std::byte* storage_{nullptr};
    std::array<AllocationInfo, CAPACITY_BYTES> allocated_counts_{}; // map pointer-metadata
    std::array<FreeBlock, CAPACITY_BYTES> free_blocks_;
    std::size_t block_count_{1};
public:
    Pool() {
        free_blocks_[0] = FreeBlock{0, CAPACITY_BYTES};
        storage_ = upstream_.allocate(CAPACITY_BYTES);
    }
    ~Pool() noexcept {
        upstream_.deallocate(storage_, CAPACITY_BYTES);
    }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    Pool(Pool&& other) = delete;
    Pool& operator=(Pool&& other) = delete;

public:
    // bytes: requested storage size in bytes.
    void* allocate(std::size_t bytes, std::size_t alignment) {
        if(bytes == 0) return nullptr;

        if(bytes > CAPACITY_BYTES || block_count_ < 1){
            throw std::bad_alloc{};
        }

        for(std::size_t i = 0; i < block_count_; ++i){
            const auto& block = free_blocks_[i];
            void* ptr = storage_ + block.start;
            std::size_t space = block.count;
            // On success, ptr moves to an aligned address.
            // space decrease by the padding only; bytes are not deducted
            if(!std::align(alignment, bytes, ptr, space)){
                // this block cannot fit both alignment padding and requested bytes
                continue;
            }

            const auto padding = block.count - space;
            const auto remaining = block.count - padding - bytes;
            if(remaining == 0){
                if(block_count_ > 1){ 
                    std::swap(free_blocks_[i], free_blocks_[block_count_-1]);
                    // no need to pop_back here
                } 
                block_count_--;
            }
            else{
                free_blocks_[i].count -= (bytes+padding);
                free_blocks_[i].start += (bytes+padding);
            }
            const auto aligned_start = static_cast<std::size_t>(static_cast<std::byte*>(ptr) - storage_);
            allocated_counts_[aligned_start] = {bytes, padding};
            return ptr;
        }

        throw std::bad_alloc{};
    }

    void* try_allocate(std::size_t bytes, std::size_t alignment) noexcept {
        if(bytes == 0) return nullptr;

        if(block_count_ < 1) return nullptr;

        bool found{false};
        for(std::size_t i = 0; i < block_count_; ++i){
            const auto& block = free_blocks_[i];
            void* ptr = storage_ + block.start;
            std::size_t space = block.count;
            if(!std::align(alignment, bytes, ptr, space)){
                continue;
            }

            const auto padding = block.count - space;
            const auto remaining = block.count - padding - bytes;
            if(remaining == 0){
                if(block_count_ > 1)
                    std::swap(free_blocks_[i], free_blocks_[block_count_-1]);
                --block_count_;
            }
            else{
                free_blocks_[i].count -= (bytes+padding);
                free_blocks_[i].start += (bytes+padding);
            }
            const auto aligned_start = static_cast<std::size_t>(static_cast<std::byte*>(ptr)-storage_);
            allocated_counts_[aligned_start] = {bytes, padding};
            return ptr;
        }

        return nullptr;
    }

    // only deallocate has chance to merge
    void deallocate(void* ptr, std::size_t bytes) noexcept{
        if(ptr == nullptr || bytes == 0) return;

        const auto aligned_start = static_cast<std::size_t>(static_cast<std::byte*>(ptr)-storage_);
        if(aligned_start >= CAPACITY_BYTES) return;
        const auto& info = allocated_counts_[aligned_start];
        if(info.requested_bytes != bytes) return;

        auto original_start = aligned_start - info.padding;
        auto returned_bytes = info.requested_bytes + info.padding;

        FreeBlock temp{original_start, returned_bytes};
        
        // Can merge with backward and forward slot
        std::size_t i{0}, j{0};
        bool found_backward{false};
        bool found_forward{false};

        // merge with backward
        for(; i < block_count_; ++i){
            if(temp.start == (free_blocks_[i].start+free_blocks_[i].count)){
                found_backward = true; 
                temp.start = free_blocks_[i].start;
                temp.count += free_blocks_[i].count;
                free_blocks_[i] = temp;
                break;
            }
        }

        // merge with forward
        for(; j<block_count_; ++j){
            if((temp.start+temp.count) == free_blocks_[j].start){
                found_forward = true;
                temp.count += free_blocks_[j].count;
                free_blocks_[j] = temp;
                if(found_backward){
                    std::swap(free_blocks_[i], free_blocks_[block_count_-1]);
                    --block_count_;
                }
                break;
            }
        }

        if(!found_backward && !found_forward){
            free_blocks_[block_count_] = temp;
            ++block_count_;
        }

        // mark as free
        allocated_counts_[aligned_start] = {};
    }

    std::size_t count() const noexcept {
        std::size_t total = 0;
        for (std::size_t i=0; i<block_count_; ++i) {
            total += free_blocks_[i].count;
        }
        return total;
    }
};


template<std::size_t CAPACITY_BYTES>
class FixedSlotPool{
    static_assert(CAPACITY_BYTES>0);
private:
    struct FreeBlock{
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
    std::size_t first_valid_start_{}; // also is padding
public:
    FixedSlotPool(){
        storage_ = upstream_.allocate(CAPACITY_BYTES);
    }

    ~FixedSlotPool(){
        upstream_.deallocate(storage_, CAPACITY_BYTES);
    }

    // delete copy/move here
    FixedSlotPool(const FixedSlotPool&) = delete;
    FixedSlotPool& operator=(const FixedSlotPool&) = delete;
    FixedSlotPool(FixedSlotPool&&) = delete;
    FixedSlotPool& operator=(FixedSlotPool&&) = delete;

    // only support number of element = 1
    void* allocate(std::size_t bytes, std::size_t alignment){
        if(bytes == 0) return nullptr;
        
        if(!first_init){
            // 
            void* aligned_storage_ = storage_;
            auto space = CAPACITY_BYTES;
            if(!std::align(alignment, bytes, aligned_storage_, space)){
                throw std::bad_alloc();
            }
            const std::size_t count = space/bytes;
            const auto padding = CAPACITY_BYTES - space;
            free_slots_.reserve(count);
            in_uses_.reserve(count);
            free_count_ = count;
            for(std::size_t i = 0; i < count; ++i){
                free_slots_.emplace_back(padding + i*bytes);
                in_uses_.emplace_back(false);
            }
            slot_sz_ = bytes;
            slot_alignment_ = alignment;
            last_valid_start_ = free_slots_[count-1].start;
            first_valid_start_ = padding;
            first_init = true;
        }

        if(free_count_ == 0) throw std::bad_alloc();
        if(slot_sz_ != bytes || slot_alignment_ != alignment) throw std::bad_alloc();

        const auto block = free_slots_[--free_count_];
        in_uses_[(block.start-first_valid_start_)/bytes] = true;
        return storage_ + block.start; // return aligned address 
    }

    // only support number of element = 1
    void* try_allocate(std::size_t bytes, std::size_t alignment) noexcept{
        if(bytes == 0) return nullptr;
        
        if(!first_init){
            // 
            void* aligned_storage_ = storage_;
            auto space = CAPACITY_BYTES;
            if(!std::align(alignment, bytes, aligned_storage_, space)){
               return nullptr; 
            }
            const std::size_t count = space/bytes;
            const auto padding = CAPACITY_BYTES - space;
            try{
                free_slots_.reserve(count);
                in_uses_.reserve(count);
            }
            catch(const std::bad_alloc&){
                return nullptr;
            }
            free_count_ = count;
            for(std::size_t i = 0; i < count; ++i){
                free_slots_.emplace_back(padding + i*bytes);
                in_uses_.emplace_back(false);
            }
            slot_sz_ = bytes;
            slot_alignment_ = alignment;
            last_valid_start_ = free_slots_[count-1].start;
            first_valid_start_ = padding;
            first_init = true;
        }

        if(free_count_ == 0) return nullptr;
        if(slot_sz_ != bytes || slot_alignment_ != alignment) return nullptr;

        const auto block = free_slots_[--free_count_];
        in_uses_[(block.start-first_valid_start_)/bytes] = true;
        return storage_ + block.start; // return aligned address 
    }

    void deallocate(void* ptr, std::size_t bytes) noexcept {
        if(ptr == nullptr || bytes == 0 || !first_init || bytes != slot_sz_)
            return;

        const auto start = static_cast<std::size_t>(static_cast<std::byte*>(ptr)-storage_);
        if(start > last_valid_start_ || start < first_valid_start_)
            return;

        const auto offset = start - first_valid_start_;
        if(offset % slot_sz_ != 0)
            return;

        auto space = slot_sz_;
        if(!std::align(slot_alignment_, slot_sz_, ptr, space)) // if ptr is not aligned
            return;

        auto idx = (start-first_valid_start_)/bytes;
        if(!in_uses_[idx]) return;

        in_uses_[idx] = false;
        free_slots_[free_count_++] = {start};
    }
    
    std::size_t count() const{
        return free_count_ * slot_sz_;
    }
};

template<typename T, std::size_t CAPACITY_BYTES>
class AllocatorAdaptor{
private:
    FixedSlotPool<CAPACITY_BYTES>* pool_;

    // allow different specializations to access each other's pool
    template<typename, std::size_t>
    friend class AllocatorAdaptor;
public:
    template<typename U>
    struct rebind{
        using other = AllocatorAdaptor<U, CAPACITY_BYTES>;
    };

    using value_type = T;

    template<typename U>
    AllocatorAdaptor(const AllocatorAdaptor<U, CAPACITY_BYTES>& other) noexcept : pool_(other.pool_) {}

    AllocatorAdaptor(FixedSlotPool<CAPACITY_BYTES>& pool) : pool_(&pool) {}
    AllocatorAdaptor(const AllocatorAdaptor& other) : pool_(other.pool_) {}
    AllocatorAdaptor& operator=(const AllocatorAdaptor& other) {
        if(this != &other){
            pool_ = other.pool_;
        }
        return *this;
    }
    AllocatorAdaptor(AllocatorAdaptor&& other) noexcept : pool_(std::move(other.pool_)) {};
    AllocatorAdaptor& operator=(AllocatorAdaptor&& other) noexcept{
        if(this != &other){
            pool_ = std::move(other.pool_);
        }
        return *this;
    }

    template<typename U>
    bool operator==(const AllocatorAdaptor<U,CAPACITY_BYTES>& other) const noexcept {
        return pool_ == other.pool_;
    }

    std::size_t count() const noexcept {
        return pool_->count();
    }
    T* try_allocate(std::size_t n) noexcept{
        if(n > CAPACITY_BYTES/sizeof(T)) return nullptr;

        if(n > 1) return nullptr;

        return static_cast<T*>(pool_->try_allocate(n*sizeof(T), alignof(T)));
    }
    T* allocate(std::size_t n){
        if(n > CAPACITY_BYTES/sizeof(T)) throw std::bad_alloc{};

        if(n > 1) throw std::bad_alloc{};

        return static_cast<T*>(pool_->allocate(n*sizeof(T), alignof(T)));
    }
    void deallocate(T* ptr, std::size_t n) noexcept{
        if(n > 1) return;
        return pool_->deallocate(ptr, n*sizeof(T));
    }
};
