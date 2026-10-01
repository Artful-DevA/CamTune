// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "V4l2Util.h"
#include "core/Frame.h"
#include "core/Params.h"

#include <string>
#include <vector>

namespace cam::v4l2 {

// Writer side of a v4l2loopback virtual camera.
class LoopbackOutput {
public:
    ~LoopbackOutput();

    // Lists v4l2loopback devices.
    static std::vector<DeviceInfo> findDevices();

    // Opens and configures the device. If another application is already
    // consuming the device in a different format, the existing format is
    // adopted instead (`adoptedExisting` is set) so the consumer keeps working.
    bool open(const std::string &path, int width, int height, int fps, OutputPixelFormat fmt,
              std::string &error, bool &adoptedExisting);
    void close();
    bool isOpen() const { return m_fd >= 0; }

    // Writes one I420 frame (converted to the device format if needed). The
    // frame must match width() x height().
    enum class WriteResult { Ok, Dropped, Disconnected, Error };
    WriteResult write(const Frame &i420, int &errnoOut);

    int width() const { return m_width; }
    int height() const { return m_height; }
    OutputPixelFormat pixelFormat() const { return m_format; }
    const std::string &path() const { return m_path; }

private:
    int m_fd = -1;
    std::string m_path;
    int m_width = 0, m_height = 0;
    OutputPixelFormat m_format = OutputPixelFormat::I420;
    size_t m_frameSize = 0;
    std::vector<uint8_t> m_packed; // conversion buffer for YUYV output
};

} // namespace cam::v4l2
