#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "order_book/io/event_io.hpp"
#include "order_book/net/protocol.hpp"
#include "order_book/net/tcp.hpp"
#include "tcp_demo_args.hpp"
#include "tcp_demo_logging.hpp"

int main(int argc, char** argv) {
    using namespace order_book;
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: tcp_event_client <events.csv> [port=9000] [log_file=tcp_client.log]\n";
        return 1;
    }
    std::unique_ptr<Logger> logger;
    LogRecord failure{.level = LogLevel::Error, .event = LogEvent::InputOpenFailed};
    int status = 1;
    try {
        const auto port = tcp_demo_port(argc >= 3 ? argv[2] : "9000");
        logger = std::make_unique<Logger>(argc == 4 ? argv[3] : "tcp_client.log");
        failure.port = port;
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("Cannot open events CSV");
        failure.event = LogEvent::TcpTransportFailed;
        auto socket = net::connect_tcp("127.0.0.1", port);
        (void)logger->try_log(
            {.level = LogLevel::Info, .event = LogEvent::TcpConnected, .port = port});
        std::string line;
        Sequence last_applied{};
        bool rejected = false;
        while (std::getline(input, line)) {
            ++failure.line_number;
            failure.event = LogEvent::ParseFailed;
            failure.result = ApplyResult::ParseError;
            const auto parsed = parse_event_line(line);
            if (!parsed.event) {
                if (parsed.error.empty()) continue;
                throw std::runtime_error(parsed.error);
            }
            const auto& event = *parsed.event;
            failure.sequence = event.sequence;
            failure.instrument_id = event.instrument_id;
            failure.event = LogEvent::TcpProtocolFailed;
            const auto frame = net::encode_event(event);
            failure.event = LogEvent::TcpTransportFailed;
            net::send_all(socket, frame);
            net::AckFrame ack{};
            if (!net::receive_exact(socket, ack))
                throw std::runtime_error("Server closed before ACK; outcome unknown");
            failure.event = LogEvent::TcpProtocolFailed;
            const auto response = net::decode_apply_ack(ack);
            if (!response || response->sequence != event.sequence)
                throw std::runtime_error("Invalid or mismatched ACK");
            (void)logger->try_log(
                {.level = response->result == ApplyResult::Ok ? LogLevel::Debug : LogLevel::Warn,
                 .event = LogEvent::TcpAckReceived,
                 .sequence = response->sequence,
                 .instrument_id = event.instrument_id,
                 .result = response->result,
                 .line_number = failure.line_number,
                 .port = port});
            std::cout << "apply_ack sequence=" << response->sequence
                      << " result=" << to_string(response->result) << '\n';
            if (response->result != ApplyResult::Ok) {
                std::cerr << "Client: Event rejected by order book\n";
                rejected = true;
                break;
            }
            last_applied = response->sequence;
        }
        failure.event = LogEvent::InputReadFailed;
        if (input.bad() || (!input.eof() && input.fail()))
            throw std::runtime_error("Failed to read events CSV");
        socket = net::Socket{};
        (void)logger->try_log({.level = LogLevel::Info,
                               .event = LogEvent::TcpDisconnected,
                               .sequence = last_applied,
                               .port = port});
        if (!rejected) {
            (void)logger->try_log({.level = LogLevel::Info,
                                   .event = LogEvent::TcpFinished,
                                   .sequence = last_applied,
                                   .port = port});
            status = 0;
        }
    } catch (const std::exception& error) {
        tcp_log_failure(logger.get(), failure, error);
        std::cerr << "Client: " << error.what() << '\n';
    }
    if (logger && !tcp_finish_log(*logger)) status = 1;
    return status;
}
