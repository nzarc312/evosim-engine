// M1 skeleton: state layout and an empty step(). The simulation lands in M2.
#pragma once

#include <cstdint>
#include <vector>

#include "evosim/config.hpp"

namespace evosim {

constexpr double DT = 1.0 / 60.0;

// Structure-of-arrays, not array-of-structs: the movement loop touches four of
// nine fields, and AoS would drag the other five through cache for nothing.
struct AgentBuffer {
    std::vector<uint64_t> id;
    std::vector<double>   pos_x, pos_y, vel_x, vel_y, energy;
    std::vector<double>   gene_speed, gene_size, gene_sense;
    std::vector<uint32_t> age;
    std::vector<uint8_t>  alive;

    size_t count() const { return pos_x.size(); }
};

class World {
public:
    World(const Config& cfg, uint64_t seed) : cfg_(cfg), seed_(seed) {}

    void step(double /*dt*/) { ++tick_; }

    uint64_t tick() const { return tick_; }
    uint64_t seed() const { return seed_; }
    const Config& config() const { return cfg_; }

private:
    Config   cfg_;
    uint64_t seed_ = 0;
    uint64_t tick_ = 0;

    AgentBuffer front_, back_;  // read front_, write back_, swap at tick end
};

}  // namespace evosim
