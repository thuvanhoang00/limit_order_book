#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "order_book/core/lfqueue.hpp"

namespace order_book {
namespace {
TEST(LFQueue, UsesEverySlotAndPreservesFifoAcrossRingWraps) {
    LFQueue<unsigned, 4> queue;
    unsigned value = 99;
    EXPECT_FALSE(queue.try_pop(value));
    EXPECT_EQ(value, 99U);
    for (unsigned round = 0; round < 1000; ++round) {
        for (unsigned i = 0; i < 4; ++i) ASSERT_TRUE(queue.try_push(round * 4 + i));
        EXPECT_FALSE(queue.try_push(9999));
        for (unsigned i = 0; i < 4; ++i) {
            ASSERT_TRUE(queue.try_pop(value));
            EXPECT_EQ(value, round * 4 + i);
        }
        EXPECT_FALSE(queue.try_pop(value));
    }
}

TEST(LFQueue, SupportsSingleSlotCapacity) {
    LFQueue<int, 1> queue;
    int value{};
    ASSERT_TRUE(queue.try_push(42));
    EXPECT_FALSE(queue.try_push(43));
    ASSERT_TRUE(queue.try_pop(value));
    EXPECT_EQ(value, 42);
    EXPECT_FALSE(queue.try_pop(value));
    EXPECT_TRUE(queue.try_push(43));
}

TEST(LFQueue, PublishesCompleteRecordsBetweenOneProducerAndOneConsumer) {
    struct Record {
        std::uint64_t sequence{};
        std::uint64_t checksum{};
    };
    LFQueue<Record, 64> queue;
    constexpr std::uint64_t count = 200'000;
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < count; ++i) {
            while (!queue.try_push({i, ~i})) std::this_thread::yield();
        }
        done.store(true, std::memory_order_release);
    });
    std::uint64_t received = 0;
    std::uint64_t mismatches = 0;
    for (;;) {
        Record record;
        if (queue.try_pop(record)) {
            if (record.sequence != received || record.checksum != ~received) ++mismatches;
            ++received;
        } else if (done.load(std::memory_order_acquire)) {
            // Retry after observing done: the first pop may precede the final push.
            if (!queue.try_pop(record)) break;
            if (record.sequence != received || record.checksum != ~received) ++mismatches;
            ++received;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    EXPECT_EQ(received, count);
    EXPECT_EQ(mismatches, 0U);
}
}  // namespace
}  // namespace order_book
