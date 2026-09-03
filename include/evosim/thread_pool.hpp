// Persistent worker pool with a hand-rolled reusable barrier.
//
// Threads are created once, not per tick: thread creation costs microseconds
// and the tick budget here is sub-millisecond. C++20's std::barrier is not
// available under C++17, so the barrier is a mutex, two condition variables and
// a generation counter.
//
// The important design decision is kNumChunks. Work is always split into the
// same fixed number of chunks no matter how many threads exist, and every
// per-chunk output (a partial sum, a claim buffer) is indexed by chunk. That is
// what decouples results from the thread count: 4 threads and 16 threads both
// produce 64 partial sums, reduced in the same order, giving the same bits.
// Partition the work by thread instead and the number of partial sums changes
// with the thread count, and so does the last bit of every float sum.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace evosim {

class ThreadPool {
public:
    static constexpr unsigned kNumChunks = 64;

    using Job = std::function<void(unsigned chunk, size_t begin, size_t end)>;

    // n_threads == 0 selects hardware_concurrency. The calling thread is one of
    // the participants, so a pool of size 1 spawns nothing and runs inline --
    // the single-thread baseline goes through exactly the same code path, with
    // no dispatch overhead to flatter the speedup numbers.
    explicit ThreadPool(unsigned n_threads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Split [0, n_items) into kNumChunks chunks and invoke `job` once per chunk.
    // Blocks until every chunk is done. Empty chunks are still invoked, so a
    // caller may rely on every chunk index being visited.
    void run(const Job& job, size_t n_items);

    unsigned size() const noexcept { return n_threads_; }
    static constexpr unsigned chunks() noexcept { return kNumChunks; }

    // Item range of chunk `c` over `n` items.
    static void chunk_range(unsigned c, size_t n, size_t& begin, size_t& end) noexcept {
        begin = static_cast<size_t>(c)       * n / kNumChunks;
        end   = static_cast<size_t>(c + 1)   * n / kNumChunks;
    }

private:
    void worker_loop(unsigned participant);
    void run_participant(unsigned participant, const Job& job, size_t n_items);

    std::vector<std::thread> workers_;      // n_threads_ - 1 of them
    unsigned                 n_threads_ = 1;

    std::mutex              m_;
    std::condition_variable cv_start_;
    std::condition_variable cv_done_;
    const Job*              job_        = nullptr;
    size_t                  n_items_    = 0;
    uint64_t                generation_ = 0;
    unsigned                pending_    = 0;
    bool                    stop_       = false;
};

}  // namespace evosim
