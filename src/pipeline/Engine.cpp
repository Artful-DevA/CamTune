// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine.h"

#include "CaptureWorker.h"
#include "ControlWorker.h"
#include "OutputWorker.h"
#include "ProcessingWorker.h"
#include "core/Clock.h"

namespace cam {

Engine::Engine(EngineCallbacks callbacks) : m_userCb(std::move(callbacks))
{
    m_cb = m_userCb;
    m_cb.cameraState = [this](CameraState s, const std::string &msg) { onCameraState(s, msg); };

    m_capture = std::make_unique<CaptureWorker>(m_captured, m_counters, m_cb);
    m_output = std::make_unique<OutputWorker>(m_counters, m_cb);
    m_processing = std::make_unique<ProcessingWorker>(m_captured, *m_capture, *m_output, m_counters, m_cb);
    m_controls = std::make_unique<ControlWorker>(m_cb);
}

Engine::~Engine() { stop(); }

void Engine::start()
{
    m_controls->start();
    m_output->start();
    m_processing->start();
    m_capture->start();
}

void Engine::stop()
{
    // Upstream first so nothing feeds a stopped stage.
    m_capture->stop();
    m_processing->stop();
    m_output->stop();
    m_controls->stop();
}

void Engine::onCameraState(CameraState s, const std::string &msg)
{
    // Runs on the capture thread.
    const bool live = s == CameraState::Streaming || s == CameraState::Opening;
    m_output->setCameraLive(s == CameraState::Streaming);
    std::string path = live ? m_capture->currentPath() : std::string();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (path != m_controlPath) {
            m_controlPath = path;
            m_controls->setDevice(path);
            // Restore the user's hardware settings: cameras forget them on replug.
            if (!path.empty() && !m_desiredControls.empty())
                m_controls->applyAll(m_desiredControls);
        }
    }
    if (m_userCb.cameraState)
        m_userCb.cameraState(s, msg);
}

void Engine::selectCamera(const CameraSelection &sel)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Saved values belong to the previous camera.
        m_desiredControls.clear();
    }
    m_capture->selectCamera(sel);
}

void Engine::setCaptureRequest(const CaptureRequest &req) { m_capture->setRequest(req); }
void Engine::setColor(const ColorParams &c) { m_processing->setColor(c); }
void Engine::setFraming(const FramingParams &f, int transitionMs) { m_processing->setFraming(f, transitionMs); }
void Engine::setEffects(const EffectParams &e) { m_processing->setEffects(e); }

void Engine::setOutput(const OutputConfig &cfg)
{
    m_output->setConfig(cfg);
    m_capture->setTarget(cfg.width, cfg.height, cfg.fps);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_outputEnabled = cfg.enabled;
    }
    updateCaptureActive();
}

void Engine::setPreviewWanted(bool wanted)
{
    m_processing->setPreviewWanted(wanted);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_previewWanted = wanted;
    }
    updateCaptureActive();
}

void Engine::updateCaptureActive()
{
    bool active;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        active = m_previewWanted || m_outputEnabled;
    }
    // Keep the camera streaming either way (so it stays ours and starts
    // instantly), but skip decoding when nobody consumes the frames.
    m_capture->setActive(active);
}

void Engine::setPlaceholder(FramePtr frame) { m_output->setPlaceholder(std::move(frame)); }
void Engine::setSuspended(bool suspended) { m_capture->setSuspended(suspended); }

void Engine::setControl(uint32_t id, int64_t value)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_desiredControls[id] = value;
    }
    m_controls->setControl(id, value);
}

void Engine::resetControls()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_desiredControls.clear();
    }
    m_controls->resetToDefaults();
}

void Engine::refreshControls() { m_controls->refresh(); }

void Engine::setDesiredControls(const std::map<uint32_t, int64_t> &values)
{
    std::string path;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_desiredControls = values;
        path = m_controlPath;
    }
    if (!path.empty() && !values.empty())
        m_controls->applyAll(values);
}

FramePtr Engine::takePreviewFrame() { return m_processing->takePreviewFrame(); }

EngineStats Engine::stats()
{
    const int64_t now = monotonicNs();
    const uint64_t cap = m_counters.captured.load();
    const uint64_t proc = m_counters.processed.load();
    const uint64_t wr = m_counters.written.load();
    EngineStats s = m_lastStats;
    if (m_lastStatsNs) {
        const double dt = (now - m_lastStatsNs) / 1e9;
        if (dt > 0.2) {
            s.captureFps = (cap - m_lastCaptured) / dt;
            s.processFps = (proc - m_lastProcessed) / dt;
            s.outputFps = (wr - m_lastWritten) / dt;
        } else {
            return m_lastStats;
        }
    }
    s.processMs = m_counters.processNsEma.load() / 1e6;
    s.latencyMs = m_counters.latencyNsEma.load() / 1e6;
    s.droppedFrames = m_counters.dropped.load();
    s.decodeScale = m_processing->decodeScale();
    s.overloaded = m_processing->overloaded();
    m_lastStatsNs = now;
    m_lastCaptured = cap;
    m_lastProcessed = proc;
    m_lastWritten = wr;
    m_lastStats = s;
    return s;
}

} // namespace cam
