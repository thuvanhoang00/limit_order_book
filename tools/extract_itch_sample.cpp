#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <map>
#include <string_view>
#include <system_error>

#include "order_book/feed/input.hpp"
#include "order_book/feed/decoder.hpp"
#include "trading_args.hpp"

namespace {
// Refuse to replace existing files. Failed extraction removes its partial output.
class Output final {
 public:
    explicit Output(std::string path) : path_(std::move(path)) {
        const int fd = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) throw std::system_error(errno, std::generic_category(), "Create sample");
        file_ = ::gzdopen(fd, "wb9");
        if (!file_) {
            ::close(fd);
            std::remove(path_.c_str());
            throw std::runtime_error("Cannot initialize sample compression");
        }
    }
    ~Output() {
        if (file_) ::gzclose(file_);
        if (!complete_) std::remove(path_.c_str());
    }
    void write(const std::vector<std::uint8_t>& message) {
        const std::array<std::uint8_t, 2> prefix{
            static_cast<std::uint8_t>(message.size() >> 8),
            static_cast<std::uint8_t>(message.size() & 255)};
        if (::gzwrite(file_, prefix.data(), 2) != 2 ||
            ::gzwrite(file_, message.data(), static_cast<unsigned>(message.size())) !=
                static_cast<int>(message.size()))
            throw std::runtime_error("Cannot write compressed sample");
    }
    void finish() {
        const auto result = ::gzclose(file_);
        file_ = nullptr;
        if (result != Z_OK) throw std::runtime_error("Cannot finish compressed sample");
        complete_ = true;
    }
 private:
    std::string path_;
    gzFile file_{};
    bool complete_{};
};
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "extract_itch_sample --input archive.gz --symbol AAPL --output sample.itch.gz\n";
        return 0;
    }
    try {
        TradingArgs args(argc, argv);
        const auto input = args.text("--input", "");
        const auto output = args.text("--output", "");
        const auto symbol = args.text("--symbol", "AAPL");
        args.finish();
        if (input.empty() || output.empty() || symbol.empty() || symbol.size() > 8)
            throw std::runtime_error("Input, output and an ITCH symbol of 1..8 bytes required");
        for (char value : symbol) {
            const auto c = static_cast<unsigned char>(value);
            if (c < 33 || c > 126) throw std::runtime_error("Invalid symbol");
        }
        order_book::feed::Input source(input, order_book::feed::Format::Itch50);
        Output destination(output);
        std::vector<std::uint8_t> message;
        std::uint64_t records{}, kept{};
        std::uint16_t selected_locate{};
        bool ended{};
        std::map<char, std::uint64_t> counts;
        while (source.next(message)) {
            if (ended || message.size() < 11)
                throw std::runtime_error("Message after session end or incomplete ITCH header");
            const char kind = static_cast<char>(message[0]);
            const auto locate = static_cast<std::uint16_t>(message[1] * 256U + message[2]);
            if (records == 0 && (kind != 'S' || message.size() != 12 || message[11] != 'O'))
                throw std::runtime_error("Source must start at Start of Messages");
            if (kind == 'S') {
                if (locate != 0 || message.size() != 12 ||
                    std::string_view("OSQMEC").find(static_cast<char>(message[11])) == std::string_view::npos ||
                    (records != 0 && message[11] == 'O'))
                    throw std::runtime_error("Invalid system event/session restart");
                ended = message[11] == 'C';
            }
            if (kind == 'R') {
                if (message.size() != 39 || locate == 0)
                    throw std::runtime_error("Invalid Stock Directory");
                std::string name(message.begin() + 11, message.begin() + 19);
                while (!name.empty() && name.back() == ' ') name.pop_back();
                if (name == symbol) {
                    if (selected_locate && selected_locate != locate)
                        throw std::runtime_error("Selected symbol changes stock locate");
                    selected_locate = locate;
                } else if (selected_locate == locate)
                    throw std::runtime_error("Selected stock locate changes symbol");
            }
            ++records;
            // Preserve global messages plus every original record for the symbol.
            if (locate == 0 || (selected_locate && locate == selected_locate)) {
                destination.write(message);
                ++kept;
                ++counts[kind];
            }
            if (records % 50'000'000 == 0)
                std::cerr << "Read " << records << " records; kept " << kept << '\n';
        }
        if (!ended || !selected_locate)
            throw std::runtime_error("Complete session and selected Stock Directory required");
        destination.finish();
        std::cout << "{\"symbol\":" << std::quoted(symbol) << ",\"stock_locate\":" << selected_locate
                  << ",\"input_records\":" << records << ",\"output_records\":" << kept
                  << ",\"complete_input\":true,\"session_ended\":true,\"message_counts\":{";
        bool first = true;
        for (const auto& [kind, count] : counts) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << std::quoted(std::string(1, kind)) << ':' << count;
        }
        std::cout << "}}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Sample extraction failed: " << error.what() << '\n';
        return 1;
    }
}
