#include "order_book/core/logger.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace order_book {
namespace {
const char* level_name(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        case LogLevel::Off:
            return "OFF";
    }
    return "UNKNOWN";
}

const char* event_name(LogEvent event) noexcept {
    switch (event) {
        case LogEvent::AddOrder:
            return "AddOrder";
        case LogEvent::CancelOrder:
            return "CancelOrder";
        case LogEvent::ExecuteOrder:
            return "ExecuteOrder";
        case LogEvent::ReplayStarted:
            return "ReplayStarted";
        case LogEvent::ReplayFinished:
            return "ReplayFinished";
        case LogEvent::ParseFailed:
            return "ParseFailed";
        case LogEvent::InvalidSequence:
            return "InvalidSequence";
        case LogEvent::InvariantFailed:
            return "InvariantFailed";
        case LogEvent::InputOpenFailed:
            return "InputOpenFailed";
        case LogEvent::InputReadFailed:
            return "InputReadFailed";
        case LogEvent::TcpListening:
            return "TcpListening";
        case LogEvent::TcpConnected:
            return "TcpConnected";
        case LogEvent::TcpDisconnected:
            return "TcpDisconnected";
        case LogEvent::TcpFinished:
            return "TcpFinished";
        case LogEvent::TcpTransportFailed:
            return "TcpTransportFailed";
        case LogEvent::TcpProtocolFailed:
            return "TcpProtocolFailed";
        case LogEvent::TcpAckSent:
            return "TcpAckSent";
        case LogEvent::TcpAckReceived:
            return "TcpAckReceived";
        case LogEvent::UdpJoined:
            return "UdpJoined";
        case LogEvent::UdpFinished:
            return "UdpFinished";
        case LogEvent::UdpTimeout:
            return "UdpTimeout";
        case LogEvent::UdpProtocolFailed:
            return "UdpProtocolFailed";
        case LogEvent::UdpTransportFailed:
            return "UdpTransportFailed";
        case LogEvent::UdpStaleDropped:
            return "UdpStaleDropped";
        case LogEvent::UdpGapDetected:
            return "UdpGapDetected";
        case LogEvent::UdpApplyRejected:
            return "UdpApplyRejected";
        case LogEvent::ExchangeRequest: return "ExchangeRequest";
        case LogEvent::ExchangeReport: return "ExchangeReport";
        case LogEvent::ExchangeMarket: return "ExchangeMarket";
        case LogEvent::TradingRequest: return "TradingRequest";
        case LogEvent::TradingReport: return "TradingReport";
        case LogEvent::TradingRecovery: return "TradingRecovery";
    }
    return "Unknown";
}
}  // namespace

Logger::Logger(const std::string& path, LogLevel minimum_level)
    : file_(path, std::ios::out | std::ios::app), minimum_level_(minimum_level) {
    if (!file_.is_open()) throw std::runtime_error("Failed to open log file: " + path);
    worker_ = std::thread([this] { do_work(); });
}

Logger::~Logger() { close(); }

void Logger::close() noexcept {
    is_stop_.store(true, std::memory_order_release);
    if (worker_.joinable()) {
        worker_.join();
        if (!has_file_write_error()) flush();
        file_.close();
        if (file_.fail()) file_write_failed_.store(true, std::memory_order_relaxed);
    }
}

bool Logger::try_log(LogRecord record, std::source_location source) noexcept {
    if (is_stop_.load(std::memory_order_acquire) || record.level == LogLevel::Off ||
        record.level < minimum_level_ || has_file_write_error())
        return false;
    record.timestamp = std::chrono::system_clock::now();
    record.source = source;
    if (!queue_.try_push(record)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void Logger::log_event(const MarketEvent& event, ApplyResult result,
                       std::source_location source) noexcept {
    LogRecord record;
    record.level = result == ApplyResult::Ok                 ? LogLevel::Debug
                   : result == ApplyResult::AllocationFailed ? LogLevel::Error
                                                             : LogLevel::Warn;
    record.sequence = event.sequence;
    record.instrument_id = event.instrument_id;
    record.result = result;
    std::visit(
        [&record](const auto& payload) {
            using Payload = std::decay_t<decltype(payload)>;
            record.order_id = payload.order_id;
            if constexpr (std::is_same_v<Payload, order_book::AddOrder>) {
                record.event = LogEvent::AddOrder;
                record.price_ticks = payload.price;
                record.quantity = payload.quantity;
                record.side = payload.side;
            } else if constexpr (std::is_same_v<Payload, order_book::CancelOrder>) {
                record.event = LogEvent::CancelOrder;
            } else {
                record.event = LogEvent::ExecuteOrder;
                record.quantity = payload.quantity;
            }
        },
        event.payload);
    (void)try_log(record, source);
}

std::uint64_t Logger::dropped_count() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
}
bool Logger::has_file_write_error() const noexcept {
    return file_write_failed_.load(std::memory_order_relaxed);
}
std::uint64_t Logger::format_failed_count() const noexcept {
    return format_failed_count_.load(std::memory_order_relaxed);
}

std::string Logger::format_record(const LogRecord& record) {
    const auto time = std::chrono::system_clock::to_time_t(record.timestamp);
    std::tm local_time{};
    if (localtime_r(&time, &local_time) == nullptr)
        throw std::runtime_error("Failed to format log timestamp");
    std::ostringstream out;
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << '[' << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << "] " << '['
        << level_name(record.level) << "] " << record.source.file_name() << ':'
        << record.source.line() << ' ' << event_name(record.event)
        << " sequence=" << record.sequence << " instrument_id=" << record.instrument_id
        << " order_id=" << record.order_id << " price_ticks=" << record.price_ticks
        << " qty=" << record.quantity;
    if (record.event == LogEvent::AddOrder || record.event == LogEvent::CancelOrder ||
        record.event == LogEvent::ExecuteOrder || record.event == LogEvent::ParseFailed ||
        record.event == LogEvent::TcpAckSent || record.event == LogEvent::TcpAckReceived) {
        out << " result=" << to_string(record.result);
    }
    out << " line=" << record.line_number;
    if (record.event == LogEvent::AddOrder) out << " side=" << to_string(record.side);
    if (record.port != 0) out << " port=" << record.port;
    if (record.error_code != 0) out << " error_code=" << record.error_code;
    if (record.event == LogEvent::ExchangeRequest || record.event == LogEvent::ExchangeReport ||
        record.event == LogEvent::ExchangeMarket || record.event == LogEvent::TradingRequest ||
        record.event == LogEvent::TradingReport || record.event == LogEvent::TradingRecovery)
        out << " client_id=" << record.client_id << " message_type=" << unsigned(record.message_type)
            << " message_status=" << unsigned(record.message_status) << " side=" << to_string(record.side);
    return out.str();
}

void Logger::flush() noexcept {
    LogRecord record;
    bool wrote = false;
    while (queue_.try_pop(record)) {
        std::string text;
        try {
            text = format_record(record);
        } catch (...) {
            format_failed_count_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        file_ << text << '\n';
        if (!file_) {
            file_write_failed_.store(true, std::memory_order_relaxed);
            return;
        }
        wrote = true;
    }
    if (wrote) {
        file_.flush();
        if (!file_) file_write_failed_.store(true, std::memory_order_relaxed);
    }
}

void Logger::do_work() noexcept {
    while (!is_stop_.load(std::memory_order_acquire) && !has_file_write_error()) {
        flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
}  // namespace order_book
