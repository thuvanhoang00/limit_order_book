#include "order_book/trading/journal.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace order_book::trading {
namespace {
[[noreturn]] void fail(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}
void write_all(int fd, const std::uint8_t* data, std::size_t size) {
    while (size != 0) {
        const auto n = ::write(fd, data, size);
        if (n < 0) {
            if (errno == EINTR) continue;
            fail("write journal");
        }
        if (n == 0) throw std::runtime_error("Journal write made no progress");
        data += n;
        size -= static_cast<std::size_t>(n);
    }
}
bool read_all(int fd, std::uint8_t* data, std::size_t size) {
    const auto total = size;
    while (size != 0) {
        const auto n = ::read(fd, data, size);
        if (n < 0) {
            if (errno == EINTR) continue;
            fail("read journal");
        }
        if (n == 0) {
            if (size != total)
                throw std::runtime_error("Torn journal record; restore a verified journal backup");
            return false;
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}
std::uint32_t checksum(const Frame& bytes) {
    std::uint32_t hash = 2166136261U;
    for (auto b : bytes) {
        hash ^= b;
        hash *= 16777619U;
    }
    return hash;
}
void sync(int fd) {
    if (::fdatasync(fd) < 0) fail("fdatasync journal");
}
}  // namespace
Journal::Journal(const std::string& path, std::uint64_t config_hash) {
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd_ < 0) fail("open journal");
    try {
        if (::flock(fd_, LOCK_EX | LOCK_NB) < 0) fail("lock journal");
        std::array<std::uint8_t, 16> expected{'L', 'J', 'R', '1', 1, 0, 0, 0};
        for (std::size_t i = 0; i != 8; ++i)
            expected[15 - i] = static_cast<std::uint8_t>((config_hash >> (8U * i)) & 0xffU);
        decltype(expected) header{};
        if (!read_all(fd_, header.data(), header.size())) {
            write_all(fd_, expected.data(), expected.size());
            sync(fd_);
            auto parent = std::filesystem::path(path).parent_path();
            if (parent.empty()) parent = ".";
            const int directory = ::open(parent.c_str(), O_DIRECTORY | O_RDONLY | O_CLOEXEC);
            if (directory < 0) fail("open journal directory");
            const int result = ::fsync(directory);
            const int saved_errno = errno;
            (void)::close(directory);
            if (result < 0) {
                errno = saved_errno;
                fail("fsync journal directory");
            }
        } else if (header != expected)
            throw std::runtime_error("Journal version/instrument configuration mismatch");
        Frame bytes{};
        while (read_all(fd_, bytes.data(), bytes.size())) {
            std::array<std::uint8_t, 4> crc{};
            if (!read_all(fd_, crc.data(), crc.size()))
                throw std::runtime_error("Missing journal checksum");
            std::uint32_t stored{};
            for (auto b : crc) stored = (stored << 8U) | b;
            const auto m = decode(bytes);
            if (stored != checksum(bytes) || !m || m->kind != Kind::Command ||
                m->action == Action::Status || m->action == Action::Snapshot)
                throw std::runtime_error("Corrupt journal record");
            recovered_.push_back(*m);
        }
    } catch (...) {
        ::close(fd_);
        fd_ = -1;
        throw;
    }
}
Journal::~Journal() {
    if (fd_ >= 0) (void)::close(fd_);
}
void Journal::append(const Message& command) {
    const auto bytes = encode(command);
    const auto hash = checksum(bytes);
    std::array<std::uint8_t, frame_size + 4> record{};
    std::copy(bytes.begin(), bytes.end(), record.begin());
    for (std::size_t i = 0; i != 4; ++i)
        record[frame_size + i] = static_cast<std::uint8_t>((hash >> (24U - 8U * i)) & 0xffU);
    write_all(fd_, record.data(), record.size());
    sync(fd_);
}
}  // namespace order_book::trading
