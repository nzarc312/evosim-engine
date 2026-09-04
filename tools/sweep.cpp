// Parameter sweep over the commons: does greed evolve up or down, and does the
// resource survive?
//
// Every configuration is an independent World, so the sweep runs them on
// separate threads with a one-thread pool each. That is safe for exactly the
// reason the rest of this project exists: the RNG is stateless and every write
// is index-stable, so a run's result does not depend on what else is running.
// Results are collected by config index and written in that order, so the CSVs
// are byte-identical whatever --jobs is set to.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "evosim/config.hpp"
#include "evosim/thread_pool.hpp"
#include "evosim/world.hpp"

using namespace evosim;

namespace {

struct Spec {
    std::string grid;
    uint64_t    seed = 42;
    double      initial_greed;      // <0 = mixed founders
    double      regen;
    double      collapse;
    double      recolonise;
    int         world_scale;
    double      greed_b     = -1.0;   // second founder group; <0 = single group
    double      greed_split = 0.5;    // fraction of founders in the first group
};

struct Sample {
    uint64_t tick;
    uint64_t population;
    uint64_t sources;
    double   mean_greed;
    double   std_greed;
    double   stock;
};

struct Outcome {
    Spec                spec;
    std::vector<Sample> series;
    uint64_t            extinct_tick = 0;      // 0 = survived
    uint64_t            final_pop = 0, min_pop = 0, max_pop = 0;
    uint64_t            final_sources = 0, min_sources = 0;
    double              greed_start = 0.0, greed_end = 0.0;
    double              total_harvest = 0.0;
    uint64_t            total_collapsed = 0;
    uint64_t            hash = 0;
    const char*         verdict = "";
};

Config config_for(const Spec& s) {
    Config c;
    const double k = static_cast<double>(s.world_scale);
    c.world.width = c.world.height = 500.0 * k;
    c.population.initial_agents = static_cast<uint32_t>(1000.0 * k * k);
    // Bound the buffers: the default cap of 500k agents per world is far more
    // than any of these configurations should reach, and several sweeps run
    // concurrently.
    c.population.max_agents     = static_cast<uint32_t>(50000.0 * k * k);
    c.population.initial_greed   = s.initial_greed;
    c.population.initial_greed_b = s.greed_b;
    c.population.greed_split     = s.greed_split;
    c.food.target_count         = static_cast<uint32_t>(2000.0 * k * k);
    c.food.energy_per_unit      = 3.0;
    c.food.regen_rate           = s.regen;
    c.food.collapse_threshold   = s.collapse;
    // Recolonisation is a whole-world rate, so it has to scale with the number
    // of source slots or a bigger world is silently a harsher one.
    c.food.recolonise_rate      = s.recolonise * k * k;
    return c;
}

Outcome run(const Spec& spec, uint64_t seed, uint64_t ticks, uint64_t sample_every) {
    Outcome o;
    o.spec = spec;
    const Config cfg = config_for(spec);

    ThreadPool pool(1);
    World w(cfg, seed, &pool);

    o.min_pop     = cfg.population.initial_agents;
    o.max_pop     = cfg.population.initial_agents;
    o.min_sources = cfg.food.target_count;
    o.greed_start = w.trait_stats().mean_greed;

    for (uint64_t t = 0; t < ticks; ++t) {
        w.step(DT);
        o.total_harvest   += w.stats().harvest_total;
        o.total_collapsed += w.stats().collapsed;

        const uint64_t pop = w.population();
        o.min_pop = std::min<uint64_t>(o.min_pop, pop);
        o.max_pop = std::max<uint64_t>(o.max_pop, pop);
        o.min_sources = std::min<uint64_t>(o.min_sources, w.stats().food_active);
        if (pop == 0 && o.extinct_tick == 0) o.extinct_tick = w.tick();

        if (w.tick() % sample_every == 0) {
            const TraitStats ts = w.trait_stats();
            o.series.push_back({w.tick(), pop, w.stats().food_active,
                                ts.mean_greed, ts.std_greed, w.stats().stock_total});
        }
    }

    const TraitStats ts = w.trait_stats();
    o.greed_end     = ts.mean_greed;
    o.final_pop     = w.population();
    o.final_sources = w.stats().food_active;
    o.hash          = w.state_hash();

    const double source_frac = static_cast<double>(o.final_sources) /
                               static_cast<double>(cfg.food.target_count);
    if (o.final_pop == 0)        o.verdict = "extinct";
    else if (source_frac < 0.10) o.verdict = "resource-collapse";
    else if (o.final_pop < cfg.population.initial_agents / 20) o.verdict = "remnant";
    else                         o.verdict = "sustained";
    return o;
}

std::vector<Spec> build_grid(const std::string& which) {
    std::vector<Spec> out;
    const double greeds[] = {0.10, 0.15, 0.25, 0.50, 0.85, -1.0};

    if (which == "economy" || which == "all") {
        for (const double g : greeds)
            for (const double regen : {1.5, 3.0, 6.0})
                for (const double coll : {0.05, 0.10, 0.20})
                    for (const double rec : {0.0, 2.0, 10.0, 50.0})
                        out.push_back({"economy", 0, g, regen, coll, rec, 1});
    }
    if (which == "competition" || which == "all") {
        // Two strategies in one world. The pure-strategy grids say which greed
        // levels are viable alone; this asks which one wins when they meet.
        for (const double split : {0.10, 0.25, 0.50, 0.75, 0.90})
            for (const double gb : {0.50, 0.90})
                for (const double rec : {2.0, 10.0})
                    out.push_back({"competition", 0, 0.12, 3.0, 0.10, rec, 1, gb, split});
    }
    if (which == "critical" || which == "all") {
        // The economy grid showed recolonisation as a step function: 0 is
        // always extinction, 20 is always survival. The whole question of
        // whether greed matters lives in the gap, so walk it finely with
        // founder greed as the other axis.
        for (const double g : {0.05, 0.10, 0.15, 0.20, 0.25, 0.35, 0.50, 0.75, 1.00})
            for (const double rec : {0.0, 1.0, 2.0, 5.0, 10.0, 25.0, 50.0})
                out.push_back({"critical", 0, g, 3.0, 0.10, rec, 1});
    }
    if (which == "dispersal" || which == "all") {
        // Same densities, bigger world. A lineage's footprint shrinks relative
        // to the world, so the damage a greedy family does stays in the patch
        // its own descendants inherit.
        for (const double g : greeds)
            for (const int k : {1, 2, 4})
                for (const double rec : {2.0, 10.0})
                    out.push_back({"dispersal", 0, g, 3.0, 0.10, rec, k});
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::string which = "all", out_dir = ".";
    uint64_t ticks = 8000, seed = 42, sample_every = 200;
    // Default to a quarter of the machine, not all of it. A sweep is a
    // background chore; it should not make the box unusable while it runs.
    // Raise it with --jobs when nothing else needs the cores.
    const unsigned hw = std::thread::hardware_concurrency();
    unsigned jobs = hw > 4 ? hw / 4 : 1;
    uint64_t n_seeds = 1;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char*) { return std::string(argv[++i]); };
        if      (a == "--grid"    && i + 1 < argc) which   = next("");
        else if (a == "--out-dir" && i + 1 < argc) out_dir = next("");
        else if (a == "--ticks"   && i + 1 < argc) ticks   = std::stoull(next(""));
        else if (a == "--seed"    && i + 1 < argc) seed    = std::stoull(next(""));
        else if (a == "--jobs"    && i + 1 < argc) jobs    = static_cast<unsigned>(std::stoul(next("")));
        else if (a == "--sample"  && i + 1 < argc) sample_every = std::stoull(next(""));
        else if (a == "--seeds"   && i + 1 < argc) n_seeds = std::stoull(next(""));
        else { std::fprintf(stderr, "sweep: unknown option %s\n", a.c_str()); return 2; }
    }
    if (jobs == 0) jobs = 1;

    // Open the outputs before doing any work. Discovering an unwritable path
    // after a multi-minute sweep has already run is a cruel way to find out.
    const std::string sum_path = out_dir + "/sweep_summary.csv";
    const std::string ser_path = out_dir + "/sweep_series.csv";
    std::FILE* fs = std::fopen(sum_path.c_str(), "w");
    std::FILE* fr = std::fopen(ser_path.c_str(), "w");
    if (!fs || !fr) {
        std::fprintf(stderr, "sweep: cannot write to %s (does the directory exist?)\n",
                     out_dir.c_str());
        return 2;
    }

    // Replicate every configuration across seeds. One run per cell is a single
    // draw from a stochastic process, and the first version of this sweep read
    // noise as signal because of it.
    std::vector<Spec> specs;
    for (const Spec& base : build_grid(which))
        for (uint64_t s_i = 0; s_i < n_seeds; ++s_i) {
            Spec sp = base;
            sp.seed = seed + s_i;
            specs.push_back(sp);
        }
    std::vector<Outcome> results(specs.size());
    std::atomic<size_t> next_idx{0};
    std::atomic<size_t> done{0};

    std::fprintf(stderr, "sweep: %zu configurations x %llu ticks on %u jobs "
                         "(machine has %u cores; raise with --jobs)\n",
                 specs.size(), static_cast<unsigned long long>(ticks), jobs, hw);

    std::vector<std::thread> workers;
    for (unsigned j = 0; j < jobs; ++j)
        workers.emplace_back([&] {
            for (;;) {
                const size_t i = next_idx.fetch_add(1);
                if (i >= specs.size()) return;
                results[i] = run(specs[i], specs[i].seed, ticks, sample_every);
                const size_t d = done.fetch_add(1) + 1;
                if (d % 10 == 0 || d == specs.size())
                    std::fprintf(stderr, "  %zu/%zu\n", d, specs.size());
            }
        });
    for (std::thread& t : workers) t.join();

    std::fprintf(fs, "config,grid,seed,initial_greed,regen_rate,collapse_threshold,recolonise_rate,"
                     "world_scale,greed_b,greed_split,initial_agents,source_slots,verdict,extinct_tick,final_pop,"
                     "min_pop,max_pop,final_sources,min_sources,greed_start,greed_end,greed_delta,"
                     "total_harvest,total_collapsed,state_hash\n");
    std::fprintf(fr, "config,grid,seed,initial_greed,regen_rate,collapse_threshold,recolonise_rate,"
                     "world_scale,tick,population,sources,mean_greed,std_greed,stock\n");

    for (size_t i = 0; i < results.size(); ++i) {
        const Outcome& o = results[i];
        const Config c = config_for(o.spec);
        std::fprintf(fs,
            "%zu,%s,%llu,%.4g,%.4g,%.4g,%.4g,%d,%.4g,%.4g,%u,%u,%s,%llu,%llu,%llu,%llu,%llu,%llu,"
            "%.6f,%.6f,%.6f,%.6f,%llu,%016llx\n",
            i, o.spec.grid.c_str(), (unsigned long long)o.spec.seed, o.spec.initial_greed, o.spec.regen, o.spec.collapse,
            o.spec.recolonise, o.spec.world_scale, o.spec.greed_b, o.spec.greed_split,
            c.population.initial_agents,
            c.food.target_count, o.verdict,
            (unsigned long long)o.extinct_tick, (unsigned long long)o.final_pop,
            (unsigned long long)o.min_pop, (unsigned long long)o.max_pop,
            (unsigned long long)o.final_sources, (unsigned long long)o.min_sources,
            o.greed_start, o.greed_end, o.greed_end - o.greed_start,
            o.total_harvest, (unsigned long long)o.total_collapsed,
            (unsigned long long)o.hash);

        for (const Sample& s : o.series)
            std::fprintf(fr, "%zu,%s,%llu,%.4g,%.4g,%.4g,%.4g,%d,%llu,%llu,%llu,%.6f,%.6f,%.6f\n",
                i, o.spec.grid.c_str(), (unsigned long long)o.spec.seed, o.spec.initial_greed, o.spec.regen, o.spec.collapse,
                o.spec.recolonise, o.spec.world_scale,
                (unsigned long long)s.tick, (unsigned long long)s.population,
                (unsigned long long)s.sources, s.mean_greed, s.std_greed, s.stock);
    }
    std::fclose(fs);
    std::fclose(fr);
    std::fprintf(stderr, "sweep: wrote %s and %s\n", sum_path.c_str(), ser_path.c_str());
    return 0;
}
