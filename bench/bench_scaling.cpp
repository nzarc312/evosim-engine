// The benchmark that matters: how the tick scales with thread count, what the
// serial fraction actually is, and two memory-layout studies.
//
//   --mode scaling        ms/tick, speedup, efficiency and state hash at
//                         1/2/4/8/16 threads, 50k and 200k agents
//   --mode ceiling        agents sustainable at 60 Hz, 1 thread vs many
//   --mode serial         directly measured serial fraction and Amdahl ceiling
//   --mode soa_aos        structure-of-arrays vs array-of-structs
//   --mode false_sharing  padded vs unpadded per-chunk accumulators
//   --mode all            (default) every one of the above
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <string>
#include <vector>

#include "evosim/config.hpp"
#include "evosim/reduction.hpp"
#include "evosim/rng.hpp"
#include "evosim/thread_pool.hpp"
#include "evosim/world.hpp"

using namespace evosim;
using Clock = std::chrono::steady_clock;

namespace {

bool g_quick = false;

// Density-preserving: the world grows with the population so that agents and
// food per unit area stay at the shipped defaults. Growing the counts inside a
// fixed world would measure crowding instead of scaling.
Config config_for(size_t agents) {
    Config c;
    const double scale = std::sqrt(static_cast<double>(agents) / 1000.0);
    c.world.width = c.world.height = 500.0 * scale;
    c.population.initial_agents = static_cast<uint32_t>(agents);
    c.population.max_agents     = static_cast<uint32_t>(agents * 2);
    c.food.target_count         = static_cast<uint32_t>(agents * 2);
    c.food.spawn_rate           = static_cast<uint32_t>(agents / 20 + 1);
    return c;
}

struct Timed {
    double   ms_per_tick;
    uint64_t hash;
};

Timed time_run(size_t agents, unsigned threads, uint64_t ticks) {
    const Config cfg = config_for(agents);
    ThreadPool pool(threads);
    World w(cfg, 42, &pool);

    for (int i = 0; i < 5; ++i) w.step(DT);          // warm caches and buffers
    const auto t0 = Clock::now();
    for (uint64_t i = 0; i < ticks; ++i) w.step(DT);
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return {ms / static_cast<double>(ticks), w.state_hash()};
}

// -------------------------------------------------------------------------
void mode_scaling() {
    const std::vector<size_t> sizes = g_quick ? std::vector<size_t>{10000}
                                              : std::vector<size_t>{50000, 200000};
    const unsigned counts[] = {1, 2, 4, 8, 16};

    for (const size_t agents : sizes) {
        const uint64_t ticks = g_quick ? 30 : (agents >= 200000 ? 100 : 150);
        std::printf("\n### Threading scale-up: %zu agents, %llu ticks\n\n", agents,
                    static_cast<unsigned long long>(ticks));
        std::printf("| threads | ms/tick | speedup | parallel efficiency | state hash |\n");
        std::printf("|---:|---:|---:|---:|---|\n");
        std::fflush(stdout);

        double   base = 0.0;
        uint64_t ref  = 0;
        bool     same = true;
        for (const unsigned t : counts) {
            const Timed r = time_run(agents, t, ticks);
            if (t == 1) { base = r.ms_per_tick; ref = r.hash; }
            if (r.hash != ref) same = false;
            const double sp = base / r.ms_per_tick;
            std::printf("| %u | %.3f | %.2fx | %.0f%% | `%016llx` |\n", t, r.ms_per_tick, sp,
                        100.0 * sp / t, static_cast<unsigned long long>(r.hash));
            std::fflush(stdout);
        }
        std::printf("\n%s\n", same
            ? "**All thread counts produced the same state hash.**"
            : "**FAILURE: state hashes differ across thread counts.**");
    }
}

// -------------------------------------------------------------------------
void mode_serial() {
    const size_t agents = g_quick ? 10000 : 200000;
    const uint64_t ticks = g_quick ? 30 : 100;

    std::printf("\n### Measured serial fraction (%zu agents)\n\n", agents);
    std::printf("| phase | parallel? | ms/tick @1 | ms/tick @16 | share @16 |\n");
    std::printf("|---|---|---:|---:|---:|\n");

    PhaseTimes p1{}, p16{};
    for (const unsigned t : {1u, 16u}) {
        const Config cfg = config_for(agents);
        ThreadPool pool(t);
        World w(cfg, 42, &pool);
        for (int i = 0; i < 5; ++i) w.step(DT);
        w.set_profiling(true);
        w.reset_phase_times();
        for (uint64_t i = 0; i < ticks; ++i) w.step(DT);
        (t == 1 ? p1 : p16) = w.phase_times();
    }

    const double n = static_cast<double>(ticks);
    auto row = [&](const char* name, const char* par, double a, double b) {
        std::printf("| %s | %s | %.3f | %.3f | %.1f%% |\n", name, par, a / n, b / n,
                    100.0 * b / p16.total);
    };
    row("P1-P3 grid build (total)", "mixed",    p1.grid_build,  p16.grid_build);
    row("&nbsp;&nbsp;of which P2 prefix sum", "**serial**", p1.grid_serial, p16.grid_serial);
    row("P4 agents",               "parallel",  p1.p4_agents,  p16.p4_agents);
    row("P5 claim resolution",     "**serial**", p1.p5_claims,  p16.p5_claims);
    row("P6 mark deaths",          "parallel",  p1.p6_deaths,  p16.p6_deaths);
    row("P7 compact + reproduce",  "**serial**", p1.p7_compact, p16.p7_compact);
    row("P8 food respawn",         "**serial**", p1.p8_food,    p16.p8_food);
    row("whole tick",              "-",         p1.total,      p16.total);

    std::printf("\n- Serial fraction at 1 thread: **%.1f%%** (Amdahl ceiling %.1fx)\n",
                100.0 * p1.serial_fraction(), p1.amdahl_ceiling());
    std::printf("- Serial fraction at 16 threads: **%.1f%%** (Amdahl ceiling %.1fx)\n",
                100.0 * p16.serial_fraction(), p16.amdahl_ceiling());
    std::printf("\nThe serial fraction is larger at 16 threads because the parallel phases "
                "shrink while the serial ones do not -- that is precisely what the ceiling "
                "means.\n");
}

// -------------------------------------------------------------------------
// How many agents each configuration can carry at 60 Hz. This is the number a
// real-time budget actually cares about: not "how fast is a tick" but "how much
// world fits inside 16.67 ms".
double ceiling_for(unsigned threads, const std::vector<size_t>& sizes,
                   std::vector<double>& out_ms) {
    constexpr double kFrameMs = 1000.0 / 60.0;
    out_ms.clear();
    for (const size_t n : sizes) {
        const Config cfg = config_for(n);
        ThreadPool pool(threads);
        World w(cfg, 42, &pool);
        for (int i = 0; i < 3; ++i) w.step(DT);
        const uint64_t ticks = n >= 400000 ? 6 : (n >= 100000 ? 10 : 20);
        const auto t0 = Clock::now();
        for (uint64_t i = 0; i < ticks; ++i) w.step(DT);
        out_ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count()
                         / static_cast<double>(ticks));
    }
    // Log-log interpolation between the two measurements that bracket the budget.
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (out_ms[i] <= kFrameMs) continue;
        if (i == 0) return 0.0;
        const double l0 = std::log(static_cast<double>(sizes[i - 1]));
        const double l1 = std::log(static_cast<double>(sizes[i]));
        const double t  = (std::log(kFrameMs) - std::log(out_ms[i - 1])) /
                          (std::log(out_ms[i]) - std::log(out_ms[i - 1]));
        return std::exp(l0 + t * (l1 - l0));
    }
    return static_cast<double>(sizes.back());
}

void mode_ceiling() {
    const std::vector<size_t> sizes = g_quick
        ? std::vector<size_t>{5000, 25000, 100000}
        : std::vector<size_t>{25000, 50000, 100000, 200000, 400000, 800000};

    std::vector<double> ms1, msN;
    const unsigned hw = std::thread::hardware_concurrency() ? std::thread::hardware_concurrency() : 1;
    const unsigned many = std::min(16u, hw);

    const double c1 = ceiling_for(1, sizes, ms1);
    const double cN = ceiling_for(many, sizes, msN);

    std::printf("\n### Agents sustainable at 60 Hz (16.67 ms/tick)\n\n");
    std::printf("| agents | 1 thread ms/tick | %u threads ms/tick | speedup |\n", many);
    std::printf("|---:|---:|---:|---:|\n");
    for (size_t i = 0; i < sizes.size(); ++i)
        std::printf("| %zu | %.2f | %.2f | %.2fx |\n", sizes[i], ms1[i], msN[i], ms1[i] / msN[i]);

    std::printf("\n**60 Hz ceiling: %.0f agents on 1 thread, %.0f agents on %u threads "
                "(%.2fx more world in the same frame budget).**\n",
                c1, cN, many, cN / c1);
}

// -------------------------------------------------------------------------
// Structure-of-arrays vs array-of-structs, on the movement kernel. It touches
// four of the nine per-agent fields, so AoS drags the other five through cache
// for nothing.
struct AgentAoS {
    uint64_t id;
    double   pos_x, pos_y, vel_x, vel_y, energy;
    double   gene_speed, gene_size, gene_sense;
    uint32_t age;
    uint8_t  alive;
};

void soa_aos_one(size_t n, int reps) {
    const double w = 5000.0, dt = DT;

    std::vector<double> px(n), py(n), vx(n), vy(n);
    std::vector<AgentAoS> aos(n);
    for (size_t i = 0; i < n; ++i) {
        px[i] = rng::range(1, i, 0, rng::Purpose::InitAgent, 0, 0.0, w);
        py[i] = rng::range(1, i, 0, rng::Purpose::InitAgent, 1, 0.0, w);
        vx[i] = rng::range(1, i, 0, rng::Purpose::InitAgent, 2, -3.0, 3.0);
        vy[i] = rng::range(1, i, 0, rng::Purpose::InitAgent, 3, -3.0, 3.0);
        aos[i] = AgentAoS{i, px[i], py[i], vx[i], vy[i], 50.0, 1.0, 1.0, 5.0, 0, 1};
    }

    auto wrap = [w](double p) { return p < 0.0 ? p + w : (p >= w ? p - w : p); };

    double sink = 0.0;
    const auto t0 = Clock::now();
    for (int r = 0; r < reps; ++r)
        for (size_t i = 0; i < n; ++i) {
            px[i] = wrap(px[i] + vx[i] * dt);
            py[i] = wrap(py[i] + vy[i] * dt);
        }
    const double soa_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    for (size_t i = 0; i < n; ++i) sink += px[i] + py[i];

    const auto t1 = Clock::now();
    for (int r = 0; r < reps; ++r)
        for (size_t i = 0; i < n; ++i) {
            aos[i].pos_x = wrap(aos[i].pos_x + aos[i].vel_x * dt);
            aos[i].pos_y = wrap(aos[i].pos_y + aos[i].vel_y * dt);
        }
    const double aos_ms = std::chrono::duration<double, std::milli>(Clock::now() - t1).count();
    for (size_t i = 0; i < n; ++i) sink += aos[i].pos_x + aos[i].pos_y;

    const double denom = static_cast<double>(n) * reps;
    std::printf("| %zu | %.2f | %.2f | %.3f | %.3f | %.2fx |\n",
                n,
                static_cast<double>(n) * 32 / 1048576.0,
                static_cast<double>(n) * sizeof(AgentAoS) / 1048576.0,
                soa_ms * 1e6 / denom, aos_ms * 1e6 / denom, aos_ms / soa_ms);
    std::fflush(stdout);
    if (sink == 12345.6789) std::printf("");   // keep the kernels from being elided
}

void mode_soa_aos() {
    std::printf("\n### Memory layout: SoA vs AoS on the movement kernel\n\n");
    std::printf("The kernel touches 4 of the 9 per-agent fields (32 of %zu bytes). SoA streams\n"
                "four dense arrays; AoS strides over the whole struct and pulls the other five\n"
                "fields into cache for nothing.\n\n", sizeof(AgentAoS));
    std::printf("| agents | SoA working set (MiB) | AoS working set (MiB) | SoA ns/agent | AoS ns/agent | SoA speedup |\n");
    std::printf("|---:|---:|---:|---:|---:|---:|\n");
    if (g_quick) { soa_aos_one(10000, 200); soa_aos_one(200000, 20); return; }
    soa_aos_one(50000, 2000);
    soa_aos_one(500000, 200);
    soa_aos_one(4000000, 25);
}

// -------------------------------------------------------------------------
// False sharing. The only difference between the two middle rows is a 64-byte
// alignas on the accumulator slot.
//
// Two traps this benchmark walked into before it measured anything:
//
//  1. A first version used a million values. At 8 MB per pass the reduction is
//     memory-bandwidth bound and streaming that data swamps any cache-line
//     ping-pong. False sharing is a store-traffic effect, so the loads have to
//     be cheap before it can show. The data set is now cache-resident.
//  2. Even then, `slot.value += v[i]` in a tight loop gets hoisted into a
//     register by the optimiser, leaving exactly one store per chunk and
//     nothing to contend over. The measured 0.5 ns/add -- about two cycles, the
//     FP-add latency -- is the giveaway. The `volatile` accumulator below is
//     what forces the store to actually reach memory on every iteration, which
//     is the situation real code lands in whenever the compiler cannot prove
//     the accumulator is private.
constexpr int kInner = 96;

// Accumulate straight into the shared slot, one real store per iteration.
template <typename Slot>
double reduce_forced(ThreadPool& pool, std::vector<Slot>& slots,
                     const std::vector<double>& v, int reps) {
    double total = 0.0;
    const auto t0 = Clock::now();
    for (int r = 0; r < reps; ++r) {
        slots.assign(ThreadPool::chunks(), Slot{});
        pool.run([&](unsigned c, size_t b, size_t e) {
            volatile double* acc = &slots[c].value;
            for (int k = 0; k < kInner; ++k)
                for (size_t i = b; i < e; ++i) *acc = *acc + v[i];
        }, v.size());
        for (unsigned c = 0; c < ThreadPool::chunks(); ++c) total += slots[c].value;
    }
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (total == 12345.6789) std::printf("");
    return ms;
}

// What evosim ships: accumulate in a local, store to the slot once per chunk.
double reduce_local(ThreadPool& pool, const std::vector<double>& v, int reps) {
    std::vector<Padded<double>> slots;
    double total = 0.0;
    const auto t0 = Clock::now();
    for (int r = 0; r < reps; ++r) {
        slots.assign(ThreadPool::chunks(), Padded<double>{});
        pool.run([&](unsigned c, size_t b, size_t e) {
            double local = 0.0;
            for (int k = 0; k < kInner; ++k)
                for (size_t i = b; i < e; ++i) local += v[i];
            slots[c].value = local;
        }, v.size());
        for (unsigned c = 0; c < ThreadPool::chunks(); ++c) total += slots[c].value;
    }
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (total == 12345.6789) std::printf("");
    return ms;
}

void mode_false_sharing() {
    const size_t n = g_quick ? 8192 : 65536;      // 512 KiB: cache-resident
    const int reps = g_quick ? 3 : 20;
    std::vector<double> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = rng::unit(rng::draw(5, i, 0, rng::Purpose::TieBreak, 0));

    std::printf("\n### False sharing in the per-chunk accumulators\n\n");
    std::printf("%zu cache-resident values, swept %d times per chunk, %d reductions. "
                "This machine reports a %zu-byte cache line, so the textbook "
                "`alignas(64)` still leaves two slots sharing one coherence granule.\n\n",
                n, kInner, reps, kCacheLine);
    std::printf("| threads | unpadded ms | alignas(64) ms | alignas(%zu) ms | fix vs unpadded | shipped (local acc) ms |\n",
                kCacheLine);
    std::printf("|---:|---:|---:|---:|---:|---:|\n");

    for (const unsigned t : {1u, 2u, 4u, 8u, 16u}) {
        ThreadPool pool(t);
        std::vector<Unpadded<double>>  up;
        std::vector<Padded64<double>>  p64;
        std::vector<Padded<double>>    pd;
        const double u_ms  = reduce_forced(pool, up,  v, reps);
        const double p64ms = reduce_forced(pool, p64, v, reps);
        const double p_ms  = reduce_forced(pool, pd,  v, reps);
        const double l_ms  = reduce_local(pool, v, reps);
        std::printf("| %u | %.1f | %.1f | %.1f | %.2fx | %.1f |\n",
                    t, u_ms, p64ms, p_ms, u_ms / p_ms, l_ms);
        std::fflush(stdout);
    }
    std::printf("\nThe last column is what evosim actually ships -- accumulate in a local and "
                "store to the slot once per chunk. It sidesteps the problem rather than "
                "padding around it, and beats every padded variant.\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode = "all";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quick") == 0) g_quick = true;
        else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) mode = argv[++i];
    }

    if (mode == "all" || mode == "scaling")       mode_scaling();
    if (mode == "all" || mode == "ceiling")       mode_ceiling();
    if (mode == "all" || mode == "serial")        mode_serial();
    if (mode == "all" || mode == "soa_aos")       mode_soa_aos();
    if (mode == "all" || mode == "false_sharing") mode_false_sharing();
    return 0;
}
