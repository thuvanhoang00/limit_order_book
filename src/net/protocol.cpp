#include "order_book/net/protocol.hpp"

#include <cstddef>
#include <stdexcept>

namespace order_book::net {
namespace {
constexpr std::uint32_t magic = 0x4c4f4231U;  // LOB1
// Explicit stable wire mapping; independent of enum declaration order.
constexpr std::array results{ApplyResult::Ok,
                             ApplyResult::DuplicateOrder,
                             ApplyResult::UnknownOrder,
                             ApplyResult::InvalidPrice,
                             ApplyResult::InvalidQuantity,
                             ApplyResult::UnknownInstrument,
                             ApplyResult::QuantityExceedsRemaining,
                             ApplyResult::ParseError,
                             ApplyResult::NotImplemented,
                             ApplyResult::AllocationFailed};
void put(std::uint8_t* out, std::uint64_t value, std::size_t width) noexcept {
    for (std::size_t i = 0; i < width; ++i) {
        out[width - 1U - i] = static_cast<std::uint8_t>(value & 0xffU);
        value >>= 8U;
    }
}
std::uint64_t get(const std::uint8_t* in, std::size_t width) noexcept {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) value = (value << 8U) | in[i];
    return value;
}
bool valid_header(const std::uint8_t* frame) noexcept {
    return get(frame, 4) == magic && frame[4] == 1 && frame[7] == 0;
}
}  // namespace

EventFrame encode_event(const MarketEvent& event) {
    EventFrame frame{};
    put(frame.data(), magic, 4);
    frame[4] = 1;
    put(frame.data() + 8, event.sequence, 8);
    put(frame.data() + 16, event.instrument_id, 8);
    if (const auto* add = std::get_if<AddOrder>(&event.payload)) {
        if (add->side != Side::Bid && add->side != Side::Ask)
            throw std::invalid_argument("Invalid order side");
        frame[5] = 1;
        frame[6] = add->side == Side::Bid ? 1 : 2;
        put(frame.data() + 24, add->order_id, 8);
        put(frame.data() + 32, add->price, 4);
        put(frame.data() + 36, add->quantity, 4);
    } else if (const auto* cancel = std::get_if<CancelOrder>(&event.payload)) {
        frame[5] = 2;
        put(frame.data() + 24, cancel->order_id, 8);
    } else {
        const auto& execute = std::get<ExecuteOrder>(event.payload);
        frame[5] = 3;
        put(frame.data() + 24, execute.order_id, 8);
        put(frame.data() + 36, execute.quantity, 4);
    }
    return frame;
}
std::optional<MarketEvent> decode_event(const EventFrame& frame) noexcept {
    if (!valid_header(frame.data())) return std::nullopt;
    const auto sequence = get(frame.data() + 8, 8);
    const auto instrument = get(frame.data() + 16, 8);
    const auto order = get(frame.data() + 24, 8);
    const auto price = static_cast<Price>(get(frame.data() + 32, 4));
    const auto quantity = static_cast<Quantity>(get(frame.data() + 36, 4));
    switch (frame[5]) {
        case 1:
            if (frame[6] != 1 && frame[6] != 2) return std::nullopt;
            return MarketEvent{
                sequence, instrument,
                AddOrder{order, frame[6] == 1 ? Side::Bid : Side::Ask, price, quantity}};
        case 2:
            if (frame[6] != 0 || price != 0 || quantity != 0) return std::nullopt;
            return MarketEvent{sequence, instrument, CancelOrder{order}};
        case 3:
            if (frame[6] != 0 || price != 0) return std::nullopt;
            return MarketEvent{sequence, instrument, ExecuteOrder{order, quantity}};
        default:
            return std::nullopt;
    }
}
AckFrame encode_received_ack(Sequence sequence) noexcept {
    AckFrame frame{};
    put(frame.data(), magic, 4);
    frame[4] = 1;
    frame[5] = 128;
    put(frame.data() + 8, sequence, 8);
    return frame;
}
std::optional<Sequence> decode_received_ack(const AckFrame& frame) noexcept {
    if (!valid_header(frame.data()) || frame[5] != 128 || frame[6] != 0) return std::nullopt;
    return get(frame.data() + 8, 8);
}
AckFrame encode_apply_ack(ApplyAck ack) {
    auto frame = encode_received_ack(ack.sequence);
    frame[5] = 129;
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (results[i] == ack.result) {
            frame[6] = static_cast<std::uint8_t>(i);
            return frame;
        }
    }
    throw std::invalid_argument("Unknown ApplyResult");
}
std::optional<ApplyAck> decode_apply_ack(const AckFrame& frame) noexcept {
    if (!valid_header(frame.data()) || frame[5] != 129 || frame[6] >= results.size())
        return std::nullopt;
    return ApplyAck{get(frame.data() + 8, 8), results[frame[6]]};
}
}  // namespace order_book::net
