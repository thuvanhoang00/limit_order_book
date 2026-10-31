#pragma once
#include <map>
#include <string>
#include <vector>

#include "order_book/core/order_book.hpp"
#include "order_book/trading/messages.hpp"
namespace order_book::trading {
struct InstrumentRules {
    Price tick_size{1};
    Quantity lot_size{1};
    Price min_price{1}, max_price{1'000'000};
    Quantity max_order_quantity{100'000};
    std::uint64_t max_notional{10'000'000'000ULL};
    std::int64_t max_position{1'000'000};
    std::size_t max_open_orders{128};
    friend bool operator==(const InstrumentRules&, const InstrumentRules&) = default;
};
using Instruments = std::map<InstrumentId, InstrumentRules>;
[[nodiscard]] Instruments default_instruments();
[[nodiscard]] Instruments load_instruments(const std::string& path);
[[nodiscard]] std::uint64_t config_fingerprint(const Instruments& instruments);
struct Position {
    std::int64_t quantity{};
    std::uint64_t volume{};
    long double average_price{}, realized_pnl{}, cash{};
    void fill(Side side, Price price, Quantity size) noexcept;
    [[nodiscard]] long double pnl(long double mark) const noexcept;
};
struct Batch {
    std::vector<Message> reports, updates;
};
struct EngineStats {
    std::uint64_t commands{}, rejected{}, trades{}, volume{};
};
// One writer. All predictable validation and resource rejection precedes mutation.
// Unexpected exceptions require the host to stop and replay the durable journal.
class MatchingEngine final {
   public:
    explicit MatchingEngine(Instruments instruments = default_instruments(),
                            std::uint64_t epoch = 1);
    [[nodiscard]] Batch process(const Message& command);
    [[nodiscard]] std::vector<Message> snapshot() const;
    [[nodiscard]] const OrderBook& book(InstrumentId instrument) const;
    [[nodiscard]] Position position(ClientId client, InstrumentId instrument) const;
    [[nodiscard]] Sequence sequence() const noexcept { return sequence_; }
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }
    [[nodiscard]] const EngineStats& stats() const noexcept { return stats_; }
    [[nodiscard]] bool validate_invariants(std::string* reason = nullptr) const;
    [[nodiscard]] std::uint64_t last_request(ClientId client) const;
    [[nodiscard]] const Instruments& instruments() const noexcept { return instruments_; }

   private:
    struct Resting {
        Message order;
    };
    struct ClientState {
        std::uint64_t last_request{};
        OrderId max_order_id{};
        Message last_command;
        std::vector<Message> cached;
    };
    using Key = std::pair<ClientId, OrderId>;
    Instruments instruments_;
    std::map<InstrumentId, OrderBook> books_;
    std::map<OrderId, Resting> resting_;
    std::map<Key, OrderId> by_client_;
    std::map<ClientId, ClientState> clients_;
    std::map<std::pair<ClientId, InstrumentId>, Position> positions_;
    std::uint64_t epoch_;
    Sequence sequence_{}, response_sequence_{};
    OrderId next_order_{1};
    std::uint64_t next_trade_{1};
    EngineStats stats_;
    void report(Batch&, Message, ReportType, Reject = Reject::None);
    void update(Batch&, Message, UpdateType);
    void reject(Batch&, const Message&, Reject);
    void cancel(Batch&, OrderId, std::uint64_t request_id);
    void submit(Batch&, const Message&, OrderId old_id = 0);
};
}  // namespace order_book::trading
