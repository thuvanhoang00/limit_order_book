#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <future>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include "order_book/net/protocol.hpp"
#include "order_book/net/tcp.hpp"

namespace order_book::net {
namespace {
std::pair<Socket, Socket> socket_pair() {
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0)
        throw std::runtime_error("socketpair failed");
    // Bound failures in transport tests rather than hanging the test runner.
    const timeval timeout{5, 0};
    for (const auto fd : fds) {
        (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    }
    return {Socket(fds[0]), Socket(fds[1])};
}
TEST(TcpProtocol, RoundTripsNativeEventsAndFullWidthIntegers) {
    for (const MarketEvent& event :
         {MarketEvent{0xffffffffffffffffULL, 0x0123456789abcdefULL,
                      AddOrder{0xfedcba9876543210ULL, Side::Ask, 0xffffffffU, 0xffffffffU}},
          MarketEvent{2, 55, AddOrder{7, Side::Bid, 100, 5}}, MarketEvent{3, 55, CancelOrder{7}},
          MarketEvent{4, 55, ExecuteOrder{7, 2}}}) {
        const auto decoded = decode_event(encode_event(event));
        ASSERT_TRUE(decoded);
        EXPECT_EQ(*decoded, event);
    }
}
TEST(TcpProtocol, MatchesIndependentBigEndianFixture) {
    const EventFrame expected{0x4c, 0x4f, 0x42, 0x31, 1, 1, 2,    0,    0, 0,  0, 0, 0, 0,
                              0,    1,    0,    0,    0, 0, 0,    0,    0, 55, 0, 0, 0, 0,
                              0,    0,    0,    7,    0, 0, 0x27, 0x10, 0, 0,  0, 5};
    EXPECT_EQ(encode_event(MarketEvent{1, 55, AddOrder{7, Side::Ask, 10000, 5}}), expected);
    const AckFrame ack{0x4c, 0x4f, 0x42, 0x31, 1, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    EXPECT_EQ(encode_received_ack(1), ack);
    EXPECT_EQ(decode_received_ack(ack), 1U);
}
TEST(TcpProtocol, ApplyAckUsesDistinctKindAndStableStatusCodes) {
    const AckFrame expected{0x4c, 0x4f, 0x42, 0x31, 1, 129, 5, 0, 0, 0, 0, 0, 0, 0, 0, 9};
    EXPECT_EQ(encode_apply_ack({9, ApplyResult::UnknownInstrument}), expected);
    EXPECT_EQ(decode_apply_ack(expected), (ApplyAck{9, ApplyResult::UnknownInstrument}));
    EXPECT_FALSE(decode_received_ack(expected));
    EXPECT_FALSE(decode_apply_ack(encode_received_ack(9)));
    const std::array codes{ApplyResult::Ok,
                           ApplyResult::DuplicateOrder,
                           ApplyResult::UnknownOrder,
                           ApplyResult::InvalidPrice,
                           ApplyResult::InvalidQuantity,
                           ApplyResult::UnknownInstrument,
                           ApplyResult::QuantityExceedsRemaining,
                           ApplyResult::ParseError,
                           ApplyResult::NotImplemented,
                           ApplyResult::AllocationFailed};
    for (std::size_t i = 0; i < codes.size(); ++i) {
        const auto frame = encode_apply_ack({9, codes[i]});
        EXPECT_EQ(frame[6], i);
        EXPECT_EQ(decode_apply_ack(frame), (ApplyAck{9, codes[i]}));
    }
    auto invalid = expected;
    invalid[6] = 255;
    EXPECT_FALSE(decode_apply_ack(invalid));
    EXPECT_THROW((void)encode_apply_ack({9, static_cast<ApplyResult>(255)}), std::invalid_argument);
}
TEST(TcpProtocol, RejectsInvalidHeadersKindsAndSide) {
    const auto valid = encode_event(MarketEvent{1, 55, AddOrder{7, Side::Bid, 100, 5}});
    for (const auto index : {0U, 4U, 5U, 6U, 7U}) {
        auto corrupt = valid;
        corrupt[index] = 0xff;
        EXPECT_FALSE(decode_event(corrupt)) << index;
    }
    auto ack = encode_received_ack(1);
    ack[6] = 1;
    EXPECT_FALSE(decode_received_ack(ack));
    EXPECT_THROW((void)encode_event(MarketEvent{1, 55, AddOrder{7, static_cast<Side>(42), 100, 5}}),
                 std::invalid_argument);
}
TEST(TcpProtocol, RejectsNoncanonicalUnusedFields) {
    auto cancel = encode_event(MarketEvent{1, 55, CancelOrder{7}});
    for (const auto index : {6U, 32U, 36U}) {
        auto corrupt = cancel;
        corrupt[index] = 1;
        EXPECT_FALSE(decode_event(corrupt));
    }
    auto execute = encode_event(MarketEvent{1, 55, ExecuteOrder{7, 2}});
    execute[32] = 1;
    EXPECT_FALSE(decode_event(execute));
}
TEST(TcpTransport, ReceivesFragmentedAndCoalescedFrames) {
    auto [writer, reader] = socket_pair();
    const auto frame = encode_event(MarketEvent{1, 55, CancelOrder{7}});
    auto producer = std::async(std::launch::async, [&] {
        // Arbitrary chunks cross message boundaries, just as a TCP byte stream does.
        send_all(writer, std::span(frame).first(3));
        std::array<std::uint8_t, 77> rest{};
        std::copy(frame.begin() + 3, frame.end(), rest.begin());
        std::copy(frame.begin(), frame.end(), rest.begin() + 37);
        send_all(writer, rest);
        (void)::shutdown(writer.fd(), SHUT_WR);
    });
    EventFrame received{};
    ASSERT_TRUE(receive_exact(reader, received));
    EXPECT_EQ(received, frame);
    ASSERT_TRUE(receive_exact(reader, received));
    EXPECT_EQ(received, frame);
    EXPECT_FALSE(receive_exact(reader, received));
    producer.get();
}
TEST(TcpTransport, DistinguishesCleanEofFromTruncatedFrame) {
    auto [writer, reader] = socket_pair();
    const std::array<std::uint8_t, 3> partial{1, 2, 3};
    send_all(writer, partial);
    (void)::shutdown(writer.fd(), SHUT_WR);
    EventFrame frame{};
    EXPECT_THROW((void)receive_exact(reader, frame), std::runtime_error);
}
TEST(TcpTransport, SendsLargePayloadWithSmallKernelBuffer) {
    auto [writer, reader] = socket_pair();
    const int size = 1024;
    ASSERT_EQ(::setsockopt(writer.fd(), SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)), 0);
    std::vector<std::uint8_t> source(1024U * 1024U);
    for (std::size_t i = 0; i < source.size(); ++i) source[i] = static_cast<std::uint8_t>(i);
    auto producer = std::async(std::launch::async, [&] { send_all(writer, source); });
    std::vector<std::uint8_t> received(source.size());
    EXPECT_TRUE(receive_exact(reader, received));
    producer.get();
    EXPECT_EQ(received, source);
}
TEST(TcpTransport, ClosedPeerThrowsInsteadOfRaisingSigpipe) {
    auto [writer, reader] = socket_pair();
    reader = Socket{};
    const auto ack = encode_received_ack(1);
    EXPECT_THROW(send_all(writer, ack), std::system_error);
}
TEST(TcpTransport, MoveTransfersFdOwnership) {
    auto [writer, reader] = socket_pair();
    const int fd = writer.fd();
    auto moved = std::move(writer);
    EXPECT_EQ(writer.fd(), -1);
    EXPECT_EQ(moved.fd(), fd);
    writer = std::move(moved);
    EXPECT_EQ(moved.fd(), -1);
    EXPECT_EQ(writer.fd(), fd);
    send_all(writer, encode_received_ack(99));
    AckFrame ack{};
    ASSERT_TRUE(receive_exact(reader, ack));
    EXPECT_EQ(decode_received_ack(ack), 99U);
}
TEST(TcpTransport, LoopbackTcpEventAndAck) {
    auto listener = listen_tcp("127.0.0.1", 0);
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    ASSERT_EQ(::getsockname(listener.fd(), reinterpret_cast<sockaddr*>(&address), &length), 0);
    auto client = connect_tcp("127.0.0.1", ntohs(address.sin_port));
    auto peer = accept_tcp(listener);
    const timeval timeout{5, 0};
    for (const auto fd : {client.fd(), peer.fd()}) {
        ASSERT_EQ(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)), 0);
        ASSERT_EQ(::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)), 0);
    }
    const MarketEvent event{9, 55, AddOrder{7, Side::Bid, 100, 5}};
    send_all(client, encode_event(event));
    EventFrame frame{};
    ASSERT_TRUE(receive_exact(peer, frame));
    ASSERT_EQ(decode_event(frame), event);
    send_all(peer, encode_received_ack(event.sequence));
    AckFrame ack{};
    ASSERT_TRUE(receive_exact(client, ack));
    EXPECT_EQ(decode_received_ack(ack), event.sequence);
}
}  // namespace
}  // namespace order_book::net
