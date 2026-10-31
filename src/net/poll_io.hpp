#pragma once
// Internal helpers for the trading-system reactors; legacy blocking demos keep
// their original API and protocol.
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <system_error>

namespace order_book::net::detail {
inline void nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::system_error(errno, std::generic_category(), "fcntl nonblocking");
}
inline void no_delay(int fd) {
    const int one = 1;
    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0)
        throw std::system_error(errno, std::generic_category(), "TCP_NODELAY");
}
enum class Read { Again, Frame, Eof };
template <std::size_t N>
Read read_frame(int fd, std::array<std::uint8_t, N>& frame, std::size_t& used) {
    while (used < N) {
        const auto count = ::recv(fd, frame.data() + used, N - used, 0);
        if (count < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return Read::Again;
            throw std::system_error(errno, std::generic_category(), "recv trading frame");
        }
        if (!count) {
            if (used) throw std::runtime_error("Peer disconnected inside trading frame");
            return Read::Eof;
        }
        used += static_cast<std::size_t>(count);
    }
    used = 0;
    return Read::Frame;
}
template <std::size_t N>
void flush(int fd, std::deque<std::array<std::uint8_t, N>>& frames, std::size_t& sent) {
    // Bound each visit so one fast client cannot monopolize the reactor.
    for (unsigned budget = 0; !frames.empty() && budget < 64; ++budget) {
        const auto& frame = frames.front();
        const auto count = ::send(fd, frame.data() + sent, N - sent, MSG_NOSIGNAL);
        if (count < 0) {
            if (errno == EINTR) {
                --budget;
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            throw std::system_error(errno, std::generic_category(), "send trading frame");
        }
        if (!count) throw std::runtime_error("Trading send made no progress");
        sent += static_cast<std::size_t>(count);
        if (sent == N) {
            sent = 0;
            frames.pop_front();
        }
    }
}
}  // namespace order_book::net::detail
