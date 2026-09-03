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
    } population;

    struct Food {
        uint32_t target_count    = 2000;
        uint32_t spawn_rate      = 50;
        double   energy_per_food = 25.0;
    } food;

    struct Energy {
        double   base_cost       = 0.05;
        double   move_coef       = 0.02;
        double   sense_coef      = 0.01;
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
