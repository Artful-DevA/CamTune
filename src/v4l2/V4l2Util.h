// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Frame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cam::v4l2 {

// ioctl() that retries on EINTR. Returns -1 and leaves errno set on failure.
int xioctl(int fd, unsigned long request, void *arg);

std::string fourccToString(uint32_t fourcc);
// Maps a V4L2 fourcc to the pipeline's pixel format (Invalid if unsupported).
PixelFormat fromFourcc(uint32_t fourcc);

struct DeviceInfo {
    std::string path;     // /dev/videoN
    std::string card;     // human readable name
    std::string driver;
    std::string busInfo;
    std::string byIdPath; // /dev/v4l/by-id/... symlink if any (stable across replugs)
    bool isCapture = false;
    bool isLoopback = false; // v4l2loopback device (our output, never an input)
};

// Lists /dev/video* nodes. Never throws; unreadable nodes are skipped.
std::vector<DeviceInfo> enumerateDevices();
bool queryDevice(const std::string &path, DeviceInfo &info);

// A capture mode supported by the camera.
struct CameraMode {
    uint32_t fourcc = 0;
    int width = 0;
    int height = 0;
    // Frame intervals as fps numerators/denominators: fps = num / den.
    struct Rate {
        uint32_t num = 30;
        uint32_t den = 1;
        double fps() const { return den ? double(num) / den : 0.0; }
    };
    std::vector<Rate> rates;

    bool operator==(const CameraMode &o) const
    {
        return fourcc == o.fourcc && width == o.width && height == o.height;
    }
};

std::vector<CameraMode> enumerateModes(int fd);

// Picks the best mode for a target output: the smallest size covering the
// target at (at least) the target frame rate, preferring uncompressed formats
// when they can sustain the rate.
bool chooseMode(const std::vector<CameraMode> &modes, int targetW, int targetH, int targetFps,
                CameraMode &mode, CameraMode::Rate &rate);

std::string errnoString(int err);

} // namespace cam::v4l2
