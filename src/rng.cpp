#include "evosim/rng.hpp"

#include <cmath>

#include "evosim/math.hpp"

namespace evosim::rng {

double gaussian(uint64_t seed, uint64_t agent_id, uint64_t tick,
                Purpose p, uint32_t sub) noexcept {
    const double u1 = unit_open(draw(seed, agent_id, tick, p, sub));
    const double u2 = unit(draw(seed, agent_id, tick, p, sub + 1));
    constexpr double kTwoPi = mathd::kTwoPi;
    // Deterministic sin/cos/log; sqrt is IEEE-exact and stays as-is.
    return std::sqrt(-2.0 * mathd::dlog(u1)) * mathd::dcos(kTwoPi * u2);
}

}  // namespace evosim::rng
