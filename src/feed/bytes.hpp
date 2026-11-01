#pragma once
#include <span>
#include "order_book/feed/decoder.hpp"

namespace order_book::feed::detail {
inline void require(bool value, const char* message) {
    if (!value) throw Error(message);
}
inline void require(bool value, const std::string& message) {
    if (!value) throw Error(message);
}
inline std::uint64_t integer(std::span<const std::uint8_t> b, std::size_t pos,
                             std::size_t size, bool little = false) {
    require(size <= 8 && pos <= b.size() && size <= b.size() - pos, "Truncated integer");
    std::uint64_t value{};
    for (std::size_t i = 0; i != size; ++i)
        value = (value << 8U) | b[pos + (little ? size - 1 - i : i)];
    return value;
}
inline std::string symbol(std::span<const std::uint8_t> b, std::size_t pos, std::size_t size) {
    require(pos <= b.size() && size <= b.size() - pos, "Truncated symbol");
    std::string result;
    for (std::size_t i = 0; i != size; ++i) {
        require(b[pos + i] >= 32 && b[pos + i] <= 126, "Non-ASCII symbol");
        result.push_back(static_cast<char>(b[pos + i]));
    }
    while (!result.empty() && result.back() == ' ') result.pop_back();
    require(!result.empty() && result.front() != ' ' && result.find(' ') == std::string::npos,
            "Empty or malformed symbol");
    return result;
}
inline Side side(std::uint8_t c) {
    require(c == 'B' || c == 'S', "Invalid order side");
    return c == 'B' ? Side::Bid : Side::Ask;
}
}  // namespace order_book::feed::detail
