// The reduction must return bit-identical results at every thread count.
#include "evosim/reduction.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "evosim/rng.hpp"
#include "test_util.hpp"

using namespace evosim;

namespace {
std::string hex(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%a", v);
    return b;
}
}  // namespace

int main() {
    // Values chosen to be nasty for float addition: wildly different magnitudes
    // and alternating signs, so any change in summation order shows up in the
    // low bits immediately.
    constexpr size_t kN = 200000;
    std::vector<double> v(kN);
    for (size_t i = 0; i < kN; ++i) {
        const double u = rng::unit(rng::draw(99, i, 0, rng::Purpose::TieBreak, 0));
        const double mag = std::pow(10.0, (u - 0.5) * 20.0);
        v[i] = (i % 3 == 0) ? -mag : mag;
    }

    const unsigned counts[] = {1, 2, 3, 4, 7, 8, 16};

    double reference = 0.0;
    bool   first     = true;
    for (const unsigned t : counts) {
        ThreadPool pool(t);
        CHECK_EQ(pool.size(), t);
        const double s = deterministic_sum(pool, kN, [&](size_t i) { return v[i]; });
        if (first) { reference = s; first = false; }
        CHECK_MSG(s == reference,
                  std::to_string(t) + " threads gave " + hex(s) + ", expected " + hex(reference));
    }

    // Same pool, repeated calls: stable.
    {
        ThreadPool pool(8);
        for (int i = 0; i < 20; ++i)
            CHECK(deterministic_sum(pool, kN, [&](size_t j) { return v[j]; }) == reference);
    }

    // The reduction has to actually match a plain serial sum over the same
    // chunk decomposition -- otherwise it is merely self-consistent.
    {
        double expect = 0.0;
        for (unsigned c = 0; c < ThreadPool::chunks(); ++c) {
            size_t b, e;
            ThreadPool::chunk_range(c, kN, b, e);
            double local = 0.0;
            for (size_t i = b; i < e; ++i) local += v[i];
            expect += local;
        }
        CHECK_MSG(expect == reference, "chunked serial sum " + hex(expect) + " != " + hex(reference));
    }

    // This is the trap the fixed chunk count exists to avoid: summing one
    // partial per THREAD really does change the answer, so the test asserts
    // that the naive scheme is different -- if it ever stops being different,
    // the data set has become too benign to be testing anything.
    {
        double per_thread_4 = 0.0, per_thread_16 = 0.0;
        for (const unsigned t : {4u, 16u}) {
            double total = 0.0;
            for (unsigned p = 0; p < t; ++p) {
                double local = 0.0;
                const size_t b = static_cast<size_t>(p) * kN / t;
                const size_t e = static_cast<size_t>(p + 1) * kN / t;
                for (size_t i = b; i < e; ++i) local += v[i];
                total += local;
            }
            (t == 4 ? per_thread_4 : per_thread_16) = total;
        }
        CHECK_MSG(per_thread_4 != per_thread_16,
                  "per-thread partitioning happened to agree; test data is too benign");
    }

    // Edge cases.
    {
        ThreadPool pool(4);
        CHECK(deterministic_sum(pool, 0, [&](size_t) { return 1.0; }) == 0.0);
        CHECK(deterministic_sum(pool, 1, [&](size_t) { return 2.5; }) == 2.5);
        // Fewer items than chunks: most chunks are empty and must contribute 0.
        CHECK(deterministic_sum(pool, 5, [&](size_t) { return 1.0; }) == 5.0);
    }

    // deterministic_max is order-free but still must not race.
    {
        std::vector<Padded<double>> slots;
        double expect = -1e300;
        for (size_t i = 0; i < kN; ++i) expect = std::max(expect, v[i]);
        for (const unsigned t : counts) {
            ThreadPool pool(t);
            const double m = deterministic_max(pool, slots, kN, -1e300,
                                               [&](size_t i) { return v[i]; });
            CHECK_MSG(m == expect, std::to_string(t) + " threads: max mismatch");
        }
    }

    // Every chunk index must be visited exactly once, whatever the thread count.
    for (const unsigned t : counts) {
        ThreadPool pool(t);
        std::vector<int> hits(ThreadPool::chunks(), 0);
        pool.run([&](unsigned c, size_t, size_t) { hits[c]++; }, 1234);
        for (unsigned c = 0; c < ThreadPool::chunks(); ++c)
            CHECK_MSG(hits[c] == 1, "chunk " + std::to_string(c) + " visited " +
                                    std::to_string(hits[c]) + " times at " +
                                    std::to_string(t) + " threads");
    }

    return evosim::test::finish("test_reduction");
}
