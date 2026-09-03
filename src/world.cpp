#include "evosim/world.hpp"

#include <algorithm>
#include <cmath>

#include "evosim/genome.hpp"
#include "evosim/rng.hpp"

namespace evosim {
namespace {

constexpr double kInitialEnergy   = 50.0;
constexpr double kCapacityBase    = 100.0;  // capacity = kCapacityBase * (1 + size)
constexpr double kEatRadiusCoef   = 1.0;    // eating radius = coef * size
constexpr double kMaxTurn         = 0.40;   // radians of wander per tick
constexpr double kTwoPi           = 6.283185307179586476925286766559;

double capacity_of(double size) { return kCapacityBase * (1.0 + size); }

}  // namespace

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------

void AgentBuffer::resize(size_t n) {
    id.resize(n);
    pos_x.resize(n); pos_y.resize(n);
    vel_x.resize(n); vel_y.resize(n);
    energy.resize(n);
    gene_speed.resize(n); gene_size.resize(n); gene_sense.resize(n);
    age.resize(n);
    alive.resize(n);
}

void AgentBuffer::clear() { resize(0); }

void AgentBuffer::reserve(size_t n) {
    id.reserve(n);
    pos_x.reserve(n); pos_y.reserve(n);
    vel_x.reserve(n); vel_y.reserve(n);
    energy.reserve(n);
    gene_speed.reserve(n); gene_size.reserve(n); gene_sense.reserve(n);
    age.reserve(n);
    alive.reserve(n);
}

void AgentBuffer::append_from(const AgentBuffer& s, size_t i) {
    id.push_back(s.id[i]);
    pos_x.push_back(s.pos_x[i]); pos_y.push_back(s.pos_y[i]);
    vel_x.push_back(s.vel_x[i]); vel_y.push_back(s.vel_y[i]);
    energy.push_back(s.energy[i]);
    gene_speed.push_back(s.gene_speed[i]);
    gene_size.push_back(s.gene_size[i]);
    gene_sense.push_back(s.gene_sense[i]);
    age.push_back(s.age[i]);
    alive.push_back(s.alive[i]);
}

void AgentBuffer::append(uint64_t id_, double x, double y, double vx, double vy, double e,
                         double speed, double size, double sense, uint32_t age_) {
    id.push_back(id_);
    pos_x.push_back(x); pos_y.push_back(y);
    vel_x.push_back(vx); vel_y.push_back(vy);
    energy.push_back(e);
    gene_speed.push_back(speed); gene_size.push_back(size); gene_sense.push_back(sense);
    age.push_back(age_);
    alive.push_back(1);
}

void FoodBuffer::resize(size_t n) {
    pos_x.resize(n); pos_y.resize(n); active.resize(n);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

World::World(const Config& cfg, uint64_t seed) : cfg_(cfg), seed_(seed) {
    chunk_claims_.resize(1);
    chunk_candidates_.resize(1);
    seed_population();
    seed_food();
}

void World::seed_population() {
    const size_t n = cfg_.population.initial_agents;
    front_.reserve(n * 2);
    front_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const uint64_t id = next_agent_id_++;
        front_.id[i]    = id;
        front_.pos_x[i] = rng::range(seed_, id, 0, rng::Purpose::InitAgent, 0, 0.0, cfg_.world.width);
        front_.pos_y[i] = rng::range(seed_, id, 0, rng::Purpose::InitAgent, 1, 0.0, cfg_.world.height);
        front_.gene_speed[i] = genome::seed_trait(genome::kSpeed, seed_, id, 2);
        front_.gene_size[i]  = genome::seed_trait(genome::kSize,  seed_, id, 3);
        front_.gene_sense[i] = genome::seed_trait(genome::kSense, seed_, id, 4);

        const double heading = kTwoPi * rng::unit(rng::draw(seed_, id, 0, rng::Purpose::InitAgent, 5));
        front_.vel_x[i] = std::cos(heading) * front_.gene_speed[i];
        front_.vel_y[i] = std::sin(heading) * front_.gene_speed[i];
        front_.energy[i] = kInitialEnergy;
        front_.age[i]    = 0;
        front_.alive[i]  = 1;
    }
    stats_.population = n;
}

void World::seed_food() {
    const size_t n = cfg_.food.target_count;
    food_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        food_.pos_x[i]  = rng::range(seed_, i, 0, rng::Purpose::FoodSpawn, 0, 0.0, cfg_.world.width);
        food_.pos_y[i]  = rng::range(seed_, i, 0, rng::Purpose::FoodSpawn, 1, 0.0, cfg_.world.height);
        food_.active[i] = 1;
    }
    stats_.food_active = n;
}

double World::mean_energy() const {
    if (front_.count() == 0) return 0.0;
    double s = 0.0;
    for (const double e : front_.energy) s += e;
    return s / static_cast<double>(front_.count());
}

TraitStats World::trait_stats() const {
    TraitStats t;
    const size_t n = front_.count();
    if (n == 0) return t;

    double s1 = 0.0, s2 = 0.0, z1 = 0.0, z2 = 0.0, e1 = 0.0, e2 = 0.0, en = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double a = front_.gene_speed[i];
        const double b = front_.gene_size[i];
        const double c = front_.gene_sense[i];
        s1 += a; s2 += a * a;
        z1 += b; z2 += b * b;
        e1 += c; e2 += c * c;
        en += front_.energy[i];
    }
    const double inv = 1.0 / static_cast<double>(n);
    auto sd = [](double sum, double sumsq, double invn) {
        const double m = sum * invn;
        const double v = sumsq * invn - m * m;
        return v > 0.0 ? std::sqrt(v) : 0.0;   // clamp the cancellation floor
    };
    t.mean_speed = s1 * inv; t.std_speed = sd(s1, s2, inv);
    t.mean_size  = z1 * inv; t.std_size  = sd(z1, z2, inv);
    t.mean_sense = e1 * inv; t.std_sense = sd(e1, e2, inv);
    t.mean_energy = en * inv;
    return t;
}

// ---------------------------------------------------------------------------
// Geometry -- the world is a torus, so every distance is a minimum image.
// ---------------------------------------------------------------------------

double World::wrap_delta(double d, double extent) const {
    if (d >  0.5 * extent) return d - extent;
    if (d < -0.5 * extent) return d + extent;
    return d;
}

double World::wrap_pos(double p, double extent) const {
    // A single tick cannot move an agent more than one world width, so a pair
    // of conditionals is enough and avoids fmod's rounding behaviour.
    if (p <  0.0)    return p + extent;
    if (p >= extent) return p - extent;
    return p;
}

int32_t World::nearest_food_naive(double x, double y, double radius) const {
    const double r2 = radius * radius;
    double  best  = r2;
    int32_t best_i = -1;
    const size_t n = food_.count();
    for (size_t f = 0; f < n; ++f) {
        if (!food_.active[f]) continue;
        const double dx = wrap_delta(food_.pos_x[f] - x, cfg_.world.width);
        const double dy = wrap_delta(food_.pos_y[f] - y, cfg_.world.height);
        const double d2 = dx * dx + dy * dy;
        // Strict '<' plus ascending scan order means ties go to the lower
        // index. Never '<=': that would make the answer depend on scan order.
        if (d2 < best) { best = d2; best_i = static_cast<int32_t>(f); }
    }
    return best_i;
}

// ---------------------------------------------------------------------------
// The step
// ---------------------------------------------------------------------------

void World::step(double dt) {
    stats_.births = 0;
    stats_.deaths = 0;
    stats_.eaten  = 0;

    if (!naive_) p1_p3_build_grid();
    p4_agents(dt);
    p5_resolve_claims();
    p6_mark_deaths();
    p7_compact_and_reproduce();
    p8_respawn_food();

    std::swap(front_, back_);
    ++tick_;

    stats_.population = front_.count();
}

double World::max_query_radius() const {
    // max() is associative and commutative and exact in floating point, so this
    // reduction is order-independent -- it needs no fixed-order treatment.
    double r = 1.0;   // floor: a degenerate population must not make cells tiny
    const size_t n = front_.count();
    for (size_t i = 0; i < n; ++i)
        r = std::max(r, std::max(front_.gene_sense[i], kEatRadiusCoef * front_.gene_size[i]));
    return r;
}

// P1-P3: rebuild the food grid. Cell size tracks the evolving population.
void World::p1_p3_build_grid() {
    grid_.build(food_.pos_x.data(), food_.pos_y.data(), food_.active.data(), food_.count(),
                cfg_.world.width, cfg_.world.height, max_query_radius(), nullptr);
}

int32_t World::nearest_food_grid(double x, double y, double radius,
                                 std::vector<uint32_t>& scratch) const {
    grid_.query(x, y, radius, scratch);
    const double r2 = radius * radius;
    double  best   = r2;
    int32_t best_i = -1;
    for (const uint32_t f : scratch) {
        if (!food_.active[f]) continue;
        const double dx = wrap_delta(food_.pos_x[f] - x, cfg_.world.width);
        const double dy = wrap_delta(food_.pos_y[f] - y, cfg_.world.height);
        const double d2 = dx * dx + dy * dy;
        // The grid hands back candidates in ascending index order within each
        // cell, but cells are visited in block order, so index order across the
        // whole candidate list is NOT guaranteed. Strict '<' alone would then
        // pick a different winner than the naive scan on an exact tie, so ties
        // are broken explicitly on the index.
        if (d2 < best || (d2 == best && best_i >= 0 && static_cast<int32_t>(f) < best_i)) {
            best = d2;
            best_i = static_cast<int32_t>(f);
        }
    }
    return best_i;
}

// P4: sense -> steer -> integrate -> energy drain -> age.
// Reads front_, writes back_, records food claims. No agent reads back_, and
// no agent writes any index but its own -- that is what makes it parallel-safe
// in M6 without changing a line of the physics.
void World::p4_agents(double dt) {
    const size_t n = front_.count();
    back_.resize(n);
    eaten_.assign(n, 0);
    for (auto& c : chunk_claims_) c.clear();

    const double w = cfg_.world.width;
    const double h = cfg_.world.height;
    // Costs are per simulated second; dt is fixed, so this is a constant, but
    // writing it out keeps the energy model independent of the timestep.
    const double base   = cfg_.energy.base_cost;
    const double movec  = cfg_.energy.move_coef;
    const double sensec = cfg_.energy.sense_coef;

    for (size_t i = 0; i < n; ++i) {
        const uint64_t id    = front_.id[i];
        const double   speed = front_.gene_speed[i];
        const double   size  = front_.gene_size[i];
        const double   sense = front_.gene_sense[i];
        const double   x     = front_.pos_x[i];
        const double   y     = front_.pos_y[i];

        // --- sense ---
        // One query per agent, not two. The radius covers both jobs: steering
        // uses the target only if it is inside the sense radius, eating only if
        // the post-move distance is inside the (usually much smaller) eating
        // radius. An agent can only eat what it could sense.
        const double eat_r = kEatRadiusCoef * size;
        const double query_r = std::max(sense, eat_r);
        const int32_t target = naive_ ? nearest_food_naive(x, y, query_r)
                                      : nearest_food_grid(x, y, query_r, chunk_candidates_[0]);

        bool steer_to_target = false;
        if (target >= 0) {
            const double tx = wrap_delta(food_.pos_x[target] - x, w);
            const double ty = wrap_delta(food_.pos_y[target] - y, h);
            steer_to_target = (tx * tx + ty * ty) <= sense * sense;
        }

        // --- steer ---
        double dirx, diry;
        if (steer_to_target) {
            dirx = wrap_delta(food_.pos_x[target] - x, w);
            diry = wrap_delta(food_.pos_y[target] - y, h);
            const double len = std::sqrt(dirx * dirx + diry * diry);
            if (len > 1e-12) { dirx /= len; diry /= len; }
            else             { dirx = 1.0; diry = 0.0; }
        } else {
            // Wander: keep the current heading and perturb it. A fresh random
            // direction every tick would just be Brownian noise and would go
            // nowhere.
            dirx = front_.vel_x[i];
            diry = front_.vel_y[i];
            const double len = std::sqrt(dirx * dirx + diry * diry);
            if (len > 1e-12) {
                dirx /= len; diry /= len;
            } else {
                const double a = kTwoPi * rng::unit(rng::draw(seed_, id, tick_, rng::Purpose::Wander, 0));
                dirx = std::cos(a); diry = std::sin(a);
            }
            const double turn = (rng::unit(rng::draw(seed_, id, tick_, rng::Purpose::Wander, 1)) - 0.5)
                                * 2.0 * kMaxTurn;
            const double c = std::cos(turn), s = std::sin(turn);
            const double nx = dirx * c - diry * s;
            const double ny = dirx * s + diry * c;
            dirx = nx; diry = ny;
        }

        const double vx = dirx * speed;
        const double vy = diry * speed;

        // --- integrate ---
        const double nx = wrap_pos(x + vx * dt, w);
        const double ny = wrap_pos(y + vy * dt, h);

        back_.id[i]         = id;
        back_.pos_x[i]      = nx;
        back_.pos_y[i]      = ny;
        back_.vel_x[i]      = vx;
        back_.vel_y[i]      = vy;
        back_.gene_speed[i] = speed;
        back_.gene_size[i]  = size;
        back_.gene_sense[i] = sense;
        back_.age[i]        = front_.age[i] + 1;
        back_.alive[i]      = front_.alive[i];

        // --- energy drain ---
        // Superlinear in speed and cubic in size: this cost function IS the
        // selection pressure. Make it linear and every trait saturates.
        const double cost = base + movec * speed * speed * size * size * size + sensec * sense;
        back_.energy[i] = front_.energy[i] - cost * dt;

        // --- claim ---
        if (target >= 0) {
            const double bx = wrap_delta(food_.pos_x[target] - nx, w);
            const double by = wrap_delta(food_.pos_y[target] - ny, h);
            if (bx * bx + by * by <= eat_r * eat_r)
                chunk_claims_[0].push_back({static_cast<uint32_t>(target),
                                            static_cast<uint32_t>(i)});
        }
    }
}

// P5 (serial): concatenate the per-chunk claim vectors in chunk order, sort by
// (food_idx, agent_idx), and award each food item to the lowest agent index.
// Racing for the food would be nondeterministic; this is not, and it is cheap
// because claims are far fewer than agents.
void World::p5_resolve_claims() {
    all_claims_.clear();
    for (const auto& c : chunk_claims_)
        all_claims_.insert(all_claims_.end(), c.begin(), c.end());

    std::sort(all_claims_.begin(), all_claims_.end(), [](const Claim& a, const Claim& b) {
        if (a.food_idx != b.food_idx) return a.food_idx < b.food_idx;
        return a.agent_idx < b.agent_idx;
    });

    const double gain = cfg_.food.energy_per_food;
    uint32_t prev_food = 0xFFFFFFFFu;
    for (const Claim& c : all_claims_) {
        if (c.food_idx == prev_food) continue;   // already awarded, lowest index won
        prev_food = c.food_idx;
        if (!food_.active[c.food_idx]) continue;
        food_.active[c.food_idx] = 0;
        ++stats_.eaten;
        const size_t a = c.agent_idx;
        back_.energy[a] = std::min(back_.energy[a] + gain, capacity_of(back_.gene_size[a]));
        eaten_[a] = 1;
    }
}

void World::p6_mark_deaths() {
    const size_t n = back_.count();
    const uint32_t max_age = cfg_.energy.max_age;
    for (size_t i = 0; i < n; ++i)
        if (back_.energy[i] <= 0.0 || back_.age[i] > max_age) back_.alive[i] = 0;
}

// P7 (serial): stable compaction, then offspring appended in parent-index
// order. Relative order is preserved on purpose -- swap-and-pop would reorder
// agents and diverge two otherwise identical runs.
void World::p7_compact_and_reproduce() {
    const size_t n = back_.count();
    size_t w = 0;
    for (size_t r = 0; r < n; ++r) {
        if (!back_.alive[r]) { ++stats_.deaths; continue; }
        if (w != r) {
            back_.id[w]         = back_.id[r];
            back_.pos_x[w]      = back_.pos_x[r];
            back_.pos_y[w]      = back_.pos_y[r];
            back_.vel_x[w]      = back_.vel_x[r];
            back_.vel_y[w]      = back_.vel_y[r];
            back_.energy[w]     = back_.energy[r];
            back_.gene_speed[w] = back_.gene_speed[r];
            back_.gene_size[w]  = back_.gene_size[r];
            back_.gene_sense[w] = back_.gene_sense[r];
            back_.age[w]        = back_.age[r];
            back_.alive[w]      = 1;
        }
        ++w;
    }
    back_.resize(w);

    // Reproduction. Parent splits its energy with the child; the child's genome
    // is the parent's plus a clamped gaussian step. Agent ids come from a
    // serial counter, so an agent's random stream is stable across the
    // compaction that keeps renumbering array indices.
    const double   thr       = cfg_.energy.repro_threshold;
    const double   sigma     = cfg_.mutation.sigma;
    const size_t   max_pop   = cfg_.population.max_agents;
    const size_t   parents_n = w;
    for (size_t i = 0; i < parents_n; ++i) {
        if (back_.energy[i] < thr) continue;
        if (back_.count() >= max_pop) break;

        const double half   = back_.energy[i] * 0.5;
        const double p_spd  = back_.gene_speed[i];
        const double p_siz  = back_.gene_size[i];
        const double p_sen  = back_.gene_sense[i];
        const double px     = back_.pos_x[i];
        const double py     = back_.pos_y[i];
        back_.energy[i] = half;

        const uint64_t cid = next_agent_id_++;
        // gaussian() consumes sub and sub+1, so traits step by two.
        const double c_spd = genome::mutate(p_spd, genome::kSpeed, sigma, seed_, cid, tick_, 0);
        const double c_siz = genome::mutate(p_siz, genome::kSize,  sigma, seed_, cid, tick_, 2);
        const double c_sen = genome::mutate(p_sen, genome::kSense, sigma, seed_, cid, tick_, 4);
        const double a = kTwoPi * rng::unit(rng::draw(seed_, cid, tick_, rng::Purpose::Mutation, 6));

        back_.append(cid, px, py, std::cos(a) * c_spd, std::sin(a) * c_spd, half,
                     c_spd, c_siz, c_sen, 0);
        ++stats_.births;
    }
}

// P8 (serial): top the food back up to target density. Scanning from index 0
// every tick keeps slot reuse in a fixed order.
void World::p8_respawn_food() {
    size_t active = 0;
    for (const uint8_t a : food_.active) active += a;

    const size_t target = cfg_.food.target_count;
    uint32_t budget = cfg_.food.spawn_rate;
    const size_t n = food_.count();
    for (size_t f = 0; f < n && active < target && budget > 0; ++f) {
        if (food_.active[f]) continue;
        food_.pos_x[f]  = rng::range(seed_, f, tick_, rng::Purpose::FoodSpawn, 0, 0.0, cfg_.world.width);
        food_.pos_y[f]  = rng::range(seed_, f, tick_, rng::Purpose::FoodSpawn, 1, 0.0, cfg_.world.height);
        food_.active[f] = 1;
        ++active;
        --budget;
    }
    stats_.food_active = active;
}

}  // namespace evosim
