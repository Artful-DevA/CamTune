// SPDX-License-Identifier: GPL-3.0-or-later
#include "LoopbackOutput.h"

#include "core/Processor.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <unistd.h>

namespace cam::v4l2 {

LoopbackOutput::~LoopbackOutput() { close(); }

std::vector<DeviceInfo> LoopbackOutput::findDevices()
{
    std::vector<DeviceInfo> out;
    for (auto &d : enumerateDevices())
        if (d.isLoopback)
            out.push_back(d);
    return out;
}

static uint32_t fourccFor(OutputPixelFormat f)
{
    return f == OutputPixelFormat::YUYV ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_YUV420;
}

bool LoopbackOutput::open(const std::string &path, int width, int height, int fps, OutputPixelFormat fmt,
                          std::string &error, bool &adoptedExisting)
{
    close();
    adoptedExisting = false;
    width &= ~1;
    height &= ~1;
    if (width <= 0 || height <= 0) {
        error = "Invalid output size";
        return false;
    }
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        int e = errno;
        error = "Cannot open virtual camera " + path + ": " + errnoString(e);
        if (e == EACCES)
            error += " (is your user in the 'video' group?)";
        return false;
    }
    v4l2_capability cap{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) != 0 ||
        std::string(reinterpret_cast<const char *>(cap.driver)) != "v4l2 loopback") {
        error = path + " is not a v4l2loopback device";
        ::close(fd);
        return false;
    }

    v4l2_format f{};
    f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    f.fmt.pix.width = uint32_t(width);
    f.fmt.pix.height = uint32_t(height);
    f.fmt.pix.pixelformat = fourccFor(fmt);
    f.fmt.pix.field = V4L2_FIELD_NONE;
    f.fmt.pix.bytesperline = uint32_t(fmt == OutputPixelFormat::YUYV ? width * 2 : width);
    f.fmt.pix.sizeimage = uint32_t(fmt == OutputPixelFormat::YUYV ? width * height * 2 : width * height * 3 / 2);
    f.fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
    f.fmt.pix.quantization = V4L2_QUANTIZATION_LIM_RANGE;
    bool setOk = xioctl(fd, VIDIOC_S_FMT, &f) == 0;
    int setErr = setOk ? 0 : errno;

    // Read back what is actually in effect. When a consumer holds the device the
    // format is locked; we then render at the locked size instead of failing.
    v4l2_format g{};
    g.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (xioctl(fd, VIDIOC_G_FMT, &g) != 0) {
        if (!setOk) {
            error = "Cannot configure virtual camera: " + errnoString(setErr);
            ::close(fd);
            return false;
        }
        g = f;
    }
    OutputPixelFormat actualFmt;
    if (g.fmt.pix.pixelformat == V4L2_PIX_FMT_YUV420)
        actualFmt = OutputPixelFormat::I420;
    else if (g.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
        actualFmt = OutputPixelFormat::YUYV;
    else {
        error = "Virtual camera is locked to unsupported format " + fourccToString(g.fmt.pix.pixelformat) +
                "; close the applications using it and try again";
        ::close(fd);
        return false;
    }
    int aw = int(g.fmt.pix.width), ah = int(g.fmt.pix.height);
    if (aw <= 0 || ah <= 0 || (aw & 1) || (ah & 1) || aw > 8192 || ah > 8192) {
        error = "Virtual camera reported an invalid size";
        ::close(fd);
        return false;
    }
    adoptedExisting = !setOk || aw != width || ah != height || actualFmt != fmt;

    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    parm.parm.output.timeperframe.numerator = 1;
    parm.parm.output.timeperframe.denominator = uint32_t(fps > 0 ? fps : 30);
    xioctl(fd, VIDIOC_S_PARM, &parm); // advisory only

    m_fd = fd;
    m_path = path;
    m_width = aw;
    m_height = ah;
    m_format = actualFmt;
    m_frameSize = actualFmt == OutputPixelFormat::YUYV ? size_t(aw) * ah * 2 : size_t(aw) * ah * 3 / 2;
    if (actualFmt == OutputPixelFormat::YUYV)
        m_packed.resize(m_frameSize);
    else
        m_packed.clear();
    return true;
}

void LoopbackOutput::close()
{
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
}

LoopbackOutput::WriteResult LoopbackOutput::write(const Frame &src, int &errnoOut)
{
    errnoOut = 0;
    if (m_fd < 0)
        return WriteResult::Error;
    if (src.format != PixelFormat::I420 || src.width != m_width || src.height != m_height)
        return WriteResult::Dropped;

    ssize_t r;
    if (m_format == OutputPixelFormat::YUYV) {
        packI420ToYuyv(src, m_packed.data(), m_width * 2);
        r = ::write(m_fd, m_packed.data(), m_frameSize);
    } else if (src.plane[1] == src.plane[0] + size_t(src.stride[0]) * src.height &&
               src.stride[0] == src.width && src.stride[1] == src.width / 2) {
        // Contiguous, tightly packed I420: written directly, no copy.
        r = ::write(m_fd, src.plane[0], m_frameSize);
    } else {
        // v4l2loopback treats every write() as one frame, so writev() with
        // several segments is not an option: gather into one buffer instead.
        m_packed.resize(m_frameSize);
        uint8_t *d = m_packed.data();
        for (int p = 0; p < 3; ++p) {
            const int pw = p == 0 ? src.width : src.width / 2;
            const int ph = p == 0 ? src.height : src.height / 2;
            for (int y = 0; y < ph; ++y, d += pw)
                std::memcpy(d, src.plane[p] + size_t(y) * src.stride[p], size_t(pw));
        }
        r = ::write(m_fd, m_packed.data(), m_frameSize);
    }
    if (r < 0) {
        int e = errno;
        errnoOut = e;
        if (e == EAGAIN || e == EINTR)
            return WriteResult::Dropped;
        if (e == ENODEV || e == ENXIO || e == EIO)
            return WriteResult::Disconnected;
        return WriteResult::Error;
    }
    return WriteResult::Ok;
}

} // namespace cam::v4l2
