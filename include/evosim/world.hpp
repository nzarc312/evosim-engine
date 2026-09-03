#pragma once

#include <cstdint>
#include <vector>

#include "evosim/config.hpp"
#include "evosim/spatial_grid.hpp"

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
    // Append from scalars. Offspring are appended to the buffer being iterated,
    // so the values must be read out before any push_back can reallocate.
    void   append(uint64_t id_, double x, double y, double vx, double vy, double e,
                  double speed, double size, double sense, uint32_t age_);
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

// Population aggregates. Computed with sums and sums-of-squares in one pass:
// the two-pass form is numerically nicer but needs a second traversal, and this
// shape is what P9 turns into a fixed-order parallel reduction in M6.
struct TraitStats {
    double mean_speed = 0.0, std_speed = 0.0;
    double mean_size  = 0.0, std_size  = 0.0;
    double mean_sense = 0.0, std_sense = 0.0;
    double mean_energy = 0.0;
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
    TraitStats         trait_stats() const;

    // M4 introduces the counting-sort grid; the naive O(n^2) scan stays
    // permanently as the benchmark baseline and the correctness oracle.
    void set_naive(bool naive) { naive_ = naive; }
    const SpatialGrid& grid() const { return grid_; }
    bool naive() const { return naive_; }

private:
    void seed_population();
    void seed_food();

    // Largest query radius in the current population. This is the grid's cell
    // size, so it has to be recomputed every tick: sense_radius is a heritable
    // trait and it evolves.
    double max_query_radius() const;

    void p1_p3_build_grid();
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
    // Same answer, via the grid. Kept next to the naive version on purpose:
    // test_spatial_grid asserts they agree exactly.
    int32_t nearest_food_grid(double x, double y, double radius,
                              std::vector<uint32_t>& scratch) const;

    Config   cfg_;
    uint64_t seed_ = 0;
    uint64_t tick_ = 0;
    bool     naive_ = false;

    AgentBuffer front_, back_;   // read front_, write back_, swap at tick end
    FoodBuffer  food_;
    uint64_t    next_agent_id_ = 0;

    // Per-chunk claim buffers. One chunk while the step is serial; P4 becomes
    // parallel in M6 and the chunk count becomes fixed at kNumChunks.
    std::vector<std::vector<Claim>> chunk_claims_;
    // Reusable per-chunk neighbour-candidate buffers. Allocating one of these
    // per agent per tick would cost more than the query it serves.
    std::vector<std::vector<uint32_t>> chunk_candidates_;
    SpatialGrid                        grid_;
    std::vector<Claim>              all_claims_;
    std::vector<uint8_t>            eaten_;   // per-agent: got food this tick

    TickStats stats_;
};

}  // namespace evosim
