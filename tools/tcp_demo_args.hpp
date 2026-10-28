#pragma once
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string_view>

inline std::uint16_t tcp_demo_port(std::string_view text) {
    unsigned port{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), port);
    if (error != std::errc{} || end != text.data() + text.size() || port == 0 || port > 65535)
        throw std::invalid_argument("Port must be in 1..65535");
    return static_cast<std::uint16_t>(port);
}
