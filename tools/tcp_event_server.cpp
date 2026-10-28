#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "order_book/core/order_registry.hpp"
#include "order_book/io/event_io.hpp"
#include "order_book/net/protocol.hpp"
#include "order_book/net/tcp.hpp"
#include "tcp_demo_args.hpp"
#include "tcp_demo_logging.hpp"

int main(int argc, char** argv) {
    using namespace order_book;
    if (argc > 3) {
        std::cerr << "Usage: tcp_event_server [port=9000] [log_file=tcp_server.log]\n";
        return 1;
    }
    std::unique_ptr<Logger> logger;
    LogRecord failure{.level = LogLevel::Error, .event = LogEvent::TcpTransportFailed};
    int status = 1;
    try {
        const auto port = tcp_demo_port(argc >= 2 ? argv[1] : "9000");
        logger = std::make_unique<Logger>(argc == 3 ? argv[2] : "tcp_server.log");
        failure.port = port;
        auto listener = net::listen_tcp("127.0.0.1", port);
        (void)logger->try_log(
            {.level = LogLevel::Info, .event = LogEvent::TcpListening, .port = port});
        std::cerr << "Listening on 127.0.0.1:" << port << std::endl;
        auto peer = net::accept_tcp(listener);
        (void)logger->try_log(
            {.level = LogLevel::Info, .event = LogEvent::TcpConnected, .port = port});
        OrderBookRegistry registry(logger.get());
        net::EventFrame frame{};
        while (true) {
            failure.event = LogEvent::TcpTransportFailed;
            failure.sequence = registry.get_seq();
            failure.instrument_id = 0;
            if (!net::receive_exact(peer, frame)) break;
            failure.event = LogEvent::TcpProtocolFailed;
            const auto event = net::decode_event(frame);
            if (!event) throw std::runtime_error("Malformed event frame; closing connection");
            failure.sequence = event->sequence;
            failure.instrument_id = event->instrument_id;
            // Rejections leave the last successful sequence unchanged, allowing
            // a corrected event with the same sequence on this connection.
            failure.event = LogEvent::InvalidSequence;
            if (!registry.seq_valid(event->sequence))
                throw std::runtime_error("Invalid event sequence; closing connection");
            const auto result = registry.apply(*event);
            write_event_csv(std::cout, *event);
            failure.event = LogEvent::TcpTransportFailed;
            net::send_all(peer, net::encode_apply_ack({event->sequence, result}));
            (void)logger->try_log(
                {.level = result == ApplyResult::Ok ? LogLevel::Debug : LogLevel::Warn,
                 .event = LogEvent::TcpAckSent,
                 .sequence = event->sequence,
                 .instrument_id = event->instrument_id,
                 .result = result,
                 .port = port});
        }
        (void)logger->try_log({.level = LogLevel::Info,
                               .event = LogEvent::TcpDisconnected,
                               .sequence = registry.get_seq(),
                               .port = port});
        failure.event = LogEvent::InvariantFailed;
        std::string reason;
        if (!registry.validate_invariants(&reason))
            throw std::runtime_error("Book invariant failure: " + reason);
        std::cerr << "sequence=" << registry.get_seq()
                  << " instruments=" << registry.instrument_count()
                  << " remaining_orders=" << registry.order_count()
                  << " bid_levels=" << registry.bid_level_count()
                  << " ask_levels=" << registry.ask_level_count() << '\n';
        std::cerr << "Client disconnected cleanly\n";
        (void)logger->try_log({.level = LogLevel::Info,
                               .event = LogEvent::TcpFinished,
                               .sequence = registry.get_seq(),
                               .port = port});
        status = 0;
    } catch (const std::exception& error) {
        tcp_log_failure(logger.get(), failure, error);
        std::cerr << "Server: " << error.what() << '\n';
    }
    if (logger && !tcp_finish_log(*logger)) status = 1;
    return status;
}
