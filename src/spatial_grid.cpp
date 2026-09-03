#include "evosim/spatial_grid.hpp"

#include <algorithm>
#include <cmath>

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
    (void)pool;   // M6 makes P1 and P3 parallel; the result is unchanged.

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

    // P1: cell index per point. Pure function of position -- parallel-safe.
    cell_of_.resize(n);
    for (size_t i = 0; i < n; ++i)
        cell_of_[i] = (active && !active[i]) ? 0xFFFFFFFFu : cell_index(xs[i], ys[i]);

    // P2 (serial): histogram, then prefix sum into bucket offsets.
    counts_.assign(n_cells + 1, 0u);
    size_t n_active = 0;
    for (size_t i = 0; i < n; ++i) {
        if (cell_of_[i] == 0xFFFFFFFFu) continue;
        ++counts_[cell_of_[i] + 1];
        ++n_active;
    }
    cell_starts_.resize(n_cells + 1);
    cell_starts_[0] = 0;
    for (size_t c = 0; c < n_cells; ++c)
        cell_starts_[c + 1] = cell_starts_[c] + counts_[c + 1];

    // P3: scatter. Walking i in ascending order means each cell's bucket ends
    // up sorted by point index, which is what makes the grid's contents
    // independent of how the build was partitioned.
    sorted_.resize(n_active);
    counts_.assign(cell_starts_.begin(), cell_starts_.end());   // cursors
    for (size_t i = 0; i < n; ++i) {
        const uint32_t c = cell_of_[i];
        if (c == 0xFFFFFFFFu) continue;
        sorted_[counts_[c]++] = static_cast<uint32_t>(i);
    }
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
