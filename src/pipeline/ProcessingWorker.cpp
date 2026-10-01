// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProcessingWorker.h"

#include "CaptureWorker.h"
#include "OutputWorker.h"
#include "core/Clock.h"

#include <algorithm>
#include <chrono>
#include <pthread.h>

namespace cam {

namespace {
constexpr int64_t kMs = 1000000;
}

ProcessingWorker::ProcessingWorker(Mailbox<FramePtr> &input, CaptureWorker &capture, OutputWorker &output,
                                   Counters &counters, const EngineCallbacks &cb)
    : m_input(input), m_capture(capture), m_output(output), m_counters(counters), m_cb(cb)
{
}

ProcessingWorker::~ProcessingWorker() { stop(); }

void ProcessingWorker::start()
{
    if (m_thread.joinable())
        return;
    m_stop = false;
    m_thread = std::thread([this] { run(); });
    pthread_setname_np(m_thread.native_handle(), "cam-process");
}

void ProcessingWorker::stop()
{
    m_stop = true;
    m_input.wake();
    if (m_thread.joinable())
        m_thread.join();
}

void ProcessingWorker::setColor(const ColorParams &c)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_color = c;
    ++m_version;
}

void ProcessingWorker::setFraming(const FramingParams &f, int transitionMs)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_framing = f;
    m_transitionMs = transitionMs;
    m_framingChanged = true;
    ++m_version;
}

void ProcessingWorker::setEffects(const EffectParams &e)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_effects = e;
    m_effectsChanged = true;
    ++m_version;
}

FramingParams ProcessingWorker::targetFraming() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_framing;
}

FramePtr ProcessingWorker::takePreviewFrame()
{
    m_previewNotified.store(false);
    FramePtr f;
    m_preview.tryTake(f);
    return f;
}

void ProcessingWorker::updateDecodeScale(const Frame &src, const FramingParams &framing, int outW, int outH,
                                         int64_t processNs)
{
    const int64_t now = monotonicNs();
    const double fps = std::max(1.0, m_capture.sourceFps());
    const double interval = 1e9 / fps;
    const int64_t ema = m_counters.processNsEma.load(std::memory_order_relaxed);
    (void)processNs;

    // Overload detection with hysteresis so quality does not flap.
    bool overloaded = m_overloaded.load();
    if (ema > 0.75 * interval) {
        m_relaxedSinceNs = 0;
        if (!m_overloadSinceNs)
            m_overloadSinceNs = now;
        if (!overloaded && now - m_overloadSinceNs > 1000 * kMs)
            overloaded = true;
    } else if (ema < 0.4 * interval) {
        m_overloadSinceNs = 0;
        if (!m_relaxedSinceNs)
            m_relaxedSinceNs = now;
        if (overloaded && now - m_relaxedSinceNs > 5000 * kMs)
            overloaded = false;
    }
    m_overloaded = overloaded;

    // Reduced-scale JPEG decoding only applies to MJPEG sources (decoded to Planar/Grey).
    if (src.format != PixelFormat::Planar && src.format != PixelFormat::Grey) {
        m_decodeScale = 1;
        return;
    }
    int fullW = 0, fullH = 0;
    m_capture.sourceSize(fullW, fullH);
    if (fullW <= 0 || fullH <= 0)
        return;

    // Size of the visible source region at full resolution (fill-mode estimate).
    const double cropW = fullW * std::max(0.05, 1.0 - framing.cropLeft - framing.cropRight);
    const double cropH = fullH * std::max(0.05, 1.0 - framing.cropTop - framing.cropBottom);
    const double aspect = double(outW) / std::max(1, outH);
    const double zoom = std::max(1.0, framing.zoom);
    const double visW = std::min(cropW, cropH * aspect) / zoom;
    const double visH = std::min(cropH, cropW / aspect) / zoom;

    // Without overload, only scale down when it costs no detail; under
    // overload accept a modest loss to keep the frame rate.
    const double quality = overloaded ? 0.7 : 1.0;
    int best = 1;
    for (int s : {2, 4})
        if (visW / s >= quality * outW && visH / s >= quality * outH)
            best = s;
    m_decodeScale = best;
    m_capture.setDecodeScale(best);
}

void ProcessingWorker::run()
{
    using namespace std::chrono_literals;
    Processor processor;
    FramingAnimator animator;
    auto pool = FramePool::create(8);
    uint64_t seenVersion = 0;

    while (!m_stop) {
        FramePtr src;
        if (!m_input.waitTake(src, 100ms) || !src)
            continue;

        const bool outputWanted = m_output.active();
        const bool previewWanted = m_previewWanted.load();
        if (!outputWanted && !previewWanted)
            continue; // nobody is watching; drop the frame (returns its buffer)

        const int64_t now = monotonicNs();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_version != seenVersion) {
                seenVersion = m_version;
                processor.setColor(m_color);
                if (m_effectsChanged) {
                    processor.setEffects(m_effects);
                    m_effectsChanged = false;
                }
                if (m_framingChanged) {
                    animator.setTarget(m_framing, m_transitionMs, now);
                    m_framingChanged = false;
                }
            }
        }

        int w = 0, h = 0;
        m_output.renderSize(w, h);
        w = std::clamp(w, 16, 7680) & ~1;
        h = std::clamp(h, 16, 4320) & ~1;
        FramePtr dst = pool->acquire();
        if (!dst->allocI420(w, h))
            continue;

        const FramingParams framing = animator.current(now);
        const int64_t t0 = monotonicNs();
        if (!processor.process(*src, *dst, framing))
            continue;
        const int64_t t1 = monotonicNs();
        dst->timestampNs = src->timestampNs;
        dst->sequence = src->sequence;

        const int64_t took = t1 - t0;
        const int64_t prev = m_counters.processNsEma.load(std::memory_order_relaxed);
        m_counters.processNsEma.store(prev ? prev + (took - prev) / 16 : took, std::memory_order_relaxed);
        m_counters.processed.fetch_add(1, std::memory_order_relaxed);
        if (!outputWanted && src->timestampNs > 0) {
            // Without the virtual camera, report latency up to the preview hand-off.
            const int64_t lat = t1 - src->timestampNs;
            const int64_t p = m_counters.latencyNsEma.load(std::memory_order_relaxed);
            m_counters.latencyNsEma.store(p ? p + (lat - p) / 16 : lat, std::memory_order_relaxed);
        }
        updateDecodeScale(*src, framing, w, h, took);
        src.reset(); // release the camera buffer as early as possible

        if (outputWanted)
            m_output.submit(dst);
        if (previewWanted) {
            m_preview.put(dst);
            // Coalesce notifications: at most one pending wake-up for the UI.
            if (!m_previewNotified.exchange(true) && m_cb.previewReady)
                m_cb.previewReady();
        }
    }
}

} // namespace cam
