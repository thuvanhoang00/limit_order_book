#include "order_book/feed/decoder.hpp"
#include <limits>
#include "bytes.hpp"

namespace order_book::feed {
bool PitchDecoder::complete() const noexcept {
    bool seen{};
    for (const auto& unit : units_) if (unit.next != 1) {
        seen = true;
        if (!unit.ended) return false;
    }
    return seen && !failed_;
}
std::vector<Event> PitchDecoder::decode(std::span<const std::uint8_t> p) {
    using namespace detail;
    require(!failed_, "PITCH decoder requires restart after error");
    try {
        require(p.size() >= 8 && integer(p, 0, 2, true) == p.size(), "Invalid PITCH packet length");
        const auto count = p[2], channel = p[3];
        const auto sequence = integer(p, 4, 4, true);
        auto state = units_[channel];
        if (count == 0) {
            require(p.size() == 8 && (sequence == state.next || sequence == 0),
                    "PITCH heartbeat sequence gap");
            return {};
        }
        require(sequence == state.next, "PITCH sequence gap/duplicate or mid-session capture");
        require(!state.ended, "PITCH sequenced message follows End of Session");
        require(sequence + count - 1 <= std::numeric_limits<std::uint32_t>::max(),
                "PITCH sequence rollover requires a new decoder/session");
        std::vector<Event> events;
        events.reserve(count);
        std::size_t offset = 8;
        for (unsigned index = 0; index != count; ++index) {
            require(!state.ended, "PITCH message follows End of Session");
            require(offset + 2 <= p.size(), "Truncated PITCH message header");
            const auto length = p[offset];
            require(length >= 6 && length <= p.size() - offset, "Invalid PITCH message length");
            const auto m = p.subspan(offset, length);
            const auto type = m[1];
            const auto number = [&](std::size_t pos, std::size_t size) {
                return integer(m, pos, size, true);
            };
            const auto sized = [&](std::size_t want) {
                require(length == want, "Unexpected PITCH equities message length/type " +
                                            std::to_string(type));
            };
            Event e;
            e.channel = channel;
            e.sequence = sequence + index;
            if (type == 0x20) {
                require(length == 6 || length == 10, "Invalid PITCH Time length");
                state.seconds = number(2, 4);
                require(state.seconds < 86400, "Invalid PITCH seconds since midnight");
                state.timed = true;
                e.timestamp_ns = state.seconds * 1'000'000'000ULL;
            } else {
                require(state.timed, "PITCH message precedes Time");
                const auto nanos = number(2, 4);
                require(nanos < 1'000'000'000ULL, "Invalid PITCH nanosecond offset");
                e.timestamp_ns = state.seconds * 1'000'000'000ULL + nanos;
                switch (type) {
                    case 0x21: case 0x22: case 0x2f: {
                        const bool short_form = type == 0x22, expanded = type == 0x2f;
                        if (expanded) require(length == 40 || length == 41,
                                              "Unsupported expanded PITCH Add (equities only)");
                        else sized(short_form ? 26 : 34);
                        e.type = EventType::Add;
                        e.order_id = number(6, 8);
                        e.side = side(m[14]);
                        e.quantity = number(15, short_form ? 2 : 4);
                        e.symbol = symbol(m, short_form ? 17 : 19, expanded ? 8 : 6);
                        e.price = short_form ? number(23, 2) * 100 : number(expanded ? 27 : 25, 8);
                        break;
                    }
                    case 0x23:
                        sized(26); e.type = EventType::Execute;
                        e.order_id = number(6, 8); e.quantity = number(14, 4);
                        e.match_id = number(18, 8); break;
                    case 0x24:
                        sized(38); e.type = EventType::ExecuteRemaining;
                        e.order_id = number(6, 8); e.quantity = number(14, 4);
                        e.remaining = number(18, 4); e.match_id = number(22, 8);
                        e.price = number(30, 8); break;
                    case 0x25: case 0x26:
                        sized(type == 0x25 ? 18 : 16); e.type = EventType::Reduce;
                        e.order_id = number(6, 8); e.quantity = number(14, type == 0x25 ? 4 : 2);
                        break;
                    case 0x27: case 0x28: {
                        const bool short_form = type == 0x28;
                        sized(short_form ? 19 : 27); e.type = EventType::Replace;
                        e.order_id = e.new_order_id = number(6, 8);
                        e.quantity = number(14, short_form ? 2 : 4);
                        e.price = short_form ? number(16, 2) * 100 : number(18, 8);
                        e.maintain_priority = (m[short_form ? 18 : 26] & 2U) != 0;
                        break;
                    }
                    case 0x29:
                        sized(14); e.type = EventType::Delete; e.order_id = number(6, 8); break;
                    case 0x2a: case 0x2b: case 0x30: {
                        const bool short_form = type == 0x2b, expanded = type == 0x30;
                        sized(short_form ? 33 : expanded ? 43 : 41);
                        e.type = EventType::Trade;
                        e.quantity = number(15, short_form ? 2 : 4);
                        e.symbol = symbol(m, short_form ? 17 : 19, expanded ? 8 : 6);
                        e.price = short_form ? number(23, 2) * 100 : number(expanded ? 27 : 25, 8);
                        e.match_id = number(short_form ? 25 : expanded ? 35 : 33, 8);
                        break;
                    }
                    case 0x97: sized(6); e.type = EventType::Clear; break;
                    case 0x2c: sized(14); break; // Broken trades never restore displayed orders.
                    case 0x2d: sized(6); state.ended = true; break;
                    case 0x31: sized(18); break; // Trading status, not an order mutation.
                    default: throw Error("Unsupported PITCH equities message type " + std::to_string(type));
                }
            }
            events.push_back(std::move(e));
            offset += length;
        }
        require(offset == p.size(), "PITCH message count/trailing bytes mismatch");
        state.next = sequence + count;
        units_[channel] = state; // Commit only after the ENTIRE packet validates.
        return events;
    } catch (...) { failed_ = true; throw; }
}
}  // namespace order_book::feed
