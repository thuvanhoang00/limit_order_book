#include "order_book/feed/decoder.hpp"
#include "bytes.hpp"

namespace order_book::feed {
Event Itch50Decoder::decode(std::span<const std::uint8_t> m) {
    using namespace detail;
    require(!failed_, "ITCH decoder requires restart after error");
    try {
        require(!complete_, "ITCH message follows End of Messages");
        require(!m.empty(), "Empty ITCH message");
        std::size_t length{};
        switch (m[0]) {
            case 'S': length = 12; break;
            case 'R': length = 39; break;
            case 'H': length = 25; break;
            case 'Y': length = 20; break;
            case 'L': length = 26; break;
            case 'V': length = 35; break;
            case 'W': length = 12; break;
            case 'K': length = 28; break;
            case 'J': length = 35; break;
            case 'h': length = 21; break;
            case 'A': length = 36; break;
            case 'F': length = 40; break;
            case 'E': length = 31; break;
            case 'C': length = 36; break;
            case 'X': length = 23; break;
            case 'D': length = 19; break;
            case 'U': length = 35; break;
            case 'P': length = 44; break;
            case 'Q': length = 40; break;
            case 'B': length = 19; break;
            case 'I': length = 50; break;
            case 'N': length = 20; break;
            case 'O': length = 48; break;
            default: throw Error("Unsupported ITCH message type " + std::to_string(m[0]));
        }
        require(m.size() == length, "Invalid ITCH message length");
        if (record_ == 0) require(m[0] == 'S' && m[11] == 'O', "ITCH archive must start at Start of Messages");
        Event e;
        e.sequence = record_ + 1;
        e.timestamp_ns = integer(m, 5, 6);
        require(e.timestamp_ns < 86'400'000'000'000ULL, "Invalid ITCH timestamp");
        const auto locate = static_cast<std::uint16_t>(integer(m, 1, 2));
        if (m[0] == 'S') {
            require(std::string("OSQMEC").find(static_cast<char>(m[11])) != std::string::npos,
                    "Invalid ITCH system event");
            if (m[11] == 'O') { e.type = EventType::Clear; symbols_.clear(); }
            if (m[11] == 'C') complete_ = true;
        } else if (m[0] == 'R') {
            require(locate != 0, "Zero stock locate");
            e.type = EventType::Directory;
            e.symbol = symbol(m, 11, 8);
            auto [it, added] = symbols_.emplace(locate, e.symbol);
            require(added || it->second == e.symbol, "Stock locate changed within session");
        } else if (std::string("AFECXDUPQ").find(static_cast<char>(m[0])) != std::string::npos) {
            const auto found = symbols_.find(locate);
            require(found != symbols_.end(), "ITCH order/trade precedes Stock Directory");
            e.symbol = found->second;
            if (m[0] == 'A' || m[0] == 'F') {
                e.type = EventType::Add;
                e.order_id = integer(m, 11, 8);
                e.side = side(m[19]);
                e.quantity = integer(m, 20, 4);
                require(symbol(m, 24, 8) == e.symbol, "ITCH symbol disagrees with locate");
                e.price = integer(m, 32, 4);
            } else if (m[0] == 'U') {
                e.type = EventType::Replace;
                e.order_id = integer(m, 11, 8);
                e.new_order_id = integer(m, 19, 8);
                e.quantity = integer(m, 27, 4);
                e.price = integer(m, 31, 4);
            } else if (m[0] == 'P' || m[0] == 'Q') {
                e.type = EventType::Trade;
                const bool cross = m[0] == 'Q';
                e.quantity = integer(m, cross ? 11 : 20, cross ? 8 : 4);
                require(symbol(m, cross ? 19 : 24, 8) == e.symbol, "ITCH trade symbol mismatch");
                e.price = integer(m, cross ? 27 : 32, 4);
                e.match_id = integer(m, cross ? 31 : 36, 8);
            } else {
                e.order_id = integer(m, 11, 8);
                if (m[0] == 'D') e.type = EventType::Delete;
                else {
                    e.type = m[0] == 'X' ? EventType::Reduce : EventType::Execute;
                    e.quantity = integer(m, 19, 4);
                    if (m[0] != 'X') e.match_id = integer(m, 23, 8);
                    if (m[0] == 'C') {
                        require(m[31] == 'Y' || m[31] == 'N', "Invalid printable flag");
                        e.price = integer(m, 32, 4); // Execution price never reprices the resting order.
                    }
                }
            }
        }
        ++record_;
        return e;
    } catch (...) { failed_ = true; throw; }
}
}  // namespace order_book::feed
