#pragma once
#include <charconv>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

class TradingArgs final {
   public:
    TradingArgs(int argc, char** argv) {
        for (int i = 1; i < argc; i += 2) {
            const std::string key = argv[i];
            if (!key.starts_with("--") || i + 1 == argc ||
                !values_.emplace(key, argv[i + 1]).second)
                throw std::invalid_argument("Expected unique --option value pairs");
        }
    }
    std::string text(const std::string& key, std::string fallback) {
        const auto it = values_.find(key);
        if (it == values_.end()) return fallback;
        auto value = it->second;
        values_.erase(it);
        return value;
    }
    std::uint64_t number(const std::string& key, std::uint64_t fallback, std::uint64_t low,
                         std::uint64_t high) {
        const auto s = text(key, std::to_string(fallback));
        std::uint64_t n{};
        const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
        if (error != std::errc{} || end != s.data() + s.size() || n < low || n > high)
            throw std::invalid_argument("Invalid numeric value for " + key);
        return n;
    }
    void finish() const {
        if (!values_.empty())
            throw std::invalid_argument("Unknown option: " + values_.begin()->first);
    }

   private:
    std::map<std::string, std::string> values_;
};
