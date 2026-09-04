#include "evosim/config.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace evosim {
namespace {

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool parse_double(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return false;
    out = v;
    return true;
}

bool parse_u32(const std::string& s, uint32_t& out) {
    double v;
    if (!parse_double(s, v)) return false;
    if (v < 0.0 || v > 4294967295.0) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool parse_uint(const std::string& s, unsigned& out) {
    uint32_t v;
    if (!parse_u32(s, v)) return false;
    out = static_cast<unsigned>(v);
    return true;
}

}  // namespace

bool Config::set(const std::string& k, const std::string& raw) {
    std::string v = trim(raw);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);

    if (k == "world.width")               return parse_double(v, world.width);
    if (k == "world.height")              return parse_double(v, world.height);
    if (k == "population.initial_agents") return parse_u32(v, population.initial_agents);
    if (k == "population.max_agents")     return parse_u32(v, population.max_agents);
    if (k == "population.initial_greed")  return parse_double(v, population.initial_greed);
    if (k == "population.initial_greed_b") return parse_double(v, population.initial_greed_b);
    if (k == "population.greed_split")    return parse_double(v, population.greed_split);
    if (k == "food.target_count")         return parse_u32(v, food.target_count);
    if (k == "food.spawn_rate")           return parse_u32(v, food.spawn_rate);
    if (k == "food.capacity")            return parse_double(v, food.capacity);
    if (k == "food.energy_per_unit")     return parse_double(v, food.energy_per_unit);
    if (k == "food.regen_rate")          return parse_double(v, food.regen_rate);
    if (k == "food.collapse_threshold")  return parse_double(v, food.collapse_threshold);
    if (k == "food.recolonise_rate")     return parse_double(v, food.recolonise_rate);
    if (k == "food.digest_ticks")        return parse_u32(v, food.digest_ticks);
    if (k == "energy.base_cost")          return parse_double(v, energy.base_cost);
    if (k == "energy.move_coef")          return parse_double(v, energy.move_coef);
    if (k == "energy.sense_coef")         return parse_double(v, energy.sense_coef);
    if (k == "energy.repro_threshold")    return parse_double(v, energy.repro_threshold);
    if (k == "energy.max_age")            return parse_u32(v, energy.max_age);
    if (k == "mutation.sigma")            return parse_double(v, mutation.sigma);
    if (k == "mutation.greed_sigma")      return parse_double(v, mutation.greed_sigma);
    if (k == "threading.threads")         return parse_uint(v, threading.threads);
    return false;
}

Config Config::from_string(const std::string& text, const std::string& origin) {
    Config cfg;
    std::istringstream in(text);
    std::string line;
    std::string section;
    unsigned lineno = 0;

    while (std::getline(in, line)) {
        ++lineno;
        if (const size_t h = line.find('#'); h != std::string::npos) line.resize(h);

        // A line may hold several pairs separated by ';'.
        std::vector<std::string> parts;
        std::string cur;
        for (const char c : line) {
            if (c == ';') { parts.push_back(cur); cur.clear(); }
            else          { cur.push_back(c); }
        }
        parts.push_back(cur);

        for (const std::string& part : parts) {
            const std::string t = trim(part);
            if (t.empty()) continue;

            // A section header may be followed by pairs on the same line:
            //   [world]  width = 500.0 ; height = 500.0
            // so consume the header and keep parsing the remainder of the part.
            std::string rest = t;
            if (rest.front() == '[') {
                const size_t close = rest.find(']');
                if (close == std::string::npos)
                    throw std::runtime_error(origin + ":" + std::to_string(lineno) +
                                             ": unterminated section header");
                section = trim(rest.substr(1, close - 1));
                rest = trim(rest.substr(close + 1));
                if (rest.empty()) continue;
            }
            const std::string& t2 = rest;

            const size_t eq = t2.find('=');
            if (eq == std::string::npos)
                throw std::runtime_error(origin + ":" + std::to_string(lineno) +
                                         ": expected 'key = value', got '" + t2 + "'");

            const std::string key = section.empty() ? trim(t2.substr(0, eq))
                                                    : section + "." + trim(t2.substr(0, eq));
            if (!cfg.set(key, t2.substr(eq + 1)))
                throw std::runtime_error(origin + ":" + std::to_string(lineno) +
                                         ": unknown key or bad value: '" + key + "'");
        }
    }
    return cfg;
}

Config Config::from_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open config file: " + path);
    std::ostringstream buf;
    buf << f.rdbuf();
    return from_string(buf.str(), path);
}

std::string Config::to_string() const {
    std::ostringstream o;
    o << "[world]      width = " << world.width << " ; height = " << world.height << "\n"
      << "[population] initial_agents = " << population.initial_agents
      << " ; max_agents = " << population.max_agents
      << " ; initial_greed = " << population.initial_greed << "\n"
      << "             initial_greed_b = " << population.initial_greed_b
      << " ; greed_split = " << population.greed_split << "\n"
      << "[food]       target_count = " << food.target_count
      << " ; spawn_rate = " << food.spawn_rate
      << " ; capacity = " << food.capacity << "\n"
      << "             energy_per_unit = " << food.energy_per_unit
      << " ; regen_rate = " << food.regen_rate
      << " ; collapse_threshold = " << food.collapse_threshold
      << " ; recolonise_rate = " << food.recolonise_rate << "\n"
      << "[energy]     base_cost = " << energy.base_cost
      << " ; move_coef = " << energy.move_coef
      << " ; sense_coef = " << energy.sense_coef << "\n"
      << "             repro_threshold = " << energy.repro_threshold
      << " ; max_age = " << energy.max_age << "\n"
      << "[mutation]   sigma = " << mutation.sigma
      << " ; greed_sigma = " << mutation.greed_sigma << "\n"
      << "[threading]  threads = " << threading.threads << "\n";
    return o.str();
}

}  // namespace evosim
