#pragma once

#include <cstdint>
#include <vector>

#include "evosim/config.hpp"

namespace evosim {

constexpr double DT = 1.0 / 60.0;

// Structure-of-arrays, not array-of-structs: the movement loop touches four of
// nine fields, and AoS would drag the other five through cache for nothing.
// (bench_scaling --mode soa_aos measures exactly how much that is worth.)
struct AgentBuffer {
    std::vector<uint64_t> id;
    std::vector<double>   pos_x, pos_y, vel_x, vel_y, energy;
    std::vector<double>   gene_speed, gene_size, gene_sense;
    std::vector<uint32_t> age;
    std::vector<uint8_t>  alive;

    size_t count() const { return pos_x.size(); }
    void   resize(size_t n);
    void   clear();
    void   reserve(size_t n);
    // Append agent `i` of `src`. Used by the serial compaction in P7, which
    // must preserve relative order.
    void   append_from(const AgentBuffer& src, size_t i);
};

struct FoodBuffer {
    std::vector<double>  pos_x, pos_y;
    std::vector<uint8_t> active;

    size_t count() const { return pos_x.size(); }
    void   resize(size_t n);
};

// One agent's bid for one food item. Collected per chunk during the parallel
// phase, then resolved serially -- see World::p5_resolve_claims.
struct Claim {
    uint32_t food_idx;
    uint32_t agent_idx;
};

struct TickStats {
    size_t   population   = 0;
    size_t   food_active  = 0;
    uint32_t births       = 0;
    uint32_t deaths       = 0;
    uint32_t eaten        = 0;   // food items consumed this tick
};

class World {
public:
    World(const Config& cfg, uint64_t seed);

    void step(double dt);

    uint64_t          tick() const { return tick_; }
    uint64_t          seed() const { return seed_; }
    const Config&     config() const { return cfg_; }
    const AgentBuffer& agents() const { return front_; }
    const FoodBuffer&  food() const { return food_; }
    const TickStats&   stats() const { return stats_; }
    size_t             population() const { return front_.count(); }
    double             mean_energy() const;

    // M4 introduces the counting-sort grid; the naive O(n^2) scan stays
    // permanently as the benchmark baseline and the correctness oracle.
    void set_naive(bool naive) { naive_ = naive; }
    bool naive() const { return naive_; }

private:
    void seed_population();
    void seed_food();

    void p4_agents(double dt);
    void p5_resolve_claims();
    void p6_mark_deaths();
    void p7_compact_and_reproduce();
    void p8_respawn_food();

    // Toroidal minimum-image delta.
    double wrap_delta(double d, double extent) const;
    double wrap_pos(double p, double extent) const;

    // Nearest active food to (x,y) within `radius`, or -1. Ties break to the
    // lower food index so the result cannot depend on scan order.
    int32_t nearest_food_naive(double x, double y, double radius) const;

    Config   cfg_;
    uint64_t seed_ = 0;
    uint64_t tick_ = 0;
    bool     naive_ = true;

    AgentBuffer front_, back_;   // read front_, write back_, swap at tick end
    FoodBuffer  food_;
    uint64_t    next_agent_id_ = 0;

    // Per-chunk claim buffers. One chunk while the step is serial; P4 becomes
    // parallel in M6 and the chunk count becomes fixed at kNumChunks.
    std::vector<std::vector<Claim>> chunk_claims_;
    std::vector<Claim>              all_claims_;
    std::vector<uint8_t>            eaten_;   // per-agent: got food this tick

    TickStats stats_;
};

}  // namespace evosim
