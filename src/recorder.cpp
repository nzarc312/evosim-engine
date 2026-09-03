#include "evosim/recorder.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "evosim/world.hpp"

namespace evosim {
namespace {

void put_u8 (std::vector<uint8_t>& b, uint8_t v)  { b.push_back(v); }
void put_u16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(uint8_t(v)); b.push_back(uint8_t(v >> 8)); }
void put_u32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i))); }
void put_u64(std::vector<uint8_t>& b, uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(uint8_t(v >> (8 * i))); }
void put_f32(std::vector<uint8_t>& b, double v) {
    const float f = static_cast<float>(v);
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    put_u32(b, bits);
}
void put_f64(std::vector<uint8_t>& b, double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    put_u64(b, bits);
}

uint16_t quant(double v, double extent) {
    if (!(extent > 0.0)) return 0;
    double t = v / extent;
    t = std::min(std::max(t, 0.0), 1.0);
    return static_cast<uint16_t>(t * 65535.0 + 0.5);
}

}  // namespace

Recorder::~Recorder() { close(); }

bool Recorder::open(const std::string& path, const World& w, uint64_t total_ticks,
                    uint64_t every, uint32_t max_agents) {
    f_ = std::fopen(path.c_str(), "wb");
    if (!f_) return false;

    every_      = every ? every : 1;
    max_agents_ = max_agents;
    world_w_    = w.config().world.width;
    world_h_    = w.config().world.height;
    capacity_   = w.config().food.capacity;

    std::vector<uint8_t> h;
    h.insert(h.end(), kMagic, kMagic + 8);
    put_f64(h, world_w_);
    put_f64(h, world_h_);
    put_f64(h, capacity_);
    put_u64(h, total_ticks);
    put_u64(h, every_);
    put_u32(h, max_agents_);
    put_u32(h, static_cast<uint32_t>(w.food().count()));
    std::fwrite(h.data(), 1, h.size(), f_);

    // Frame count is only known at the end; leave a slot and rewrite it.
    frame_count_pos_ = std::ftell(f_);
    uint64_t placeholder = 0;
    std::fwrite(&placeholder, sizeof(placeholder), 1, f_);
    return true;
}

void Recorder::capture(const World& w) {
    if (!f_) return;

    const AgentBuffer& a = w.agents();
    const FoodBuffer&  f = w.food();
    const size_t pop = a.count();

    // Stride sampling from index 0. Compaction is order-preserving, so the same
    // slots track roughly the same part of the population across frames.
    const size_t stride = (max_agents_ && pop > max_agents_)
                              ? (pop + max_agents_ - 1) / max_agents_ : 1;
    size_t sampled = 0;
    for (size_t i = 0; i < pop; i += stride) ++sampled;

    buf_.clear();
    put_u64(buf_, w.tick());
    put_u32(buf_, static_cast<uint32_t>(pop));
    put_u32(buf_, static_cast<uint32_t>(w.stats().food_active));
    const TraitStats ts = w.trait_stats();
    put_f32(buf_, ts.mean_greed);
    put_f32(buf_, ts.std_greed);
    put_f32(buf_, ts.mean_speed);
    put_f32(buf_, ts.mean_sense);
    put_f32(buf_, w.stats().stock_total);
    put_u32(buf_, static_cast<uint32_t>(sampled));

    for (size_t i = 0; i < pop; i += stride) {
        put_u16(buf_, quant(a.pos_x[i], world_w_));
        put_u16(buf_, quant(a.pos_y[i], world_h_));
        // greed is in [0,1] by construction, so a byte is plenty.
        put_u8 (buf_, static_cast<uint8_t>(std::min(1.0, std::max(0.0, a.gene_greed[i])) * 255.0 + 0.5));
        put_u16(buf_, static_cast<uint16_t>(a.lineage[i] & 0xFFFFu));
    }

    for (size_t i = 0; i < f.count(); ++i) {
        put_u16(buf_, quant(f.pos_x[i], world_w_));
        put_u16(buf_, quant(f.pos_y[i], world_h_));
        // 0 is reserved for "collapsed", so a live source floors at 1.
        uint8_t s = 0;
        if (f.alive[i]) {
            const double frac = capacity_ > 0.0 ? f.stock[i] / capacity_ : 0.0;
            s = static_cast<uint8_t>(std::min(1.0, std::max(0.0, frac)) * 254.0 + 1.0);
        }
        put_u8(buf_, s);
    }

    std::fwrite(buf_.data(), 1, buf_.size(), f_);
    ++frames_;
}

void Recorder::close() {
    if (!f_) return;
    std::fseek(f_, frame_count_pos_, SEEK_SET);
    std::fwrite(&frames_, sizeof(frames_), 1, f_);
    std::fclose(f_);
    f_ = nullptr;
}

}  // namespace evosim
