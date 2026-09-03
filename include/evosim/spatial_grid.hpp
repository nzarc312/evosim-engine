// Uniform grid built by counting sort into flat arrays.
//
// Not a vector-of-vectors: that cannot be filled in parallel deterministically,
// and it scatters the candidate list across the heap. A counting sort into
// three flat arrays is both faster and deterministic by construction -- within
// a cell, items land in ascending index order no matter how the build was
// partitioned, so the built grid is identical at any thread count.
//
// The grid indexes the *food* set, not the agents: cell size is the largest
// query radius in the population (section 7.4), and what agents query for is
// food. Agents are the things doing the querying.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace evosim {

class ThreadPool;

class SpatialGrid {
public:
    // Rebuild over a point set. Inactive points are excluded entirely.
    // `pool` may be null, in which case the build runs serially; the result is
    // bit-identical either way.
    void build(const double* xs, const double* ys, const uint8_t* active, size_t n,
               double world_w, double world_h, double cell_size, ThreadPool* pool = nullptr);

    // Append every point whose cell may fall within `r` of (x,y) to `out`.
    // `out` is caller-owned and reused: a per-tick heap allocation here would
    // dominate the benchmark and hide the result being measured.
    // The returned set is a superset -- the caller still does the exact
    // distance test, which it has to do anyway.
    void query(double x, double y, double r, std::vector<uint32_t>& out) const;

    size_t   cells() const { return static_cast<size_t>(nx_) * static_cast<size_t>(ny_); }
    uint32_t nx() const { return nx_; }
    uint32_t ny() const { return ny_; }
    size_t   indexed() const { return sorted_.size(); }
    // Milliseconds spent in the serial part of the last build (the P2 prefix
    // sum). Reported separately because it is one of the terms in the measured
    // serial fraction.
    double   last_serial_ms() const { return last_serial_ms_; }
    bool     last_build_parallel() const { return last_build_parallel_; }

    double   cell_w() const { return cell_w_; }
    double   cell_h() const { return cell_h_; }

    // Largest number of cells along one axis. Bounds the memory a degenerate
    // (tiny) cell size can ask for.
    static constexpr uint32_t kMaxCellsPerAxis = 4096;

private:
    uint32_t cell_index(double x, double y) const;

    std::vector<uint32_t> cell_of_;      // [n]          cell index per point
    std::vector<uint32_t> cell_starts_;  // [n_cells+1]  prefix sums
    std::vector<uint32_t> sorted_;       // [n_active]   point indices, cell-major
    std::vector<uint32_t> counts_;       // scratch, reused across ticks
    std::vector<uint32_t> chunk_counts_; // scratch for the parallel build

    double   last_serial_ms_ = 0.0;
    bool     last_build_parallel_ = false;

    uint32_t nx_ = 1, ny_ = 1;
    double   world_w_ = 1.0, world_h_ = 1.0;
    double   cell_w_ = 1.0, cell_h_ = 1.0;
    double   inv_cell_w_ = 1.0, inv_cell_h_ = 1.0;
};

}  // namespace evosim
