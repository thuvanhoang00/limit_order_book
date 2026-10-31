#pragma once

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>

inline std::uint64_t udp_demo_number(std::string_view text, std::uint64_t minimum,
                                      std::uint64_t maximum) {
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        value < minimum || value > maximum)
        throw std::invalid_argument("Invalid numeric argument or value outside allowed range");
    return value;
}
