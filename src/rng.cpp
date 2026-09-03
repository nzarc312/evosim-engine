#include "evosim/rng.hpp"

#include <cmath>

namespace evosim::rng {

double gaussian(uint64_t seed, uint64_t agent_id, uint64_t tick,
                Purpose p, uint32_t sub) noexcept {
    const double u1 = unit_open(draw(seed, agent_id, tick, p, sub));
    const double u2 = unit(draw(seed, agent_id, tick, p, sub + 1));
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(kTwoPi * u2);
}

}  // namespace evosim::rng
