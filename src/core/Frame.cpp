// SPDX-License-Identifier: GPL-3.0-or-later
#include "Frame.h"

#include <cstring>

namespace cam {

const char *pixelFormatName(PixelFormat f)
{
    switch (f) {
    case PixelFormat::I420: return "I420";
    case PixelFormat::YV12: return "YV12";
    case PixelFormat::NV12: return "NV12";
    case PixelFormat::NV21: return "NV21";
    case PixelFormat::YUYV: return "YUYV";
    case PixelFormat::YVYU: return "YVYU";
    case PixelFormat::UYVY: return "UYVY";
    case PixelFormat::Planar: return "Planar";
    case PixelFormat::Grey: return "GREY";
    case PixelFormat::MJPEG: return "MJPEG";
    case PixelFormat::Invalid: break;
    }
    return "invalid";
}

bool Frame::ensureCapacity(size_t bytes)
{
    if (bytes <= capacity && storage)
        return true;
    std::free(storage);
    storage = nullptr;
    capacity = 0;
    // 64-byte alignment keeps rows friendly to SIMD loads.
    size_t rounded = (bytes + 63) & ~size_t(63);
    void *p = std::aligned_alloc(64, rounded ? rounded : 64);
    if (!p)
        return false;
    storage = static_cast<uint8_t *>(p);
    capacity = rounded;
    return true;
}

bool Frame::allocI420(int w, int h)
{
    w &= ~1;
    h &= ~1;
    if (w <= 0 || h <= 0)
        return false;
    width = w;
    height = h;
    format = PixelFormat::I420;
    size_t ySize = size_t(w) * h;
    size_t cSize = size_t(w / 2) * (h / 2);
    if (!ensureCapacity(ySize + 2 * cSize))
        return false;
    plane[0] = storage;
    plane[1] = storage + ySize;
    plane[2] = storage + ySize + cSize;
    stride[0] = w;
    stride[1] = stride[2] = w / 2;
    planeWidth[0] = w;
    planeHeight[0] = h;
    planeWidth[1] = planeWidth[2] = w / 2;
    planeHeight[1] = planeHeight[2] = h / 2;
    bytesUsed = ySize + 2 * cSize;
    keepAlive.reset();
    return true;
}

bool Frame::allocPlanar(int w, int h, int cw, int ch, bool hasChroma)
{
    if (w <= 0 || h <= 0)
        return false;
    width = w;
    height = h;
    format = hasChroma ? PixelFormat::Planar : PixelFormat::Grey;
    // Row strides rounded to 32 bytes; turbojpeg accepts arbitrary strides.
    int ys = (w + 31) & ~31;
    int cs = hasChroma ? ((cw + 31) & ~31) : 0;
    size_t ySize = size_t(ys) * h;
    size_t cSize = hasChroma ? size_t(cs) * ch : 0;
    if (!ensureCapacity(ySize + 2 * cSize))
        return false;
    plane[0] = storage;
    stride[0] = ys;
    planeWidth[0] = w;
    planeHeight[0] = h;
    if (hasChroma) {
        plane[1] = storage + ySize;
        plane[2] = storage + ySize + cSize;
        stride[1] = stride[2] = cs;
        planeWidth[1] = planeWidth[2] = cw;
        planeHeight[1] = planeHeight[2] = ch;
    } else {
        plane[1] = plane[2] = nullptr;
        stride[1] = stride[2] = 0;
        planeWidth[1] = planeWidth[2] = 0;
        planeHeight[1] = planeHeight[2] = 0;
    }
    bytesUsed = ySize + 2 * cSize;
    keepAlive.reset();
    return true;
}

bool Frame::allocBytes(size_t bytes)
{
    if (!ensureCapacity(bytes))
        return false;
    plane[0] = storage;
    plane[1] = plane[2] = nullptr;
    bytesUsed = bytes;
    keepAlive.reset();
    return true;
}

std::shared_ptr<FramePool> FramePool::create(size_t maxFree)
{
    return std::shared_ptr<FramePool>(new FramePool(maxFree));
}

FramePool::~FramePool()
{
    for (Frame *f : m_free)
        delete f;
}

FramePtr FramePool::acquire()
{
    Frame *f = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_free.empty()) {
            f = m_free.back();
            m_free.pop_back();
        }
        ++m_outstanding;
    }
    if (!f)
        f = new Frame();
    // The deleter keeps only a weak reference so a pool can be destroyed while
    // frames are still in flight; those frames are then simply freed.
    std::weak_ptr<FramePool> weak = shared_from_this();
    return FramePtr(f, [weak](Frame *fr) {
        fr->keepAlive.reset();
        if (auto pool = weak.lock())
            pool->release(fr);
        else
            delete fr;
    });
}

void FramePool::release(Frame *f)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    --m_outstanding;
    if (m_free.size() < m_maxFree)
        m_free.push_back(f);
    else
        delete f;
}

size_t FramePool::outstanding() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_outstanding;
}

} // namespace cam
