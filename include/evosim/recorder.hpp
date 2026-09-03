// Records a run to a compact binary file for the timeline viewer.
//
// A 50k-tick run at a few thousand agents is far too much data to ship whole,
// so frames are sampled in time and agents are sampled within a frame, with
// positions quantised to 16 bits. Sampling is by stride from index 0, and P7
// compaction preserves relative order, so the sample is stable from frame to
// frame rather than flickering between unrelated agents.
//
// Aggregate numbers in each frame (population, sources, mean greed) are the
// TRUE values, not the sampled ones -- only the drawn dots are a subset.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace evosim {

class World;

class Recorder {
public:
    static constexpr char     kMagic[8] = {'E','V','O','R','E','C','0','1'};
    static constexpr uint32_t kDeadSource = 0;   // stock byte 0 means collapsed

    Recorder() = default;
    ~Recorder();

    Recorder(const Recorder&)            = delete;
    Recorder& operator=(const Recorder&) = delete;

    // `every` ticks between frames; `max_agents` dots per frame.
    bool open(const std::string& path, const World& w, uint64_t total_ticks,
              uint64_t every, uint32_t max_agents);
    bool is_open() const { return f_ != nullptr; }

    void capture(const World& w);
    void close();

    uint64_t frames() const { return frames_; }
    uint64_t every() const { return every_; }

private:
    std::FILE* f_ = nullptr;
    uint64_t   every_ = 0;
    uint32_t   max_agents_ = 0;
    uint64_t   frames_ = 0;
    long       frame_count_pos_ = 0;
    double     world_w_ = 1.0, world_h_ = 1.0;
    double     capacity_ = 1.0;
    std::vector<uint8_t> buf_;
};

}  // namespace evosim
