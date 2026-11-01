#pragma once

#include <sys/resource.h>
#include <time.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <system_error>

#include "order_book/feed/decoder.hpp"

namespace order_book::measurement {
struct FeedOutlier {
    std::uint64_t record{}, timestamp_ns{}, order_id{}, wall_ns{}, cpu_ns{};
    long voluntary{}, involuntary{}, minor_faults{}, major_faults{};
    char message_type{};
};
// Diagnostic-only observation window: rusage -> CPU -> wall -> work -> wall ->
// CPU -> rusage. CPU/resource windows are slightly wider than the wall window;
// these are evidence associated with slow records, not a scheduler call trace.
class FeedOutliers final {
   public:
    using Clock = std::chrono::steady_clock;
    struct Start { rusage usage; std::uint64_t cpu; Clock::time_point wall; };
    FeedOutliers(std::string symbol, std::uint64_t threshold, std::size_t capacity)
        : symbol_(std::move(symbol)), threshold_(threshold), capacity_(capacity) {}
    bool selected(std::span<const std::uint8_t> message) const noexcept {
        if (message.size() < 3 || std::string_view("AFECXDU").find(static_cast<char>(message[0])) ==
                                      std::string_view::npos) return false;
        return locates_[static_cast<std::size_t>(message[1]) * 256 + message[2]];
    }
    void mapping(std::span<const std::uint8_t> message, const feed::Event& event) {
        if (event.type == feed::EventType::Clear) locates_.fill(false);
        if (event.type == feed::EventType::Directory && event.symbol == symbol_ && message.size() >= 3)
            locates_[static_cast<std::size_t>(message[1]) * 256 + message[2]] = true;
    }
    static Start start() {
        Start result;
        if (::getrusage(RUSAGE_THREAD, &result.usage) != 0) fail("getrusage before record");
        result.cpu = cpu_time();
        result.wall = Clock::now();
        return result;
    }
    void finish(const Start& start, Clock::time_point end, std::uint64_t record,
                char type, const feed::Event& event) {
        const auto cpu = cpu_time();
        rusage usage{};
        if (::getrusage(RUSAGE_THREAD, &usage) != 0) fail("getrusage after record");
        observe({record, event.timestamp_ns, event.order_id,
                 static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                     end - start.wall).count()), cpu - start.cpu,
                 usage.ru_nvcsw - start.usage.ru_nvcsw, usage.ru_nivcsw - start.usage.ru_nivcsw,
                 usage.ru_minflt - start.usage.ru_minflt, usage.ru_majflt - start.usage.ru_majflt, type});
    }
    void observe(const FeedOutlier& sample) {
        ++observations_;
        if (sample.wall_ns < threshold_) return;
        ++slow_;
        if (sample.voluntary || sample.involuntary) ++switches_;
        if (sample.minor_faults || sample.major_faults) ++faults_;
        // 50% of wall time absent from thread CPU time, plus a switch counter,
        // is strong associated evidence; no attempt to label every outlier.
        if (sample.cpu_ns <= sample.wall_ns / 2 && (sample.voluntary || sample.involuntary))
            ++off_cpu_switches_;
        if (sample.cpu_ns >= sample.wall_ns - sample.wall_ns / 5) ++cpu_dominant_;
        max_wall_ = std::max(max_wall_, sample.wall_ns);
        if (slowest_.size() < capacity_ || (!slowest_.empty() && sample.wall_ns > slowest_.begin()->first)) {
            slowest_.emplace(sample.wall_ns, sample);
            if (slowest_.size() > capacity_) slowest_.erase(slowest_.begin());
        }
    }
    std::string json() const {
        std::ostringstream out;
        out << "{\"symbol\":" << std::quoted(symbol_) << ",\"scope\":\"selected_itch_decode_apply\","
            << "\"threshold_ns\":" << threshold_ << ",\"observations\":" << observations_
            << ",\"slow_records\":" << slow_ << ",\"slow_with_context_switch\":" << switches_
            << ",\"slow_with_fault\":" << faults_ << ",\"slow_off_cpu_with_switch\":" << off_cpu_switches_
            << ",\"slow_cpu_dominant\":" << cpu_dominant_ << ",\"max_wall_ns\":" << max_wall_
            << ",\"retained\":" << slowest_.size() << ",\"capacity\":" << capacity_ << ",\"records\":[";
        bool first = true;
        for (auto it = slowest_.rbegin(); it != slowest_.rend(); ++it) {
            const auto& sample = it->second;
            if (!first) out << ',';
            first = false;
            out << "{\"record\":" << sample.record << ",\"type\":\"" << sample.message_type
                << "\",\"timestamp_ns\":" << sample.timestamp_ns << ",\"order_id\":" << sample.order_id
                << ",\"wall_ns\":" << sample.wall_ns << ",\"thread_cpu_ns\":" << sample.cpu_ns
                << ",\"estimated_off_cpu_ns\":" << (sample.wall_ns > sample.cpu_ns ? sample.wall_ns - sample.cpu_ns : 0)
                << ",\"voluntary_switches\":" << sample.voluntary << ",\"involuntary_switches\":" << sample.involuntary
                << ",\"minor_faults\":" << sample.minor_faults << ",\"major_faults\":" << sample.major_faults << '}';
        }
        out << "]}";
        return out.str();
    }
   private:
    [[noreturn]] static void fail(const char* message) {
        throw std::system_error(errno, std::generic_category(), message);
    }
    static std::uint64_t cpu_time() {
        timespec value{};
        if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) fail("thread CPU clock");
        return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000ULL +
               static_cast<std::uint64_t>(value.tv_nsec);
    }
    std::string symbol_;
    std::uint64_t threshold_, observations_{}, slow_{}, switches_{}, faults_{}, off_cpu_switches_{},
                  cpu_dominant_{}, max_wall_{};
    std::size_t capacity_;
    std::array<bool, 65536> locates_{};
    std::multimap<std::uint64_t, FeedOutlier> slowest_;
};
}  // namespace order_book::measurement
