// Test/benchmark executable only: scoped failure injection and allocation counting.
#include "support/allocation_probe.hpp"

#include <cstdlib>
#include <new>
namespace order_book::detail::allocation_probe {
thread_local State state{};
void observe(std::size_t size) {
    if (state.fail_after != disabled) {
        if (state.fail_after == 0) {
            state.fail_after = disabled;  // fail once, allowing rollback to complete
            throw std::bad_alloc{};
        }
        --state.fail_after;
    }
    if (state.count) {
        ++state.calls;
        state.bytes += size;
    }
}
}  // namespace order_book::detail::allocation_probe
void* operator new(std::size_t size) {
    order_book::detail::allocation_probe::observe(size);
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    order_book::detail::allocation_probe::observe(size);
    void* p = nullptr;
    if (posix_memalign(&p,
                       static_cast<std::size_t>(alignment) < sizeof(void*)
                           ? sizeof(void*)
                           : static_cast<std::size_t>(alignment),
                       size ? size : 1) == 0)
        return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

// Route nothrow allocations through the same family too. Otherwise ASan's
// nothrow new can allocate memory that our replacement delete would free.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new[](size);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size, alignment);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return ::operator new[](size, alignment);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
