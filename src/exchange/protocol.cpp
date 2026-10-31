#include "order_book/exchange/protocol.hpp"

#include <algorithm>
#include <stdexcept>

namespace order_book::exchange::wire {
namespace {
void put(std::uint8_t* out, std::uint64_t value, std::size_t width = 8) noexcept {
    for (std::size_t i = 0; i < width; ++i) {
        out[width - i - 1] = static_cast<std::uint8_t>(value & 255U);
        value >>= 8U;
    }
}
std::uint64_t get(const std::uint8_t* in, std::size_t width = 8) noexcept {
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < width; ++i) result = result * 256U + in[i];
    return result;
}
bool header(const std::uint8_t* in, std::uint8_t kind) noexcept {
    return get(in, 4) == 0x4c425432U && in[4] == 1 && in[5] == kind && (in[7] == 1 || in[7] == 2);
}
void header(std::uint8_t* out, std::uint8_t kind, std::uint8_t type, Side side) {
    if (side != Side::Bid && side != Side::Ask) throw std::invalid_argument("Invalid side");
    put(out, 0x4c425432U, 4);  // LBT2, independent of legacy event ingress.
    out[4] = 1;
    out[5] = kind;
    out[6] = type;
    out[7] = side == Side::Bid ? 1 : 2;
}
bool zero(const std::uint8_t* begin, const std::uint8_t* end) noexcept {
    return std::all_of(begin, end, [](auto byte) { return byte == 0; });
}
}  // namespace
RequestFrame encode(const Request& r) {
    RequestFrame f{};
    header(f.data(), 1, static_cast<std::uint8_t>(r.type), r.side);
    f[8] = static_cast<std::uint8_t>((r.market ? 1 : 0) | (r.post_only ? 2 : 0) |
                                     (r.prevent_self_trade ? 4 : 0));
    f[9] = static_cast<std::uint8_t>(r.tif);
    put(f.data() + 16, r.client_id);
    put(f.data() + 24, r.sequence);
    put(f.data() + 32, r.instrument_id);
    put(f.data() + 40, r.order_id);
    put(f.data() + 48, r.price, 4);
    put(f.data() + 52, r.quantity, 4);
    if (!decode(f)) throw std::invalid_argument("Invalid request encoding");
    return f;
}
std::optional<Request> decode(const RequestFrame& f) noexcept {
    if (!header(f.data(), 1) || f[6] < 1 || f[6] > 3 || f[8] > 7 || f[9] > 2 ||
        !zero(f.data() + 10, f.data() + 16) || !zero(f.data() + 56, f.data() + 64))
        return std::nullopt;
    Request r;
    r.type = static_cast<RequestType>(f[6]);
    r.side = f[7] == 1 ? Side::Bid : Side::Ask;
    r.market = (f[8] & 1) != 0;
    r.post_only = (f[8] & 2) != 0;
    r.prevent_self_trade = (f[8] & 4) != 0;
    r.tif = static_cast<TimeInForce>(f[9]);
    r.client_id = get(f.data() + 16);
    r.sequence = get(f.data() + 24);
    r.instrument_id = get(f.data() + 32);
    r.order_id = get(f.data() + 40);
    r.price = static_cast<Price>(get(f.data() + 48, 4));
    r.quantity = static_cast<Quantity>(get(f.data() + 52, 4));
    return r;
}
ReportFrame encode(const Report& r) {
    ReportFrame f{};
    header(f.data(), 2, static_cast<std::uint8_t>(r.type), r.side);
    f[8] = static_cast<std::uint8_t>(r.status);
    put(f.data() + 16, r.sequence);
    put(f.data() + 24, r.request_sequence);
    put(f.data() + 32, r.client_id);
    put(f.data() + 40, r.instrument_id);
    put(f.data() + 48, r.order_id);
    put(f.data() + 56, r.market_order_id);
    put(f.data() + 64, r.trade_id);
    put(f.data() + 72, r.price, 4);
    put(f.data() + 76, r.quantity, 4);
    put(f.data() + 80, r.leaves, 4);
    put(f.data() + 88, r.session);
    if (!decode(f)) throw std::invalid_argument("Invalid report encoding");
    return f;
}
std::optional<Report> decode(const ReportFrame& f) noexcept {
    if (!header(f.data(), 2) || f[6] < 1 || f[6] > 6 || f[8] > 11 ||
        !zero(f.data() + 9, f.data() + 16) || !zero(f.data() + 84, f.data() + 88))
        return std::nullopt;
    Report r;
    r.type = static_cast<ReportType>(f[6]);
    r.status = static_cast<Status>(f[8]);
    r.side = f[7] == 1 ? Side::Bid : Side::Ask;
    r.sequence = get(f.data() + 16);
    r.request_sequence = get(f.data() + 24);
    r.client_id = get(f.data() + 32);
    r.instrument_id = get(f.data() + 40);
    r.order_id = get(f.data() + 48);
    r.market_order_id = get(f.data() + 56);
    r.trade_id = get(f.data() + 64);
    r.price = static_cast<Price>(get(f.data() + 72, 4));
    r.quantity = static_cast<Quantity>(get(f.data() + 76, 4));
    r.leaves = static_cast<Quantity>(get(f.data() + 80, 4));
    r.session = get(f.data() + 88);
    return r;
}
MarketFrame encode(const MarketUpdate& u) {
    MarketFrame f{};
    header(f.data(), 3, static_cast<std::uint8_t>(u.type), u.side);
    put(f.data() + 8, u.session);
    put(f.data() + 16, u.sequence);
    put(f.data() + 24, u.snapshot_id);
    put(f.data() + 32, u.watermark);
    put(f.data() + 40, u.instrument_id);
    put(f.data() + 48, u.order_id);
    put(f.data() + 56, u.trade_id);
    put(f.data() + 64, u.price, 4);
    put(f.data() + 68, u.quantity, 4);
    put(f.data() + 72, u.count);
    if (!decode(f)) throw std::invalid_argument("Invalid market update encoding");
    return f;
}
std::optional<MarketUpdate> decode(const MarketFrame& f) noexcept {
    if (!header(f.data(), 3) || f[6] < 1 || f[6] > 7) return std::nullopt;
    MarketUpdate u;
    u.type = static_cast<UpdateType>(f[6]);
    u.side = f[7] == 1 ? Side::Bid : Side::Ask;
    u.session = get(f.data() + 8);
    u.sequence = get(f.data() + 16);
    u.snapshot_id = get(f.data() + 24);
    u.watermark = get(f.data() + 32);
    u.instrument_id = get(f.data() + 40);
    u.order_id = get(f.data() + 48);
    u.trade_id = get(f.data() + 56);
    u.price = static_cast<Price>(get(f.data() + 64, 4));
    u.quantity = static_cast<Quantity>(get(f.data() + 68, 4));
    u.count = get(f.data() + 72);
    if (!u.session) return std::nullopt;
    if (u.type == UpdateType::SnapshotBegin || u.type == UpdateType::SnapshotEnd) {
        if (!u.snapshot_id || u.instrument_id || u.order_id || u.trade_id || u.price ||
            u.quantity || (u.type == UpdateType::SnapshotBegin && u.sequence))
            return std::nullopt;
    } else if (u.type == UpdateType::Heartbeat) {
        if (u.snapshot_id || u.sequence != u.watermark || u.instrument_id || u.order_id ||
            u.trade_id || u.price || u.quantity || u.count)
            return std::nullopt;
    } else {
        if (!u.sequence || !u.instrument_id || !u.price || u.count ||
            (!u.snapshot_id && u.watermark) || (u.snapshot_id && u.type != UpdateType::Add))
            return std::nullopt;
        if (u.type == UpdateType::Trade) {
            if (!u.trade_id || !u.quantity || u.order_id) return std::nullopt;
        } else if (!u.order_id || u.trade_id || (u.type != UpdateType::Cancel && !u.quantity))
            return std::nullopt;
    }
    return u;
}
}  // namespace order_book::exchange::wire
