#include <chrono>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

#include "order_book/core/logger.hpp"
#include "order_book/core/order_registry.hpp"
#include "order_book/io/event_io.hpp"
#include "order_book/net/protocol.hpp"
#include "order_book/net/udp.hpp"
#include "udp_demo_args.hpp"

int main(int argc, char** argv) {
    using namespace order_book;
    if (argc < 2 || argc > 7) {
        std::cerr << "Usage: udp_event_subscriber <event_count> [port=9001] "
                     "[group=239.255.0.1] [interface=127.0.0.1] "
                     "[timeout_ms=30000] [log_file=udp_subscriber.log]\n";
        return 1;
    }
    std::unique_ptr<Logger> logger;
    LogRecord failure{.level = LogLevel::Error, .event = LogEvent::UdpTransportFailed};
    Sequence last_applied{};
    std::uint64_t applied{}, received{}, stale{};
    int status = 1;
    try {
        const auto target = udp_demo_number(argv[1], 1, std::numeric_limits<std::uint64_t>::max());
        const auto port = static_cast<std::uint16_t>(udp_demo_number(argc >= 3 ? argv[2] : "9001", 1, 65535));
        const auto* group = argc >= 4 ? argv[3] : "239.255.0.1";
        const auto* interface = argc >= 5 ? argv[4] : "127.0.0.1";
        const auto timeout = std::chrono::milliseconds(
            udp_demo_number(argc >= 6 ? argv[5] : "30000", 1, 60'000));
        logger = std::make_unique<Logger>(argc >= 7 ? argv[6] : "udp_subscriber.log");
        failure.port = port;
        net::MulticastSubscriber subscriber(group, port, interface, timeout);
        OrderBookRegistry registry(logger.get());
        (void)logger->try_log({.level = LogLevel::Info, .event = LogEvent::UdpJoined, .port = port});
        std::cerr << "Joined group=" << group << " port=" << port
                  << " interface=" << interface << std::endl;
        net::EventFrame frame{};
        while (applied < target) {
            failure.event = LogEvent::UdpTransportFailed;
            failure.sequence = last_applied;
            failure.instrument_id = 0;
            const auto packet = subscriber.receive(frame);
            if (!packet) {
                failure.event = LogEvent::UdpTimeout;
                throw std::runtime_error("Receive timeout before requested event count; feed incomplete");
            }
            ++received;
            failure.event = LogEvent::UdpProtocolFailed;
            // UDP returns one datagram, not a fragment to append to another.
            if (packet->truncated || packet->size != frame.size())
                throw std::runtime_error("Datagram must be exactly 40 bytes and not truncated");
            const auto event = net::decode_event(frame);
            if (!event) throw std::runtime_error("Malformed event datagram");
            failure.sequence = event->sequence;
            failure.instrument_id = event->instrument_id;
            if (event->sequence <= registry.get_seq()) {
                ++stale;
                (void)logger->try_log({.level = LogLevel::Warn,
                                       .event = LogEvent::UdpStaleDropped,
                                       .sequence = event->sequence,
                                       .instrument_id = event->instrument_id,
                                       .port = port});
                continue;
            }
            // Empty book requires sequence 1. A future sequence is a gap; stop
            // before apply rather than building a book from incomplete history.
            if (!registry.seq_valid(event->sequence)) {
                failure.event = LogEvent::UdpGapDetected;
                throw std::runtime_error("Sequence gap: expected=" +
                    std::to_string(registry.get_seq() + 1U) + " received=" +
                    std::to_string(event->sequence) + "; recovery required");
            }
            failure.event = LogEvent::UdpApplyRejected;
            const auto result = registry.apply(*event);
            failure.result = result;
            if (result != ApplyResult::Ok)
                throw std::runtime_error("Book rejected event: " + std::string(to_string(result)));
            last_applied = registry.get_seq();
            ++applied;
            write_event_csv(std::cout, *event);
        }
        failure.event = LogEvent::InvariantFailed;
        std::string reason;
        if (!registry.validate_invariants(&reason))
            throw std::runtime_error("Book invariant failure: " + reason);
        std::cerr << "instruments=" << registry.instrument_count()
                  << " remaining_orders=" << registry.order_count()
                  << " bid_levels=" << registry.bid_level_count()
                  << " ask_levels=" << registry.ask_level_count() << '\n';
        (void)logger->try_log({.level = LogLevel::Info, .event = LogEvent::UdpFinished,
                               .sequence = last_applied, .port = port});
        status = 0;
    } catch (const std::exception& error) {
        if (logger) {
            if (const auto* system = dynamic_cast<const std::system_error*>(&error))
                failure.error_code = system->code().value();
            (void)logger->try_log(failure);
        }
        std::cerr << "Subscriber: " << error.what() << '\n';
    }
    std::cerr << "book_synchronized=" << (status == 0) << " sequence=" << last_applied
              << " received=" << received << " applied=" << applied << " stale=" << stale << '\n';
    if (logger) {
        logger->close();
        std::cerr << "log_dropped=" << logger->dropped_count()
                  << " log_format_failures=" << logger->format_failed_count()
                  << " log_write_failed=" << logger->has_file_write_error() << '\n';
        if (logger->has_file_write_error() || logger->format_failed_count() != 0) status = 1;
    }
    return status;
}
