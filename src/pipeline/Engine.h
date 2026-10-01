// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Types.h"
#include "core/Mailbox.h"

#include <map>
#include <memory>
#include <mutex>

namespace cam {

class CaptureWorker;
class ProcessingWorker;
class OutputWorker;
class ControlWorker;

// The complete camera pipeline:
//
//   CaptureWorker ──(latest frame)──> ProcessingWorker ──> OutputWorker ──> v4l2loopback
//        │                                   └──> preview (latest frame, UI pulls)
//   ControlWorker (UVC controls, own fd)
//
// Every stage runs on its own thread and hands frames over through
// single-slot mailboxes, so a slow stage drops stale frames instead of
// building up latency. All methods are thread-safe and non-blocking.
class Engine {
public:
    explicit Engine(EngineCallbacks callbacks);
    ~Engine();

    void start();
    void stop();

    void selectCamera(const CameraSelection &sel);
    void setCaptureRequest(const CaptureRequest &req);
    void setColor(const ColorParams &c);
    void setFraming(const FramingParams &f, int transitionMs);
    void setEffects(const EffectParams &e);
    void setOutput(const OutputConfig &cfg);
    void setPreviewWanted(bool wanted);
    void setPlaceholder(FramePtr frame);
    void setSuspended(bool suspended);

    void setControl(uint32_t id, int64_t value);
    void resetControls();
    void refreshControls();
    // Hardware control values to (re)apply whenever the camera (re)connects.
    void setDesiredControls(const std::map<uint32_t, int64_t> &values);

    FramePtr takePreviewFrame();
    EngineStats stats();

private:
    void onCameraState(CameraState s, const std::string &msg);
    void updateCaptureActive();

    EngineCallbacks m_userCb;   // callbacks provided by the UI layer
    EngineCallbacks m_cb;       // wrapped callbacks handed to the workers
    Counters m_counters;
    Mailbox<FramePtr> m_captured;

    std::unique_ptr<CaptureWorker> m_capture;
    std::unique_ptr<OutputWorker> m_output;
    std::unique_ptr<ProcessingWorker> m_processing;
    std::unique_ptr<ControlWorker> m_controls;

    std::mutex m_mutex;
    std::map<uint32_t, int64_t> m_desiredControls;
    std::string m_controlPath;
    bool m_previewWanted = true;
    bool m_outputEnabled = false;

    // For rate computation in stats().
    int64_t m_lastStatsNs = 0;
    uint64_t m_lastCaptured = 0, m_lastProcessed = 0, m_lastWritten = 0;
    EngineStats m_lastStats;
};

} // namespace cam
