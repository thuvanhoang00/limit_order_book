#include "order_book/net/udp.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace order_book::net {
namespace {
[[noreturn]] void fail(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}
in_addr ipv4(const char* text) {
    in_addr address{};
    if (!text || ::inet_pton(AF_INET, text, &address) != 1)
        throw std::invalid_argument("Expected numeric IPv4 address");
    return address;
}
in_addr multicast_group(const char* text) {
    const auto address = ipv4(text);
    if (!IN_MULTICAST(ntohl(address.s_addr)))
        throw std::invalid_argument("Group must be an IPv4 multicast address (224.0.0.0/4)");
    return address;
}
in_addr interface_address(const char* text) {
    const auto address = ipv4(text);
    const auto host = ntohl(address.s_addr);
    if (host == INADDR_ANY || host == INADDR_BROADCAST || IN_MULTICAST(host))
        throw std::invalid_argument("Choose a concrete local interface IPv4 address");
    return address;
}
Socket make_socket() {
    Socket result(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    if (result.fd() < 0) fail("socket UDP");
    return result;
}
void option(const Socket& socket, int level, int name, const void* value, socklen_t size) {
    if (::setsockopt(socket.fd(), level, name, value, size) < 0) fail("setsockopt multicast");
}
}  // namespace

MulticastPublisher::MulticastPublisher(const char* group, std::uint16_t port,
                                     const char* interface_ipv4) {
    const auto group_address = multicast_group(group);
    const auto local = interface_address(interface_ipv4);
    if (port == 0) throw std::invalid_argument("Destination port must be nonzero");
    socket_ = make_socket();
    option(socket_, IPPROTO_IP, IP_MULTICAST_IF, &local, sizeof(local));
    const unsigned char enabled = 1;
    option(socket_, IPPROTO_IP, IP_MULTICAST_LOOP, &enabled, sizeof(enabled));
    option(socket_, IPPROTO_IP, IP_MULTICAST_TTL, &enabled, sizeof(enabled));
    sockaddr_in source{};
    source.sin_family = AF_INET;
    source.sin_addr = local;
    if (::bind(socket_.fd(), reinterpret_cast<const sockaddr*>(&source), sizeof(source)) < 0)
        fail("bind UDP publisher interface");
    destination_.sin_family = AF_INET;
    destination_.sin_addr = group_address;
    destination_.sin_port = htons(port);
}
void MulticastPublisher::send(std::span<const std::uint8_t> bytes) const {
    if (bytes.size() > 65507U) throw std::length_error("UDP IPv4 payload exceeds 65507 bytes");
    ssize_t sent;
    do {
        sent = ::sendto(socket_.fd(), bytes.data(), bytes.size(), 0,
                        reinterpret_cast<const sockaddr*>(&destination_), sizeof(destination_));
    } while (sent < 0 && errno == EINTR);
    if (sent < 0) fail("sendto multicast");
    // Never send a remainder: that would create another datagram.
    if (static_cast<std::size_t>(sent) != bytes.size())
        throw std::runtime_error("UDP send did not accept the entire datagram");
}
MulticastSubscriber::MulticastSubscriber(const char* group, std::uint16_t port,
                                       const char* interface_ipv4,
                                       std::chrono::milliseconds timeout) {
    const auto group_address = multicast_group(group);
    const auto local = interface_address(interface_ipv4);
    if (timeout.count() <= 0 || timeout > std::chrono::hours(24))
        throw std::invalid_argument("Receive timeout must be in (0, 24 hours]");
    socket_ = make_socket();
    const int enabled = 1;
    option(socket_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    const int disabled = 0;
    option(socket_, IPPROTO_IP, IP_MULTICAST_ALL, &disabled, sizeof(disabled));
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout);
    const timeval wait{static_cast<time_t>(seconds.count()),
                       static_cast<suseconds_t>((timeout - seconds).count() * 1000)};
    option(socket_, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    // Linux group-specific bind avoids receiving unicast/other groups on this port.
    address.sin_addr = group_address;
    address.sin_port = htons(port);
    if (::bind(socket_.fd(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0)
        fail("bind multicast subscriber");
    const ip_mreq membership{group_address, local};
    option(socket_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership));
}
std::uint16_t MulticastSubscriber::local_port() const {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (::getsockname(socket_.fd(), reinterpret_cast<sockaddr*>(&address), &length) < 0)
        fail("getsockname multicast");
    return ntohs(address.sin_port);
}
std::optional<Datagram> MulticastSubscriber::receive(std::span<std::uint8_t> bytes) const {
    if (bytes.empty()) throw std::invalid_argument("Receive buffer must be nonempty");
    iovec buffer{bytes.data(), bytes.size()};
    msghdr message{};
    message.msg_iov = &buffer;
    message.msg_iovlen = 1;
    ssize_t size;
    do {
        message.msg_flags = 0;
        size = ::recvmsg(socket_.fd(), &message, 0);
    } while (size < 0 && errno == EINTR);
    if (size < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return std::nullopt;
        fail("recvmsg multicast");
    }
    return Datagram{static_cast<std::size_t>(size), (message.msg_flags & MSG_TRUNC) != 0};
}
}  // namespace order_book::net
