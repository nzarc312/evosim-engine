// Heritable traits and their mutation rule.
//
// The three traits are deliberately cheap scalars. What makes selection happen
// is not the genome, it is the shape of the energy cost function in world.cpp:
// movement cost is quadratic in speed and cubic in size, so maxing out a trait
// is fatal rather than free.
#pragma once

#include <algorithm>

#include "evosim/rng.hpp"

namespace evosim::genome {

struct Range {
    double lo, hi;
    double span() const { return hi - lo; }
    double clamp(double v) const { return std::min(std::max(v, lo), hi); }
};

inline constexpr Range kSpeed{0.5, 3.0};   // movement units/sec
inline constexpr Range kSize {0.5, 2.0};   // energy capacity, eating radius
inline constexpr Range kSense{1.0, 15.0};  // food detection distance

// child = clamp(parent + gaussian * sigma * range)
inline double mutate(double parent, const Range& r, double sigma, uint64_t seed,
                     uint64_t child_id, uint64_t tick, uint32_t sub) {
    const double g = rng::gaussian(seed, child_id, tick, rng::Purpose::Mutation, sub);
    return r.clamp(parent + g * sigma * r.span());
}

// Uniform draw across a trait range, for the founding population.
inline double seed_trait(const Range& r, uint64_t seed, uint64_t agent_id, uint32_t sub) {
    return rng::range(seed, agent_id, 0, rng::Purpose::InitAgent, sub, r.lo, r.hi);
}

}  // namespace evosim::genome
