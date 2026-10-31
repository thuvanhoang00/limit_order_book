#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "order_book/core/types.hpp"

namespace order_book::trading {
using ClientId = std::uint32_t;
enum class Kind : std::uint8_t {
    Command = 1,
    Report,
    Delta,
    Heartbeat,
    SnapshotBegin,
    SnapshotOrder,
    SnapshotEnd
};
enum class Action : std::uint8_t { New = 1, Cancel, Replace, CancelAll, Snapshot, Status };
enum class OrderType : std::uint8_t { Limit = 1, Market };
enum class TimeInForce : std::uint8_t { GTC = 1, IOC, FOK };
enum class ReportType : std::uint8_t {
    Accepted = 1,
    Rejected,
    Fill,
    Cancelled,
    Replaced,
    Complete,
    Status
};
enum class UpdateType : std::uint8_t { Add = 1, Cancel, Execute, Reduce, Trade };
enum class Reject : std::uint8_t {
    None,
    InvalidCommand,
    UnknownInstrument,
    InvalidPrice,
    InvalidQuantity,
    DuplicateOrder,
    UnknownOrder,
    Sequence,
    PostOnly,
    Fok,
    SelfTrade,
    RiskQuantity,
    RiskNotional,
    RiskPosition,
    RiskOpenOrders,
    Capacity,
    SessionBusy,
    KillSwitch
};

[[nodiscard]] std::string_view to_string(Reject value) noexcept;
[[nodiscard]] std::string_view to_string(ReportType value) noexcept;

// One value type for bounded SPSC queues. Wire encoding never copies this layout.
struct Message {
    Kind kind{Kind::Command};
    Action action{Action::New};
    Side side{Side::Bid};
    OrderType order_type{OrderType::Limit};
    TimeInForce tif{TimeInForce::GTC};
    ReportType report{ReportType::Accepted};
    Reject reject{Reject::None};
    UpdateType update{UpdateType::Add};
    bool post_only{};
    std::uint64_t epoch{};
    Sequence sequence{};
    ClientId client_id{};
    Price price{};
    std::uint64_t request_id{};
    OrderId order_id{};
    OrderId exchange_id{};
    InstrumentId instrument_id{};
    Quantity quantity{};
    Quantity remaining{};
    std::uint64_t trade_id{};
    friend bool operator==(const Message&, const Message&) = default;
};

inline constexpr std::size_t frame_size = 96;
using Frame = std::array<std::uint8_t, frame_size>;
[[nodiscard]] Frame encode(const Message& message);
[[nodiscard]] std::optional<Message> decode(const Frame& frame) noexcept;
}  // namespace order_book::trading
