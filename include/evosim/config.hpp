// Configuration: a small hand-rolled TOML subset parser. Deliberately zero
// third-party dependencies -- the config grammar in use here is a dozen
// scalar keys, and a vendored library would be more code than the parser.
//
// Supported: [section] headers, key = value pairs, '#' comments, and multiple
// pairs on one line separated by ';'.
#pragma once

#include <cstdint>
#include <string>

namespace evosim {

struct Config {
    struct World {
        double width  = 500.0;
        double height = 500.0;
    } world;

    struct Population {
        uint32_t initial_agents = 1000;
        uint32_t max_agents     = 500000;
        // Founder greed. Negative means "draw uniformly across the trait range";
        // a value in [0,1] starts every founder at exactly that greed, which is
        // how the greedy-vs-prudent comparison is set up.
        double   initial_greed  = -1.0;
    } population;

    // Food is a renewable resource, not a pickup. Each source holds a stock of
    // biomass that regrows toward `capacity`; harvesting it below
    // `collapse_threshold` of capacity kills it permanently. `recolonise_rate`
    // is the only way a dead source ever comes back, and setting it to zero
    // makes collapse irreversible.
    struct Food {
        uint32_t target_count       = 2000;   // number of source slots
        uint32_t spawn_rate         = 50;     // initial fill rate, sources/tick
        double   capacity           = 25.0;   // max stock per source
        double   energy_per_unit    = 3.0;    // energy per unit of stock eaten
        double   regen_rate         = 3.00;   // stock regrown per second
        double   collapse_threshold = 0.10;   // fraction of capacity; below = dead
        double   recolonise_rate    = 10.0;   // dead sources revived per second
        // Ticks an agent must wait between bites. Without this an agent camps
        // on a source and bites 60 times a second, which drains it to death
        // whatever its greed -- and then greed has no ecological consequence
        // at all. A bite has to be a discrete visit for prudence to mean
        // anything.
        uint32_t digest_ticks       = 60;
    } food;

    // Costs are per simulated second and are applied as `energy -= cost * dt`.
    // The spec's starting values (0.05 / 0.02 / 0.01) are the same numbers read
    // as per-tick costs; at the 60 Hz timestep that is ~30x too cheap and the
    // population grows without bound, so they were tuned per section 6.4 until
    // the population oscillates. See README "Tuning the energy economy".
    struct Energy {
        double   base_cost       = 1.00;
        double   move_coef       = 0.35;
        double   sense_coef      = 0.15;
        double   repro_threshold = 100.0;
        uint32_t max_age         = 3000;
    } energy;

    struct Mutation {
        double sigma = 0.05;
    } mutation;

    struct Threading {
        unsigned threads = 0;  // 0 = hardware_concurrency
    } threading;

    // "section.key" -> value. Returns false for an unknown key or a value that
    // does not parse; the caller decides whether that is fatal.
    bool set(const std::string& dotted_key, const std::string& value);

    // Throws std::runtime_error on a missing file or a malformed line.
    static Config from_file(const std::string& path);

    // Parse from an in-memory buffer. Same errors as from_file.
    static Config from_string(const std::string& text, const std::string& origin = "<string>");

    std::string to_string() const;
};

}  // namespace evosim
