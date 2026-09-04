#include "evosim/config.hpp"

#include <stdexcept>
#include <string>

#include "test_util.hpp"

using namespace evosim;

int main() {
    // Defaults match the documented starting parameters.
    {
        const Config c;
        CHECK(c.world.width == 500.0);
        CHECK(c.population.initial_agents == 1000u);
        CHECK(c.food.target_count == 2000u);
        CHECK(c.food.capacity == 25.0);
        CHECK(c.food.collapse_threshold == 0.10);
        CHECK(c.food.digest_ticks == 60u);
        CHECK(c.population.initial_greed == -1.0);
        CHECK(c.energy.base_cost == 1.00);
        CHECK(c.energy.max_age == 3000u);
        CHECK(c.mutation.sigma == 0.05);
        CHECK(c.threading.threads == 0u);
    }

    // The shipped grammar: several pairs per line, ';' separated, '#' comments,
    // and a section header whose keys continue onto the next line.
    {
        const Config c = Config::from_string(
            "[world]      width = 501.5 ; height = 250.0\n"
            "[population] initial_agents = 123 ; max_agents = 4567\n"
            "[food]       target_count = 20 ; spawn_rate = 5 ; capacity = 25.5\n"
            "             regen_rate = 0.25 ; collapse_threshold = 0.2 ; recolonise_rate = 3.5\n"
            "[energy]     base_cost = 0.5 ; move_coef = 0.25 ; sense_coef = 0.125\n"
            "             repro_threshold = 90.0 ; max_age = 77\n"
            "[mutation]   sigma = 0.5\n"
            "[threading]  threads = 4        # 0 = hardware_concurrency\n");
        CHECK(c.world.width == 501.5);   // same line as the [world] header
        CHECK(c.world.height == 250.0);
        CHECK(c.population.initial_agents == 123u);
        CHECK(c.population.max_agents == 4567u);
        CHECK(c.food.target_count == 20u);
        CHECK(c.food.spawn_rate == 5u);
        CHECK(c.food.capacity == 25.5);
        CHECK(c.food.regen_rate == 0.25);
        CHECK(c.food.collapse_threshold == 0.2);
        CHECK(c.food.recolonise_rate == 3.5);
        CHECK(c.energy.base_cost == 0.5);
        CHECK(c.energy.move_coef == 0.25);
        CHECK(c.energy.sense_coef == 0.125);
        CHECK(c.energy.repro_threshold == 90.0);
        CHECK(c.energy.max_age == 77u);
        CHECK(c.mutation.sigma == 0.5);
        CHECK(c.threading.threads == 4u);
    }

    // Blank lines and comment-only lines are not errors.
    CHECK(Config::from_string("\n  # just a comment\n\n[world]\n\nwidth = 1.0\n").world.width == 1.0);

    // The shipped config file must round-trip to exactly its written values.
    {
        const Config c = Config::from_string(
            "[world]      width = 500.0 ; height = 500.0\n"
            "[food]       target_count = 2000 ; spawn_rate = 50 ; capacity = 25.0\n");
        CHECK(c.world.width == 500.0 && c.world.height == 500.0);
        CHECK(c.food.target_count == 2000u && c.food.spawn_rate == 50u);
        CHECK(c.food.capacity == 25.0);
    }

    // set() is the CLI override path.
    {
        Config c;
        CHECK(c.set("energy.max_age", "42"));
        CHECK(c.energy.max_age == 42u);
        CHECK(!c.set("energy.no_such_key", "1"));
        CHECK(!c.set("world.width", "not-a-number"));
        CHECK(!c.set("world.width", "1.0garbage"));
    }

    // Malformed input is rejected loudly rather than silently defaulted.
    auto throws = [](const std::string& text) {
        try { Config::from_string(text); } catch (const std::runtime_error&) { return true; }
        return false;
    };
    CHECK(throws("[world width = 1.0\n"));
    CHECK(throws("width_without_section_or_equals\n"));
    CHECK(throws("[world] widht = 1.0\n"));
    CHECK(throws("[world] width = wide\n"));

    // A round trip through to_string() re-parses to the same values.
    {
        Config a;
        a.world.height = 321.5;
        a.energy.move_coef = 0.031;
        a.population.initial_greed   = 0.12;
        a.population.initial_greed_b = 0.9;
        a.population.greed_split     = 0.25;
        a.mutation.greed_sigma       = 0.2;
        a.food.regen_rate            = 4.5;
        const Config b = Config::from_string(a.to_string(), "<roundtrip>");
        CHECK(b.world.height == 321.5);
        CHECK(b.energy.move_coef == 0.031);
        // to_string() has to emit every key it can parse, or a round trip
        // silently reverts whatever it forgot.
        CHECK(b.population.initial_greed == 0.12);
        CHECK(b.population.initial_greed_b == 0.9);
        CHECK(b.population.greed_split == 0.25);
        CHECK(b.mutation.greed_sigma == 0.2);
        CHECK(b.food.regen_rate == 4.5);
    }

    return evosim::test::finish("test_config");
}
