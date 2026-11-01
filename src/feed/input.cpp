#include "order_book/feed/input.hpp"
#include <array>
#include <zlib.h>
#include "bytes.hpp"

namespace order_book::feed {
struct Input::Impl {
    gzFile file{};
    Format format;
    Impl(const std::string& path, Format value) : format(value) {
        file = ::gzopen(path.c_str(), "rb");
        if (!file) throw Error("Cannot open feed input: " + path);
        if (::gzbuffer(file, 256 * 1024) != 0) {
            ::gzclose(file);
            throw Error("Cannot allocate gzip input buffer");
        }
    }
    ~Impl() { if (file) ::gzclose(file); }
    bool read(std::span<std::uint8_t> bytes, bool boundary) {
        std::size_t used{};
        while (used < bytes.size()) {
            const auto count = ::gzread(file, bytes.data() + used,
                                         static_cast<unsigned>(bytes.size() - used));
            int code{};
            const auto* error = ::gzerror(file, &code);
            if (count < 0 || (code != Z_OK && code != Z_STREAM_END))
                throw Error("Feed decompression/read error: " + std::string(error));
            if (count == 0) {
                if (boundary && used == 0) return false;
                throw Error("Truncated feed record");
            }
            used += static_cast<std::size_t>(count);
        }
        return true;
    }
};
Input::Input(const std::string& path, Format format) : impl_(std::make_unique<Impl>(path, format)) {}
Input::~Input() = default;
bool Input::next(std::vector<std::uint8_t>& record) {
    std::array<std::uint8_t, 2> prefix{};
    if (!impl_->read(prefix, true)) return false;
    const bool pitch = impl_->format == Format::Pitch;
    const auto length = static_cast<std::size_t>(detail::integer(prefix, 0, 2, pitch));
    detail::require(length >= (pitch ? 8U : 1U), "Invalid archive record length");
    record.resize(length);
    if (pitch) {
        record[0] = prefix[0]; record[1] = prefix[1];
        (void)impl_->read(std::span(record).subspan(2), false);
    } else (void)impl_->read(record, false);
    return true;
}
}  // namespace order_book::feed
