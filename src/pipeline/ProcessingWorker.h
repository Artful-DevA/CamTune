// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Types.h"
#include "core/Frame.h"
#include "core/Framing.h"
#include "core/Mailbox.h"
#include "core/Processor.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace cam {

class CaptureWorker;
class OutputWorker;

// Takes the newest captured frame, renders it at the output size with the
// current color/framing/effects and hands the result to the virtual camera
// and the preview. Parameter changes are picked up on the next frame, so
// sliders never wait on the pipeline and the pipeline never waits on the UI.
class ProcessingWorker {
public:
    ProcessingWorker(Mailbox<FramePtr> &input, CaptureWorker &capture, OutputWorker &output,
                     Counters &counters, const EngineCallbacks &cb);
    ~ProcessingWorker();

    void start();
    void stop();

    void setColor(const ColorParams &c);
    void setFraming(const FramingParams &f, int transitionMs);
    void setEffects(const EffectParams &e);
    void setPreviewWanted(bool wanted) { m_previewWanted.store(wanted); }

    // Latest processed frame for the preview (nullptr if none since last call).
    FramePtr takePreviewFrame();
    FramingParams targetFraming() const;

    bool overloaded() const { return m_overloaded.load(); }
    int decodeScale() const { return m_decodeScale.load(); }

private:
    void run();
    void updateDecodeScale(const Frame &src, const FramingParams &framing, int outW, int outH,
                           int64_t processNs);

    Mailbox<FramePtr> &m_input;
    CaptureWorker &m_capture;
    OutputWorker &m_output;
    Counters &m_counters;
    const EngineCallbacks &m_cb;

    std::thread m_thread;
    std::atomic<bool> m_stop{false};

    mutable std::mutex m_mutex;
    uint64_t m_version = 1;
    ColorParams m_color;
    FramingParams m_framing;
    int m_transitionMs = 0;
    bool m_framingChanged = true;
    EffectParams m_effects;
    bool m_effectsChanged = true;

    std::atomic<bool> m_previewWanted{true};
    Mailbox<FramePtr> m_preview;
    std::atomic<bool> m_previewNotified{false};

    std::atomic<bool> m_overloaded{false};
    std::atomic<int> m_decodeScale{1};
    int64_t m_overloadSinceNs = 0;
    int64_t m_relaxedSinceNs = 0;
};

} // namespace cam
