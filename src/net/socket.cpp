#include "order_book/net/socket.hpp"

#include <unistd.h>
#include <utility>

namespace order_book::net {
Socket::~Socket() {
    if (fd_ >= 0) (void)::close(fd_); // Do not retry close on EINTR on Linux.
}
Socket::Socket(Socket&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) (void)::close(fd_);
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}
}  // namespace order_book::net
