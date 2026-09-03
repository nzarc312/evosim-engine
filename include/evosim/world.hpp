#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "evosim/config.hpp"
#include "evosim/reduction.hpp"
#include "evosim/spatial_grid.hpp"
#include "evosim/thread_pool.hpp"

namespace evosim {

constexpr double DT = 1.0 / 60.0;

// Structure-of-arrays, not array-of-structs: the movement loop touches four of
// nine fields, and AoS would drag the other five through cache for nothing.
// (bench_scaling --mode soa_aos measures exactly how much that is worth.)
struct AgentBuffer {
    std::vector<uint64_t> id;
    std::vector<double>   pos_x, pos_y, vel_x, vel_y, energy;
    std::vector<double>   gene_speed, gene_size, gene_sense, gene_greed;
    std::vector<uint32_t> age;
    std::vector<uint32_t> cooldown;   // ticks left before the next bite
    std::vector<uint32_t> lineage;   // founder ancestor, inherited unchanged
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
                  double speed, double size, double sense, double greed,
                  uint32_t age_, uint32_t lineage_);
};

// A renewable food source, not a pickup. `stock` regrows toward capacity while
// the source is alive; harvesting it below the collapse threshold kills it, and
// only recolonisation brings it back.
//
// Invariant worth stating: a living source always holds at least
// collapse_threshold * capacity, because the collapse check runs immediately
// after every bite. So `alive` alone is a sufficient harvestable predicate, and
// the spatial grid can filter on it exactly as it filtered on `active` before.
struct FoodBuffer {
    std::vector<double>  pos_x, pos_y;
    std::vector<double>  stock;
    std::vector<uint8_t> alive;

    size_t count() const { return pos_x.size(); }
    void   resize(size_t n);
};

// One agent's bid for one food item. Collected per chunk during the parallel
// phase, then resolved serially -- see World::p5_resolve_claims.
struct Claim {
    uint32_t food_idx;
    uint32_t agent_idx;
};

// Per-chunk telemetry accumulators, aligned to the real cache line so two
// chunks never share one -- see kCacheLine in reduction.hpp.
struct alignas(kCacheLine) TraitPartials {
    double speed = 0.0, speed_sq = 0.0;
    double size  = 0.0, size_sq  = 0.0;
    double sense = 0.0, sense_sq = 0.0;
    double greed = 0.0, greed_sq = 0.0;
    double energy = 0.0;
};

// Population aggregates. Computed with sums and sums-of-squares in one pass:
// the two-pass form is numerically nicer but needs a second traversal, and this
// shape is what P9 turns into a fixed-order parallel reduction in M6.
struct TraitStats {
    double mean_speed = 0.0, std_speed = 0.0;
    double mean_size  = 0.0, std_size  = 0.0;
    double mean_sense = 0.0, std_sense = 0.0;
    double mean_greed = 0.0, std_greed = 0.0;
    double mean_energy = 0.0;
};

// Wall-clock spent in each phase, accumulated across ticks. Off by default:
// the timing calls are cheap but a benchmark should not pay for them unless it
// is the thing being measured.
struct PhaseTimes {
    double grid_build  = 0.0;   // P1-P3 (P1 and P3 parallel, P2 serial)
    double grid_serial = 0.0;   // the P2 prefix sum inside the above
    double p4_agents   = 0.0;   // parallel
    double p5_claims   = 0.0;   // SERIAL
    double p6_deaths   = 0.0;   // parallel
    double p7_compact  = 0.0;   // SERIAL
    double p8_food     = 0.0;   // SERIAL
    double total       = 0.0;

    double serial() const { return grid_serial + p5_claims + p7_compact + p8_food; }
    double serial_fraction() const { return total > 0.0 ? serial() / total : 0.0; }
    // Amdahl: the most any thread count can ever buy you.
    double amdahl_ceiling() const {
        const double s = serial_fraction();
        return s > 0.0 ? 1.0 / s : 0.0;
    }
};

struct TickStats {
    size_t   population    = 0;
    size_t   food_active   = 0;   // sources still alive
    uint32_t births        = 0;
    uint32_t deaths        = 0;
    uint32_t eaten         = 0;   // bites taken this tick
    uint32_t collapsed     = 0;   // sources harvested to death this tick
    uint32_t recolonised   = 0;   // dead sources brought back this tick
    double   stock_total   = 0.0; // standing biomass across all live sources
    double   harvest_total = 0.0; // biomass removed this tick
};

class World {
public:
    // `pool` may be null, in which case the world runs on an internal pool of
    // one. There is no separate serial code path: a one-thread pool spawns no
    // threads and dispatches nothing, so single-threaded runs execute the same
    // phases with the same chunk decomposition as a sixteen-thread run.
    World(const Config& cfg, uint64_t seed, ThreadPool* pool = nullptr);

    void step(double dt);

    // FNV-1a over the whole front buffer, the food state and the tick counter.
    // Every claim this project makes is a comparison of these values: run vs
    // run, debug vs release, 1 thread vs 16. When a run diverges, binary-search
    // the first tick whose hash differs.
    uint64_t state_hash() const;

    // Bumped whenever the phase order or the hashed field set changes, so that
    // hashes recorded by an older build can never be silently compared against
    // a newer one.
    // v2: added the greed trait, lineage ids, and renewable food stock.
    static constexpr uint64_t kStateHashVersion = 2;

    uint64_t          tick() const { return tick_; }
    uint64_t          seed() const { return seed_; }
    const Config&     config() const { return cfg_; }
    const AgentBuffer& agents() const { return front_; }
    const FoodBuffer&  food() const { return food_; }
    const TickStats&   stats() const { return stats_; }

    void               set_profiling(bool on) { profiling_ = on; }
    const PhaseTimes&  phase_times() const { return phases_; }
    void               reset_phase_times() { phases_ = PhaseTimes{}; }
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
    void p9_telemetry() const;
    void p7_compact_and_reproduce();
    void p8_respawn_food(double dt);

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
    // Fractional recolonisation carried between ticks. Hashed, because it is
    // simulation state that affects when the next source comes back.
    double      recolonise_accum_ = 0.0;

    ThreadPool*                 pool_ = nullptr;
    std::unique_ptr<ThreadPool>  owned_pool_;

    // Per-chunk buffers, one slot per fixed chunk. Nothing here is indexed by
    // thread id: that is the whole trick behind thread-count invariance.
    std::vector<std::vector<Claim>> chunk_claims_;
    // Reusable per-chunk neighbour-candidate buffers. Allocating one of these
    // per agent per tick would cost more than the query it serves.
    std::vector<std::vector<uint32_t>> chunk_candidates_;
    SpatialGrid                        grid_;
    std::vector<Claim>              all_claims_;
    std::vector<uint8_t>            eaten_;   // per-agent: got food this tick

    // Reduction scratch, reused every tick: a per-tick allocation here would
    // cost more than the reduction it serves.
    mutable std::vector<Padded<double>> max_slots_;
    mutable std::vector<TraitPartials>  trait_slots_;
    mutable TraitStats                  trait_cache_;

    TickStats  stats_;
    PhaseTimes phases_;
    bool       profiling_ = false;
};

}  // namespace evosim
