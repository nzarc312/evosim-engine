// Deterministic parallel reductions.
//
// Floating-point addition is not associative, so a parallel sum whose
// combination order depends on which thread finished first is not
// reproducible. Two rules make it reproducible:
//
//   1. Accumulate into per-chunk slots, then combine those slots serially in
//      chunk order. The final pass is what pins the order down.
//   2. Use a FIXED number of chunks, independent of the thread count. Four
//      threads producing four partials and sixteen producing sixteen would sum
//      to different last bits, which would break the cross-thread-count
//      guarantee even though each individual run is reproducible.
//
// Padded<T> is 64-byte aligned so adjacent slots never share a cache line.
// Without it, cores writing neighbouring accumulators invalidate each other's
// copy on every store -- see bench_scaling --mode false_sharing, which uses the
// Unpadded variant below purely so the two can be compared.
#pragma once

#include <cstddef>
#include <vector>

#include "evosim/thread_pool.hpp"

namespace evosim {

template <typename T>
struct alignas(64) Padded {
    T value{};
};

template <typename T>
struct Unpadded {
    T value{};
};

// Sum value_at(i) for i in [0, n), into caller-owned slots. Slot must expose
// `.value`. Reusing the slot vector keeps this off the per-tick allocation path.
template <typename Slot, typename F>
double sum_into(ThreadPool& pool, std::vector<Slot>& partial, size_t n, F&& value_at) {
    partial.assign(ThreadPool::chunks(), Slot{});
    pool.run([&](unsigned chunk, size_t b, size_t e) {
        double local = 0.0;
        for (size_t i = b; i < e; ++i) local += value_at(i);
        partial[chunk].value = local;
    }, n);

    double total = 0.0;
    for (unsigned c = 0; c < ThreadPool::chunks(); ++c) total += partial[c].value;  // fixed order
    return total;
}

// Convenience form. Allocates, so do not call it inside a tick.
template <typename F>
double deterministic_sum(ThreadPool& pool, size_t n, F&& value_at) {
    std::vector<Padded<double>> partial;
    return sum_into(pool, partial, n, std::forward<F>(value_at));
}

// max is associative, commutative and exact in floating point, so it needs no
// fixed-order treatment -- but it still needs per-chunk slots to avoid a data
// race, and the serial combine costs nothing.
template <typename F>
double deterministic_max(ThreadPool& pool, std::vector<Padded<double>>& partial,
                         size_t n, double identity, F&& value_at) {
    partial.assign(ThreadPool::chunks(), Padded<double>{identity});
    pool.run([&](unsigned chunk, size_t b, size_t e) {
        double local = identity;
        for (size_t i = b; i < e; ++i) {
            const double v = value_at(i);
            if (v > local) local = v;
        }
        partial[chunk].value = local;
    }, n);

    double total = identity;
    for (unsigned c = 0; c < ThreadPool::chunks(); ++c)
        if (partial[c].value > total) total = partial[c].value;
    return total;
}

}  // namespace evosim
