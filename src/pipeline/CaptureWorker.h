// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Types.h"
#include "core/Frame.h"
#include "core/Mailbox.h"
#include "core/MjpegDecoder.h"
#include "v4l2/CaptureDevice.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace cam {

// Owns the physical camera. Runs a small state machine that opens the selected
// device, keeps it streaming, and recovers from unplug/replug, suspend/resume,
// "device busy" and stalled streams without user intervention.
class CaptureWorker {
public:
    CaptureWorker(Mailbox<FramePtr> &output, Counters &counters, const EngineCallbacks &cb);
    ~CaptureWorker();

    void start();
    void stop();

    void selectCamera(const CameraSelection &sel);
    void setRequest(const CaptureRequest &req);
    // Output size/rate used to choose a mode in automatic mode.
    void setTarget(int width, int height, int fps);
    void setSuspended(bool suspended);
    // When inactive, frames are returned to the driver without decoding.
    void setActive(bool active) { m_active.store(active); }
    void setDecodeScale(int scale) { m_decodeScale.store(scale); }
    // Full-resolution size of the stream (before any reduced-scale decode).
    void sourceSize(int &w, int &h) const;
    double sourceFps() const { return m_fps.load(); }
    // Path of the device currently streaming (empty if none).
    std::string currentPath() const;

private:
    struct Config {
        CameraSelection selection;
        CaptureRequest request;
        int targetW = 1280, targetH = 720, targetFps = 30;
        bool suspended = false;
    };

    void run();
    void wake();
    // Waits up to timeoutMs for a wake-up; returns true if woken.
    bool waitWake(int timeoutMs);
    void setState(CameraState s, const std::string &msg);
    bool resolvePath(const CameraSelection &sel, std::string &path);
    bool openAndStart(const Config &cfg, const std::string &path);
    void runTestPattern(const Config &cfg, uint64_t generation);
    void deliver(FramePtr frame);

    Mailbox<FramePtr> &m_output;
    Counters &m_counters;
    const EngineCallbacks &m_cb;

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    int m_wakeFd = -1;

    mutable std::mutex m_mutex;
    Config m_config;
    uint64_t m_generation = 1;
    std::string m_currentPath;

    std::atomic<bool> m_active{true};
    std::atomic<int> m_decodeScale{1};
    std::atomic<int> m_srcW{0}, m_srcH{0};
    std::atomic<double> m_fps{0};

    CameraState m_state = CameraState::NoCamera;
    std::string m_stateMsg;

    v4l2::CaptureDevice m_device;
    MjpegDecoder m_decoder;
    std::shared_ptr<FramePool> m_pool;
    int m_decodeFailures = 0;
};

} // namespace cam
