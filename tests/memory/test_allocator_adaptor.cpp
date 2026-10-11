#include <gtest/gtest.h>

#include <list>
#include <memory>
#include <new>

#include "order_book/memory/allocator_adaptor.hpp"

namespace order_book::memory {
namespace {

TEST(AllocatorAdaptor, ReboundAllocatorsCompareByPoolIdentity) {
    FixedSlotPool<1024> pool;
    FixedSlotPool<1024> other_pool;
    AllocatorAdaptor<int, 1024> allocator(pool);
    using Rebound = std::allocator_traits<decltype(allocator)>::rebind_alloc<long>;
    const Rebound rebound(allocator);
    const AllocatorAdaptor<int, 1024> other_allocator(other_pool);
    EXPECT_TRUE(allocator == rebound);
    EXPECT_TRUE(rebound == allocator);
    EXPECT_FALSE(allocator == other_allocator);
}

TEST(AllocatorAdaptor, UnsupportedCountsLeavePoolAvailable) {
    FixedSlotPool<1024> pool;
    AllocatorAdaptor<int, 1024> allocator(pool);
    EXPECT_EQ(allocator.allocate(0), nullptr);
    EXPECT_EQ(allocator.try_allocate(0), nullptr);
    EXPECT_EQ(allocator.try_allocate(2), nullptr);
    EXPECT_THROW(allocator.allocate(2), std::bad_alloc);
    EXPECT_EQ(allocator.try_allocate(1025), nullptr);
    EXPECT_THROW(allocator.allocate(1025), std::bad_alloc);
    int* p = allocator.allocate(1);
    ASSERT_NE(p, nullptr);
    std::construct_at(p, 42);
    EXPECT_EQ(*p, 42);
    std::destroy_at(p);
    allocator.deallocate(p, 1);
}

TEST(AllocatorAdaptor, ListReusesNodesAcrossMixedInsertEraseAndClear) {
    FixedSlotPool<4096> pool;
    using Allocator = AllocatorAdaptor<int, 4096>;
    std::list<int, Allocator> list{Allocator(pool)};
    for (int round = 0; round < 100; ++round) {
        for (int i = 0; i < 50; ++i) list.push_back(i);
        auto survivor = list.begin();
        ++survivor;
        list.erase(list.begin());
        EXPECT_EQ(*survivor, 1);
        list.push_front(-1);
        list.pop_back();
        list.push_back(99);
        EXPECT_EQ(list.size(), 50U);
        EXPECT_EQ(list.front(), -1);
        EXPECT_EQ(list.back(), 99);
        list.clear();
    }
}

TEST(AllocatorAdaptor, FailedListInsertionPreservesExistingNodesAndAllowsReuse) {
    FixedSlotPool<256> pool;
    using Allocator = AllocatorAdaptor<int, 256>;
    std::list<int, Allocator> list{Allocator(pool)};
    ASSERT_NO_THROW(list.push_back(42));
    const auto survivor = list.begin();
    while (pool.count() != 0) list.push_back(42);
    const auto size = list.size();
    EXPECT_THROW(list.push_back(99), std::bad_alloc);
    EXPECT_EQ(list.size(), size);
    EXPECT_EQ(*survivor, 42);
    list.pop_back();
    EXPECT_NO_THROW(list.push_back(99));
    EXPECT_EQ(list.back(), 99);
}

}  // namespace
}  // namespace order_book::memory
