// SPDX-License-Identifier: GPL-3.0-or-later
#include "OutputWorker.h"

#include "core/Clock.h"

#include <chrono>
#include <cstring>
#include <pthread.h>

namespace cam {

namespace {
constexpr int64_t kMs = 1000000;

uint64_t packSize(int w, int h) { return (uint64_t(uint32_t(w)) << 32) | uint32_t(h); }

std::string autoDevicePath()
{
    auto devs = v4l2::LoopbackOutput::findDevices();
    if (devs.empty())
        return {};
    // Prefer a device labelled for us by the setup script.
    for (auto &d : devs)
        if (d.card.find("CamTune") != std::string::npos || d.card.find("Camera Adjust") != std::string::npos)
            return d.path;
    return devs.front().path;
}
} // namespace

OutputWorker::OutputWorker(Counters &counters, const EngineCallbacks &cb) : m_counters(counters), m_cb(cb)
{
    m_renderSize = packSize(m_config.width, m_config.height);
}

OutputWorker::~OutputWorker() { stop(); }

void OutputWorker::start()
{
    if (m_thread.joinable())
        return;
    m_stop = false;
    m_thread = std::thread([this] { run(); });
    pthread_setname_np(m_thread.native_handle(), "cam-output");
}

void OutputWorker::stop()
{
    m_stop = true;
    m_input.wake();
    if (m_thread.joinable())
        m_thread.join();
    m_out.close();
}

void OutputWorker::setConfig(const OutputConfig &cfg)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const OutputConfig &o = m_config;
        if (o.enabled == cfg.enabled && o.devicePath == cfg.devicePath && o.width == cfg.width &&
            o.height == cfg.height && o.fps == cfg.fps && o.pixelFormat == cfg.pixelFormat)
            return;
        m_config = cfg;
        ++m_generation;
        if (!m_active.load() || !cfg.enabled)
            m_renderSize = packSize(cfg.width & ~1, cfg.height & ~1);
    }
    m_input.wake();
}

OutputConfig OutputWorker::config() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

void OutputWorker::submit(FramePtr frame)
{
    if (m_input.put(std::move(frame)))
        m_counters.dropped.fetch_add(1, std::memory_order_relaxed);
}

void OutputWorker::setPlaceholder(FramePtr frame)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_placeholder = std::move(frame);
}

void OutputWorker::renderSize(int &w, int &h) const
{
    uint64_t v = m_renderSize.load();
    w = int(v >> 32);
    h = int(v & 0xffffffffu);
}

void OutputWorker::setState(OutputState s, const std::string &msg)
{
    if (s == m_state && msg == m_stateMsg)
        return;
    m_state = s;
    m_stateMsg = msg;
    if (m_cb.outputState) {
        int w, h;
        renderSize(w, h);
        m_cb.outputState(s, msg, w, h);
    }
}

void OutputWorker::run()
{
    using namespace std::chrono_literals;
    uint64_t applied = 0;
    int64_t nextRetry = 0;
    int64_t lastWrite = 0;
    int64_t nextDue = 0;
    int64_t lastIdleWrite = 0;
    FramePtr lastFrame;
    FramePtr black;

    while (!m_stop) {
        OutputConfig cfg;
        uint64_t gen;
        FramePtr placeholder;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cfg = m_config;
            gen = m_generation;
            placeholder = m_placeholder;
        }
        if (gen != applied) {
            m_out.close();
            m_active = false;
            applied = gen;
            nextRetry = 0;
            lastFrame.reset();
            m_renderSize = packSize(cfg.width & ~1, cfg.height & ~1);
        }
        FramePtr frame;
        if (!cfg.enabled) {
            setState(OutputState::Disabled, "Virtual camera is off");
            m_input.waitTake(frame, 500ms);
            continue;
        }

        int64_t now = monotonicNs();
        if (!m_out.isOpen() && now >= nextRetry) {
            std::string path = cfg.devicePath.empty() ? autoDevicePath() : cfg.devicePath;
            if (path.empty()) {
                setState(OutputState::NoDevice,
                         "No virtual camera device found. The v4l2loopback kernel module is not loaded — use "
                         "“Set up virtual camera…” in the Output section.");
                nextRetry = now + 2000 * kMs;
            } else {
                std::string err;
                bool adopted = false;
                if (m_out.open(path, cfg.width, cfg.height, cfg.fps, cfg.pixelFormat, err, adopted)) {
                    m_renderSize = packSize(m_out.width(), m_out.height());
                    m_active = true;
                    nextDue = 0;
                    lastWrite = 0;
                    std::string msg;
                    if (adopted)
                        msg = "Another application is using the virtual camera at " + std::to_string(m_out.width()) +
                              "×" + std::to_string(m_out.height()) +
                              "; keeping that size. To change it, stop the camera in that application, then "
                              "turn the virtual camera off and on.";
                    setState(OutputState::Active, msg);
                } else {
                    setState(OutputState::Error, err);
                    nextRetry = now + 2000 * kMs;
                }
            }
        }
        if (!m_out.isOpen()) {
            m_input.waitTake(frame, 250ms);
            continue;
        }

        const int64_t interval = int64_t(1e9 / (cfg.fps > 0 ? cfg.fps : 30));
        bool got = m_input.waitTake(frame, 100ms);
        now = monotonicNs();

        FramePtr toWrite;
        if (got) {
            if (frame->width != m_out.width() || frame->height != m_out.height())
                continue; // processing has not caught up with a size change yet
            if (nextDue && now < nextDue - interval / 2) {
                // Camera faster than the requested output rate: thin out evenly.
                m_counters.dropped.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            nextDue = (nextDue == 0 || now - nextDue > interval) ? now + interval : nextDue + interval;
            toWrite = frame;
            lastFrame = frame;
        } else if (now - lastWrite > 500 * kMs && now - lastIdleWrite >= 90 * kMs) {
            // No fresh frames for 0.5 s: keep consumers fed so they don't time out.
            if (m_cameraLive.load() && lastFrame && now - lastWrite < 3000 * kMs) {
                toWrite = lastFrame;
            } else if (placeholder && placeholder->width == m_out.width() &&
                       placeholder->height == m_out.height()) {
                toWrite = placeholder;
                lastFrame.reset();
            } else {
                if (!black || black->width != m_out.width() || black->height != m_out.height()) {
                    black = std::make_shared<Frame>();
                    if (black->allocI420(m_out.width(), m_out.height())) {
                        std::memset(black->plane[0], 16, size_t(black->width) * black->height);
                        std::memset(black->plane[1], 128, size_t(black->width / 2) * (black->height / 2) * 2);
                    }
                }
                toWrite = black;
                lastFrame.reset();
            }
            // Keep writing at ~10 fps while idle.
            lastIdleWrite = now;
        }
        if (!toWrite)
            continue;

        int err = 0;
        auto res = m_out.write(*toWrite, err);
        if (res == v4l2::LoopbackOutput::WriteResult::Ok) {
            if (got) {
                lastWrite = now;
                m_counters.written.fetch_add(1, std::memory_order_relaxed);
                if (toWrite->timestampNs > 0) {
                    int64_t lat = monotonicNs() - toWrite->timestampNs;
                    int64_t prev = m_counters.latencyNsEma.load(std::memory_order_relaxed);
                    m_counters.latencyNsEma.store(prev ? prev + (lat - prev) / 16 : lat, std::memory_order_relaxed);
                }
            }
        } else if (res == v4l2::LoopbackOutput::WriteResult::Disconnected ||
                   res == v4l2::LoopbackOutput::WriteResult::Error) {
            m_out.close();
            m_active = false;
            setState(OutputState::Error, "Writing to the virtual camera failed (" + v4l2::errnoString(err) +
                                             ") — reopening…");
            nextRetry = now + 1000 * kMs;
        }
    }
    m_out.close();
}

} // namespace cam
