// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Types.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace cam {

// Applies hardware (UVC) control changes on its own thread and its own file
// descriptor. UVC control transfers can take tens of milliseconds; doing them
// here keeps both the UI and frame capture free of stalls. Rapid slider
// changes are coalesced so only the latest value per control is sent.
class ControlWorker {
public:
    explicit ControlWorker(const EngineCallbacks &cb);
    ~ControlWorker();

    void start();
    void stop();

    // Opens the given device node for control access ("" closes it).
    void setDevice(const std::string &path);
    void setControl(uint32_t id, int64_t value);
    // Applies a full set (e.g. restoring settings after a reconnect).
    void applyAll(const std::map<uint32_t, int64_t> &values);
    void resetToDefaults();
    void refresh();

private:
    void run();

    const EngineCallbacks &m_cb;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;

    std::string m_wantedPath;
    std::map<uint32_t, int64_t> m_pending;
    bool m_resetPending = false;
    bool m_refreshPending = false;
};

} // namespace cam
