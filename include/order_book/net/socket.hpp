#pragma once

namespace order_book::net {

// Unique fd ownership, shared by TCP and UDP. One thread owns each socket's I/O.
class Socket final {
   public:
    explicit Socket(int fd = -1) noexcept : fd_(fd) {}
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;
    [[nodiscard]] int fd() const noexcept { return fd_; }

   private:
    int fd_;
};

}  // namespace order_book::net
