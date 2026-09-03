#include "evosim/spatial_grid.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "evosim/thread_pool.hpp"

namespace evosim {

uint32_t SpatialGrid::cell_index(double x, double y) const {
    int32_t cx = static_cast<int32_t>(x * inv_cell_w_);
    int32_t cy = static_cast<int32_t>(y * inv_cell_h_);
    // Positions are already wrapped into [0,extent), but clamp anyway: a value
    // exactly equal to the extent, or a rounding artefact at the boundary,
    // must not index off the end of the array.
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= static_cast<int32_t>(nx_)) cx = static_cast<int32_t>(nx_) - 1;
    if (cy >= static_cast<int32_t>(ny_)) cy = static_cast<int32_t>(ny_) - 1;
    return static_cast<uint32_t>(cy) * nx_ + static_cast<uint32_t>(cx);
}

void SpatialGrid::build(const double* xs, const double* ys, const uint8_t* active, size_t n,
                        double world_w, double world_h, double cell_size, ThreadPool* pool) {
    world_w_ = world_w;
    world_h_ = world_h;

    const double min_cell_w = world_w / static_cast<double>(kMaxCellsPerAxis);
    const double min_cell_h = world_h / static_cast<double>(kMaxCellsPerAxis);
    const double cw = std::max(cell_size, min_cell_w);
    const double ch = std::max(cell_size, min_cell_h);

    nx_ = std::max(1u, static_cast<uint32_t>(world_w / cw));
    ny_ = std::max(1u, static_cast<uint32_t>(world_h / ch));
    cell_w_ = world_w / static_cast<double>(nx_);
    cell_h_ = world_h / static_cast<double>(ny_);
    inv_cell_w_ = 1.0 / cell_w_;
    inv_cell_h_ = 1.0 / cell_h_;

    const size_t n_cells = cells();
    constexpr uint32_t kInactive = 0xFFFFFFFFu;

    // P1 (parallel): cell index per point. A pure function of position, written
    // to the point's own slot, so it parallelises with nothing to coordinate.
    cell_of_.resize(n);
    if (pool != nullptr) {
        pool->run([&](unsigned, size_t b, size_t e) {
            for (size_t i = b; i < e; ++i)
                cell_of_[i] = (active && !active[i]) ? kInactive : cell_index(xs[i], ys[i]);
        }, n);
    } else {
        for (size_t i = 0; i < n; ++i)
            cell_of_[i] = (active && !active[i]) ? kInactive : cell_index(xs[i], ys[i]);
    }

    // The parallel scatter needs a per-chunk histogram, whose prefix pass costs
    // chunks * n_cells. Taking it only when that is no larger than the scatter
    // it accelerates means it needs roughly kNumChunks points per cell -- a
    // genuinely dense grid. This simulation's food density gives about two
    // points per cell, so the shipped configuration takes the serial path; see
    // the note in the README. Both paths emit byte-identical output, so the
    // choice can never move a state hash, only the time to reach it.
    last_build_parallel_ = false;
    const bool parallel_scatter =
        pool != nullptr && pool->size() > 1 &&
        static_cast<uint64_t>(n_cells) * ThreadPool::chunks() <= static_cast<uint64_t>(n);

    cell_starts_.resize(n_cells + 1);

    if (!parallel_scatter) {
        // P2 (serial): histogram, then prefix sum into bucket offsets.
        const auto t0 = std::chrono::steady_clock::now();
        counts_.assign(n_cells + 1, 0u);
        size_t n_active = 0;
        for (size_t i = 0; i < n; ++i) {
            if (cell_of_[i] == kInactive) continue;
            ++counts_[cell_of_[i] + 1];
            ++n_active;
        }
        cell_starts_[0] = 0;
        for (size_t c = 0; c < n_cells; ++c)
            cell_starts_[c + 1] = cell_starts_[c] + counts_[c + 1];

        // P3: scatter. Walking i in ascending order means each cell's bucket
        // ends up sorted by point index, which is what makes the grid's
        // contents independent of how the build was partitioned.
        last_serial_ms_ =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        sorted_.resize(n_active);
        counts_.assign(cell_starts_.begin(), cell_starts_.end());   // cursors
        for (size_t i = 0; i < n; ++i) {
            const uint32_t c = cell_of_[i];
            if (c == kInactive) continue;
            sorted_[counts_[c]++] = static_cast<uint32_t>(i);
        }
        return;
    }
    last_build_parallel_ = true;

    // P2a (parallel): each chunk histograms only its own slice of the points,
    // into its own row. No sharing, so no atomics and no contention.
    const unsigned C = ThreadPool::chunks();
    chunk_counts_.assign(static_cast<size_t>(C) * n_cells, 0u);
    pool->run([&](unsigned chunk, size_t b, size_t e) {
        uint32_t* row = chunk_counts_.data() + static_cast<size_t>(chunk) * n_cells;
        for (size_t i = b; i < e; ++i) {
            const uint32_t c = cell_of_[i];
            if (c != kInactive) ++row[c];
        }
    }, n);

    // P2b (serial): for each cell, lay the chunks out in chunk order and turn
    // each chunk's count into its write cursor. Chunks are contiguous ascending
    // index ranges, so cursors assigned in chunk order give each cell a bucket
    // sorted by point index -- exactly what the serial scatter produces.
    const auto t0 = std::chrono::steady_clock::now();
    uint32_t running = 0;
    for (size_t k = 0; k < n_cells; ++k) {
        cell_starts_[k] = running;
        for (unsigned c = 0; c < C; ++c) {
            uint32_t& slot = chunk_counts_[static_cast<size_t>(c) * n_cells + k];
            const uint32_t cnt = slot;
            slot = running;
            running += cnt;
        }
    }
    cell_starts_[n_cells] = running;
    last_serial_ms_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    // P3 (parallel): every chunk writes only into the ranges its own cursors
    // point at, which no other chunk can touch.
    sorted_.resize(running);
    pool->run([&](unsigned chunk, size_t b, size_t e) {
        uint32_t* row = chunk_counts_.data() + static_cast<size_t>(chunk) * n_cells;
        for (size_t i = b; i < e; ++i) {
            const uint32_t c = cell_of_[i];
            if (c != kInactive) sorted_[row[c]++] = static_cast<uint32_t>(i);
        }
    }, n);
}

void SpatialGrid::query(double x, double y, double r, std::vector<uint32_t>& out) const {
    out.clear();
    if (sorted_.empty()) return;

    // Cell span covering the radius. With cell size set to the largest query
    // radius in the population this is the 3x3 block; the general form costs
    // nothing and keeps the grid correct if a query ever exceeds one cell.
    const int32_t hx = static_cast<int32_t>(std::ceil(r * inv_cell_w_));
    const int32_t hy = static_cast<int32_t>(std::ceil(r * inv_cell_h_));

    const int32_t inx = static_cast<int32_t>(nx_);
    const int32_t iny = static_cast<int32_t>(ny_);
    const int32_t cx  = std::min(std::max(static_cast<int32_t>(x * inv_cell_w_), 0), inx - 1);
    const int32_t cy  = std::min(std::max(static_cast<int32_t>(y * inv_cell_h_), 0), iny - 1);

    // The world is a torus, so cell coordinates wrap. If the span covers the
    // whole axis, walk each cell once instead of visiting some of them twice.
    const bool all_x = (2 * hx + 1) >= inx;
    const bool all_y = (2 * hy + 1) >= iny;
    const int32_t x0 = all_x ? 0 : -hx, x1 = all_x ? inx - 1 : hx;
    const int32_t y0 = all_y ? 0 : -hy, y1 = all_y ? iny - 1 : hy;

    for (int32_t dy = y0; dy <= y1; ++dy) {
        int32_t gy = all_y ? dy : (cy + dy) % iny;
        if (gy < 0) gy += iny;
        const int32_t row = gy * inx;
        for (int32_t dx = x0; dx <= x1; ++dx) {
            int32_t gx = all_x ? dx : (cx + dx) % inx;
            if (gx < 0) gx += inx;
            const uint32_t cell  = static_cast<uint32_t>(row + gx);
            const uint32_t begin = cell_starts_[cell];
            const uint32_t end   = cell_starts_[cell + 1];
            out.insert(out.end(), sorted_.begin() + begin, sorted_.begin() + end);
        }
    }
}

}  // namespace evosim
