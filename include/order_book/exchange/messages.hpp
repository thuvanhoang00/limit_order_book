#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "order_book/core/lfqueue.hpp"
#include "order_book/core/types.hpp"

namespace order_book::exchange {
using ClientId = std::uint64_t;

enum class RequestType : std::uint8_t { New = 1, Cancel, Replace };
enum class TimeInForce : std::uint8_t { Gtc, Ioc, Fok };
enum class ReportType : std::uint8_t { Accepted = 1, Rejected, Filled, Canceled, Replaced, Done };
enum class Status : std::uint8_t {
    Ok,
    InvalidRequest,
    UnknownInstrument,
    DuplicateOrder,
    UnknownOrder,
    InvalidPrice,
    InvalidQuantity,
    Capacity,
    WouldCross,
    CannotFill,
    SelfTrade,
    RiskRejected
};
enum class UpdateType : std::uint8_t {
    Add = 1,
    Modify,
    Cancel,
    Trade,
    SnapshotBegin,
    SnapshotEnd,
    Heartbeat
};

[[nodiscard]] constexpr std::string_view to_string(Status value) noexcept {
    switch (value) {
        case Status::Ok:
            return "ok";
        case Status::InvalidRequest:
            return "invalid_request";
        case Status::UnknownInstrument:
            return "unknown_instrument";
        case Status::DuplicateOrder:
            return "duplicate_order";
        case Status::UnknownOrder:
            return "unknown_order";
        case Status::InvalidPrice:
            return "invalid_price";
        case Status::InvalidQuantity:
            return "invalid_quantity";
        case Status::Capacity:
            return "capacity";
        case Status::WouldCross:
            return "would_cross";
        case Status::CannotFill:
            return "cannot_fill";
        case Status::SelfTrade:
            return "self_trade";
        case Status::RiskRejected:
            return "risk_rejected";
    }
    return "unknown";
}
[[nodiscard]] constexpr std::string_view to_string(ReportType value) noexcept {
    switch (value) {
        case ReportType::Accepted:
            return "accepted";
        case ReportType::Rejected:
            return "rejected";
        case ReportType::Filled:
            return "filled";
        case ReportType::Canceled:
            return "canceled";
        case ReportType::Replaced:
            return "replaced";
        case ReportType::Done:
            return "done";
    }
    return "unknown";
}

// Commands are deliberately separate from reconstructive MarketEvent updates.
struct Request {
    RequestType type{RequestType::New};
    ClientId client_id{};
    Sequence sequence{};  // Consecutive per TCP connection, including rejected commands.
    InstrumentId instrument_id{};
    OrderId order_id{};  // Client-scoped, and independent across instruments.
    Side side{Side::Bid};
    Price price{};
    Quantity quantity{};  // Replace specifies the desired remaining quantity.
    TimeInForce tif{TimeInForce::Gtc};
    bool market{};
    bool post_only{};
    bool prevent_self_trade{};
    friend bool operator==(const Request&, const Request&) = default;
};
struct Report {
    ReportType type{ReportType::Rejected};
    Status status{Status::Ok};
    ClientId client_id{};
    Sequence sequence{};          // Assigned by the order server, consecutive per recipient.
    Sequence request_sequence{};  // 0 for unsolicited passive fills.
    InstrumentId instrument_id{};
    OrderId order_id{};
    OrderId market_order_id{};
    std::uint64_t trade_id{};
    Side side{Side::Bid};
    Price price{};
    Quantity quantity{};  // Fill quantity, or quantity canceled.
    Quantity leaves{};
    std::uint64_t session{};
    friend bool operator==(const Report&, const Report&) = default;
};
struct MarketUpdate {
    UpdateType type{UpdateType::Heartbeat};
    std::uint64_t session{};  // Monotonic boot epoch; sequences restart per session.
    Sequence sequence{};      // Snapshot messages use an independent 0-based sequence.
    std::uint64_t snapshot_id{};
    Sequence watermark{};  // Last incremental included in a snapshot/heartbeat.
    InstrumentId instrument_id{};
    OrderId order_id{};  // Exchange-generated identity, never a client order ID.
    std::uint64_t trade_id{};
    Side side{Side::Bid};
    Price price{};
    Quantity quantity{};    // Modify carries new remaining quantity.
    std::uint64_t count{};  // Number of orders in a complete snapshot cycle.
    friend bool operator==(const MarketUpdate&, const MarketUpdate&) = default;
};
struct MatchResult {
    std::vector<Report> reports;
    std::vector<MarketUpdate> updates;
};
using RequestQueue = LFQueue<Request, 4096>;
using ReportQueue = LFQueue<Report, 4096>;
using MarketQueue = LFQueue<MarketUpdate, 4096>;
}  // namespace order_book::exchange
