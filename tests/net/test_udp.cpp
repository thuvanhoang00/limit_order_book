#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <vector>

#include "order_book/net/protocol.hpp"
#include "order_book/net/udp.hpp"

namespace order_book::net {
namespace {
using namespace std::chrono_literals;
constexpr const char* group = "239.255.42.1";

TEST(UdpMulticast, ReceivesNativeEventDatagram) {
    MulticastSubscriber subscriber(group, 0, "127.0.0.1", 2s);
    MulticastPublisher publisher(group, subscriber.local_port());
    const MarketEvent event{1, 55, AddOrder{7, Side::Bid, 100, 5}};
    publisher.send(encode_event(event));
    EventFrame bytes{};
    const auto packet = subscriber.receive(bytes);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->size, bytes.size());
    EXPECT_FALSE(packet->truncated);
    EXPECT_EQ(decode_event(bytes), event);
}
TEST(UdpMulticast, OnePublisherReachesTwoSubscribers) {
    MulticastSubscriber first(group, 0, "127.0.0.1", 2s);
    MulticastSubscriber second(group, first.local_port(), "127.0.0.1", 2s);
    MulticastPublisher publisher(group, first.local_port());
    const auto expected = encode_event(MarketEvent{1, 55, CancelOrder{7}});
    publisher.send(expected);
    EventFrame a{}, b{};
    ASSERT_TRUE(first.receive(a));
    ASSERT_TRUE(second.receive(b));
    EXPECT_EQ(a, expected);
    EXPECT_EQ(b, expected);
}
TEST(UdpMulticast, SeparateDatagramsAreNeverJoinedToMakeAFrame) {
    MulticastSubscriber subscriber(group, 0, "127.0.0.1", 2s);
    MulticastPublisher publisher(group, subscriber.local_port());
    const auto frame = encode_event(MarketEvent{1, 55, CancelOrder{7}});
    publisher.send(std::span(frame).first(3));
    publisher.send(std::span(frame).subspan(3));
    EventFrame bytes{};
    const auto first = subscriber.receive(bytes);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->size, 3U);
    EXPECT_FALSE(first->truncated);
    const auto second = subscriber.receive(bytes);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->size, 37U);
    EXPECT_FALSE(second->truncated);
}
TEST(UdpMulticast, DetectsOversizeAndDiscardsItsTail) {
    MulticastSubscriber subscriber(group, 0, "127.0.0.1", 2s);
    MulticastPublisher publisher(group, subscriber.local_port());
    const auto frame = encode_event(MarketEvent{1, 55, CancelOrder{7}});
    std::array<std::uint8_t, 41> oversized{};
    std::copy(frame.begin(), frame.end(), oversized.begin());
    publisher.send(oversized);
    publisher.send(frame);
    EventFrame bytes{};
    const auto bad = subscriber.receive(bytes);
    ASSERT_TRUE(bad);
    EXPECT_EQ(bad->size, 40U);
    EXPECT_TRUE(bad->truncated); // A valid-looking 40-byte prefix must be rejected.
    const auto good = subscriber.receive(bytes);
    ASSERT_TRUE(good);
    EXPECT_EQ(good->size, 40U);
    EXPECT_FALSE(good->truncated);
    EXPECT_EQ(bytes, frame); // Not the extra byte from the previous datagram.
}
TEST(UdpMulticast, ZeroByteDatagramIsNotEofOrTimeout) {
    MulticastSubscriber subscriber(group, 0, "127.0.0.1", 100ms);
    MulticastPublisher publisher(group, subscriber.local_port());
    publisher.send({});
    EventFrame bytes{};
    const auto packet = subscriber.receive(bytes);
    ASSERT_TRUE(packet);
    EXPECT_EQ(packet->size, 0U);
    EXPECT_FALSE(packet->truncated);
    EXPECT_FALSE(subscriber.receive(bytes)); // Next receive times out; no EOF in UDP.
}
TEST(UdpMulticast, IsolatesDifferentGroupsOnSamePort) {
    MulticastSubscriber first(group, 0, "127.0.0.1", 100ms);
    MulticastSubscriber other("239.255.42.2", first.local_port(), "127.0.0.1", 100ms);
    MulticastPublisher publisher("239.255.42.2", first.local_port());
    const auto expected = encode_event(MarketEvent{1, 55, CancelOrder{7}});
    publisher.send(expected);
    EventFrame bytes{};
    ASSERT_TRUE(other.receive(bytes));
    EXPECT_EQ(bytes, expected);
    EXPECT_FALSE(first.receive(bytes));
}
TEST(UdpMulticast, RejectsInvalidConfiguration) {
    EXPECT_THROW((void)MulticastPublisher("127.0.0.1", 9001), std::invalid_argument);
    EXPECT_THROW((void)MulticastSubscriber("bad", 9001), std::invalid_argument);
    EXPECT_THROW((void)MulticastPublisher(group, 0), std::invalid_argument);
    EXPECT_THROW((void)MulticastPublisher(group, 9001, "0.0.0.0"), std::invalid_argument);
    EXPECT_THROW((void)MulticastSubscriber(group, 9001, group), std::invalid_argument);
    EXPECT_THROW((void)MulticastSubscriber(group, 0, "127.0.0.1", 0ms), std::invalid_argument);
}
TEST(UdpMulticast, RejectsOversizedSendAndEmptyReceiveBuffer) {
    MulticastSubscriber subscriber(group, 0);
    MulticastPublisher publisher(group, subscriber.local_port());
    const std::vector<std::uint8_t> huge(65508U);
    EXPECT_THROW(publisher.send(huge), std::length_error);
    EXPECT_THROW((void)subscriber.receive({}), std::invalid_argument);
}
}  // namespace
}  // namespace order_book::net
