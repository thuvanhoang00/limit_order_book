#include <gtest/gtest.h>

#include <cstdint>
#include <list>
#include <set>
#include <vector>

#include "support/slot_allocator.hpp"

namespace {

TEST(SlotArenaReference, GrowsWithDistinctAlignedSlotsAndResetsAfterFinalRelease) {
    SlotArena<64, 256> arena;
    std::vector<char*> pointers;
    for (int i = 0; i < 20; ++i) {
        char* p = arena.take();
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % 64U, 0U);
        pointers.push_back(p);
    }
    EXPECT_EQ(std::set<char*>(pointers.begin(), pointers.end()).size(), pointers.size());
    for (std::size_t i = 0; i < pointers.size(); ++i) arena.give();
    for (char* old : pointers) EXPECT_EQ(arena.take(), old);
    for (std::size_t i = 0; i < pointers.size(); ++i) arena.give();
}

TEST(SlotArenaReference, PartialReleaseDoesNotReuseLiveEpochStorage) {
    SlotArena<64, 256> arena;
    char* first = arena.take();
    char* second = arena.take();
    arena.give();
    char* third = arena.take();
    EXPECT_NE(third, first);
    EXPECT_NE(third, second);
    arena.give();
    arena.give();
    EXPECT_EQ(arena.take(), first);
    arena.give();
}

TEST(SlotAllocatorReference, ListSurvivesArenaGrowthAndRepeatedClear) {
    constexpr auto stride = list_slot_bytes<int>();
    SlotArena<stride, stride * 4> arena;
    using Allocator = SlotAllocator<int, stride, stride * 4>;
    std::list<int, Allocator> list{Allocator(arena)};
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 50; ++i) list.push_back(i);
        EXPECT_EQ(list.size(), 50U);
        EXPECT_EQ(list.front(), 0);
        EXPECT_EQ(list.back(), 49);
        list.pop_front();
        EXPECT_EQ(list.front(), 1);
        list.clear();
    }
}

TEST(FixedSlotReference, ListReusesReturnedNodes) {
    FixedSlotPoolTungSahur<4096> pool;
    using Allocator = AllocatorAdaptorTungSahur<int, 4096>;
    std::list<int, Allocator> list{Allocator(pool)};
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 50; ++i) list.push_back(i);
        EXPECT_EQ(list.front(), 0);
        EXPECT_EQ(list.back(), 49);
        list.clear();
    }
}

}  // namespace
