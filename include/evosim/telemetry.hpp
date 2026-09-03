// CSV telemetry.
//
// Rows are buffered and flushed in blocks. Writing each row with std::endl
// instead costs a flush per row and is measurably slower over a long run --
// see the profiling note in the README.
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace evosim {

struct TelemetryRow {
    uint64_t tick        = 0;
    uint64_t population  = 0;
    uint64_t food_active = 0;
    double   mean_speed  = 0.0, std_speed = 0.0;
    double   mean_size   = 0.0, std_size  = 0.0;
    double   mean_sense  = 0.0, std_sense = 0.0;
    uint64_t births      = 0;
    uint64_t deaths      = 0;
    double   mean_energy = 0.0;
    double   ms_per_tick = 0.0;
    uint64_t state_hash  = 0;
};

class TelemetryWriter {
public:
    static constexpr size_t kFlushEvery = 1000;

    TelemetryWriter() = default;
    explicit TelemetryWriter(const std::string& path);
    ~TelemetryWriter();

    TelemetryWriter(const TelemetryWriter&)            = delete;
    TelemetryWriter& operator=(const TelemetryWriter&) = delete;

    bool open(const std::string& path);
    bool is_open() const { return out_.is_open(); }
    void add(const TelemetryRow& row);
    void flush();

    static const char* header();

private:
    std::ofstream            out_;
    std::vector<TelemetryRow> buf_;
};

}  // namespace evosim
