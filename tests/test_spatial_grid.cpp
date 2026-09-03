// The grid is only useful if it returns exactly what the brute-force scan
// returns. This test is the oracle comparison that makes the O(n^2) path worth
// keeping around permanently.
#include "evosim/spatial_grid.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "evosim/rng.hpp"
#include "evosim/world.hpp"
#include "test_util.hpp"

using namespace evosim;

namespace {

double wrap_delta(double d, double extent) {
    if (d >  0.5 * extent) return d - extent;
    if (d < -0.5 * extent) return d + extent;
    return d;
}

std::vector<uint32_t> brute_force(const std::vector<double>& xs, const std::vector<double>& ys,
                                  const std::vector<uint8_t>& active,
                                  double qx, double qy, double r, double w, double h) {
    std::vector<uint32_t> out;
    const double r2 = r * r;
    for (size_t i = 0; i < xs.size(); ++i) {
        if (!active[i]) continue;
        const double dx = wrap_delta(xs[i] - qx, w);
        const double dy = wrap_delta(ys[i] - qy, h);
        if (dx * dx + dy * dy <= r2) out.push_back(static_cast<uint32_t>(i));
    }
    return out;
}

std::vector<uint32_t> via_grid(const SpatialGrid& g,
                               const std::vector<double>& xs, const std::vector<double>& ys,
                               const std::vector<uint8_t>& active,
                               double qx, double qy, double r, double w, double h) {
    std::vector<uint32_t> cand, out;
    g.query(qx, qy, r, cand);
    const double r2 = r * r;
    for (const uint32_t i : cand) {
        if (!active[i]) continue;
        const double dx = wrap_delta(xs[i] - qx, w);
        const double dy = wrap_delta(ys[i] - qy, h);
        if (dx * dx + dy * dy <= r2) out.push_back(i);
    }
    std::sort(out.begin(), out.end());
    // The candidate list can repeat a cell only if the query span wraps onto
    // itself, which query() is supposed to prevent. Assert it does.
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

struct PointSet {
    std::vector<double>  x, y;
    std::vector<uint8_t> active;
};

PointSet make_points(size_t n, double w, double h, uint64_t seed, double inactive_frac = 0.0) {
    PointSet p;
    p.x.resize(n); p.y.resize(n); p.active.resize(n);
    for (size_t i = 0; i < n; ++i) {
        p.x[i] = rng::range(seed, i, 0, rng::Purpose::FoodSpawn, 0, 0.0, w);
        p.y[i] = rng::range(seed, i, 0, rng::Purpose::FoodSpawn, 1, 0.0, h);
        p.active[i] = rng::unit(rng::draw(seed, i, 0, rng::Purpose::FoodSpawn, 2)) >= inactive_frac;
    }
    return p;
}

// One full comparison sweep at a given configuration.
void sweep(size_t n_points, size_t n_queries, double w, double h, double cell,
           double radius, uint64_t seed, double inactive_frac, const char* label) {
    const PointSet p = make_points(n_points, w, h, seed, inactive_frac);
    SpatialGrid g;
    g.build(p.x.data(), p.y.data(), p.active.data(), n_points, w, h, cell);

    size_t total_hits = 0;
    for (size_t q = 0; q < n_queries; ++q) {
        const double qx = rng::range(seed ^ 0xABCD, q, 1, rng::Purpose::TieBreak, 0, 0.0, w);
        const double qy = rng::range(seed ^ 0xABCD, q, 1, rng::Purpose::TieBreak, 1, 0.0, h);

        auto expect = brute_force(p.x, p.y, p.active, qx, qy, radius, w, h);
        auto got    = via_grid(g, p.x, p.y, p.active, qx, qy, radius, w, h);
        std::sort(expect.begin(), expect.end());
        total_hits += expect.size();
        CHECK_MSG(expect == got,
                  std::string(label) + " query " + std::to_string(q) + ": expected " +
                  std::to_string(expect.size()) + " got " + std::to_string(got.size()));
    }
    CHECK_MSG(total_hits > 0, std::string(label) + " found nothing -- test is vacuous");
}

}  // namespace

int main() {
    // The headline case from the spec: 100 query points over 1000 items.
    sweep(1000, 100, 500.0, 500.0, /*cell*/ 15.0, /*radius*/ 15.0, 12345, 0.0, "base");

    // Radius smaller than a cell, and radius spanning several cells.
    sweep(1000, 100, 500.0, 500.0, 15.0, 4.0,  777, 0.0, "small-radius");
    sweep(1000, 100, 500.0, 500.0, 15.0, 47.0, 778, 0.0, "multi-cell");

    // Inactive points must be invisible to the grid.
    sweep(2000, 100, 500.0, 500.0, 12.0, 20.0, 999, 0.35, "with-inactive");

    // Non-square world, and cells that do not divide the extent evenly.
    sweep(1500, 100, 313.0, 97.5, 11.3, 9.0, 4242, 0.1, "ragged");

    // Degenerate grids: fewer than three cells per axis, so the 3x3 block would
    // otherwise visit the same cell twice and double-report its contents.
    sweep(500, 60, 20.0, 20.0, 15.0, 9.0,  31, 0.0, "two-cells");
    sweep(500, 60, 10.0, 10.0, 40.0, 4.0,  32, 0.0, "one-cell");
    sweep(500, 60, 500.0, 500.0, 15.0, 400.0, 33, 0.0, "radius-covers-world");

    // Dense clustering into few cells.
    sweep(5000, 50, 60.0, 60.0, 15.0, 14.0, 55, 0.0, "dense");

    // Empty and all-inactive sets must not crash and must return nothing.
    {
        SpatialGrid g;
        std::vector<uint32_t> out{7, 7, 7};
        g.build(nullptr, nullptr, nullptr, 0, 100.0, 100.0, 10.0);
        g.query(5.0, 5.0, 10.0, out);
        CHECK(out.empty());
        CHECK_EQ(g.indexed(), size_t{0});

        const PointSet p = make_points(50, 100.0, 100.0, 5, 1.1);   // all inactive
        g.build(p.x.data(), p.y.data(), p.active.data(), 50, 100.0, 100.0, 10.0);
        CHECK_EQ(g.indexed(), size_t{0});
        g.query(5.0, 5.0, 10.0, out);
        CHECK(out.empty());
    }

    // Rebuilding over a changed set must not leak state from the previous build.
    {
        SpatialGrid g;
        const PointSet a = make_points(1000, 500.0, 500.0, 1);
        const PointSet b = make_points(300,  500.0, 500.0, 2);
        g.build(a.x.data(), a.y.data(), a.active.data(), 1000, 500.0, 500.0, 15.0);
        g.build(b.x.data(), b.y.data(), b.active.data(),  300, 500.0, 500.0, 15.0);
        CHECK_EQ(g.indexed(), size_t{300});
        for (size_t q = 0; q < 50; ++q) {
            const double qx = rng::range(9, q, 0, rng::Purpose::TieBreak, 0, 0.0, 500.0);
            const double qy = rng::range(9, q, 0, rng::Purpose::TieBreak, 1, 0.0, 500.0);
            auto expect = brute_force(b.x, b.y, b.active, qx, qy, 15.0, 500.0, 500.0);
            auto got    = via_grid(g, b.x, b.y, b.active, qx, qy, 15.0, 500.0, 500.0);
            std::sort(expect.begin(), expect.end());
            CHECK(expect == got);
        }
    }

    // Every active point is indexed exactly once: no drops, no duplicates.
    {
        const PointSet p = make_points(4000, 500.0, 500.0, 606, 0.2);
        SpatialGrid g;
        g.build(p.x.data(), p.y.data(), p.active.data(), 4000, 500.0, 500.0, 15.0);
        size_t expect_active = 0;
        for (const uint8_t a : p.active) expect_active += a;
        CHECK_EQ(g.indexed(), expect_active);
    }

    // World level: the grid path and the naive path must agree tick for tick.
    {
        Config cfg;
        cfg.population.initial_agents = 400;
        cfg.food.target_count = 900;
        World naive(cfg, 2024);
        World grid (cfg, 2024);
        naive.set_naive(true);
        grid.set_naive(false);
        for (int t = 0; t < 400; ++t) { naive.step(DT); grid.step(DT); }

        CHECK_EQ(naive.population(), grid.population());
        const AgentBuffer& a = naive.agents();
        const AgentBuffer& b = grid.agents();
        bool same = a.count() == b.count();
        for (size_t i = 0; same && i < a.count(); ++i)
            same = a.id[i] == b.id[i] && a.pos_x[i] == b.pos_x[i] && a.pos_y[i] == b.pos_y[i] &&
                   a.energy[i] == b.energy[i] && a.gene_speed[i] == b.gene_speed[i] &&
                   a.gene_size[i] == b.gene_size[i] && a.gene_sense[i] == b.gene_sense[i];
        CHECK_MSG(same, "grid path diverged from the naive path");
    }

    return evosim::test::finish("test_spatial_grid");
}
