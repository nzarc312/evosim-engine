// Naive O(agents x food) scan vs the counting-sort grid.
//
// Scaling is density-preserving: the world grows with the agent count so that
// agents per unit area and food per unit area stay at the shipped defaults.
// Growing the counts inside a fixed world would instead make every cell denser,
// which measures crowding rather than the algorithm.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "evosim/config.hpp"
#include "evosim/world.hpp"

using namespace evosim;
using Clock = std::chrono::steady_clock;

namespace {

constexpr double kFrameBudgetMs = 1000.0 / 60.0;   // 16.667 ms

Config config_for(size_t agents) {
    Config c;
    const double scale = std::sqrt(static_cast<double>(agents) / 1000.0);
    c.world.width  = 500.0 * scale;
    c.world.height = 500.0 * scale;
    c.population.initial_agents = static_cast<uint32_t>(agents);
    c.food.target_count = static_cast<uint32_t>(agents * 2);
    c.food.spawn_rate   = static_cast<uint32_t>(agents / 20 + 1);
    return c;
}

// Enough ticks to be stable, few enough that the population barely drifts.
uint64_t ticks_for(size_t agents, bool naive) {
    if (naive) {
        if (agents <= 1000)  return 30;
        if (agents <= 5000)  return 15;
        if (agents <= 10000) return 8;
        return 4;
    }
    if (agents <= 10000) return 40;
    if (agents <= 50000) return 20;
    return 10;
}

double measure(size_t agents, bool naive) {
    const Config cfg = config_for(agents);
    World w(cfg, 42);
    w.set_naive(naive);

    w.step(DT);   // warm the caches and the first grid build
    const uint64_t n = ticks_for(agents, naive);
    const auto t0 = Clock::now();
    for (uint64_t i = 0; i < n; ++i) w.step(DT);
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return ms / static_cast<double>(n);
}

// Largest agent count still inside the frame budget, by log-log interpolation
// between the two bracketing measurements.
double sixty_hz_limit(const std::vector<size_t>& ns, const std::vector<double>& ms) {
    for (size_t i = 0; i < ns.size(); ++i) {
        if (ms[i] <= kFrameBudgetMs) continue;
        if (i == 0) return 0.0;
        const double l0 = std::log(static_cast<double>(ns[i - 1]));
        const double l1 = std::log(static_cast<double>(ns[i]));
        const double m0 = std::log(ms[i - 1]);
        const double m1 = std::log(ms[i]);
        const double t  = (std::log(kFrameBudgetMs) - m0) / (m1 - m0);
        return std::exp(l0 + t * (l1 - l0));
    }
    return static_cast<double>(ns.back());   // never exceeded the budget
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<size_t> sizes = {100, 1000, 5000, 10000, 25000, 50000};
    // Grid-only sizes past the point where the naive scan is unusable, so the
    // 60 Hz crossover for the grid is measured rather than extrapolated.
    std::vector<size_t> grid_only = {100000, 200000, 400000};
    bool quick = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--quick") == 0) quick = true;
    if (quick) { sizes = {100, 1000, 5000, 10000}; grid_only = {25000}; }

    std::printf("### Neighbour search: naive O(agents x food) vs counting-sort grid\n\n");
    std::printf("| agents | food | world | naive ms/tick | grid ms/tick | speedup |\n");
    std::printf("|---:|---:|---:|---:|---:|---:|\n");
    std::fflush(stdout);

    std::vector<size_t> gn;
    std::vector<double> gms, nms;

    for (const size_t n : sizes) {
        const double a = measure(n, true);
        const double b = measure(n, false);
        const Config c = config_for(n);
        std::printf("| %zu | %u | %.0f^2 | %.3f | %.3f | %.1fx |\n",
                    n, c.food.target_count, c.world.width, a, b, a / b);
        std::fflush(stdout);
        gn.push_back(n); gms.push_back(b); nms.push_back(a);
    }
    for (const size_t n : grid_only) {
        const double b = measure(n, false);
        const Config c = config_for(n);
        std::printf("| %zu | %u | %.0f^2 | - | %.3f | - |\n", n, c.food.target_count, c.world.width, b);
        std::fflush(stdout);
        gn.push_back(n); gms.push_back(b);
    }

    std::vector<size_t> nn(gn.begin(), gn.begin() + static_cast<long>(nms.size()));
    std::printf("\n**Agents sustainable at 60 Hz (16.67 ms/tick):** naive %.0f, grid %.0f.\n",
                sixty_hz_limit(nn, nms), sixty_hz_limit(gn, gms));
    return 0;
}
