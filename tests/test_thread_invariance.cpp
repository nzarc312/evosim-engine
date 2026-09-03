// The most important test in the project.
//
// Same seed, same ticks, 1 / 2 / 4 / 8 / 16 threads -- one state hash. Not
// "close", not "within tolerance": the same 64 bits. Everything else here
// exists to make this hold.
#include <cstdio>
#include <string>
#include <vector>

#include "evosim/thread_pool.hpp"
#include "evosim/world.hpp"
#include "test_util.hpp"

using namespace evosim;

namespace {

const unsigned kThreadCounts[] = {1, 2, 4, 8, 16};

std::vector<uint64_t> trajectory(const Config& cfg, uint64_t seed, uint64_t ticks,
                                 unsigned threads, bool naive, uint64_t every = 100) {
    ThreadPool pool(threads);
    World w(cfg, seed, &pool);
    w.set_naive(naive);
    std::vector<uint64_t> out;
    out.reserve(ticks / every + 1);
    for (uint64_t i = 0; i < ticks; ++i) {
        w.step(DT);
        if (w.tick() % every == 0) out.push_back(w.state_hash());
    }
    return out;
}

void expect_same(const std::vector<uint64_t>& ref, const std::vector<uint64_t>& got,
                 unsigned threads, const char* label, uint64_t every = 100) {
    if (ref.size() != got.size()) {
        CHECK_MSG(false, std::string(label) + ": sample count differs at " +
                         std::to_string(threads) + " threads");
        return;
    }
    for (size_t i = 0; i < ref.size(); ++i) {
        if (ref[i] == got[i]) continue;
        char buf[224];
        std::snprintf(buf, sizeof(buf),
                      "%s: %u threads diverged from 1 thread at tick %llu (%016llx vs %016llx)",
                      label, threads, static_cast<unsigned long long>((i + 1) * every),
                      static_cast<unsigned long long>(ref[i]),
                      static_cast<unsigned long long>(got[i]));
        CHECK_MSG(false, buf);
        return;
    }
    CHECK_MSG(true, "");
}

}  // namespace

int main() {
    // The shipped configuration, 5000 ticks.
    {
        Config cfg;
        const auto ref = trajectory(cfg, 12345, 5000, 1, false);
        CHECK_MSG(!ref.empty(), "reference trajectory is empty -- test is vacuous");
        for (const unsigned t : kThreadCounts)
            expect_same(ref, trajectory(cfg, 12345, 5000, t, false), t, "default config");
    }

    // A larger population, so every one of the 64 chunks carries real work and
    // the parallel grid scatter is exercised rather than skipped.
    {
        Config cfg;
        cfg.population.initial_agents = 20000;
        cfg.food.target_count = 20000;
        cfg.food.spawn_rate   = 400;
        cfg.world.width = cfg.world.height = 2236.0;   // same density as default
        const auto ref = trajectory(cfg, 777, 400, 1, false);
        for (const unsigned t : kThreadCounts)
            expect_same(ref, trajectory(cfg, 777, 400, t, false), t, "20k agents");
    }

    // The naive path must be thread-invariant too: it shares P4, P5 and the
    // reductions, and only swaps out the neighbour query.
    {
        Config cfg;
        cfg.population.initial_agents = 500;
        cfg.food.target_count = 1000;
        const auto ref = trajectory(cfg, 31337, 800, 1, true);
        for (const unsigned t : {1u, 4u, 16u})
            expect_same(ref, trajectory(cfg, 31337, 800, t, true), t, "naive path");
    }

    // Repeated runs at the same thread count must also agree: a race that only
    // sometimes fires would otherwise look like a thread-count difference.
    {
        Config cfg;
        cfg.population.initial_agents = 5000;
        cfg.food.target_count = 10000;
        cfg.world.width = cfg.world.height = 1118.0;
        const auto ref = trajectory(cfg, 4242, 300, 16, false);
        for (int rep = 0; rep < 4; ++rep)
            expect_same(ref, trajectory(cfg, 4242, 300, 16, false), 16, "16-thread repeat");
    }

    // More threads than the machine has cores: oversubscription changes the
    // scheduling wildly and must change nothing else.
    {
        Config cfg;
        cfg.population.initial_agents = 2000;
        cfg.food.target_count = 4000;
        cfg.world.width = cfg.world.height = 707.0;
        const auto ref = trajectory(cfg, 5150, 300, 1, false);
        for (const unsigned t : {31u, 64u, 100u})
            expect_same(ref, trajectory(cfg, 5150, 300, t, false), t, "oversubscribed");
    }

    return evosim::test::finish("test_thread_invariance");
}
