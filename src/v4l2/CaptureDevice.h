// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "V4l2Util.h"
#include "core/Frame.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cam::v4l2 {

// Memory-mapped V4L2 streaming capture.
//
// Dequeued buffers are handed out zero-copy as Frames. When the last reference
// to such a Frame is dropped its buffer index is put on a return list, and the
// capture thread re-queues it on its next iteration; only the capture thread
// ever issues ioctls on the device fd.
class CaptureDevice {
public:
    enum class Status { Ok, NoFrame, Disconnected, Error };

    CaptureDevice() = default;
    ~CaptureDevice();
    CaptureDevice(const CaptureDevice &) = delete;
    CaptureDevice &operator=(const CaptureDevice &) = delete;

    bool open(const std::string &path, std::string &error);
    bool isOpen() const { return m_fd >= 0; }
    int fd() const { return m_fd; }
    const std::string &path() const { return m_path; }

    std::vector<CameraMode> modes() const;

    // Sets format and frame rate. On success the negotiated values are stored.
    bool configure(uint32_t fourcc, int width, int height, CameraMode::Rate rate, std::string &error,
                   int &errnoOut);
    bool start(int bufferCount, std::string &error, int &errnoOut);
    void stop();
    void close();

    // Dequeues every ready buffer and returns only the newest one; older ones go
    // straight back to the driver. This keeps latency at one frame no matter how
    // far behind the consumer is.
    Status dequeueLatest(FramePtr &out, int &errnoOut, int &droppedOut);
    // Re-queues buffers whose frames have been released.
    void requeueReturned();

    uint32_t fourcc() const { return m_fourcc; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    double fps() const { return m_fps; }

private:
    struct BufferSet;

    int m_fd = -1;
    std::string m_path;
    uint32_t m_fourcc = 0;
    int m_width = 0, m_height = 0, m_bytesPerLine = 0;
    size_t m_sizeImage = 0;
    double m_fps = 0;
    bool m_streaming = false;
    std::shared_ptr<BufferSet> m_buffers;
    uint64_t m_sequence = 0;
};

} // namespace cam::v4l2
