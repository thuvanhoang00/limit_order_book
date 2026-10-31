#include "order_book/trading/messages.hpp"

#include <algorithm>
#include <stdexcept>

namespace order_book::trading {
namespace {
void put(Frame& bytes, std::size_t at, std::uint64_t value, std::size_t count) {
    for (std::size_t i = count; i != 0; --i) {
        bytes[at + i - 1] = static_cast<std::uint8_t>(value & 0xffU);
        value >>= 8U;
    }
}
std::uint64_t get(const Frame& bytes, std::size_t at, std::size_t count) noexcept {
    std::uint64_t value{};
    for (std::size_t i = 0; i != count; ++i) value = (value << 8U) | bytes[at + i];
    return value;
}
}  // namespace
Frame encode(const Message& m) {
    Frame b{};
    put(b, 0, 0x4c545231U, 4);  // LTR1
    b[4] = 1;
    b[5] = static_cast<std::uint8_t>(m.kind);
    b[6] = static_cast<std::uint8_t>(m.action);
    b[7] = m.side == Side::Bid ? 0 : 1;
    b[8] = static_cast<std::uint8_t>(m.order_type);
    b[9] = static_cast<std::uint8_t>(m.tif);
    b[10] = static_cast<std::uint8_t>(m.report);
    b[11] = static_cast<std::uint8_t>(m.reject);
    b[12] = m.post_only ? 1 : 0;
    b[13] = static_cast<std::uint8_t>(m.update);
    put(b, 16, m.epoch, 8);
    put(b, 24, m.sequence, 8);
    put(b, 32, m.client_id, 4);
    put(b, 36, m.price, 4);
    put(b, 40, m.request_id, 8);
    put(b, 48, m.order_id, 8);
    put(b, 56, m.exchange_id, 8);
    put(b, 64, m.instrument_id, 8);
    put(b, 72, m.quantity, 4);
    put(b, 76, m.remaining, 4);
    put(b, 80, m.trade_id, 8);
    if (!decode(b) || (m.side != Side::Bid && m.side != Side::Ask))
        throw std::invalid_argument("Invalid trading message enum");
    return b;
}
std::optional<Message> decode(const Frame& b) noexcept {
    if (get(b, 0, 4) != 0x4c545231U || b[4] != 1 || b[5] < 1 || b[5] > 7 || b[6] < 1 || b[6] > 6 ||
        b[7] > 1 || b[8] < 1 || b[8] > 2 || b[9] < 1 || b[9] > 3 || b[10] < 1 || b[10] > 7 ||
        b[11] > 17 || b[12] > 1 || b[13] < 1 || b[13] > 5 || b[14] != 0 || b[15] != 0 ||
        std::any_of(b.begin() + 88, b.end(), [](auto byte) { return byte != 0; }))
        return std::nullopt;
    Message m;
    m.kind = static_cast<Kind>(b[5]);
    m.action = static_cast<Action>(b[6]);
    m.side = b[7] == 0 ? Side::Bid : Side::Ask;
    m.order_type = static_cast<OrderType>(b[8]);
    m.tif = static_cast<TimeInForce>(b[9]);
    m.report = static_cast<ReportType>(b[10]);
    m.reject = static_cast<Reject>(b[11]);
    m.post_only = b[12] != 0;
    m.update = static_cast<UpdateType>(b[13]);
    m.epoch = get(b, 16, 8);
    m.sequence = get(b, 24, 8);
    m.client_id = static_cast<ClientId>(get(b, 32, 4));
    m.price = static_cast<Price>(get(b, 36, 4));
    m.request_id = get(b, 40, 8);
    m.order_id = get(b, 48, 8);
    m.exchange_id = get(b, 56, 8);
    m.instrument_id = get(b, 64, 8);
    m.quantity = static_cast<Quantity>(get(b, 72, 4));
    m.remaining = static_cast<Quantity>(get(b, 76, 4));
    m.trade_id = get(b, 80, 8);
    return m;
}
std::string_view to_string(Reject r) noexcept {
    constexpr std::array names{"none",          "invalid_command",  "unknown_instrument",
                               "invalid_price", "invalid_quantity", "duplicate_order",
                               "unknown_order", "sequence",         "post_only",
                               "fok",           "self_trade",       "risk_quantity",
                               "risk_notional", "risk_position",    "risk_open_orders",
                               "capacity",      "session_busy",     "kill_switch"};
    const auto i = static_cast<std::size_t>(r);
    return i < names.size() ? names[i] : "unknown";
}
std::string_view to_string(ReportType r) noexcept {
    constexpr std::array names{"unknown",   "accepted", "rejected", "fill",
                               "cancelled", "replaced", "complete", "status"};
    const auto i = static_cast<std::size_t>(r);
    return i < names.size() ? names[i] : "unknown";
}
}  // namespace order_book::trading
