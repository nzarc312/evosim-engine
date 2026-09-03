#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "evosim/config.hpp"
#include "evosim/world.hpp"

using namespace evosim;

namespace {

struct Options {
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
        else { std::cerr << "evosim: unknown option '" << a << "'\n"; usage(); return false; }
    }
    return true;
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
    if (opt.threads >= 0) cfg.threading.threads = static_cast<unsigned>(opt.threads);

    std::cout << "evosim: seed=" << opt.seed << " ticks=" << opt.ticks << "\n"
              << cfg.to_string();

    World world(cfg, opt.seed);
    world.set_naive(opt.naive);

    const auto t0 = std::chrono::steady_clock::now();
    for (uint64_t i = 0; i < opt.ticks; ++i) {
        world.step(DT);   // headless: no clock, no variable dt
        if (opt.verify && world.tick() % 1000 == 0)
            std::cout << "tick " << world.tick() << "  pop " << world.population()
                      << "  food " << world.stats().food_active
                      << "  eaten " << world.stats().eaten
                      << "  mean_energy " << world.mean_energy() << "\n";
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::cout << "evosim: " << world.tick() << " ticks in " << secs << " s  ("
              << (secs * 1000.0 / static_cast<double>(opt.ticks ? opt.ticks : 1))
              << " ms/tick)\n"
              << "        final population " << world.population()
              << ", food active " << world.stats().food_active << "\n";
    return 0;
}
