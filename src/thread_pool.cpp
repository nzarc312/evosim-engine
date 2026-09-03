#include "evosim/thread_pool.hpp"

namespace evosim {

ThreadPool::ThreadPool(unsigned n_threads) {
    if (n_threads == 0) n_threads = std::thread::hardware_concurrency();
    if (n_threads == 0) n_threads = 1;
    n_threads_ = n_threads;

    workers_.reserve(n_threads_ - 1);
    for (unsigned i = 1; i < n_threads_; ++i)
        workers_.emplace_back([this, i] { worker_loop(i); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_ = true;
        ++generation_;
    }
    cv_start_.notify_all();
    for (std::thread& t : workers_) t.join();
}

void ThreadPool::run_participant(unsigned participant, const Job& job, size_t n_items) {
    // Contiguous chunk ranges per participant: each thread streams linearly
    // through the structure-of-arrays rather than hopping.
    const unsigned c0 = participant * kNumChunks / n_threads_;
    const unsigned c1 = (participant + 1) * kNumChunks / n_threads_;
    for (unsigned c = c0; c < c1; ++c) {
        size_t b, e;
        chunk_range(c, n_items, b, e);
        job(c, b, e);
    }
}

void ThreadPool::run(const Job& job, size_t n_items) {
    if (workers_.empty()) {           // pool of one: no dispatch, no barrier
        run_participant(0, job, n_items);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(m_);
        job_     = &job;
        n_items_ = n_items;
        pending_ = static_cast<unsigned>(workers_.size());
        ++generation_;
    }
    cv_start_.notify_all();

    // The caller is participant 0 and does its own share rather than idling.
    run_participant(0, job, n_items);

    std::unique_lock<std::mutex> lk(m_);
    cv_done_.wait(lk, [this] { return pending_ == 0; });
    job_ = nullptr;
}

void ThreadPool::worker_loop(unsigned participant) {
    uint64_t seen = 0;
    for (;;) {
        const Job* job = nullptr;
        size_t     n   = 0;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_start_.wait(lk, [this, &seen] { return stop_ || generation_ != seen; });
            if (stop_) return;
            seen = generation_;
            job  = job_;
            n    = n_items_;
        }

        run_participant(participant, *job, n);

        {
            std::lock_guard<std::mutex> lk(m_);
            if (--pending_ == 0) cv_done_.notify_one();
        }
    }
}

}  // namespace evosim
