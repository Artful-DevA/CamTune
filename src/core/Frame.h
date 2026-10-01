// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>

namespace cam {

// Pixel layouts understood by the pipeline. Input frames may be any of these;
// processed frames are always I420 (planar 4:2:0, tightly packed).
enum class PixelFormat : uint8_t {
    Invalid,
    I420,     // Y plane, U plane (w/2 x h/2), V plane
    YV12,     // like I420 with U/V swapped
    NV12,     // Y plane, interleaved UV plane
    NV21,     // Y plane, interleaved VU plane
    YUYV,     // packed 4:2:2  Y0 U Y1 V
    YVYU,     // packed 4:2:2  Y0 V Y1 U
    UYVY,     // packed 4:2:2  U Y0 V Y1
    Planar,   // generic planar YUV with per-plane geometry (MJPEG decode output)
    Grey,     // luma only
    MJPEG,    // compressed, plane[0] holds the bitstream (bytesUsed bytes)
};

const char *pixelFormatName(PixelFormat f);

struct Frame {
    PixelFormat format = PixelFormat::Invalid;
    int width = 0;
    int height = 0;

    // Up to three planes. For packed formats only plane[0] is used.
    uint8_t *plane[3] = {nullptr, nullptr, nullptr};
    int stride[3] = {0, 0, 0};
    int planeWidth[3] = {0, 0, 0};
    int planeHeight[3] = {0, 0, 0};

    size_t bytesUsed = 0;     // payload size (compressed frames, contiguous I420)
    bool fullRange = false;   // JPEG/JFIF data is full range; raw webcam YUV is usually limited
    int64_t timestampNs = 0;  // CLOCK_MONOTONIC capture time
    uint64_t sequence = 0;

    // Owned storage (pooled frames).
    uint8_t *storage = nullptr;
    size_t capacity = 0;

    // External memory owner (e.g. a V4L2 mmap buffer set). Released with the frame.
    std::shared_ptr<void> keepAlive;

    Frame() = default;
    Frame(const Frame &) = delete;
    Frame &operator=(const Frame &) = delete;
    ~Frame() { std::free(storage); }

    bool ensureCapacity(size_t bytes);

    // Lay the frame out as contiguous, tightly packed I420 in owned storage.
    bool allocI420(int w, int h);
    // Generic planar layout with chroma planes of cw x ch (MJPEG decode target).
    bool allocPlanar(int w, int h, int cw, int ch, bool hasChroma);
    // Packed or compressed payload of the given size.
    bool allocBytes(size_t bytes);

    size_t i420Size() const { return size_t(width) * height + 2 * size_t(width / 2) * (height / 2); }
};

using FramePtr = std::shared_ptr<Frame>;

// Bounded pool of reusable frames. Frames handed out return to the pool when the
// last reference is dropped, so steady-state operation performs no allocation.
class FramePool : public std::enable_shared_from_this<FramePool> {
public:
    static std::shared_ptr<FramePool> create(size_t maxFree = 4);

    FramePtr acquire();
    size_t outstanding() const;

private:
    explicit FramePool(size_t maxFree) : m_maxFree(maxFree) {}
    void release(Frame *f);

    mutable std::mutex m_mutex;
    std::vector<Frame *> m_free;
    size_t m_maxFree;
    size_t m_outstanding = 0;

public:
    ~FramePool();
};

} // namespace cam
