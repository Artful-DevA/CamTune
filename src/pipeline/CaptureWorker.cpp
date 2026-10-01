// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaptureWorker.h"

#include "core/Clock.h"
#include "core/Processor.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <linux/videodev2.h>
#include <poll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace cam {

namespace {
constexpr int kBufferCount = 4;
constexpr int64_t kMs = 1000000;
} // namespace

CaptureWorker::CaptureWorker(Mailbox<FramePtr> &output, Counters &counters, const EngineCallbacks &cb)
    : m_output(output), m_counters(counters), m_cb(cb)
{
    m_wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    m_pool = FramePool::create(4);
}

CaptureWorker::~CaptureWorker()
{
    stop();
    if (m_wakeFd >= 0)
        ::close(m_wakeFd);
}

void CaptureWorker::start()
{
    if (m_thread.joinable())
        return;
    m_stop = false;
    m_thread = std::thread([this] { run(); });
    pthread_setname_np(m_thread.native_handle(), "cam-capture");
}

void CaptureWorker::stop()
{
    m_stop = true;
    wake();
    if (m_thread.joinable())
        m_thread.join();
    m_device.close();
}

void CaptureWorker::wake()
{
    if (m_wakeFd >= 0) {
        uint64_t one = 1;
        ssize_t r = ::write(m_wakeFd, &one, sizeof one);
        (void)r;
    }
}

bool CaptureWorker::waitWake(int timeoutMs)
{
    pollfd p{m_wakeFd, POLLIN, 0};
    int r = poll(&p, 1, timeoutMs);
    if (r > 0 && (p.revents & POLLIN)) {
        uint64_t v;
        ssize_t n = ::read(m_wakeFd, &v, sizeof v);
        (void)n;
        return true;
    }
    return false;
}

void CaptureWorker::selectCamera(const CameraSelection &sel)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_config.selection == sel)
            return;
        m_config.selection = sel;
        ++m_generation;
    }
    wake();
}

void CaptureWorker::setRequest(const CaptureRequest &req)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_config.request == req)
            return;
        m_config.request = req;
        ++m_generation;
    }
    wake();
}

void CaptureWorker::setTarget(int width, int height, int fps)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_config.targetW == width && m_config.targetH == height && m_config.targetFps == fps)
            return;
        m_config.targetW = width;
        m_config.targetH = height;
        m_config.targetFps = fps;
        // Only automatic mode depends on the target.
        if (m_config.request.automatic)
            ++m_generation;
    }
    wake();
}

void CaptureWorker::setSuspended(bool suspended)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_config.suspended == suspended)
            return;
        m_config.suspended = suspended;
        ++m_generation;
    }
    wake();
}

void CaptureWorker::sourceSize(int &w, int &h) const
{
    w = m_srcW.load();
    h = m_srcH.load();
}

std::string CaptureWorker::currentPath() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentPath;
}

void CaptureWorker::setState(CameraState s, const std::string &msg)
{
    if (s == m_state && msg == m_stateMsg)
        return;
    m_state = s;
    m_stateMsg = msg;
    if (s != CameraState::Streaming && s != CameraState::Opening) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentPath.clear();
    }
    if (m_cb.cameraState)
        m_cb.cameraState(s, msg);
}

bool CaptureWorker::resolvePath(const CameraSelection &sel, std::string &path)
{
    if (!sel.byIdPath.empty()) {
        char resolved[PATH_MAX];
        if (realpath(sel.byIdPath.c_str(), resolved)) {
            v4l2::DeviceInfo info;
            if (v4l2::queryDevice(resolved, info) && info.isCapture) {
                path = resolved;
                return true;
            }
        }
    }
    auto devices = v4l2::enumerateDevices();
    if (!sel.card.empty()) {
        for (auto &d : devices)
            if (d.isCapture && d.card == sel.card && d.busInfo == sel.busInfo) {
                path = d.path;
                return true;
            }
        // Same model plugged into another port.
        for (auto &d : devices)
            if (d.isCapture && d.card == sel.card) {
                path = d.path;
                return true;
            }
        return false;
    }
    for (auto &d : devices)
        if (d.isCapture && d.path == sel.path) {
            path = d.path;
            return true;
        }
    return false;
}

bool CaptureWorker::openAndStart(const Config &cfg, const std::string &path)
{
    const std::string name = cfg.selection.displayName().empty() ? path : cfg.selection.displayName();
    setState(CameraState::Opening, "Opening " + name + "…");
    std::string err;
    if (!m_device.open(path, err)) {
        setState(CameraState::Error, err);
        return false;
    }
    auto modes = m_device.modes();
    if (modes.empty()) {
        setState(CameraState::Error, name + " offers no supported video formats");
        m_device.close();
        return false;
    }

    v4l2::CameraMode mode;
    v4l2::CameraMode::Rate rate;
    bool chosen = false;
    if (!cfg.request.automatic) {
        for (auto &m : modes) {
            if (m.fourcc != cfg.request.fourcc || m.width != cfg.request.width || m.height != cfg.request.height)
                continue;
            mode = m;
            rate = m.rates.front();
            for (auto &r : m.rates)
                if (uint64_t(r.num) * cfg.request.rate.den == uint64_t(cfg.request.rate.num) * r.den)
                    rate = r;
            chosen = true;
        }
    }
    if (!chosen && !v4l2::chooseMode(modes, cfg.targetW, cfg.targetH, cfg.targetFps, mode, rate)) {
        setState(CameraState::Error, "No usable capture mode on " + name);
        m_device.close();
        return false;
    }

    int e = 0;
    const std::string busyMsg = name +
                                " is in use by another application. Close the camera there (in Zoom, Teams "
                                "or the browser select the virtual camera instead) — retrying automatically.";
    if (!m_device.configure(mode.fourcc, mode.width, mode.height, rate, err, e) ||
        !m_device.start(kBufferCount, err, e)) {
        if (e == EBUSY)
            setState(CameraState::Busy, busyMsg);
        else
            setState(CameraState::Error, name + ": " + err);
        m_device.close();
        return false;
    }

    m_srcW = m_device.width();
    m_srcH = m_device.height();
    m_fps = m_device.fps();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentPath = path;
    }
    if (m_cb.cameraModes)
        m_cb.cameraModes(modes, ActiveMode{m_device.fourcc(), m_device.width(), m_device.height(), m_device.fps()});
    setState(CameraState::Opening, "Starting " + name + "…");
    m_decodeFailures = 0;
    return true;
}

void CaptureWorker::deliver(FramePtr frame)
{
    if (!m_active.load())
        return; // frame (and its V4L2 buffer) is released immediately
    if (frame->format == PixelFormat::MJPEG) {
        FramePtr out = m_pool->acquire();
        int scale = std::clamp(m_decodeScale.load(), 1, 8);
        if (!m_decoder.decode(frame->plane[0], frame->bytesUsed, *out, scale)) {
            // Occasional corrupt frames are normal on busy USB buses; skip them.
            ++m_decodeFailures;
            m_counters.dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        m_decodeFailures = 0;
        out->timestampNs = frame->timestampNs;
        out->sequence = frame->sequence;
        frame.reset(); // hand the V4L2 buffer back before publishing
        if (m_output.put(std::move(out)))
            m_counters.dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (m_output.put(std::move(frame)))
        m_counters.dropped.fetch_add(1, std::memory_order_relaxed);
}

void CaptureWorker::runTestPattern(const Config &cfg, uint64_t generation)
{
    m_device.close();
    int w = cfg.request.automatic || cfg.request.width <= 0 ? cfg.targetW : cfg.request.width;
    int h = cfg.request.automatic || cfg.request.height <= 0 ? cfg.targetH : cfg.request.height;
    double fps = cfg.request.automatic ? cfg.targetFps : cfg.request.rate.fps();
    w = std::clamp(w, 64, 3840) & ~1;
    h = std::clamp(h, 64, 2160) & ~1;
    if (fps < 1 || fps > 120)
        fps = 30;
    m_srcW = w;
    m_srcH = h;
    m_fps = fps;

    std::vector<v4l2::CameraMode> modes;
    for (auto sz : {std::pair{640, 360}, std::pair{1280, 720}, std::pair{1920, 1080}}) {
        v4l2::CameraMode m;
        m.fourcc = V4L2_PIX_FMT_YUV420;
        m.width = sz.first;
        m.height = sz.second;
        m.rates = {{60, 1}, {30, 1}, {15, 1}};
        modes.push_back(m);
    }
    if (m_cb.cameraModes)
        m_cb.cameraModes(modes, ActiveMode{V4L2_PIX_FMT_YUV420, w, h, fps});
    setState(CameraState::Streaming, "");

    const int64_t interval = int64_t(1e9 / fps);
    int64_t next = monotonicNs();
    uint64_t index = 0;
    for (;;) {
        if (m_stop)
            return;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_generation != generation)
                return;
        }
        int64_t now = monotonicNs();
        if (now < next) {
            if (waitWake(int((next - now + kMs - 1) / kMs)))
                continue; // configuration changed; re-check
            now = monotonicNs();
        }
        ++index;
        m_counters.captured.fetch_add(1, std::memory_order_relaxed);
        if (m_active.load()) {
            FramePtr f = m_pool->acquire();
            if (f->allocI420(w, h)) {
                renderTestPattern(*f, index);
                f->timestampNs = now;
                f->sequence = index;
                f->fullRange = false;
                if (m_output.put(std::move(f)))
                    m_counters.dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }
        next += interval;
        if (next < now - interval)
            next = now; // we fell behind (e.g. suspend); do not try to catch up
    }
}

void CaptureWorker::run()
{
    uint64_t appliedGeneration = 0;
    bool needOpen = true;
    int64_t lastFrameNs = 0;
    int stallRestarts = 0;
    int failedOpens = 0;

    while (!m_stop) {
        Config cfg;
        uint64_t gen;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            cfg = m_config;
            gen = m_generation;
        }
        if (gen != appliedGeneration) {
            m_device.close();
            m_output.clear();
            needOpen = true;
            appliedGeneration = gen;
            stallRestarts = 0;
            failedOpens = 0;
        }
        if (cfg.suspended) {
            m_device.close();
            needOpen = true;
            setState(CameraState::Suspended, "Camera paused while the system is suspended");
            waitWake(1000);
            continue;
        }
        if (cfg.selection.empty()) {
            m_device.close();
            setState(CameraState::NoCamera, "No camera selected");
            waitWake(1000);
            continue;
        }
        if (cfg.selection.testPattern) {
            runTestPattern(cfg, gen);
            continue;
        }

        if (needOpen) {
            std::string path;
            if (!resolvePath(cfg.selection, path)) {
                setState(CameraState::Waiting,
                         "Waiting for “" + cfg.selection.displayName() + "” to be connected…");
                waitWake(1000);
                continue;
            }
            if (!openAndStart(cfg, path)) {
                // Back off gently, but keep trying forever: the camera may be freed later.
                ++failedOpens;
                waitWake(std::min(5000, 1000 * failedOpens));
                continue;
            }
            needOpen = false;
            failedOpens = 0;
            lastFrameNs = monotonicNs();
        }

        pollfd fds[2] = {{m_device.fd(), POLLIN, 0}, {m_wakeFd, POLLIN, 0}};
        int r = poll(fds, 2, 250);
        if (r > 0 && (fds[1].revents & POLLIN)) {
            uint64_t v;
            ssize_t n = ::read(m_wakeFd, &v, sizeof v);
            (void)n;
        }
        m_device.requeueReturned();

        const int64_t now = monotonicNs();
        if (r > 0 && (fds[0].revents & (POLLIN | POLLERR | POLLHUP))) {
            FramePtr frame;
            int err = 0, dropped = 0;
            auto st = m_device.dequeueLatest(frame, err, dropped);
            if (dropped)
                m_counters.dropped.fetch_add(uint64_t(dropped), std::memory_order_relaxed);
            if (st == v4l2::CaptureDevice::Status::Ok) {
                lastFrameNs = now;
                stallRestarts = 0;
                if (m_state != CameraState::Streaming)
                    setState(CameraState::Streaming, "");
                m_counters.captured.fetch_add(1, std::memory_order_relaxed);
                deliver(std::move(frame));
                continue;
            }
            if (st == v4l2::CaptureDevice::Status::Disconnected) {
                m_device.close();
                needOpen = true;
                setState(CameraState::Waiting, "“" + cfg.selection.displayName() +
                                                   "” was disconnected — waiting for it to come back…");
                waitWake(500);
                continue;
            }
            if (st == v4l2::CaptureDevice::Status::Error) {
                m_device.close();
                needOpen = true;
                setState(CameraState::Error, "Camera error (" + v4l2::errnoString(err) + ") — restarting…");
                waitWake(1000);
                continue;
            }
        }

        // Watchdog: a stream that silently stops (common after suspend/resume or
        // USB bandwidth trouble) is restarted.
        const double fps = std::max(1.0, m_fps.load());
        const int64_t stallLimit = std::max<int64_t>(3000 * kMs, int64_t(8e9 / fps));
        if (!needOpen && now - lastFrameNs > stallLimit) {
            ++stallRestarts;
            m_device.close();
            needOpen = true;
            setState(CameraState::Error, "Camera stopped sending frames — restarting it (attempt " +
                                             std::to_string(stallRestarts) + ")…");
            waitWake(stallRestarts > 3 ? 3000 : 300);
        }
    }
    m_device.close();
}

} // namespace cam
