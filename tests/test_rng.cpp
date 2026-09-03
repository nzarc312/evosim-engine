// The counter-based RNG is the foundation of every determinism claim in this
// project, so it gets tested before anything is built on top of it.
#include "evosim/rng.hpp"

#include <cmath>
#include <set>
#include <vector>

#include "test_util.hpp"

using namespace evosim;
using rng::Purpose;

int main() {
    // Purity: the same coordinate always yields the same bits.
    for (int i = 0; i < 1000; ++i) {
        const uint64_t a = rng::draw(42, i, 7, Purpose::Mutation, 3);
        const uint64_t b = rng::draw(42, i, 7, Purpose::Mutation, 3);
        CHECK(a == b);
    }

    // Every coordinate axis actually changes the output.
    const uint64_t base = rng::draw(42, 100, 7, Purpose::Mutation, 0);
    CHECK(rng::draw(43, 100, 7, Purpose::Mutation, 0) != base);
    CHECK(rng::draw(42, 101, 7, Purpose::Mutation, 0) != base);
    CHECK(rng::draw(42, 100, 8, Purpose::Mutation, 0) != base);
    CHECK(rng::draw(42, 100, 7, Purpose::Wander, 0)   != base);
    CHECK(rng::draw(42, 100, 7, Purpose::Mutation, 1) != base);

    // Order independence / seekability: agent 9000 at tick 50000 is computable
    // without having produced a single draw for any other agent or tick.
    const uint64_t far = rng::draw(42, 9000, 50000, Purpose::Wander, 0);
    for (uint64_t a = 0; a < 500; ++a)
        for (uint64_t t = 0; t < 20; ++t) (void)rng::draw(42, a, t, Purpose::Wander, 0);
    CHECK(rng::draw(42, 9000, 50000, Purpose::Wander, 0) == far);

    // No collisions across a dense block of coordinates.
    {
        std::set<uint64_t> seen;
        size_t n = 0;
        for (uint64_t a = 0; a < 200; ++a)
            for (uint64_t t = 0; t < 200; ++t, ++n) seen.insert(rng::draw(1, a, t, Purpose::Wander, 0));
        CHECK_EQ(seen.size(), n);
    }

    // unit() stays in [0,1) and covers the range roughly evenly.
    {
        constexpr int kN = 200000;
        std::vector<int> bucket(10, 0);
        double sum = 0.0;
        for (int i = 0; i < kN; ++i) {
            const double u = rng::unit(rng::draw(7, i, 0, Purpose::FoodSpawn, 0));
            CHECK(u >= 0.0 && u < 1.0);
            sum += u;
            bucket[static_cast<size_t>(u * 10.0)]++;
        }
        const double mean = sum / kN;
        CHECK_MSG(std::fabs(mean - 0.5) < 0.01, "mean=" + std::to_string(mean));
        for (int b = 0; b < 10; ++b)
            CHECK_MSG(bucket[b] > kN / 10 * 9 / 10 && bucket[b] < kN / 10 * 11 / 10,
                      "bucket " + std::to_string(b) + " = " + std::to_string(bucket[b]));
    }

    // unit_open() never returns exactly zero -- gaussian() feeds it to log().
    for (int i = 0; i < 100000; ++i) {
        const double u = rng::unit_open(rng::draw(9, i, 0, Purpose::Mutation, 0));
        CHECK(u > 0.0 && u < 1.0);
    }
    CHECK(rng::unit_open(0) > 0.0);
    CHECK(rng::unit_open(~uint64_t{0}) < 1.0);

    // gaussian(): mean ~0, stddev ~1, and finite everywhere.
    {
        constexpr int kN = 200000;
        double sum = 0.0, sumsq = 0.0;
        for (int i = 0; i < kN; ++i) {
            const double g = rng::gaussian(3, i, 11, Purpose::Mutation, 0);
            CHECK(std::isfinite(g));
            sum += g;
            sumsq += g * g;
        }
        const double mean = sum / kN;
        const double var  = sumsq / kN - mean * mean;
        CHECK_MSG(std::fabs(mean) < 0.02, "mean=" + std::to_string(mean));
        CHECK_MSG(std::fabs(std::sqrt(var) - 1.0) < 0.02, "sd=" + std::to_string(std::sqrt(var)));
    }

    // range() respects its bounds.
    for (int i = 0; i < 10000; ++i) {
        const double v = rng::range(5, i, 2, Purpose::InitAgent, 0, -3.5, 9.25);
        CHECK(v >= -3.5 && v < 9.25);
    }

    return evosim::test::finish("test_rng");
}
