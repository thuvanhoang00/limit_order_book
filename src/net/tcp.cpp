#include "order_book/net/tcp.hpp"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace order_book::net {
namespace {
[[noreturn]] void fail(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}
sockaddr_in endpoint(const char* ip, std::uint16_t port) {
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip, &result.sin_addr) != 1)
        throw std::invalid_argument("Expected numeric IPv4 address");
    return result;
}
void no_delay(const Socket& socket) {
    const int enabled = 1;
    if (::setsockopt(socket.fd(), IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) < 0)
        fail("setsockopt TCP_NODELAY");
}
Socket make_socket() {
    Socket result(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (result.fd() < 0) fail("socket");
    return result;
}
std::size_t syscall_size(std::size_t size) {
    return std::min(size, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
}
}  // namespace

Socket listen_tcp(const char* address, std::uint16_t port) {
    const auto addr = endpoint(address, port);
    auto socket = make_socket();
    const int enabled = 1;
    if (::setsockopt(socket.fd(), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0)
        fail("setsockopt SO_REUSEADDR");
    if (::bind(socket.fd(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0)
        fail("bind");
    if (::listen(socket.fd(), 1) < 0) fail("listen");
    return socket;
}
Socket accept_tcp(const Socket& listener) {
    int fd;
    do {
        fd = ::accept4(listener.fd(), nullptr, nullptr, SOCK_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) fail("accept4");
    Socket socket(fd);
    no_delay(socket);
    return socket;
}
Socket connect_tcp(const char* address, std::uint16_t port) {
    const auto addr = endpoint(address, port);
    auto socket = make_socket();
    // An interrupted connect also discards this socket; don't blindly reconnect
    // an fd whose connection establishment may still be in progress.
    if (::connect(socket.fd(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0)
        fail("connect");
    no_delay(socket);
    return socket;
}
void send_all(const Socket& socket, std::span<const std::uint8_t> bytes) {
    while (!bytes.empty()) {
        const auto sent =
            ::send(socket.fd(), bytes.data(), syscall_size(bytes.size()), MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            fail("send");
        }
        if (sent == 0) throw std::runtime_error("send made no progress");
        bytes = bytes.subspan(static_cast<std::size_t>(sent));
    }
}
bool receive_exact(const Socket& socket, std::span<std::uint8_t> bytes) {
    const auto total = bytes.size();
    while (!bytes.empty()) {
        const auto received = ::recv(socket.fd(), bytes.data(), syscall_size(bytes.size()), 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            fail("recv");
        }
        if (received == 0) {
            if (bytes.size() == total) return false;
            throw std::runtime_error("Peer disconnected inside a frame");
        }
        bytes = bytes.subspan(static_cast<std::size_t>(received));
    }
    return true;
}
}  // namespace order_book::net
