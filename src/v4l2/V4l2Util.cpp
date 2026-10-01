// SPDX-License-Identifier: GPL-3.0-or-later
#include "V4l2Util.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <tuple>
#include <unistd.h>

namespace cam::v4l2 {

int xioctl(int fd, unsigned long request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

std::string errnoString(int err)
{
    char buf[256];
    // GNU strerror_r returns a char*.
    const char *s = strerror_r(err, buf, sizeof buf);
    return s ? std::string(s) : std::string("error ") + std::to_string(err);
}

std::string fourccToString(uint32_t f)
{
    std::string s;
    for (int i = 0; i < 4; ++i) {
        char c = char((f >> (8 * i)) & 0xff);
        s.push_back(c >= 32 && c < 127 ? c : '?');
    }
    return s;
}

PixelFormat fromFourcc(uint32_t f)
{
    switch (f) {
    case V4L2_PIX_FMT_MJPEG:
    case V4L2_PIX_FMT_JPEG:
        return PixelFormat::MJPEG;
    case V4L2_PIX_FMT_YUYV: return PixelFormat::YUYV;
    case V4L2_PIX_FMT_YVYU: return PixelFormat::YVYU;
    case V4L2_PIX_FMT_UYVY: return PixelFormat::UYVY;
    case V4L2_PIX_FMT_NV12: return PixelFormat::NV12;
    case V4L2_PIX_FMT_NV21: return PixelFormat::NV21;
    case V4L2_PIX_FMT_YUV420: return PixelFormat::I420;
    case V4L2_PIX_FMT_YVU420: return PixelFormat::YV12;
    case V4L2_PIX_FMT_GREY: return PixelFormat::Grey;
    default: return PixelFormat::Invalid;
    }
}

static std::string cstr(const __u8 *s, size_t n)
{
    size_t len = strnlen(reinterpret_cast<const char *>(s), n);
    return std::string(reinterpret_cast<const char *>(s), len);
}

bool queryDevice(const std::string &path, DeviceInfo &info)
{
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return false;
    v4l2_capability cap{};
    bool ok = xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
    ::close(fd);
    if (!ok)
        return false;
    info.path = path;
    info.card = cstr(cap.card, sizeof cap.card);
    info.driver = cstr(cap.driver, sizeof cap.driver);
    info.busInfo = cstr(cap.bus_info, sizeof cap.bus_info);
    uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    info.isLoopback = info.driver == "v4l2 loopback";
    // UVC cameras also expose metadata nodes; only real capture nodes count.
    info.isCapture = (caps & V4L2_CAP_VIDEO_CAPTURE) && (caps & V4L2_CAP_STREAMING) && !info.isLoopback;
    return true;
}

static std::vector<std::pair<std::string, std::string>> byIdLinks()
{
    std::vector<std::pair<std::string, std::string>> links; // realpath -> link
    const char *dirPath = "/dev/v4l/by-id";
    DIR *d = opendir(dirPath);
    if (!d)
        return links;
    while (dirent *e = readdir(d)) {
        if (e->d_name[0] == '.')
            continue;
        std::string link = std::string(dirPath) + "/" + e->d_name;
        char resolved[PATH_MAX];
        if (realpath(link.c_str(), resolved))
            links.emplace_back(resolved, link);
    }
    closedir(d);
    std::sort(links.begin(), links.end());
    return links;
}

std::vector<DeviceInfo> enumerateDevices()
{
    std::vector<std::pair<int, std::string>> nodes;
    if (DIR *d = opendir("/dev")) {
        while (dirent *e = readdir(d)) {
            if (strncmp(e->d_name, "video", 5) != 0)
                continue;
            char *end = nullptr;
            long n = strtol(e->d_name + 5, &end, 10);
            if (end && *end == '\0' && end != e->d_name + 5)
                nodes.emplace_back(int(n), std::string("/dev/") + e->d_name);
        }
        closedir(d);
    }
    std::sort(nodes.begin(), nodes.end());
    auto links = byIdLinks();

    std::vector<DeviceInfo> out;
    for (auto &n : nodes) {
        DeviceInfo info;
        if (!queryDevice(n.second, info))
            continue;
        for (auto &l : links)
            if (l.first == n.second) {
                // Prefer the "-video-index0" link: it identifies the capture node.
                if (info.byIdPath.empty() || l.second.find("index0") != std::string::npos)
                    info.byIdPath = l.second;
            }
        out.push_back(std::move(info));
    }
    return out;
}

static void addRate(std::vector<CameraMode::Rate> &rates, uint32_t num, uint32_t den)
{
    if (num == 0 || den == 0)
        return;
    for (auto &r : rates)
        if (uint64_t(r.num) * den == uint64_t(num) * r.den)
            return;
    rates.push_back({num, den});
}

static std::vector<CameraMode::Rate> enumerateRates(int fd, uint32_t fourcc, int w, int h)
{
    std::vector<CameraMode::Rate> rates;
    v4l2_frmivalenum iv{};
    iv.pixel_format = fourcc;
    iv.width = uint32_t(w);
    iv.height = uint32_t(h);
    for (iv.index = 0; iv.index < 64; ++iv.index) {
        if (xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &iv) != 0)
            break;
        if (iv.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
            // interval = numerator/denominator seconds -> fps = den/num
            addRate(rates, iv.discrete.denominator, iv.discrete.numerator);
        } else {
            // Stepwise/continuous: offer the common rates within range.
            const v4l2_fract &mn = iv.stepwise.min, &mx = iv.stepwise.max;
            double maxFps = mn.numerator ? double(mn.denominator) / mn.numerator : 30.0;
            double minFps = mx.numerator ? double(mx.denominator) / mx.numerator : 1.0;
            for (int f : {60, 50, 30, 25, 24, 20, 15, 10, 5})
                if (f <= maxFps + 0.01 && f >= minFps - 0.01)
                    addRate(rates, uint32_t(f), 1);
            break;
        }
    }
    if (rates.empty())
        rates.push_back({30, 1}); // driver does not report intervals; assume a sane default
    std::sort(rates.begin(), rates.end(),
              [](const CameraMode::Rate &a, const CameraMode::Rate &b) { return a.fps() > b.fps(); });
    return rates;
}

std::vector<CameraMode> enumerateModes(int fd)
{
    std::vector<CameraMode> modes;
    v4l2_fmtdesc fd_{};
    fd_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (fd_.index = 0; fd_.index < 64; ++fd_.index) {
        if (xioctl(fd, VIDIOC_ENUM_FMT, &fd_) != 0)
            break;
        if (fromFourcc(fd_.pixelformat) == PixelFormat::Invalid)
            continue;
        v4l2_frmsizeenum fs{};
        fs.pixel_format = fd_.pixelformat;
        for (fs.index = 0; fs.index < 128; ++fs.index) {
            if (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) != 0)
                break;
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                CameraMode m;
                m.fourcc = fd_.pixelformat;
                m.width = int(fs.discrete.width);
                m.height = int(fs.discrete.height);
                if (m.width <= 0 || m.height <= 0 || m.width > 16384 || m.height > 16384)
                    continue;
                m.rates = enumerateRates(fd, m.fourcc, m.width, m.height);
                modes.push_back(std::move(m));
            } else {
                const auto &sw = fs.stepwise;
                static const int common[][2] = {{3840, 2160}, {2560, 1440}, {1920, 1080}, {1600, 1200},
                                                {1280, 960},  {1280, 720},  {1024, 768},  {960, 540},
                                                {800, 600},   {640, 480},   {640, 360},   {320, 240}};
                for (auto &c : common) {
                    if (uint32_t(c[0]) < sw.min_width || uint32_t(c[0]) > sw.max_width ||
                        uint32_t(c[1]) < sw.min_height || uint32_t(c[1]) > sw.max_height)
                        continue;
                    CameraMode m;
                    m.fourcc = fd_.pixelformat;
                    m.width = c[0];
                    m.height = c[1];
                    m.rates = enumerateRates(fd, m.fourcc, m.width, m.height);
                    modes.push_back(std::move(m));
                }
                break;
            }
        }
    }
    return modes;
}

bool chooseMode(const std::vector<CameraMode> &modes, int tw, int th, int tfps, CameraMode &best,
                CameraMode::Rate &bestRate)
{
    if (modes.empty())
        return false;
    tw = std::max(tw, 16);
    th = std::max(th, 16);
    const double aspect = double(tw) / th;
    bool found = false;
    // Lexicographic score; larger is better.
    std::tuple<int, int, double, int, double> bestScore;
    for (const auto &m : modes) {
        // Usable area once cropped to the output aspect (fill mode).
        double usableW = std::min<double>(m.width, m.height * aspect);
        double usableH = std::min<double>(m.height, m.width / aspect);
        bool covers = usableW + 0.5 >= tw && usableH + 0.5 >= th;
        double pixels = double(m.width) * m.height;
        bool raw = fromFourcc(m.fourcc) != PixelFormat::MJPEG;
        for (const auto &r : m.rates) {
            double fps = r.fps();
            bool fpsOk = fps + 0.5 >= tfps;
            // Size preference: smallest covering size, else the largest.
            double sizeScore = covers ? -pixels : pixels;
            // Rate preference: closest to the target from above, else the highest.
            double rateScore = fpsOk ? -fps : fps;
            auto score = std::make_tuple(int(fpsOk), int(covers), sizeScore, int(raw), rateScore);
            if (!found || score > bestScore) {
                found = true;
                bestScore = score;
                best = m;
                bestRate = r;
            }
        }
    }
    return found;
}

} // namespace cam::v4l2
