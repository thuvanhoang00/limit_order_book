#pragma once

#include <map>
#include <memory>
#include <vector>

#include "order_book/core/order_book.hpp"
#include "order_book/exchange/messages.hpp"

namespace order_book::exchange {
// Reconstructive books used by both the snapshot publisher and trading client.
class FeedBook final {
   public:
    [[nodiscard]] bool apply(const MarketUpdate& update);
    [[nodiscard]] const OrderBook* book(InstrumentId id) const noexcept;
    [[nodiscard]] std::vector<MarketUpdate> orders() const;
    [[nodiscard]] std::vector<InstrumentId> instruments() const;
    [[nodiscard]] std::size_t order_count() const noexcept;
    [[nodiscard]] bool validate_invariants() const;

   private:
    std::map<InstrumentId, OrderBook> books_;
};

[[nodiscard]] std::vector<MarketUpdate> snapshot_cycle(const FeedBook& book, std::uint64_t session,
                                                       std::uint64_t snapshot_id,
                                                       Sequence watermark);

enum class FeedResult { Dropped, Applied, Recovering, Recovered };
class FeedConsumer final {
   public:
    explicit FeedConsumer(std::size_t buffer_limit = 65536) : limit_(buffer_limit) {}
    [[nodiscard]] FeedResult incremental(const MarketUpdate& update);
    [[nodiscard]] FeedResult snapshot(const MarketUpdate& update);
    void invalidate() noexcept;
    [[nodiscard]] bool synchronized() const noexcept { return synchronized_; }
    [[nodiscard]] const FeedBook& books() const noexcept { return live_; }
    [[nodiscard]] Sequence sequence() const noexcept { return sequence_; }
    [[nodiscard]] std::uint64_t session() const noexcept { return session_; }
    [[nodiscard]] std::size_t buffered() const noexcept { return pending_.size(); }

   private:
    bool adopt(std::uint64_t session);
    FeedResult finish();
    FeedBook live_;
    std::unique_ptr<FeedBook> candidate_;
    std::map<Sequence, MarketUpdate> pending_;
    std::size_t limit_;
    std::uint64_t session_{};
    Sequence sequence_{};
    Sequence observed_{};
    bool synchronized_{};
    bool can_start_from_one_{true};
    std::uint64_t snapshot_id_{};
    Sequence watermark_{};
    Sequence snapshot_next_{};
    std::uint64_t snapshot_count_{};
    bool ready_{};
};
}  // namespace order_book::exchange
