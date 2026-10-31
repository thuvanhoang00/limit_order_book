#pragma once
#include <charconv>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace trading_cli {
class Args {
   public:
    Args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--help") {
                help = true;
                continue;
            }
            if (!key.starts_with("--") || i + 1 == argc || !values.emplace(key, argv[++i]).second)
                throw std::invalid_argument("Expected unique --option value pairs");
        }
    }
    std::string take(const std::string& name, std::string fallback) {
        const auto it = values.find(name);
        if (it == values.end()) return fallback;
        auto value = it->second;
        values.erase(it);
        return value;
    }
    template <class T>
    static T number(std::string_view text) {
        T value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || error != std::errc{} || end != text.data() + text.size())
            throw std::invalid_argument("Invalid unsigned integer: " + std::string(text));
        return value;
    }
    template <class T>
    T take_number(const std::string& name, T fallback) {
        return number<T>(take(name, std::to_string(fallback)));
    }
    std::vector<std::uint64_t> instruments() {
        auto text = take("--instruments", "1");
        std::vector<std::uint64_t> result;
        std::size_t offset = 0;
        for (;;) {
            const auto end = text.find(',', offset);
            const auto id = number<std::uint64_t>(std::string_view(text).substr(
                offset, end == std::string::npos ? end : end - offset));
            if (!id) throw std::invalid_argument("Instrument IDs must be nonzero");
            result.push_back(id);
            if (end == std::string::npos) break;
            offset = end + 1;
        }
        return result;
    }
    void finish() const {
        if (!values.empty())
            throw std::invalid_argument("Unknown option: " + values.begin()->first);
    }
    bool help{};

   private:
    std::map<std::string, std::string> values;
};
}  // namespace trading_cli
