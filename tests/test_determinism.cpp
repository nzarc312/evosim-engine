// Same seed, same answer -- every time, from any starting condition, on either
// neighbour path. This is the single-threaded oracle that M6 diffs against.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "evosim/world.hpp"
#include "test_util.hpp"

using namespace evosim;

namespace {

Config test_config() {
    Config c;
    c.population.initial_agents = 1000;
    c.food.target_count = 2000;
    return c;
}

// Hash after every `every` ticks, so a divergence can be located rather than
// just detected.
std::vector<uint64_t> hash_trajectory(uint64_t seed, uint64_t ticks, bool naive,
                                      uint64_t every = 100) {
    World w(test_config(), seed);
    w.set_naive(naive);
    std::vector<uint64_t> out;
    out.reserve(ticks / every + 1);
    for (uint64_t i = 0; i < ticks; ++i) {
        w.step(DT);
        if (w.tick() % every == 0) out.push_back(w.state_hash());
    }
    return out;
}

void compare(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b,
             const char* what, uint64_t every = 100) {
    CHECK_MSG(a.size() == b.size(), std::string(what) + ": sample count differs");
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        if (a[i] == b[i]) continue;
        char buf[192];
        std::snprintf(buf, sizeof(buf), "%s: first divergence at tick %llu (%016llx vs %016llx)",
                      what, static_cast<unsigned long long>((i + 1) * every),
                      static_cast<unsigned long long>(a[i]),
                      static_cast<unsigned long long>(b[i]));
        CHECK_MSG(false, buf);
        return;   // one report is enough; everything after is downstream noise
    }
    CHECK_MSG(true, "");
}

}  // namespace

int main(int argc, char** argv) {
    // 5000 ticks twice at seed 12345, identical the whole way.
    const auto a = hash_trajectory(12345, 5000, false);
    const auto b = hash_trajectory(12345, 5000, false);
    compare(a, b, "seed 12345 rerun");
    CHECK_MSG(!a.empty() && a.back() != 0, "trajectory is empty -- test is vacuous");

    // Several seeds, and different seeds must actually produce different states
    // (otherwise the hash is not hashing anything that matters).
    std::vector<uint64_t> finals;
    for (const uint64_t seed : {uint64_t{1}, uint64_t{2}, uint64_t{999}, uint64_t{0}}) {
        const auto x = hash_trajectory(seed, 1200, false);
        const auto y = hash_trajectory(seed, 1200, false);
        compare(x, y, ("seed " + std::to_string(seed) + " rerun").c_str());
        finals.push_back(x.back());
    }
    for (size_t i = 0; i < finals.size(); ++i)
        for (size_t j = i + 1; j < finals.size(); ++j)
            CHECK_MSG(finals[i] != finals[j], "two different seeds produced the same state");

    // The grid and the naive scan are two implementations of one query and must
    // agree bit for bit, not approximately.
    compare(hash_trajectory(4242, 1500, true), hash_trajectory(4242, 1500, false),
            "naive vs grid");

    // Two worlds stepped alternately must not influence each other. This is the
    // check that catches accidental global or static mutable state, which is
    // exactly the kind of thing that survives a same-seed rerun and then breaks
    // the moment anything runs concurrently.
    {
        World p(test_config(), 31337), q(test_config(), 31337);
        World solo(test_config(), 31337);
        bool ok = true;
        for (int i = 0; i < 1500; ++i) {
            p.step(DT);
            q.step(DT);
            solo.step(DT);
            if (p.state_hash() != solo.state_hash() || q.state_hash() != solo.state_hash()) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "interleaved worlds diverged at tick %d", i + 1);
                CHECK_MSG(false, buf);
                ok = false;
                break;
            }
        }
        CHECK_MSG(ok, "");
    }

    // A hash must depend on everything it claims to cover: mutate one field of
    // the state and the hash has to move.
    {
        Config c = test_config();
        World w1(c, 5150);
        c.food.target_count += 1;
        World w2(c, 5150);
        for (int i = 0; i < 50; ++i) { w1.step(DT); w2.step(DT); }
        CHECK_MSG(w1.state_hash() != w2.state_hash(), "hash ignores the food set");
    }

    // Optional: dump the trajectory so an outside script can diff a Debug build
    // against a Release build. A hardcoded golden constant would be wrong here,
    // because libm's log/cos are not bit-identical across platforms -- the claim
    // is reproducibility on a machine, not a universal constant.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hash-out") != 0 || i + 1 >= argc) continue;
        if (std::FILE* f = std::fopen(argv[i + 1], "w")) {
            for (size_t k = 0; k < a.size(); ++k)
                std::fprintf(f, "%llu %016llx\n", static_cast<unsigned long long>((k + 1) * 100),
                             static_cast<unsigned long long>(a[k]));
            std::fclose(f);
            std::printf("wrote %zu hashes to %s\n", a.size(), argv[i + 1]);
        } else {
            CHECK_MSG(false, std::string("cannot write ") + argv[i + 1]);
        }
    }

    return evosim::test::finish("test_determinism");
}
