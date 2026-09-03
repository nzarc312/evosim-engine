#include "evosim/telemetry.hpp"

#include <cstdio>

namespace evosim {

const char* TelemetryWriter::header() {
    return "tick,population,food_active,mean_speed,std_speed,mean_size,std_size,"
           "mean_sense,std_sense,births,deaths,mean_energy,ms_per_tick,state_hash";
}

TelemetryWriter::TelemetryWriter(const std::string& path) { open(path); }

TelemetryWriter::~TelemetryWriter() { flush(); }

bool TelemetryWriter::open(const std::string& path) {
    out_.open(path, std::ios::out | std::ios::trunc);
    if (!out_) return false;
    buf_.reserve(kFlushEvery);
    out_ << header() << "\n";
    return true;
}

void TelemetryWriter::add(const TelemetryRow& row) {
    if (!out_.is_open()) return;
    buf_.push_back(row);
    if (buf_.size() >= kFlushEvery) flush();
}

void TelemetryWriter::flush() {
    if (!out_.is_open() || buf_.empty()) return;
    char line[512];
    for (const TelemetryRow& r : buf_) {
        // %.17g round-trips a double exactly, so a CSV column can be compared
        // bit-for-bit against a rerun rather than only to plotting precision.
        const int n = std::snprintf(
            line, sizeof(line),
            "%llu,%llu,%llu,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%llu,%llu,%.17g,%.6f,%llu\n",
            static_cast<unsigned long long>(r.tick),
            static_cast<unsigned long long>(r.population),
            static_cast<unsigned long long>(r.food_active),
            r.mean_speed, r.std_speed, r.mean_size, r.std_size, r.mean_sense, r.std_sense,
            static_cast<unsigned long long>(r.births),
            static_cast<unsigned long long>(r.deaths),
            r.mean_energy, r.ms_per_tick,
            static_cast<unsigned long long>(r.state_hash));
        if (n > 0) out_.write(line, n);
    }
    buf_.clear();
    out_.flush();
}

}  // namespace evosim
