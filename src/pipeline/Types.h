// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Params.h"
#include "v4l2/CameraControls.h"
#include "v4l2/V4l2Util.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cam {

// Identifies a camera across unplug/replug, where /dev/videoN may change.
struct CameraSelection {
    std::string byIdPath; // /dev/v4l/by-id/... (most stable)
    std::string card;
    std::string busInfo;
    std::string path;     // last known node
    bool testPattern = false;

    bool empty() const { return !testPattern && path.empty() && byIdPath.empty() && card.empty(); }
    bool operator==(const CameraSelection &o) const
    {
        return byIdPath == o.byIdPath && card == o.card && busInfo == o.busInfo && path == o.path &&
               testPattern == o.testPattern;
    }
    bool operator!=(const CameraSelection &o) const { return !(*this == o); }
    std::string displayName() const { return testPattern ? std::string("Test pattern") : card; }
};

// Requested capture mode. Auto picks a mode that suits the output settings.
struct CaptureRequest {
    bool automatic = true;
    uint32_t fourcc = 0;
    int width = 0;
    int height = 0;
    v4l2::CameraMode::Rate rate;

    bool operator==(const CaptureRequest &o) const
    {
        return automatic == o.automatic && fourcc == o.fourcc && width == o.width && height == o.height &&
               rate.num == o.rate.num && rate.den == o.rate.den;
    }
};

enum class CameraState { NoCamera, Opening, Streaming, Waiting, Busy, Error, Suspended };
enum class OutputState { Disabled, NoDevice, Active, Error };

struct ActiveMode {
    uint32_t fourcc = 0;
    int width = 0;
    int height = 0;
    double fps = 0;
};

struct EngineStats {
    double captureFps = 0;
    double processFps = 0;
    double outputFps = 0;
    double processMs = 0;   // average time to process one frame
    double latencyMs = 0;   // capture timestamp -> written to virtual camera (or processed)
    uint64_t droppedFrames = 0;
    int decodeScale = 1;    // >1 when MJPEG is decoded at reduced resolution
    bool overloaded = false;
};

// Callbacks are invoked from worker threads; the UI layer must marshal them.
struct EngineCallbacks {
    std::function<void(CameraState, const std::string &message)> cameraState;
    std::function<void(const std::vector<v4l2::CameraMode> &modes, const ActiveMode &active)> cameraModes;
    std::function<void(const std::vector<v4l2::ControlInfo> &controls)> controls;
    std::function<void(const std::string &message)> controlError;
    std::function<void(OutputState, const std::string &message, int width, int height)> outputState;
    std::function<void()> previewReady;
};

// Plain counters shared between workers (monotonic, read by stats()).
struct Counters {
    std::atomic<uint64_t> captured{0};
    std::atomic<uint64_t> processed{0};
    std::atomic<uint64_t> written{0};
    std::atomic<uint64_t> dropped{0};
    std::atomic<int64_t> processNsEma{0};
    std::atomic<int64_t> latencyNsEma{0};
};

} // namespace cam
