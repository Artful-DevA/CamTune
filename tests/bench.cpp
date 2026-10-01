// SPDX-License-Identifier: GPL-3.0-or-later
// Measures per-frame processing cost for typical webcam scenarios.
//   camcore_bench [threads]

#include "core/Clock.h"
#include "core/MjpegDecoder.h"
#include "core/Processor.h"

#include <turbojpeg.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

using namespace cam;

static double timeIt(int iterations, const std::function<void()> &fn)
{
    fn(); // warm-up
    int64_t t0 = monotonicNs();
    for (int i = 0; i < iterations; ++i)
        fn();
    return (monotonicNs() - t0) / 1e6 / iterations;
}

int main(int argc, char **argv)
{
    int threads = argc > 1 ? std::atoi(argv[1]) : ThreadPool::defaultThreadCount();
    std::printf("threads: %d\n", threads);
    Processor proc(threads);

    Frame src1080;
    src1080.allocI420(1920, 1080);
    renderTestPattern(src1080, 1);

    // YUYV 1080p source, packed as a camera would deliver it.
    Frame yuyv;
    yuyv.allocBytes(size_t(1920) * 1080 * 2);
    yuyv.format = PixelFormat::YUYV;
    yuyv.width = 1920;
    yuyv.height = 1080;
    yuyv.stride[0] = 1920 * 2;
    packI420ToYuyv(src1080, yuyv.plane[0], yuyv.stride[0]);

    // MJPEG 1080p 4:2:2 source.
    tjhandle enc = tjInitCompress();
    std::vector<uint8_t> p422(size_t(1920) * 1080 * 2);
    uint8_t *yp = p422.data(), *up = yp + 1920 * 1080, *vp = up + 960 * 1080;
    for (int y = 0; y < 1080; ++y) {
        std::memcpy(yp + y * 1920, src1080.plane[0] + y * 1920, 1920);
        std::memcpy(up + y * 960, src1080.plane[1] + (y / 2) * 960, 960);
        std::memcpy(vp + y * 960, src1080.plane[2] + (y / 2) * 960, 960);
    }
    const unsigned char *planes[3] = {yp, up, vp};
    unsigned char *jpeg = nullptr;
    unsigned long jpegSize = 0;
    tjCompressFromYUVPlanes(enc, planes, 1920, nullptr, 1080, TJSAMP_422, &jpeg, &jpegSize, 85, 0);
    tjDestroy(enc);

    Frame out720, out1080, decoded;
    out720.allocI420(1280, 720);
    out1080.allocI420(1920, 1080);
    MjpegDecoder dec;

    FramingParams none;
    FramingParams zoomed;
    zoomed.zoom = 1.37;
    zoomed.panX = 0.3;
    FramingParams rotated = zoomed;
    rotated.rotation = 3.5;
    ColorParams color;
    color.contrast = 1.1;
    color.saturation = 1.2;
    color.gamma = 1.1;

    std::printf("%-46s %8s\n", "scenario", "ms/frame");
    auto row = [](const char *n, double ms) { std::printf("%-46s %8.2f\n", n, ms); };

    row("MJPEG 1080p decode (full)", timeIt(30, [&] { dec.decode(jpeg, jpegSize, decoded, 1); }));
    row("MJPEG 1080p decode (1/2 scale)", timeIt(30, [&] { dec.decode(jpeg, jpegSize, decoded, 2); }));
    dec.decode(jpeg, jpegSize, decoded, 1);
    row("I420 1080p -> 1080p identity", timeIt(60, [&] { proc.process(src1080, out1080, none); }));
    proc.setColor(color);
    row("I420 1080p -> 1080p color", timeIt(60, [&] { proc.process(src1080, out1080, none); }));
    row("YUYV 1080p -> 1080p zoom+pan+color", timeIt(60, [&] { proc.process(yuyv, out1080, zoomed); }));
    row("MJPEG-planar 1080p -> 720p zoom+color", timeIt(60, [&] { proc.process(decoded, out720, zoomed); }));
    row("MJPEG-planar 1080p -> 1080p rotate+zoom", timeIt(60, [&] { proc.process(decoded, out1080, rotated); }));
    color.sharpness = 0.6;
    proc.setColor(color);
    row("YUYV 1080p -> 1080p zoom+color+sharpen", timeIt(60, [&] { proc.process(yuyv, out1080, zoomed); }));
    color.sharpness = 0;
    proc.setColor(color);
    EffectParams e;
    e.mode = EffectMode::BlurAll;
    proc.setEffects(e);
    row("YUYV 1080p -> 1080p + background blur", timeIt(30, [&] { proc.process(yuyv, out1080, zoomed); }));
    e.mode = EffectMode::Foreground;
    proc.setEffects(e);
    row("YUYV 1080p -> 1080p + foreground mask", timeIt(30, [&] { proc.process(yuyv, out1080, zoomed); }));
    std::vector<uint8_t> packed(size_t(1920) * 1080 * 2);
    row("I420 -> YUYV pack 1080p", timeIt(60, [&] { packI420ToYuyv(out1080, packed.data(), 3840); }));
    tjFree(jpeg);
    return 0;
}
