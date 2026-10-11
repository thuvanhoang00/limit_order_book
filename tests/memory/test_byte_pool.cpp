#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <numeric>
#include <random>
#include <vector>

#include "order_book/memory/byte_pool.hpp"

namespace order_book::memory {
namespace {

TEST(BytePool, EmptyAndOversizedRequestsDoNotConsumeCapacity) {
    Pool<128> pool;
    EXPECT_EQ(pool.allocate(0, 1), nullptr);
    EXPECT_EQ(pool.try_allocate(0, 1), nullptr);
    EXPECT_EQ(pool.try_allocate(129, 1), nullptr);
    EXPECT_THROW(pool.allocate(129, 1), std::bad_alloc);
    EXPECT_EQ(pool.count(), 128U);
}

TEST(BytePool, ExhaustedStorageCanBeReused) {
    Pool<128> pool;
    void* p = pool.allocate(128, 1);
    EXPECT_EQ(pool.count(), 0U);
    EXPECT_EQ(pool.try_allocate(1, 1), nullptr);
    EXPECT_THROW(pool.allocate(1, 1), std::bad_alloc);
    pool.deallocate(p, 128);
    EXPECT_EQ(pool.count(), 128U);
    void* reused = pool.allocate(128, 1);
    EXPECT_EQ(reused, p);
    pool.deallocate(reused, 128);
}

TEST(BytePool, AlignmentPaddingIsRecovered) {
    Pool<256> pool;
    void* prefix = pool.allocate(1, 1);
    void* aligned = pool.try_allocate(64, 64);
    ASSERT_NE(aligned, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned) % 64U, 0U);
    pool.deallocate(prefix, 1);
    pool.deallocate(aligned, 64);
    EXPECT_EQ(pool.count(), 256U);
    void* whole = pool.allocate(256, 1);
    pool.deallocate(whole, 256);
}

TEST(BytePool, CoalescesAdjacentBlocksInEveryReleaseOrder) {
    std::array<std::size_t, 4> order{0, 1, 2, 3};
    do {
        Pool<128> pool;
        std::array<void*, 4> allocations{};
        for (auto& p : allocations) p = pool.allocate(32, 1);
        for (const auto index : order) pool.deallocate(allocations[index], 32);
        ASSERT_EQ(pool.count(), 128U);
        void* whole = pool.try_allocate(128, 1);
        ASSERT_NE(whole, nullptr);
        pool.deallocate(whole, 128);
    } while (std::next_permutation(order.begin(), order.end()));
}

TEST(BytePool, InvalidSizesAndDoubleReleaseDoNotDuplicateFreeStorage) {
    Pool<128> pool;
    void* p = pool.allocate(32, 1);
    pool.deallocate(nullptr, 32);
    pool.deallocate(p, 0);
    pool.deallocate(p, 31);
    EXPECT_EQ(pool.count(), 96U);
    pool.deallocate(p, 32);
    pool.deallocate(p, 32);
    EXPECT_EQ(pool.count(), 128U);
    void* whole = pool.allocate(128, 1);
    EXPECT_EQ(pool.try_allocate(1, 1), nullptr);
    pool.deallocate(whole, 128);
}

TEST(BytePool, MixedSizedAllocationsPreservePayloadsAndRecoverCapacity) {
    Pool<1024> pool;
    std::mt19937 rng(42);
    for (int round = 0; round < 100; ++round) {
        std::vector<void*> pointers;
        std::vector<std::size_t> sizes;
        for (std::size_t i = 1; i <= 16; ++i) {
            const std::size_t size = i * 3;
            void* p = pool.allocate(size, 8);
            std::fill_n(static_cast<std::byte*>(p), size, static_cast<std::byte>(i));
            pointers.push_back(p);
            sizes.push_back(size);
        }
        std::array<std::size_t, 16> order{};
        std::iota(order.begin(), order.end(), 0U);
        std::shuffle(order.begin(), order.end(), rng);
        for (const auto i : order) {
            const auto* p = static_cast<const std::byte*>(pointers[i]);
            for (std::size_t j = 0; j < sizes[i]; ++j) {
                ASSERT_EQ(p[j], static_cast<std::byte>(i + 1));
            }
            pool.deallocate(pointers[i], sizes[i]);
        }
        ASSERT_EQ(pool.count(), 1024U);
        void* whole = pool.allocate(1024, 1);
        pool.deallocate(whole, 1024);
    }
}

}  // namespace
}  // namespace order_book::memory
