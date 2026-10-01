// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace cam {

// Minimal fork/join pool for splitting a frame into row bands.
// The calling thread participates, so a pool of size 1 runs inline.
class ThreadPool {
public:
    explicit ThreadPool(int threads);
    ~ThreadPool();

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    int size() const { return int(m_workers.size()) + 1; }

    // Calls fn(begin, end) over [0, count) split into chunks; blocks until done.
    void parallelFor(int count, const std::function<void(int, int)> &fn, int minChunk = 16);

    static int defaultThreadCount();

private:
    void workerLoop();
    void runChunks();

    std::vector<std::thread> m_workers;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::condition_variable m_doneCv;
    const std::function<void(int, int)> *m_fn = nullptr;
    int m_count = 0;
    int m_chunk = 1;
    std::atomic<int> m_next{0};
    int m_finished = 0;
    uint64_t m_generation = 0;
    bool m_stop = false;
};

} // namespace cam
