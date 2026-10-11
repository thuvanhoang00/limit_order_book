#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

#include "order_book/memory/memory_pool.hpp"

namespace order_book::memory {
namespace {

struct alignas(64) Tracked {
    int* live;
    int value;

    Tracked(int& live_count, int input) : live(&live_count), value(input) {
        if (input < 0) throw std::runtime_error("construction failed");
        ++*live;
    }
    ~Tracked() { --*live; }
};

TEST(MemoryPool, ConstructsAlignedObjectsAndReportsExhaustion) {
    int live = 0;
    MemoryPool<Tracked, 2> pool;
    const auto a = pool.try_acquire(live, 10);
    const auto b = pool.try_acquire(live, 20);
    ASSERT_NE(a.ptr, nullptr);
    ASSERT_NE(b.ptr, nullptr);
    EXPECT_EQ(a.ptr->value, 10);
    EXPECT_EQ(b.ptr->value, 20);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(a.ptr) % alignof(Tracked), 0U);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(b.ptr) % alignof(Tracked), 0U);
    EXPECT_EQ(pool.try_acquire(live, 30).ptr, nullptr);
    EXPECT_EQ(live, 2);
}

TEST(MemoryPool, ConstructorFailureDoesNotConsumeCapacity) {
    int live = 0;
    MemoryPool<Tracked, 1> pool;
    EXPECT_THROW(pool.try_acquire(live, -1), std::runtime_error);
    EXPECT_EQ(live, 0);
    const auto slot = pool.try_acquire(live, 42);
    ASSERT_NE(slot.ptr, nullptr);
    EXPECT_EQ(slot.ptr->value, 42);
    EXPECT_EQ(live, 1);
}

TEST(MemoryPool, StaleHandleCannotReleaseReusedSlot) {
    int live = 0;
    MemoryPool<Tracked, 1> pool;
    const auto old = pool.try_acquire(live, 1);
    pool.release(old);
    const auto current = pool.try_acquire(live, 2);
    ASSERT_NE(current.ptr, nullptr);
    EXPECT_EQ(old.ptr, current.ptr);
    EXPECT_NE(old.generation, current.generation);
    pool.release(old);
    EXPECT_EQ(live, 1);
    EXPECT_EQ(current.ptr->value, 2);
    EXPECT_EQ(pool.try_acquire(live, 3).ptr, nullptr);
    pool.release(current);
    EXPECT_EQ(live, 0);
}

TEST(MemoryPool, InvalidHandlesAndDoubleReleaseDoNotChangeLiveObjects) {
    int live = 0;
    MemoryPool<Tracked, 1> pool;
    MemoryPool<Tracked, 1> other;
    const auto slot = pool.try_acquire(live, 1);
    const auto foreign = other.try_acquire(live, 2);
    auto invalid = slot;
    invalid.idx = 1;
    pool.release(invalid);
    invalid = slot;
    invalid.ptr = nullptr;
    pool.release(invalid);
    pool.release(foreign);
    EXPECT_EQ(live, 2);
    EXPECT_EQ(pool.try_acquire(live, 3).ptr, nullptr);
    pool.release(slot);
    pool.release(slot);
    EXPECT_EQ(live, 1);
    EXPECT_NE(pool.try_acquire(live, 4).ptr, nullptr);
    EXPECT_EQ(live, 2);
    EXPECT_EQ(pool.try_acquire(live, 5).ptr, nullptr);
}

TEST(MemoryPool, RepeatedOutOfOrderReleasesPreserveCapacity) {
    MemoryPool<int, 3> pool;
    for (int round = 0; round < 10'000; ++round) {
        const auto a = pool.try_acquire(round);
        const auto b = pool.try_acquire(round + 1);
        const auto c = pool.try_acquire(round + 2);
        ASSERT_NE(a.ptr, nullptr);
        ASSERT_NE(b.ptr, nullptr);
        ASSERT_NE(c.ptr, nullptr);
        EXPECT_EQ(*a.ptr, round);
        EXPECT_EQ(*b.ptr, round + 1);
        EXPECT_EQ(*c.ptr, round + 2);
        EXPECT_EQ(pool.try_acquire(0).ptr, nullptr);
        pool.release(b);
        pool.release(a);
        pool.release(c);
    }
}

TEST(MemoryPool, DestructionDestroysOnlyRemainingLiveObjects) {
    int live = 0;
    {
        MemoryPool<Tracked, 3> pool;
        const auto released = pool.try_acquire(live, 1);
        ASSERT_NE(pool.try_acquire(live, 2).ptr, nullptr);
        ASSERT_NE(pool.try_acquire(live, 3).ptr, nullptr);
        pool.release(released);
        EXPECT_EQ(live, 2);
    }
    EXPECT_EQ(live, 0);
}

}  // namespace
}  // namespace order_book::memory
