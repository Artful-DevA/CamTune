// SPDX-License-Identifier: GPL-3.0-or-later
#include "ControlWorker.h"

#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <unistd.h>

namespace cam {

namespace {

bool isAutoControl(uint32_t id)
{
    switch (id) {
    case V4L2_CID_EXPOSURE_AUTO:
    case V4L2_CID_AUTO_WHITE_BALANCE:
    case V4L2_CID_FOCUS_AUTO:
    case V4L2_CID_AUTOGAIN:
    case V4L2_CID_HUE_AUTO:
        return true;
    default:
        return false;
    }
}

} // namespace

ControlWorker::ControlWorker(const EngineCallbacks &cb) : m_cb(cb) {}

ControlWorker::~ControlWorker() { stop(); }

void ControlWorker::start()
{
    if (m_thread.joinable())
        return;
    m_stop = false;
    m_thread = std::thread([this] { run(); });
    pthread_setname_np(m_thread.native_handle(), "cam-controls");
}

void ControlWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void ControlWorker::setDevice(const std::string &path)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_wantedPath == path)
            return;
        m_wantedPath = path;
        // Pending values belong to the previous device.
        m_pending.clear();
        m_resetPending = false;
    }
    m_cv.notify_all();
}

void ControlWorker::setControl(uint32_t id, int64_t value)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending[id] = value;
    }
    m_cv.notify_all();
}

void ControlWorker::applyAll(const std::map<uint32_t, int64_t> &values)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto &kv : values)
            m_pending[kv.first] = kv.second;
    }
    m_cv.notify_all();
}

void ControlWorker::resetToDefaults()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_resetPending = true;
    }
    m_cv.notify_all();
}

void ControlWorker::refresh()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_refreshPending = true;
    }
    m_cv.notify_all();
}

void ControlWorker::run()
{
    using namespace std::chrono_literals;
    int fd = -1;
    std::string openPath;
    std::vector<v4l2::ControlInfo> controls;
    bool dirty = false; // values changed since the last published refresh

    auto publish = [&] {
        if (m_cb.controls)
            m_cb.controls(controls);
    };

    for (;;) {
        std::string wanted;
        std::map<uint32_t, int64_t> pending;
        bool reset = false, refreshNow = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            // While changes are flowing, wake up shortly after they stop to
            // publish the new state (auto-mode flags, clamped values).
            auto ready = [&] {
                return m_stop || m_wantedPath != openPath || !m_pending.empty() || m_resetPending ||
                       m_refreshPending;
            };
            if (dirty) {
                if (!m_cv.wait_for(lock, 150ms, ready)) {
                    lock.unlock();
                    if (fd >= 0) {
                        v4l2::refreshControls(fd, controls);
                        publish();
                    }
                    dirty = false;
                    continue;
                }
            } else {
                m_cv.wait(lock, ready);
            }
            if (m_stop)
                break;
            wanted = m_wantedPath;
            pending.swap(m_pending);
            reset = m_resetPending;
            refreshNow = m_refreshPending;
            m_resetPending = m_refreshPending = false;
        }

        if (wanted != openPath) {
            if (fd >= 0)
                ::close(fd);
            fd = -1;
            controls.clear();
            openPath = wanted;
            if (!wanted.empty()) {
                fd = ::open(wanted.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
                if (fd >= 0)
                    controls = v4l2::enumerateControls(fd);
            }
            publish();
            dirty = false;
        }
        if (fd < 0)
            continue;

        if (reset) {
            // Auto modes first so dependent manual controls become writable or
            // are correctly left to the camera.
            std::vector<v4l2::ControlInfo> ordered = controls;
            std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
                return isAutoControl(a.id) && !isAutoControl(b.id);
            });
            for (auto &c : ordered) {
                if (c.readOnly || c.type == v4l2::ControlInfo::Type::Button)
                    continue;
                std::string err;
                v4l2::setControl(fd, c.id, c.defaultValue, err);
            }
            dirty = true;
        }

        if (!pending.empty()) {
            std::vector<std::pair<uint32_t, int64_t>> ordered(pending.begin(), pending.end());
            std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
                return isAutoControl(a.first) && !isAutoControl(b.first);
            });
            for (auto &kv : ordered) {
                auto it = std::find_if(controls.begin(), controls.end(),
                                       [&](const v4l2::ControlInfo &c) { return c.id == kv.first; });
                if (it == controls.end())
                    continue; // control not offered by this camera (e.g. from a preset)
                if (it->readOnly)
                    continue;
                int64_t v = kv.second;
                if (it->type == v4l2::ControlInfo::Type::Integer || it->type == v4l2::ControlInfo::Type::Boolean)
                    v = std::clamp(v, it->minimum, it->maximum);
                std::string err;
                if (!v4l2::setControl(fd, kv.first, v, err)) {
                    // Writing a manual value while its auto mode is on is expected to fail; stay quiet.
                    if (!it->inactive && m_cb.controlError)
                        m_cb.controlError("Could not set “" + it->name + "”: " + err);
                } else {
                    it->value = v;
                }
            }
            dirty = true;
        }

        if (refreshNow) {
            v4l2::refreshControls(fd, controls);
            publish();
            dirty = false;
        }
    }
    if (fd >= 0)
        ::close(fd);
}

} // namespace cam
