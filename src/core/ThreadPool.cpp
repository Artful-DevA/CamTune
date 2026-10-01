// SPDX-License-Identifier: GPL-3.0-or-later
#include "ThreadPool.h"

#include <algorithm>
#include <pthread.h>

namespace cam {

ThreadPool::ThreadPool(int threads)
{
    threads = std::max(1, threads);
    for (int i = 1; i < threads; ++i) {
        m_workers.emplace_back([this] { workerLoop(); });
        pthread_setname_np(m_workers.back().native_handle(), "cam-worker");
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for (auto &t : m_workers)
        t.join();
}

int ThreadPool::defaultThreadCount()
{
    // Image processing is memory-bound; beyond a few threads there is no gain and
    // we would only steal cycles from the conferencing app.
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0)
        hw = 2;
    return int(std::clamp<unsigned>(hw / 2, 1, 4));
}

void ThreadPool::runChunks()
{
    for (;;) {
        int begin = m_next.fetch_add(m_chunk, std::memory_order_relaxed);
        if (begin >= m_count)
            break;
        int end = std::min(m_count, begin + m_chunk);
        (*m_fn)(begin, end);
    }
}

void ThreadPool::workerLoop()
{
    uint64_t seen = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&] { return m_stop || m_generation != seen; });
            if (m_stop)
                return;
            seen = m_generation;
        }
        runChunks();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_finished;
        }
        m_doneCv.notify_all();
    }
}

void ThreadPool::parallelFor(int count, const std::function<void(int, int)> &fn, int minChunk)
{
    if (count <= 0)
        return;
    int threads = size();
    if (threads == 1 || count <= minChunk) {
        fn(0, count);
        return;
    }
    // A few chunks per thread balances uneven rows without much overhead.
    int chunk = std::max(minChunk, (count + threads * 3 - 1) / (threads * 3));
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_fn = &fn;
        m_count = count;
        m_chunk = chunk;
        m_next.store(0, std::memory_order_relaxed);
        m_finished = 0;
        ++m_generation;
    }
    m_cv.notify_all();
    runChunks();
    std::unique_lock<std::mutex> lock(m_mutex);
    // Every worker takes part in every generation, so once all have reported back
    // none can still be touching this call's state.
    m_doneCv.wait(lock, [&] { return m_finished == int(m_workers.size()); });
    m_fn = nullptr;
}

} // namespace cam
