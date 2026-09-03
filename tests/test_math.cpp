// The project's own sin/cos/log. They do not need to be correctly rounded --
// they need to be accurate enough for the simulation and identical at every
// optimisation level, on every platform. Accuracy is what this test checks;
// the identical-everywhere part is what the debug-vs-release hash diff checks.
#include "evosim/math.hpp"

#include <cmath>
#include <cstdio>
#include <string>

#include "evosim/rng.hpp"
#include "test_util.hpp"

using namespace evosim;
using namespace evosim::mathd;

namespace {
std::string sci(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.3e", v);
    return b;
}
}  // namespace

int main() {
    // Exact at the points where exactness is cheap and obvious.
    CHECK(dsin(0.0) == 0.0);
    CHECK(dcos(0.0) == 1.0);
    CHECK(dlog(1.0) == 0.0);

    double worst_sin = 0.0, worst_cos = 0.0;
    // The simulation only ever asks for angles within a few multiples of 2*pi
    // (headings and small wander turns), so that is the range that has to be
    // right. Cody-Waite reduction is not a full Payne-Hanek and is not claimed
    // to be accurate for huge arguments.
    for (int i = 0; i <= 40000; ++i) {
        const double x = -4.0 * kTwoPi + (8.0 * kTwoPi) * (i / 40000.0);
        const double es = std::fabs(dsin(x) - std::sin(x));
        const double ec = std::fabs(dcos(x) - std::cos(x));
        if (es > worst_sin) worst_sin = es;
        if (ec > worst_cos) worst_cos = ec;

        const double n = dsin(x) * dsin(x) + dcos(x) * dcos(x);
        CHECK(std::fabs(n - 1.0) < 1e-15);
        CHECK(dsin(x) >= -1.0000000001 && dsin(x) <= 1.0000000001);
    }
    // A few ULP against libm is the target: these are good sines, not
    // correctly-rounded ones, and being identical everywhere is what matters.
    CHECK_MSG(worst_sin < 1e-15, "worst |dsin - sin| = " + sci(worst_sin));
    CHECK_MSG(worst_cos < 1e-15, "worst |dcos - cos| = " + sci(worst_cos));

    // Quadrant handling: the reduction must pick the right kernel and sign.
    for (int k = -8; k <= 8; ++k) {
        const double x = k * kHalfPi;
        CHECK(std::fabs(dsin(x) - std::sin(x)) < 1e-15);
        CHECK(std::fabs(dcos(x) - std::cos(x)) < 1e-15);
    }

    // log over the range Box-Muller actually uses, (0,1), plus a wide sweep.
    double worst_log = 0.0;
    for (int i = 1; i < 20000; ++i) {
        const double u = i / 20000.0;
        const double e = std::fabs(dlog(u) - std::log(u)) / std::fabs(std::log(u) + 1e-300);
        if (e > worst_log) worst_log = e;
    }
    for (int i = -300; i <= 300; ++i) {
        const double x = std::pow(10.0, i * 0.5);
        if (!std::isfinite(x) || x == 0.0) continue;
        const double e = std::fabs(dlog(x) - std::log(x)) / (std::fabs(std::log(x)) + 1.0);
        if (e > worst_log) worst_log = e;
    }
    CHECK_MSG(worst_log < 1e-15, "worst relative log error = " + sci(worst_log));

    // The smallest value unit_open() can produce must still log finitely --
    // gaussian() feeds it straight into sqrt(-2*log(u)).
    {
        const double tiny = rng::unit_open(0);
        CHECK(tiny > 0.0);
        CHECK(std::isfinite(dlog(tiny)));
        CHECK(std::isfinite(std::sqrt(-2.0 * dlog(tiny))));
    }

    // Purity: same input, same bits, every time.
    for (int i = 0; i < 5000; ++i) {
        const double x = rng::range(11, i, 0, rng::Purpose::Wander, 0, -20.0, 20.0);
        CHECK(dsin(x) == dsin(x));
        CHECK(dcos(x) == dcos(x));
        const double u = rng::unit_open(rng::draw(12, i, 0, rng::Purpose::Wander, 0));
        CHECK(dlog(u) == dlog(u));
    }

    return evosim::test::finish("test_math");
}
