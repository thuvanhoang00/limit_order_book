#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <source_location>
#include <string>
#include <thread>

#include "order_book/core/events.hpp"
#include "order_book/core/lfqueue.hpp"

namespace order_book {

enum class LogLevel { Debug, Info, Warn, Error, Fatal, Off };
enum class LogEvent {
    AddOrder,
    CancelOrder,
    ExecuteOrder,
    ReplayStarted,
    ReplayFinished,
    ParseFailed,
    InvalidSequence,
    InvariantFailed,
    InputOpenFailed,
    InputReadFailed,
    TcpListening,
    TcpConnected,
    TcpDisconnected,
    TcpFinished,
    TcpTransportFailed,
    TcpProtocolFailed,
    TcpAckSent,
    TcpAckReceived,
    UdpJoined,
    UdpFinished,
    UdpTimeout,
    UdpProtocolFailed,
    UdpTransportFailed,
    UdpStaleDropped,
    UdpGapDetected,
    UdpApplyRejected,
    TradingCommand
};

struct LogRecord {
    std::chrono::system_clock::time_point timestamp{};
    LogLevel level{LogLevel::Debug};
    LogEvent event{LogEvent::AddOrder};
    std::source_location source{};
    Sequence sequence{};
    InstrumentId instrument_id{};
    OrderId order_id{};
    Price price_ticks{};
    Quantity quantity{};
    Side side{Side::Bid};
    ApplyResult result{ApplyResult::Ok};
    std::uint64_t line_number{};
    std::uint16_t port{};
    int error_code{};
};

// Exactly one producer calls try_log()/log_event(). The worker consumes the SPSC
// queue. Stop the producer before close()/destruction; the logger must outlive
// any registry using it. Formatting and file I/O run only on the worker, except
// for the final drain after joining it. Instances append to their supplied path.
class Logger final {
   public:
    explicit Logger(const std::string& path = "order_book.log",
                    LogLevel minimum_level = LogLevel::Debug);
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    // true means accepted, not yet persisted. Full queues drop without blocking.
    [[nodiscard]] bool try_log(
        LogRecord record, std::source_location source = std::source_location::current()) noexcept;
    void log_event(const MarketEvent& event, ApplyResult result,
                   std::source_location source = std::source_location::current()) noexcept;

    // Idempotent; drains accepted records and exposes final write errors.
    void close() noexcept;
    [[nodiscard]] std::uint64_t dropped_count() const noexcept;
    [[nodiscard]] bool has_file_write_error() const noexcept;
    [[nodiscard]] std::uint64_t format_failed_count() const noexcept;

   private:
    static std::string format_record(const LogRecord& record);
    void flush() noexcept;
    void do_work() noexcept;

    LFQueue<LogRecord, 4096> queue_;
    std::ofstream file_;
    LogLevel minimum_level_;
    std::atomic<bool> is_stop_{false};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<bool> file_write_failed_{false};
    std::atomic<std::uint64_t> format_failed_count_{0};
    std::thread worker_;
};

}  // namespace order_book
