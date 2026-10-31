#pragma once
#include <string>
#include <vector>

#include "order_book/trading/messages.hpp"

namespace order_book::trading {
// Exclusive process lock, checksummed fixed-size records, fdatasync before ACK.
// Strict recovery rejects a torn/corrupt tail rather than guessing an outcome.
class Journal final {
   public:
    Journal(const std::string& path, std::uint64_t config_hash);
    ~Journal();
    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;
    [[nodiscard]] const std::vector<Message>& recovered() const noexcept { return recovered_; }
    void append(const Message& command);

   private:
    int fd_{-1};
    std::vector<Message> recovered_;
};
}  // namespace order_book::trading
