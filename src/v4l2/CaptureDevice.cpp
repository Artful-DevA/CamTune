// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaptureDevice.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <unistd.h>

namespace cam::v4l2 {

struct CaptureDevice::BufferSet {
    struct Map {
        void *addr = MAP_FAILED;
        size_t length = 0;
    };
    std::vector<Map> maps;
    std::mutex mutex;
    std::vector<uint32_t> returned; // indices whose frames were released
    bool alive = true;

    ~BufferSet()
    {
        for (auto &m : maps)
            if (m.addr != MAP_FAILED)
                munmap(m.addr, m.length);
    }
};

static int64_t monotonicNs()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

CaptureDevice::~CaptureDevice() { close(); }

bool CaptureDevice::open(const std::string &path, std::string &error)
{
    close();
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        int e = errno;
        error = "Cannot open " + path + ": " + errnoString(e);
        if (e == EACCES)
            error += " (is your user in the 'video' group?)";
        return false;
    }
    v4l2_capability cap{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
        error = path + " is not a V4L2 device";
        ::close(fd);
        return false;
    }
    uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
        error = path + " does not support video capture streaming";
        ::close(fd);
        return false;
    }
    m_fd = fd;
    m_path = path;
    return true;
}

std::vector<CameraMode> CaptureDevice::modes() const
{
    if (m_fd < 0)
        return {};
    return enumerateModes(m_fd);
}

bool CaptureDevice::configure(uint32_t fourcc, int width, int height, CameraMode::Rate rate,
                              std::string &error, int &errnoOut)
{
    errnoOut = 0;
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.pixelformat = fourcc;
    fmt.fmt.pix.width = uint32_t(width);
    fmt.fmt.pix.height = uint32_t(height);
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(m_fd, VIDIOC_S_FMT, &fmt) != 0) {
        errnoOut = errno;
        error = "Cannot set camera format: " + errnoString(errnoOut);
        return false;
    }
    if (fromFourcc(fmt.fmt.pix.pixelformat) == PixelFormat::Invalid) {
        error = "Camera switched to unsupported pixel format " + fourccToString(fmt.fmt.pix.pixelformat);
        return false;
    }
    if (fmt.fmt.pix.width == 0 || fmt.fmt.pix.height == 0 || fmt.fmt.pix.width > 16384 ||
        fmt.fmt.pix.height > 16384) {
        error = "Camera reported an invalid frame size";
        return false;
    }
    m_fourcc = fmt.fmt.pix.pixelformat;
    m_width = int(fmt.fmt.pix.width);
    m_height = int(fmt.fmt.pix.height);
    m_bytesPerLine = int(fmt.fmt.pix.bytesperline);
    m_sizeImage = fmt.fmt.pix.sizeimage;

    // Frame rate. Failure here is not fatal: many drivers simply ignore it.
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = rate.den ? rate.den : 1;
    parm.parm.capture.timeperframe.denominator = rate.num ? rate.num : 30;
    m_fps = rate.fps();
    if (xioctl(m_fd, VIDIOC_S_PARM, &parm) == 0) {
        const auto &tpf = parm.parm.capture.timeperframe;
        if (tpf.numerator && tpf.denominator)
            m_fps = double(tpf.denominator) / tpf.numerator;
    }
    return true;
}

bool CaptureDevice::start(int bufferCount, std::string &error, int &errnoOut)
{
    errnoOut = 0;
    v4l2_requestbuffers req{};
    req.count = uint32_t(bufferCount);
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(m_fd, VIDIOC_REQBUFS, &req) != 0) {
        errnoOut = errno;
        error = "Cannot allocate camera buffers: " + errnoString(errnoOut);
        return false;
    }
    if (req.count < 2) {
        error = "Camera provided too few buffers";
        return false;
    }
    auto set = std::make_shared<BufferSet>();
    set->maps.resize(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(m_fd, VIDIOC_QUERYBUF, &buf) != 0) {
            errnoOut = errno;
            error = "Cannot query camera buffer: " + errnoString(errnoOut);
            return false;
        }
        void *addr = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, buf.m.offset);
        if (addr == MAP_FAILED) {
            errnoOut = errno;
            error = "Cannot map camera buffer: " + errnoString(errnoOut);
            return false;
        }
        set->maps[i].addr = addr;
        set->maps[i].length = buf.length;
        if (xioctl(m_fd, VIDIOC_QBUF, &buf) != 0) {
            errnoOut = errno;
            error = "Cannot queue camera buffer: " + errnoString(errnoOut);
            return false;
        }
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(m_fd, VIDIOC_STREAMON, &type) != 0) {
        errnoOut = errno;
        error = "Cannot start camera stream: " + errnoString(errnoOut);
        return false;
    }
    m_buffers = std::move(set);
    m_streaming = true;
    return true;
}

void CaptureDevice::stop()
{
    if (m_fd >= 0 && m_streaming) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(m_fd, VIDIOC_STREAMOFF, &type);
    }
    m_streaming = false;
    if (m_buffers) {
        std::lock_guard<std::mutex> lock(m_buffers->mutex);
        m_buffers->alive = false;
        m_buffers->returned.clear();
    }
    // Mappings stay valid until the last in-flight frame is released.
    m_buffers.reset();
}

void CaptureDevice::close()
{
    stop();
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
}

void CaptureDevice::requeueReturned()
{
    if (!m_buffers || m_fd < 0)
        return;
    std::vector<uint32_t> indices;
    {
        std::lock_guard<std::mutex> lock(m_buffers->mutex);
        indices.swap(m_buffers->returned);
    }
    for (uint32_t idx : indices) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = idx;
        xioctl(m_fd, VIDIOC_QBUF, &buf);
    }
}

CaptureDevice::Status CaptureDevice::dequeueLatest(FramePtr &out, int &errnoOut, int &droppedOut)
{
    errnoOut = 0;
    droppedOut = 0;
    if (!m_streaming || !m_buffers)
        return Status::Error;

    bool have = false;
    v4l2_buffer latest{};
    for (;;) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(m_fd, VIDIOC_DQBUF, &buf) != 0) {
            int e = errno;
            if (e == EAGAIN)
                break;
            if (have) // deliver what we have; the error will resurface next time
                break;
            errnoOut = e;
            return (e == ENODEV || e == EIO || e == ENXIO) ? Status::Disconnected : Status::Error;
        }
        if (buf.index >= m_buffers->maps.size())
            continue; // malformed; ignore
        if (have) {
            // A newer frame is ready: recycle the older one immediately.
            xioctl(m_fd, VIDIOC_QBUF, &latest);
            ++droppedOut;
        }
        latest = buf;
        have = true;
    }
    if (!have)
        return Status::NoFrame;

    // Corrupted or empty frames are recycled and skipped.
    if ((latest.flags & V4L2_BUF_FLAG_ERROR) || latest.bytesused == 0) {
        xioctl(m_fd, VIDIOC_QBUF, &latest);
        return Status::NoFrame;
    }

    auto set = m_buffers;
    const auto &map = set->maps[latest.index];
    auto frame = std::shared_ptr<Frame>(new Frame(), [set, idx = latest.index](Frame *f) {
        {
            std::lock_guard<std::mutex> lock(set->mutex);
            if (set->alive)
                set->returned.push_back(idx);
        }
        delete f;
    });
    // The frame does not own its memory; keepAlive pins the mappings.
    frame->keepAlive = set;
    frame->width = m_width;
    frame->height = m_height;
    frame->format = fromFourcc(m_fourcc);
    frame->bytesUsed = std::min<size_t>(latest.bytesused, map.length);
    frame->sequence = ++m_sequence;
    frame->fullRange = false;
    if ((latest.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC)
        frame->timestampNs = int64_t(latest.timestamp.tv_sec) * 1000000000LL + latest.timestamp.tv_usec * 1000LL;
    else
        frame->timestampNs = monotonicNs();

    auto *base = static_cast<uint8_t *>(map.addr);
    const int w = m_width, h = m_height;
    int bpl = m_bytesPerLine;
    switch (frame->format) {
    case PixelFormat::YUYV:
    case PixelFormat::YVYU:
    case PixelFormat::UYVY:
        if (bpl < w * 2)
            bpl = w * 2;
        frame->plane[0] = base;
        frame->stride[0] = bpl;
        if (size_t(bpl) * h > map.length)
            return Status::NoFrame;
        break;
    case PixelFormat::Grey:
        if (bpl < w)
            bpl = w;
        frame->plane[0] = base;
        frame->stride[0] = bpl;
        if (size_t(bpl) * h > map.length)
            return Status::NoFrame;
        break;
    case PixelFormat::NV12:
    case PixelFormat::NV21:
        if (bpl < w)
            bpl = w;
        if (size_t(bpl) * h * 3 / 2 > map.length)
            return Status::NoFrame;
        frame->plane[0] = base;
        frame->stride[0] = bpl;
        frame->plane[1] = base + size_t(bpl) * h;
        frame->stride[1] = bpl;
        break;
    case PixelFormat::I420:
    case PixelFormat::YV12: {
        if (bpl < w)
            bpl = w;
        const int cbpl = bpl / 2;
        const size_t ySize = size_t(bpl) * h, cSize = size_t(cbpl) * ((h + 1) / 2);
        if (ySize + 2 * cSize > map.length)
            return Status::NoFrame;
        frame->plane[0] = base;
        frame->stride[0] = bpl;
        uint8_t *first = base + ySize, *second = base + ySize + cSize;
        bool yv = frame->format == PixelFormat::YV12;
        frame->plane[1] = yv ? second : first; // U
        frame->plane[2] = yv ? first : second; // V
        frame->stride[1] = frame->stride[2] = cbpl;
        break;
    }
    case PixelFormat::MJPEG:
        frame->plane[0] = base;
        frame->fullRange = true;
        break;
    default:
        return Status::NoFrame;
    }
    out = std::move(frame);
    return Status::Ok;
}

} // namespace cam::v4l2
