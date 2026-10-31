#pragma once

#include <netinet/in.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "order_book/net/socket.hpp"

namespace order_book::net {

// Linux IPv4 multicast. Numeric addresses only; explicitly choose an interface.
// One send call = one datagram. There is no connection, ACK or retransmission.
class MulticastPublisher final {
   public:
    MulticastPublisher(const char* group, std::uint16_t port,
                       const char* interface_ipv4 = "127.0.0.1");
    void send(std::span<const std::uint8_t> bytes) const;

   private:
    Socket socket_;
    sockaddr_in destination_{};
};

struct Datagram {
    std::size_t size{}; // Bytes copied into the supplied buffer.
    bool truncated{};  // Original datagram exceeded the buffer: discard it.
};

class MulticastSubscriber final {
   public:
    // port=0 lets the kernel choose a port for tests. Closing the socket drops
    // its group membership. timeout is per blocking recvmsg(), including retries
    // after EINTR; it is not a deadline for the complete feed/session.
    MulticastSubscriber(const char* group, std::uint16_t port,
                        const char* interface_ipv4 = "127.0.0.1",
                        std::chrono::milliseconds timeout = std::chrono::seconds(3));
    [[nodiscard]] std::uint16_t local_port() const;
    [[nodiscard]] int fd() const noexcept { return socket_.fd(); }
    // nullopt = receive timeout. A zero-byte datagram is a message, never EOF.
    // Exactly one datagram is consumed; excess bytes are discarded by the kernel.
    [[nodiscard]] std::optional<Datagram> receive(std::span<std::uint8_t> bytes) const;

   private:
    Socket socket_;
};

}  // namespace order_book::net
