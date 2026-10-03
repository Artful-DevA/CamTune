// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the Qt-free core. Run with ctest or directly.

#include "core/Blur.h"
#include "core/Effects.h"
#include "core/PersonSegmenter.h"
#include "core/Frame.h"
#include "core/Framing.h"
#include "core/Mailbox.h"
#include "core/MjpegDecoder.h"
#include "core/Processor.h"
#include "core/ThreadPool.h"
#include "v4l2/V4l2Util.h"

#include <linux/videodev2.h>
#include <turbojpeg.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace cam;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                                    \
    do {                                                                                               \
        ++g_checks;                                                                                    \
        if (!(cond)) {                                                                                 \
            ++g_failures;                                                                              \
            std::fprintf(stderr, "  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                              \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                          \
    do {                                                                                               \
        ++g_checks;                                                                                    \
        double va_ = (a), vb_ = (b);                                                                   \
        if (!(std::fabs(va_ - vb_) <= (tol))) {                                                        \
            ++g_failures;                                                                              \
            std::fprintf(stderr, "  FAILED %s:%d: %s = %g, expected %g (±%g)\n", __FILE__, __LINE__, #a, \
                         va_, vb_, double(tol));                                                       \
        }                                                                                              \
    } while (0)

static std::vector<std::pair<const char *, std::function<void()>>> &registry()
{
    static std::vector<std::pair<const char *, std::function<void()>>> r;
    return r;
}
struct Register {
    Register(const char *n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); }
};
#define TEST(name)                                                                                     \
    static void name();                                                                                \
    static Register reg_##name(#name, name);                                                           \
    static void name()

// Y = f(x, y), U/V = 128 unless given.
static void fillI420(Frame &f, const std::function<int(int, int)> &yf, int u = 128, int v = 128)
{
    for (int y = 0; y < f.height; ++y)
        for (int x = 0; x < f.width; ++x)
            f.plane[0][y * f.stride[0] + x] = uint8_t(yf(x, y));
    for (int y = 0; y < f.height / 2; ++y) {
        std::memset(f.plane[1] + y * f.stride[1], u, size_t(f.width / 2));
        std::memset(f.plane[2] + y * f.stride[2], v, size_t(f.width / 2));
    }
}

static int Y(const Frame &f, int x, int y) { return f.plane[0][y * f.stride[0] + x]; }

TEST(framing_identity)
{
    FramingParams p;
    Affine m = computeFramingTransform(640, 480, 640, 480, p);
    CHECK_NEAR(m.a, 1, 1e-9);
    CHECK_NEAR(m.b, 0, 1e-9);
    CHECK_NEAR(m.c, 0, 1e-9);
    CHECK_NEAR(m.d, 0, 1e-9);
    CHECK_NEAR(m.e, 1, 1e-9);
    CHECK_NEAR(m.f, 0, 1e-9);
    CHECK(m.isAxisAligned());
}

TEST(framing_fill_crops_to_aspect)
{
    // 4:3 source into 16:9 output: full width, centered vertical band.
    FramingParams p;
    Affine m = computeFramingTransform(640, 480, 1280, 720, p);
    CHECK_NEAR(m.a * 1280, 640, 1e-6);
    CHECK_NEAR(m.e * 720, 360, 1e-6);
    CHECK_NEAR(m.f, 60, 1e-6); // (480 - 360) / 2
}

TEST(framing_pan_limits)
{
    FramingParams p;
    p.zoom = 2;
    p.panX = 1;
    p.panY = -1;
    Affine m = computeFramingTransform(640, 480, 640, 480, p);
    // Right edge of the output maps to the right edge of the source.
    CHECK_NEAR(m.a * 640 + m.c, 640, 1e-6);
    // Top edge maps to the top of the source.
    CHECK_NEAR(m.f, 0, 1e-6);
}

TEST(processor_identity_copy)
{
    Frame src, dst;
    src.allocI420(320, 240);
    renderTestPattern(src, 5);
    dst.allocI420(320, 240);
    Processor proc(2);
    CHECK(proc.process(src, dst, FramingParams()));
    CHECK(std::memcmp(src.plane[0], dst.plane[0], src.i420Size()) == 0);
}

TEST(processor_yuyv_input)
{
    const int w = 64, h = 32;
    Frame src;
    src.allocBytes(size_t(w) * h * 2);
    src.format = PixelFormat::YUYV;
    src.width = w;
    src.height = h;
    src.stride[0] = w * 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w / 2; ++x) {
            uint8_t *p = src.plane[0] + y * w * 2 + x * 4;
            p[0] = uint8_t(16 + 2 * x);
            p[1] = 100;
            p[2] = uint8_t(17 + 2 * x);
            p[3] = 150;
        }
    Frame dst;
    dst.allocI420(w, h);
    Processor proc(1);
    CHECK(proc.process(src, dst, FramingParams()));
    CHECK(Y(dst, 0, 0) == 16);
    CHECK(Y(dst, 1, 0) == 17);
    CHECK(Y(dst, 10, 7) == 26);
    CHECK(dst.plane[1][0] == 100);
    CHECK(dst.plane[2][5] == 150);
}

TEST(processor_mirror_and_rotate180)
{
    Frame src, a, b;
    src.allocI420(64, 48);
    fillI420(src, [](int x, int y) { return 16 + x + y * 2; });
    a.allocI420(64, 48);
    b.allocI420(64, 48);
    Processor proc(2);
    FramingParams m;
    m.mirror = true;
    proc.process(src, a, m);
    CHECK(Y(a, 0, 3) == Y(src, 63, 3));
    CHECK(Y(a, 63, 10) == Y(src, 0, 10));

    FramingParams r;
    r.rotation = 180;
    proc.process(src, b, r);
    CHECK(Y(b, 0, 0) == Y(src, 63, 47));
    CHECK(Y(b, 10, 20) == Y(src, 53, 27));
}

TEST(processor_rotate90_clockwise)
{
    // 64x48 source rotated 90° clockwise into a 48x64 output (exact fill).
    Frame src, dst;
    src.allocI420(64, 48);
    fillI420(src, [](int x, int y) { return 16 + x + y * 3; });
    dst.allocI420(48, 64);
    Processor proc(1);
    FramingParams p;
    p.rotation = 90;
    CHECK(proc.process(src, dst, p));
    // Clockwise: the source's left column becomes the output's top row,
    // with the source's bottom-left corner at the output's top-left.
    for (int x = 0; x < 48; x += 7)
        CHECK(std::abs(Y(dst, x, 0) - Y(src, 0, 47 - x)) <= 1);
    CHECK(std::abs(Y(dst, 47, 63) - Y(src, 63, 0)) <= 1);
}

TEST(processor_zoom_bilinear_gradient)
{
    // A horizontal ramp stays a ramp under 2x zoom (bilinear is exact on linear data).
    Frame src, dst;
    src.allocI420(200, 100);
    fillI420(src, [](int x, int) { return 16 + x; });
    dst.allocI420(200, 100);
    Processor proc(2);
    FramingParams p;
    p.zoom = 2;
    CHECK(proc.process(src, dst, p));
    for (int x = 2; x < 198; x += 13) {
        double srcPos = (x + 0.5) / 2.0 + 50 - 0.5;
        CHECK_NEAR(Y(dst, x, 50), 16 + srcPos, 1.01);
    }
}

TEST(processor_fit_letterbox)
{
    Frame src, dst;
    src.allocI420(640, 480);
    fillI420(src, [](int, int) { return 200; });
    dst.allocI420(1280, 720);
    Processor proc(2);
    FramingParams p;
    p.aspect = AspectMode::Fit;
    CHECK(proc.process(src, dst, p));
    CHECK(Y(dst, 5, 360) == 16);     // pillarbox
    CHECK(Y(dst, 1274, 360) == 16);
    CHECK(Y(dst, 640, 360) == 200);  // picture
}

TEST(processor_fine_rotation_fill_has_no_borders)
{
    Frame src, dst;
    src.allocI420(320, 240);
    fillI420(src, [](int, int) { return 120; });
    dst.allocI420(320, 180);
    Processor proc(2);
    FramingParams p;
    p.rotation = 7.5;
    CHECK(proc.process(src, dst, p));
    CHECK(Y(dst, 0, 0) == 120);
    CHECK(Y(dst, 319, 179) == 120);
    CHECK(Y(dst, 0, 179) == 120);
}

TEST(color_luts)
{
    uint8_t y[256], u[256], v[256];
    buildColorLuts(ColorParams(), true, y, u, v);
    CHECK(y[0] == 16);
    CHECK(y[255] == 235);
    CHECK(u[128] == 128);
    buildColorLuts(ColorParams(), false, y, u, v);
    for (int i = 16; i <= 235; ++i)
        CHECK(y[i] == i);
    ColorParams c;
    c.saturation = 0;
    buildColorLuts(c, false, y, u, v);
    CHECK(u[40] == 128 && v[200] == 128);
    c = ColorParams();
    c.brightness = 0.2;
    buildColorLuts(c, false, y, u, v);
    CHECK(y[100] > 100);
    c = ColorParams();
    c.gamma = 2.0;
    buildColorLuts(c, false, y, u, v);
    CHECK(y[126] > 126);
    CHECK(y[16] == 16 && y[235] == 235);
}

TEST(mjpeg_roundtrip)
{
    const int w = 320, h = 240;
    Frame src;
    src.allocI420(w, h);
    renderTestPattern(src, 3);
    // Encode with 4:2:2 like most webcams.
    tjhandle enc = tjInitCompress();
    std::vector<uint8_t> yuv422(size_t(w) * h * 2);
    // Build 4:2:2 planes from the I420 frame.
    uint8_t *yp = yuv422.data(), *up = yp + w * h, *vp = up + (w / 2) * h;
    for (int y = 0; y < h; ++y) {
        std::memcpy(yp + y * w, src.plane[0] + y * src.stride[0], size_t(w));
        std::memcpy(up + y * (w / 2), src.plane[1] + (y / 2) * src.stride[1], size_t(w / 2));
        std::memcpy(vp + y * (w / 2), src.plane[2] + (y / 2) * src.stride[2], size_t(w / 2));
    }
    const unsigned char *planes[3] = {yp, up, vp};
    unsigned char *jpeg = nullptr;
    unsigned long jpegSize = 0;
    int rc = tjCompressFromYUVPlanes(enc, planes, w, nullptr, h, TJSAMP_422, &jpeg, &jpegSize, 95, 0);
    CHECK(rc == 0);
    tjDestroy(enc);

    MjpegDecoder dec;
    Frame out;
    CHECK(dec.decode(jpeg, jpegSize, out, 1));
    CHECK(out.format == PixelFormat::Planar);
    CHECK(out.width == w && out.height == h);
    CHECK(out.planeWidth[1] == w / 2 && out.planeHeight[1] == h);
    CHECK(out.fullRange);

    Frame half;
    CHECK(dec.decode(jpeg, jpegSize, half, 2));
    CHECK(half.width == w / 2 && half.height == h / 2);

    // Truncated / garbage frames are rejected, not crashed on.
    CHECK(!dec.decode(jpeg, 3, out, 1));
    std::vector<uint8_t> junk(1000, 0x55);
    junk[0] = 0xFF;
    junk[1] = 0xD8;
    CHECK(!dec.decode(junk.data(), junk.size(), out, 1));
    // Well-formed segments but no frame header (libjpeg-turbo 3.x would reuse
    // the previous frame's header for this).
    const uint8_t noFrame[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00, 0xFF, 0xDA,
                               0x00, 0x02, 0x55, 0x55, 0xFF, 0xD9};
    CHECK(!dec.decode(noFrame, sizeof noFrame, out, 1));
    // A good frame still decodes after the rejected ones.
    CHECK(dec.decode(jpeg, jpegSize, out, 1));

    // Decoded output feeds the processor directly.
    Frame dst;
    dst.allocI420(w, h);
    Processor proc(2);
    CHECK(proc.process(out, dst, FramingParams()));
    // Full range JPEG data is mapped to limited range; luma close to the original.
    int maxDiff = 0;
    for (int y = 0; y < h; y += 9)
        for (int x = 0; x < w; x += 7)
            maxDiff = std::max(maxDiff, std::abs(Y(dst, x, y) - (16 + (Y(src, x, y) * 219 + 127) / 255)));
    CHECK(maxDiff < 24);
    tjFree(jpeg);
}

TEST(mailbox_drops_stale)
{
    Mailbox<int> mb;
    CHECK(!mb.put(1));
    CHECK(mb.put(2)); // replaced
    int v = 0;
    CHECK(mb.tryTake(v));
    CHECK(v == 2);
    CHECK(!mb.tryTake(v));
    CHECK(!mb.waitTake(v, std::chrono::milliseconds(5)));
}

TEST(frame_pool_reuses)
{
    auto pool = FramePool::create(2);
    Frame *first = nullptr;
    for (int i = 0; i < 1000; ++i) {
        FramePtr f = pool->acquire();
        f->allocI420(64, 64);
        if (!first)
            first = f.get();
        CHECK(f.get() == first);
    }
    CHECK(pool->outstanding() == 0);
    FramePtr survivor = pool->acquire();
    pool.reset(); // frames may outlive their pool
    survivor.reset();
}

TEST(thread_pool_covers_range)
{
    ThreadPool pool(4);
    for (int iter = 0; iter < 2000; ++iter) {
        const int n = 1 + iter % 777;
        std::vector<std::atomic<int>> hits(static_cast<size_t>(n));
        for (auto &h : hits)
            h = 0;
        pool.parallelFor(n, [&](int b, int e) {
            for (int i = b; i < e; ++i)
                hits[size_t(i)].fetch_add(1);
        }, 4);
        bool ok = true;
        for (auto &h : hits)
            ok &= h.load() == 1;
        CHECK(ok);
    }
}

TEST(choose_mode)
{
    using v4l2::CameraMode;
    std::vector<CameraMode> modes;
    auto add = [&](uint32_t f, int w, int h, std::vector<CameraMode::Rate> r) {
        CameraMode m;
        m.fourcc = f;
        m.width = w;
        m.height = h;
        m.rates = r;
        modes.push_back(m);
    };
    add(V4L2_PIX_FMT_YUYV, 1920, 1080, {{5, 1}});
    add(V4L2_PIX_FMT_YUYV, 1280, 720, {{10, 1}});
    add(V4L2_PIX_FMT_YUYV, 640, 480, {{30, 1}});
    add(V4L2_PIX_FMT_MJPEG, 1920, 1080, {{30, 1}});
    add(V4L2_PIX_FMT_MJPEG, 1280, 720, {{60, 1}, {30, 1}});
    CameraMode m;
    CameraMode::Rate r;
    CHECK(v4l2::chooseMode(modes, 1280, 720, 30, m, r));
    CHECK(m.fourcc == V4L2_PIX_FMT_MJPEG && m.width == 1280 && r.fps() == 30);
    CHECK(v4l2::chooseMode(modes, 1920, 1080, 30, m, r));
    CHECK(m.fourcc == V4L2_PIX_FMT_MJPEG && m.width == 1920);
    CHECK(v4l2::chooseMode(modes, 640, 480, 30, m, r));
    CHECK(m.fourcc == V4L2_PIX_FMT_YUYV && m.width == 640);
    CHECK(v4l2::chooseMode(modes, 1280, 720, 60, m, r));
    CHECK(m.width == 1280 && r.fps() == 60);
    CHECK(v4l2::chooseMode(modes, 1920, 1080, 60, m, r)); // impossible: best effort
    CHECK(r.fps() == 60);
}

TEST(effects_blur_and_foreground)
{
    Frame f;
    f.allocI420(160, 120);
    fillI420(f, [](int, int) { return 90; });
    ThreadPool pool(2);
    EffectsRenderer fx;
    EffectParams e;
    e.mode = EffectMode::BlurAll;
    fx.setParams(e);
    fx.apply(f, pool);
    CHECK(Y(f, 80, 60) == 90); // a flat image stays flat

    e = EffectParams();
    e.mode = EffectMode::Foreground;
    e.fill = BackgroundFill::Color;
    e.fillColor = 0xff000000; // black -> Y 16
    e.foreground = {0.25, 0.25, 0.5, 0.5};
    e.feather = 0.02;
    fx.setParams(e);
    fx.apply(f, pool);
    CHECK(Y(f, 80, 60) == 90);
    CHECK(Y(f, 2, 2) == 16);

    // Chroma key: a green frame is fully replaced.
    Frame g;
    g.allocI420(64, 64);
    uint8_t gy, gu, gv;
    argbToYuv(0xff00b140, gy, gu, gv);
    fillI420(g, [&](int, int) { return gy; }, gu, gv);
    e = EffectParams();
    e.mode = EffectMode::ChromaKey;
    e.fill = BackgroundFill::Color;
    e.fillColor = 0xffffffff;
    fx.setParams(e);
    fx.apply(g, pool);
    CHECK(Y(g, 10, 10) == 235);
}

TEST(animator_interpolates)
{
    FramingAnimator a;
    FramingParams p0, p1;
    p1.zoom = 4;
    p1.panX = 1;
    a.setTarget(p0, 0, 0);
    a.setTarget(p1, 1000, 0);
    FramingParams mid = a.current(500 * 1000000LL);
    CHECK_NEAR(mid.zoom, 2.0, 1e-6); // geometric midpoint
    CHECK_NEAR(mid.panX, 0.5, 1e-6);
    CHECK(a.animating());
    FramingParams end = a.current(2000 * 1000000LL);
    CHECK(end == p1);
    CHECK(!a.animating());
}

TEST(pack_yuyv)
{
    Frame f;
    f.allocI420(4, 2);
    fillI420(f, [](int x, int y) { return 20 + x + 10 * y; }, 50, 60);
    std::vector<uint8_t> out(4 * 2 * 2);
    packI420ToYuyv(f, out.data(), 8);
    const uint8_t expect[8] = {20, 50, 21, 60, 22, 50, 23, 60};
    CHECK(std::memcmp(out.data(), expect, 8) == 0);
    CHECK(out[8] == 30);
}

TEST(malformed_inputs_do_not_crash)
{
    Processor proc(2);
    Frame dst;
    dst.allocI420(64, 64);
    Frame empty;
    CHECK(!proc.process(empty, dst, FramingParams()));
    Frame src;
    src.allocI420(64, 64);
    renderTestPattern(src, 1);
    FramingParams p;
    p.zoom = std::nan("");
    p.rotation = std::numeric_limits<double>::infinity();
    p.cropLeft = 5;
    p.cropRight = 5;
    p.panX = 1e9;
    CHECK(proc.process(src, dst, p));
}

TEST(gaussian_blur_quality)
{
    const int w = 320, h = 200;
    std::vector<uint8_t> src(size_t(w) * h, 120), dst(size_t(w) * h);
    ThreadPool pool(2);
    GaussianBlur blur;
    // A flat image stays exactly flat at every strength (no blocks or ripples).
    for (double sigma : {1.0, 4.0, 12.0, 40.0}) {
        blur.blur(src.data(), w, dst.data(), w, w, h, sigma, &pool);
        bool flat = true;
        for (uint8_t v : dst)
            flat &= std::abs(int(v) - 120) <= 1;
        CHECK(flat);
    }
    // A step edge becomes a monotonic ramp: no overshoot, no streaks.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            src[size_t(y) * w + x] = x < w / 2 ? 40 : 200;
    blur.blur(src.data(), w, dst.data(), w, w, h, 10, &pool);
    bool monotonic = true;
    for (int y = 0; y < h; ++y)
        for (int x = 1; x < w; ++x)
            monotonic &= dst[size_t(y) * w + x] + 1 >= dst[size_t(y) * w + x - 1];
    CHECK(monotonic);
    CHECK(dst[size_t(h / 2) * w + w / 2] > 90 && dst[size_t(h / 2) * w + w / 2] < 150);
    CHECK(dst[size_t(h / 2) * w + 5] <= 41 && dst[size_t(h / 2) * w + w - 5] >= 199);

    // Masked blur: pixels with zero weight (the person) must not bleed into
    // the blurred background next to them.
    std::vector<uint8_t> weight(size_t(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            weight[size_t(y) * w + x] = x < w / 2 ? 0 : 255;
    std::fill(dst.begin(), dst.end(), 0);
    blur.blur(src.data(), w, dst.data(), w, w, h, 10, &pool, weight.data(), w);
    CHECK(std::abs(int(dst[size_t(h / 2) * w + w / 2 + 2]) - 200) <= 3);
}

TEST(person_segmenter_runs)
{
    PersonSegmenter seg;
    CHECK(seg.available());
    Frame f;
    f.allocI420(640, 360);
    renderTestPattern(f, 1);
    ThreadPool pool(2);
    std::vector<uint8_t> mask;
    CHECK(seg.compute(f, pool, 0.3, mask));
    CHECK(mask.size() == size_t(640) * 360);
    // Malformed input is rejected rather than crashing.
    Frame tiny;
    tiny.allocI420(8, 8);
    CHECK(!seg.compute(tiny, pool, 0.3, mask));

    EffectsRenderer fx;
    EffectParams e;
    e.mode = EffectMode::Person;
    fx.setParams(e);
    fx.apply(f, pool);
    CHECK(fx.personDetectionAvailable());
}

int main()
{
    for (auto &t : registry()) {
        int before = g_failures;
        t.second();
        std::printf("%-40s %s\n", t.first, g_failures == before ? "ok" : "FAILED");
    }
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
