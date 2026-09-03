// Minimal assertion harness. No third-party test framework: CTest already
// gives us discovery and reporting, and a nonzero exit code is the whole
// contract.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

namespace evosim::test {

inline int g_failures = 0;
inline int g_checks   = 0;

inline void report(bool ok, const char* expr, const char* file, int line,
                   const std::string& note) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::fprintf(stderr, "FAIL %s:%d: %s%s%s\n", file, line, expr,
                 note.empty() ? "" : " -- ", note.c_str());
}

inline int finish(const char* name) {
    if (g_failures == 0) {
        std::printf("PASS %s (%d checks)\n", name, g_checks);
        return 0;
    }
    std::fprintf(stderr, "FAILED %s: %d of %d checks failed\n", name, g_failures, g_checks);
    return 1;
}

}  // namespace evosim::test

#define CHECK(expr) ::evosim::test::report((expr), #expr, __FILE__, __LINE__, "")
#define CHECK_MSG(expr, note) ::evosim::test::report((expr), #expr, __FILE__, __LINE__, (note))
#define CHECK_EQ(a, b)                                                          \
    ::evosim::test::report((a) == (b), #a " == " #b, __FILE__, __LINE__,        \
                           std::to_string(a) + " vs " + std::to_string(b))
