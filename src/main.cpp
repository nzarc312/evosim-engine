#include <chrono>
#include <thread>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "evosim/config.hpp"
#include "evosim/telemetry.hpp"
#include "evosim/thread_pool.hpp"
#include "evosim/world.hpp"

using namespace evosim;

namespace {

struct Options {
    std::vector<std::string> overrides;
    std::string config_path;
    std::string out_path;
    uint64_t    seed         = 42;
    uint64_t    ticks        = 10000;
    int         threads      = -1;   // -1 = take from config
    bool        headless     = true;
    bool        naive        = false;
    bool        bench        = false;
    bool        verify       = false;
    bool        thread_sweep = false;
};

void usage() {
    std::cout <<
        "evosim -- deterministic parallel evolution simulator\n\n"
        "usage: evosim [options]\n\n"
        "  --seed N          RNG seed (default 42)\n"
        "  --ticks N         number of ticks to simulate (default 10000)\n"
        "  --threads N       worker threads; 0 = hardware_concurrency\n"
        "  --config PATH     TOML config file (default: built-in defaults)\n"
        "  --set K=V         override one config key, e.g. --set population.initial_agents=50000\n"
        "  --out PATH        write telemetry CSV here\n"
        "  --headless        run with no renderer and no clock (default)\n"
        "  --naive           use the O(n^2) neighbor path instead of the grid\n"
        "  --bench           report timing instead of telemetry\n"
        "  --verify          print the state hash every 1000 ticks\n"
        "  --thread-sweep    run 1,2,4,8,16 threads and compare state hashes\n"
        "  --help\n";
}

bool need_value(int argc, int i, const char* flag) {
    if (i + 1 < argc) return true;
    std::cerr << "evosim: " << flag << " requires a value\n";
    return false;
}

bool parse_args(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if      (a == "--help" || a == "-h") { usage(); std::exit(0); }
        else if (a == "--headless")          o.headless = true;
        else if (a == "--naive")             o.naive = true;
        else if (a == "--bench")             o.bench = true;
        else if (a == "--verify")            o.verify = true;
        else if (a == "--thread-sweep")      o.thread_sweep = true;
        else if (a == "--seed")    { if (!need_value(argc, i, "--seed")) return false;
                                     o.seed = std::strtoull(argv[++i], nullptr, 10); }
        else if (a == "--ticks")   { if (!need_value(argc, i, "--ticks")) return false;
                                     o.ticks = std::strtoull(argv[++i], nullptr, 10); }
        else if (a == "--threads") { if (!need_value(argc, i, "--threads")) return false;
                                     o.threads = std::atoi(argv[++i]); }
        else if (a == "--config")  { if (!need_value(argc, i, "--config")) return false;
                                     o.config_path = argv[++i]; }
        else if (a == "--out")     { if (!need_value(argc, i, "--out")) return false;
                                     o.out_path = argv[++i]; }
        else if (a == "--set")     { if (!need_value(argc, i, "--set")) return false;
                                     o.overrides.emplace_back(argv[++i]); }
        else { std::cerr << "evosim: unknown option '" << a << "'\n"; usage(); return false; }
    }
    return true;
}

}  // namespace

namespace {

struct RunResult {
    uint64_t hash    = 0;
    double   ms_tick = 0.0;
    size_t   population = 0;
};

// One headless run. No clock, no variable dt, no renderer.
RunResult run_headless(const Config& cfg, const Options& opt, unsigned threads,
                       TelemetryWriter* csv, bool verbose) {
    ThreadPool pool(threads);
    World world(cfg, opt.seed, &pool);
    world.set_naive(opt.naive);

    constexpr uint64_t kTelemetryEvery = 100;
    uint64_t births_acc = 0, deaths_acc = 0;

    const auto t0 = std::chrono::steady_clock::now();
    auto block_start = t0;

    for (uint64_t i = 0; i < opt.ticks; ++i) {
        world.step(DT);
        births_acc += world.stats().births;
        deaths_acc += world.stats().deaths;

        if (world.tick() % kTelemetryEvery != 0) continue;

        const auto now = std::chrono::steady_clock::now();
        const double block_ms = std::chrono::duration<double, std::milli>(now - block_start).count();
        block_start = now;

        if (csv != nullptr && csv->is_open()) {
            const TraitStats ts = world.trait_stats();
            TelemetryRow row;
            row.tick        = world.tick();
            row.population  = world.population();
            row.food_active = world.stats().food_active;
            row.mean_speed  = ts.mean_speed;  row.std_speed = ts.std_speed;
            row.mean_size   = ts.mean_size;   row.std_size  = ts.std_size;
            row.mean_sense  = ts.mean_sense;  row.std_sense = ts.std_sense;
            row.births      = births_acc;
            row.deaths      = deaths_acc;
            row.mean_energy = ts.mean_energy;
            row.ms_per_tick = block_ms / static_cast<double>(kTelemetryEvery);
            row.state_hash  = world.state_hash();
            csv->add(row);
        }
        births_acc = deaths_acc = 0;

        if (verbose && opt.verify && world.tick() % 1000 == 0) {
            const TraitStats ts = world.trait_stats();
            std::cout << "tick " << world.tick()
                      << "  pop " << world.population()
                      << "  food " << world.stats().food_active
                      << "  speed " << ts.mean_speed
                      << "  size " << ts.mean_size
                      << "  sense " << ts.mean_sense
                      << "  E " << ts.mean_energy
                      << "  hash " << std::hex << world.state_hash() << std::dec << "\n";
        }
    }
    if (csv != nullptr) csv->flush();

    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    RunResult r;
    r.hash       = world.state_hash();
    r.ms_tick    = secs * 1000.0 / static_cast<double>(opt.ticks ? opt.ticks : 1);
    r.population = world.population();
    return r;
}

// The demo command. One invocation proves the headline claim.
int thread_sweep(const Config& cfg, const Options& opt) {
    const unsigned counts[] = {1, 2, 4, 8, 16};

    std::cout << "thread sweep: seed " << opt.seed << ", " << opt.ticks << " ticks, "
              << cfg.population.initial_agents << " initial agents\n\n"
              << "| threads | ms/tick | speedup | efficiency | population | state hash |\n"
              << "|---:|---:|---:|---:|---:|---|\n";

    double base = 0.0;
    uint64_t reference = 0;
    bool all_match = true;

    for (const unsigned t : counts) {
        const RunResult r = run_headless(cfg, opt, t, nullptr, false);
        if (t == counts[0]) { base = r.ms_tick; reference = r.hash; }
        if (r.hash != reference) all_match = false;

        char line[256];
        std::snprintf(line, sizeof(line),
                      "| %u | %.3f | %.2fx | %.0f%% | %zu | %016llx |\n",
                      t, r.ms_tick, base / r.ms_tick, 100.0 * (base / r.ms_tick) / t,
                      r.population, static_cast<unsigned long long>(r.hash));
        std::cout << line << std::flush;
    }

    std::cout << "\n";
    if (all_match) {
        std::cout << "PASS: every thread count produced state hash "
                  << std::hex << reference << std::dec << "\n";
        return 0;
    }
    std::cout << "FAIL: state hashes differ across thread counts\n";
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) return 2;

    Config cfg;
    try {
        if (!opt.config_path.empty()) cfg = Config::from_file(opt.config_path);
    } catch (const std::exception& e) {
        std::cerr << "evosim: " << e.what() << "\n";
        return 2;
    }
    for (const std::string& kv : opt.overrides) {
        const size_t eq = kv.find('=');
        if (eq == std::string::npos || !cfg.set(kv.substr(0, eq), kv.substr(eq + 1))) {
            std::cerr << "evosim: bad --set '" << kv << "'\n";
            return 2;
        }
    }
    if (opt.threads >= 0) cfg.threading.threads = static_cast<unsigned>(opt.threads);

    if (opt.thread_sweep) return thread_sweep(cfg, opt);

    unsigned threads = cfg.threading.threads;
    if (threads == 0) threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 1;

    std::cout << "evosim: seed=" << opt.seed << " ticks=" << opt.ticks
              << " threads=" << threads
              << " neighbours=" << (opt.naive ? "naive" : "grid") << "\n";
    if (!opt.bench) std::cout << cfg.to_string();

    World world(cfg, opt.seed);
    world.set_naive(opt.naive);

    TelemetryWriter csv;
    if (!opt.out_path.empty() && !csv.open(opt.out_path)) {
        std::cerr << "evosim: cannot write " << opt.out_path << "\n";
        return 2;
    }

    constexpr uint64_t kTelemetryEvery = 100;
    uint64_t births_acc = 0, deaths_acc = 0;

    const auto t0 = std::chrono::steady_clock::now();
    auto block_start = t0;

    for (uint64_t i = 0; i < opt.ticks; ++i) {
        world.step(DT);   // headless: no clock, no variable dt
        births_acc += world.stats().births;
        deaths_acc += world.stats().deaths;

        if (world.tick() % kTelemetryEvery == 0) {
            const auto now = std::chrono::steady_clock::now();
            const double block_ms =
                std::chrono::duration<double, std::milli>(now - block_start).count();
            block_start = now;

            const TraitStats ts = world.trait_stats();
            TelemetryRow row;
            row.tick        = world.tick();
            row.population  = world.population();
            row.food_active = world.stats().food_active;
            row.mean_speed  = ts.mean_speed;  row.std_speed = ts.std_speed;
            row.mean_size   = ts.mean_size;   row.std_size  = ts.std_size;
            row.mean_sense  = ts.mean_sense;  row.std_sense = ts.std_sense;
            row.births      = births_acc;
            row.deaths      = deaths_acc;
            row.mean_energy = ts.mean_energy;
            row.ms_per_tick = block_ms / static_cast<double>(kTelemetryEvery);
            row.state_hash  = world.state_hash();
            csv.add(row);
            births_acc = deaths_acc = 0;

            if (opt.verify && world.tick() % 1000 == 0)
                std::cout << "tick " << world.tick()
                          << "  pop " << row.population
                          << "  food " << row.food_active
                          << "  speed " << row.mean_speed
                          << "  size " << row.mean_size
                          << "  sense " << row.mean_sense
                          << "  E " << row.mean_energy
                          << "  " << row.ms_per_tick << " ms/tick"
                          << "  hash " << std::hex << row.state_hash << std::dec << "\n";
        }
    }
    csv.flush();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::cout << "evosim: final state hash " << std::hex << world.state_hash() << std::dec << "\n";
    std::cout << "evosim: " << world.tick() << " ticks in " << secs << " s  ("
              << (secs * 1000.0 / static_cast<double>(opt.ticks ? opt.ticks : 1))
              << " ms/tick)\n"
              << "        final population " << world.population()
              << ", food active " << world.stats().food_active << "\n";
    if (!opt.out_path.empty()) std::cout << "        telemetry -> " << opt.out_path << "\n";
    return 0;
}
