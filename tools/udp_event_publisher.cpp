#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "order_book/io/event_io.hpp"
#include "order_book/net/protocol.hpp"
#include "order_book/net/udp.hpp"
#include "udp_demo_args.hpp"

int main(int argc, char** argv) {
    if (argc < 2 || argc > 6) {
        std::cerr << "Usage: udp_event_publisher <events.csv> [port=9001] "
                     "[group=239.255.0.1] [interface=127.0.0.1] [pace_us=1000]\n";
        return 1;
    }
    try {
        const auto port = static_cast<std::uint16_t>(udp_demo_number(argc >= 3 ? argv[2] : "9001", 1, 65535));
        const auto* group = argc >= 4 ? argv[3] : "239.255.0.1";
        const auto* interface = argc >= 5 ? argv[4] : "127.0.0.1";
        const auto pace = std::chrono::microseconds(
            udp_demo_number(argc >= 6 ? argv[5] : "1000", 0, 1'000'000));
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("Cannot open events CSV");
        order_book::net::MulticastPublisher publisher(group, port, interface);
        std::string line;
        std::uint64_t sent{};
        while (std::getline(input, line)) {
            const auto parsed = order_book::parse_event_line(line);
            if (!parsed.event) {
                if (parsed.error.empty()) continue;
                throw std::runtime_error(parsed.error);
            }
            publisher.send(order_book::net::encode_event(*parsed.event));
            ++sent;
            // Pacing is for repeatable local demos; it does not guarantee delivery.
            if (pace.count() != 0) std::this_thread::sleep_for(pace);
        }
        if (input.bad() || (!input.eof() && input.fail()))
            throw std::runtime_error("Failed to read events CSV");
        std::cout << "sent=" << sent << " group=" << group << " port=" << port << '\n';
        // Success means accepted by the local kernel, never subscriber receipt.
    } catch (const std::exception& error) {
        std::cerr << "Publisher: " << error.what() << '\n';
        return 1;
    }
}
