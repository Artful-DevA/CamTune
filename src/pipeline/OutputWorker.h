// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Types.h"
#include "core/Frame.h"
#include "core/Mailbox.h"
#include "v4l2/LoopbackOutput.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace cam {

// Feeds processed frames to the v4l2loopback device.
//
// Writes happen as soon as a frame arrives (no extra buffering). While the
// camera is unavailable a placeholder frame keeps flowing at a low rate so
// conferencing apps never see a frozen or vanished device.
class OutputWorker {
public:
    OutputWorker(Counters &counters, const EngineCallbacks &cb);
    ~OutputWorker();

    void start();
    void stop();

    void setConfig(const OutputConfig &cfg);
    OutputConfig config() const;

    void submit(FramePtr frame);
    // I420 frame shown while no camera frames arrive (any size; rescaled lazily by the caller).
    void setPlaceholder(FramePtr frame);
    void setCameraLive(bool live) { m_cameraLive.store(live); }

    // The size processing should render at: the device's negotiated size when
    // the output is active, otherwise the configured size.
    void renderSize(int &w, int &h) const;
    bool active() const { return m_active.load(); }

private:
    void run();
    void setState(OutputState s, const std::string &msg);

    Counters &m_counters;
    const EngineCallbacks &m_cb;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};

    mutable std::mutex m_mutex;
    OutputConfig m_config;
    uint64_t m_generation = 1;
    FramePtr m_placeholder;

    Mailbox<FramePtr> m_input;
    std::atomic<bool> m_cameraLive{false};
    std::atomic<bool> m_active{false};
    std::atomic<uint64_t> m_renderSize{0};

    OutputState m_state = OutputState::Disabled;
    std::string m_stateMsg;
    v4l2::LoopbackOutput m_out;
};

} // namespace cam
