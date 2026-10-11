#pragma once
#include <cstddef>
#include <limits>
namespace order_book::detail::allocation_probe {
inline constexpr std::size_t disabled = std::numeric_limits<std::size_t>::max();
struct State {
    bool count = false;
    std::size_t calls = 0;
    std::size_t bytes = 0;
    std::size_t fail_after = disabled;
};
extern thread_local State state;
struct Counts {
    std::size_t calls;
    std::size_t bytes;
};
class FailNth {
   public:
    explicit FailNth(std::size_t n) noexcept { state.fail_after = n; }
    ~FailNth() { state.fail_after = disabled; }
    FailNth(const FailNth&) = delete;
    FailNth& operator=(const FailNth&) = delete;
};
class CountScope {
   public:
    CountScope() noexcept {
        state.calls = 0;
        state.bytes = 0;
        state.count = true;
    }
    ~CountScope() { state.count = false; }
    Counts counts() const noexcept { return {state.calls, state.bytes}; }
    CountScope(const CountScope&) = delete;
    CountScope& operator=(const CountScope&) = delete;
};
}  // namespace order_book::detail::allocation_probe
