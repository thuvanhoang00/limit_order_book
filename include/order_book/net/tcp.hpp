#pragma once

#include <cstdint>
#include <span>

#include "order_book/net/socket.hpp"

namespace order_book::net {

// Linux, blocking IPv4 sockets. Addresses must be numeric (no DNS).
[[nodiscard]] Socket listen_tcp(const char* address, std::uint16_t port);
[[nodiscard]] Socket accept_tcp(const Socket& listener);
[[nodiscard]] Socket connect_tcp(const char* address, std::uint16_t port);

// Retry EINTR and advance by the actual byte count. Exceptions require closing
// the connection: the caller cannot safely restart an interrupted frame.
void send_all(const Socket& socket, std::span<const std::uint8_t> bytes);
// false = EOF before any byte; EOF inside a frame throws runtime_error.
[[nodiscard]] bool receive_exact(const Socket& socket, std::span<std::uint8_t> bytes);

}  // namespace order_book::net
