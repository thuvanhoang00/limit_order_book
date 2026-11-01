#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace order_book::measurement {
// Exact nanosecond frequencies: a dense 0..100us region and sparse outliers.
// Storage grows with distinct OUTLIER durations, not the number of input records.
class LatencyHistogram final {
   public:
    void observe(std::uint64_t ns) {
        if (ns < dense_.size()) ++dense_[static_cast<std::size_t>(ns)];
        else ++outliers_[ns];
        ++count_;
        total_ += ns;
        min_ = std::min(min_, ns);
        max_ = std::max(max_, ns);
    }
    [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
    [[nodiscard]] std::uint64_t quantile(unsigned permille) const {
        if (permille == 0 || permille > 1000 || count_ == 0)
            throw std::invalid_argument("Nonempty histogram and quantile 1..1000 required");
        const auto rank = count_ / 1000 * permille +
                          (count_ % 1000 * permille + 999) / 1000;
        std::uint64_t accumulated{};
        for (std::size_t ns = 0; ns != dense_.size(); ++ns) {
            accumulated += dense_[ns];
            if (accumulated >= rank) return ns;
        }
        for (const auto& [ns, frequency] : outliers_) {
            accumulated += frequency;
            if (accumulated >= rank) return ns;
        }
        throw std::logic_error("Latency histogram count mismatch");
    }
    [[nodiscard]] std::string summary() const {
        if (!count_) return "{\"count\":0,\"mean_ns\":null,\"min_ns\":null,\"p50_ns\":null,"
                            "\"p95_ns\":null,\"p99_ns\":null,\"p999_ns\":null,\"max_ns\":null}";
        return "{\"count\":" + std::to_string(count_) + ",\"mean_ns\":" +
               std::to_string(total_ / count_) + ",\"min_ns\":" + std::to_string(min_) +
               ",\"p50_ns\":" + std::to_string(quantile(500)) +
               ",\"p95_ns\":" + std::to_string(quantile(950)) +
               ",\"p99_ns\":" + std::to_string(quantile(990)) +
               ",\"p999_ns\":" + std::to_string(quantile(999)) +
               ",\"max_ns\":" + std::to_string(max_) + "}";
    }
    void frequencies(std::ostream& out) const {
        out << '[';
        bool first = true;
        const auto append = [&](std::uint64_t ns, std::uint64_t frequency) {
            if (!frequency) return;
            if (!first) out << ',';
            first = false;
            out << '[' << ns << ',' << frequency << ']';
        };
        for (std::size_t ns = 0; ns != dense_.size(); ++ns) append(ns, dense_[ns]);
        for (const auto& [ns, frequency] : outliers_) append(ns, frequency);
        out << ']';
    }

   private:
    std::vector<std::uint64_t> dense_ = std::vector<std::uint64_t>(100'001);
    std::map<std::uint64_t, std::uint64_t> outliers_;
    std::uint64_t count_{}, min_{std::numeric_limits<std::uint64_t>::max()}, max_{};
    long double total_{};
};
}  // namespace order_book::measurement
