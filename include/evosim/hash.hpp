// FNV-1a over raw bit patterns. Used for state hashing: every comparison in
// this project (run vs run, build vs build, 1 thread vs 16) is a hash diff.
#pragma once

#include <cstdint>
#include <cstring>

namespace evosim::hash {

constexpr uint64_t kOffsetBasis = 0xCBF29CE484222325ull;
constexpr uint64_t kPrime       = 0x00000100000001B3ull;

inline uint64_t byte(uint64_t h, uint8_t b) noexcept {
    return (h ^ static_cast<uint64_t>(b)) * kPrime;
}

inline uint64_t bytes(uint64_t h, const void* data, size_t n) noexcept {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) h = byte(h, p[i]);
    return h;
}

inline uint64_t u64(uint64_t h, uint64_t v) noexcept {
    return bytes(h, &v, sizeof(v));
}

inline uint64_t u32(uint64_t h, uint32_t v) noexcept {
    return bytes(h, &v, sizeof(v));
}

inline uint64_t u8(uint64_t h, uint8_t v) noexcept { return byte(h, v); }

// Hash the bit pattern, not the value: two doubles that compare equal but
// differ in the last bit must produce different hashes, or divergence hides.
inline uint64_t f64(uint64_t h, double v) noexcept {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return u64(h, bits);
}

}  // namespace evosim::hash
