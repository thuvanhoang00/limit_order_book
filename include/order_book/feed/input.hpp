#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace order_book::feed {
enum class Format { Itch50, Pitch };
// Buffered streaming, transparent raw/gzip input. Gzip errors/CRC failures are
// surfaced when read to EOF. No entire archive or decompressed copy is retained.
class Input final {
   public:
    Input(const std::string& path, Format format);
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    [[nodiscard]] bool next(std::vector<std::uint8_t>& record);
   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace order_book::feed
