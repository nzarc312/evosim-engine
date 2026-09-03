// Counter-based stateless PRNG.
//
// Randomness is a pure function of a coordinate in the simulation:
// (seed, agent_id, tick, purpose, sub). There is no generator state, so there
// is nothing to synchronize and nothing whose consumption order can vary
// between runs or between thread counts.
#pragma once

#include <cstdint>

namespace evosim::rng {

// splitmix64 finalizer -- strong avalanche, ~5 instructions.
constexpr uint64_t mix(uint64_t z) noexcept {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

enum class Purpose : uint32_t {
    Mutation  = 1,
    FoodSpawn = 2,
    Wander    = 3,
    TieBreak  = 4,
    InitAgent = 5,
};

inline uint64_t draw(uint64_t seed, uint64_t agent_id, uint64_t tick,
                     Purpose p, uint32_t sub = 0) noexcept {
    return mix(mix(mix(seed ^ static_cast<uint64_t>(p)) ^ agent_id)
               ^ (tick * 0x2545F4914F6CDD1Dull + sub));
}

// [0,1) with 53 bits of mantissa. Hand-written on purpose: the standard
// distributions are not specified to produce identical output across
// standard library implementations.
inline double unit(uint64_t bits) noexcept {
    return static_cast<double>(bits >> 11) * 0x1.0p-53;
}

// (0,1), open at both ends, so it is safe to feed to log().
//
// 52 bits rather than 53 on purpose: with 53, (2^53-1)+0.5 is not representable
// and rounds up to 2^53, so the top input would come back as exactly 1.0 and
// the interval would not actually be open. At 52 bits the +0.5 lands on a
// representable value and the bound holds.
inline double unit_open(uint64_t bits) noexcept {
    return (static_cast<double>(bits >> 12) + 0.5) * 0x1.0p-52;
}

// Box-Muller. Consumes two draws at sub and sub+1, so a caller that also
// wants a second independent gaussian must advance sub by 2.
double gaussian(uint64_t seed, uint64_t agent_id, uint64_t tick,
                Purpose p, uint32_t sub = 0) noexcept;

// Uniform in [lo, hi).
inline double range(uint64_t seed, uint64_t agent_id, uint64_t tick,
                    Purpose p, uint32_t sub, double lo, double hi) noexcept {
    return lo + unit(draw(seed, agent_id, tick, p, sub)) * (hi - lo);
}

}  // namespace evosim::rng
