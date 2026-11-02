// Self-contained SHA-256 (FIPS 180-4) used to verify every download.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace mgh {

class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const void* data, size_t len);
    std::array<uint8_t, 32> finish();
    std::string finishHex();

    static std::string hex(const void* data, size_t len);
    // Hashes a file in chunks; nullopt if it cannot be read.
    static std::optional<std::string> fileHex(const std::string& path);
    // Constant-time-ish case-insensitive comparison of hex digests.
    static bool equalHex(const std::string& a, const std::string& b);
    static bool isValidHex(const std::string& s);

private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    uint64_t total_ = 0;
    size_t bufLen_ = 0;
};

}  // namespace mgh
