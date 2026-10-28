#pragma once

#include <exception>
#include <iostream>
#include <system_error>

#include "order_book/core/logger.hpp"

inline void tcp_log_failure(order_book::Logger* logger, order_book::LogRecord record,
                            const std::exception& error) noexcept {
    if (!logger) return;
    if (const auto* system = dynamic_cast<const std::system_error*>(&error))
        record.error_code = system->code().value();
    (void)logger->try_log(record);
}

inline bool tcp_finish_log(order_book::Logger& logger) {
    logger.close();
    std::cerr << "log_dropped=" << logger.dropped_count()
              << " log_format_failures=" << logger.format_failed_count()
              << " log_write_failed=" << logger.has_file_write_error() << '\n';
    return !logger.has_file_write_error() && logger.format_failed_count() == 0;
}
