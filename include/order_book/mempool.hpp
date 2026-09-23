#ifndef MEMORYPOOL_H
#define MEMORYPOOL_H
#include <cstddef>
#include <array>
#include <new>
#include <utility>
// SoA
template<typename T>
struct MemSlot{
    T* ptr{nullptr};
    std::size_t idx{};
    std::size_t generation{};
};

template<typename T, std::size_t SIZE>
class MemoryPool{
private:
    struct StorageSlot{
        alignas(T) std::byte storage[sizeof(T)];
    };
    std::array<StorageSlot, SIZE> storages_;
    std::array<std::size_t, SIZE> free_slots_{};
    std::size_t head_{0}, tail_{SIZE}; // head , tail point to slots
    std::array<bool, SIZE> in_uses_{};
    std::array<std::size_t, SIZE> generations_{};
public:
    MemoryPool() noexcept{
        for(std::size_t i = 0; i < SIZE; ++i){
            free_slots_[i] = i;
            in_uses_[i] = false;
        }
    }

    ~MemoryPool(){
        reset();
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    MemoryPool(MemoryPool&& other) noexcept = delete;
    MemoryPool& operator=(MemoryPool&& other) noexcept = delete;

    template<typename... Args>
    MemSlot<T> try_acquire(Args&&... args){
        if(head_ == tail_){
            return MemSlot<T>{nullptr, 0};
        }
        auto buffer_idx = free_slots_[head_%SIZE];
        auto ptr = ::new (static_cast<void*>(storages_[buffer_idx].storage)) T(std::forward<Args>(args)...);
        in_uses_[buffer_idx] = true;
        ++generations_[buffer_idx];
        ++head_;
        return MemSlot<T>{ptr, buffer_idx, generations_[buffer_idx]};
    }

    void release(MemSlot<T> slot){
        if(slot.idx >= SIZE) return;

        if(!in_uses_[slot.idx]) return;

        if(static_cast<void*>(storages_[slot.idx].storage) != (void*)slot.ptr) return;

        if(slot.generation != generations_[slot.idx]) return;

        static_cast<T*>(slot.ptr)->~T();

        in_uses_[slot.idx] = false;
        free_slots_[tail_ % SIZE] = slot.idx;
        ++tail_;
    }

private:
    void reset(){
        for(std::size_t i = 0; i < SIZE; ++i){
            if(in_uses_[i]){
                auto ptr = static_cast<void*>(storages_[i].storage);
                static_cast<T*>(ptr)->~T();
            }
        }
        head_ = 0;
        tail_ = SIZE;
    }
};

#endif
