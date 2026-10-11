#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <set>
#include <vector>

#include "order_book/memory/fixed_slot_pool.hpp"

namespace order_book::memory {
namespace {

TEST(FixedSlotPool, EmptyAndOversizedRequestsAllowLaterInitialization) {
    FixedSlotPool<128> pool;
    EXPECT_EQ(pool.allocate(0, 8), nullptr);
    EXPECT_EQ(pool.try_allocate(0, 8), nullptr);
    EXPECT_EQ(pool.try_allocate(129, 1), nullptr);
    EXPECT_THROW(pool.allocate(129, 1), std::bad_alloc);
    void* p = pool.allocate(16, 8);
    ASSERT_NE(p, nullptr);
    pool.deallocate(p, 16);
}

TEST(FixedSlotPool, AlignedDistinctSlotsAreReusableAfterExhaustion) {
    FixedSlotPool<4096> pool;
    std::vector<void*> pointers;
    while (void* p = pool.try_allocate(64, 64)) {
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % 64U, 0U);
        pointers.push_back(p);
    }
    ASSERT_FALSE(pointers.empty());
    const std::set<void*> first(pointers.begin(), pointers.end());
    EXPECT_EQ(first.size(), pointers.size());
    EXPECT_EQ(pool.count(), 0U);
    EXPECT_THROW(pool.allocate(64, 64), std::bad_alloc);
    std::reverse(pointers.begin(), pointers.end());
    for (void* p : pointers) pool.deallocate(p, 64);
    EXPECT_EQ(pool.count(), first.size() * 64);
    pointers.clear();
    while (void* p = pool.try_allocate(64, 64)) pointers.push_back(p);
    EXPECT_EQ(std::set<void*>(pointers.begin(), pointers.end()), first);
    for (void* p : pointers) pool.deallocate(p, 64);
}

TEST(FixedSlotPool, RejectsDifferentSlotSizeOrAlignmentWithoutConsumingCapacity) {
    FixedSlotPool<256> pool;
    void* p = pool.allocate(16, 8);
    const auto free_bytes = pool.count();
    EXPECT_EQ(pool.try_allocate(32, 8), nullptr);
    EXPECT_EQ(pool.try_allocate(16, 16), nullptr);
    EXPECT_THROW(pool.allocate(32, 8), std::bad_alloc);
    EXPECT_THROW(pool.allocate(16, 16), std::bad_alloc);
    EXPECT_EQ(pool.count(), free_bytes);
    pool.deallocate(p, 16);
}

TEST(FixedSlotPool, InvalidReleaseAndDoubleReleaseCannotDuplicateSlots) {
    FixedSlotPool<256> pool;
    void* p = pool.allocate(16, 8);
    const auto free_bytes = pool.count();
    pool.deallocate(nullptr, 16);
    pool.deallocate(p, 0);
    pool.deallocate(p, 8);
    pool.deallocate(static_cast<std::byte*>(p) + 1, 16);
    EXPECT_EQ(pool.count(), free_bytes);
    pool.deallocate(p, 16);
    pool.deallocate(p, 16);
    EXPECT_EQ(pool.count(), free_bytes + 16);
    std::set<void*> live;
    while (void* next = pool.try_allocate(16, 8)) EXPECT_TRUE(live.insert(next).second);
    EXPECT_EQ(live.size() * 16, free_bytes + 16);
    for (void* next : live) pool.deallocate(next, 16);
}

}  // namespace
}  // namespace order_book::memory
