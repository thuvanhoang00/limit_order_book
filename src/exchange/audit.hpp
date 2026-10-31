#pragma once
#include "order_book/core/logger.hpp"
#include "order_book/exchange/messages.hpp"

namespace order_book::exchange::detail {
inline void audit(Logger* log, LogEvent event, const Request& r) noexcept {
    if (!log) return;
    LogRecord record;
    record.event = event;
    record.sequence = r.sequence;
    record.instrument_id = r.instrument_id;
    record.order_id = r.order_id;
    record.client_id = r.client_id;
    record.price_ticks = r.price;
    record.quantity = r.quantity;
    record.side = r.side;
    record.message_type = static_cast<std::uint8_t>(r.type);
    (void)log->try_log(record);
}
inline void audit(Logger* log, LogEvent event, const Report& r) noexcept {
    if (!log) return;
    LogRecord record;
    record.event = event;
    record.sequence = r.request_sequence;
    record.instrument_id = r.instrument_id;
    record.order_id = r.order_id;
    record.client_id = r.client_id;
    record.price_ticks = r.price;
    record.quantity = r.quantity;
    record.side = r.side;
    record.message_type = static_cast<std::uint8_t>(r.type);
    record.message_status = static_cast<std::uint8_t>(r.status);
    record.level = r.status == Status::Ok ? LogLevel::Debug : LogLevel::Warn;
    (void)log->try_log(record);
}
inline void audit(Logger* log, LogEvent event, const MarketUpdate& u) noexcept {
    if (!log) return;
    LogRecord record;
    record.event = event;
    record.sequence = u.sequence;
    record.instrument_id = u.instrument_id;
    record.order_id = u.order_id;
    record.price_ticks = u.price;
    record.quantity = u.quantity;
    record.side = u.side;
    record.message_type = static_cast<std::uint8_t>(u.type);
    (void)log->try_log(record);
}
inline bool log_failed(const Logger* log) noexcept {
    return log && (log->has_file_write_error() || log->format_failed_count());
}
}  // namespace order_book::exchange::detail
