// SPDX-License-Identifier: GPL-3.0-or-later
#include "Processor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cam {

namespace {

inline uint8_t clamp8(int v) { return uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v)); }

constexpr int kFracBits = 16;
constexpr int64_t kOne = int64_t(1) << kFracBits;

} // namespace

bool planeViews(const Frame &f, PlaneView v[3], bool &hasChroma)
{
    const int w = f.width, h = f.height;
    const int cw = (w + 1) / 2;
    hasChroma = true;
    switch (f.format) {
    case PixelFormat::I420:
    case PixelFormat::YV12: // capture code stores U in plane[1] and V in plane[2] for both
        v[0] = {f.plane[0], f.stride[0], 1, w, h};
        v[1] = {f.plane[1], f.stride[1], 1, cw, (h + 1) / 2};
        v[2] = {f.plane[2], f.stride[2], 1, cw, (h + 1) / 2};
        return f.plane[0] && f.plane[1] && f.plane[2];
    case PixelFormat::NV12:
        v[0] = {f.plane[0], f.stride[0], 1, w, h};
        v[1] = {f.plane[1], f.stride[1], 2, cw, (h + 1) / 2};
        v[2] = {f.plane[1] ? f.plane[1] + 1 : nullptr, f.stride[1], 2, cw, (h + 1) / 2};
        return f.plane[0] && f.plane[1];
    case PixelFormat::NV21:
        v[0] = {f.plane[0], f.stride[0], 1, w, h};
        v[1] = {f.plane[1] ? f.plane[1] + 1 : nullptr, f.stride[1], 2, cw, (h + 1) / 2};
        v[2] = {f.plane[1], f.stride[1], 2, cw, (h + 1) / 2};
        return f.plane[0] && f.plane[1];
    case PixelFormat::YUYV:
        v[0] = {f.plane[0], f.stride[0], 2, w, h};
        v[1] = {f.plane[0] + 1, f.stride[0], 4, w / 2, h};
        v[2] = {f.plane[0] + 3, f.stride[0], 4, w / 2, h};
        return f.plane[0] && w >= 2;
    case PixelFormat::YVYU:
        v[0] = {f.plane[0], f.stride[0], 2, w, h};
        v[1] = {f.plane[0] + 3, f.stride[0], 4, w / 2, h};
        v[2] = {f.plane[0] + 1, f.stride[0], 4, w / 2, h};
        return f.plane[0] && w >= 2;
    case PixelFormat::UYVY:
        v[0] = {f.plane[0] + 1, f.stride[0], 2, w, h};
        v[1] = {f.plane[0], f.stride[0], 4, w / 2, h};
        v[2] = {f.plane[0] + 2, f.stride[0], 4, w / 2, h};
        return f.plane[0] && w >= 2;
    case PixelFormat::Planar:
        for (int i = 0; i < 3; ++i)
            v[i] = {f.plane[i], f.stride[i], 1, f.planeWidth[i], f.planeHeight[i]};
        return f.plane[0] && f.plane[1] && f.plane[2] && f.planeWidth[1] > 0 && f.planeHeight[1] > 0;
    case PixelFormat::Grey:
        v[0] = {f.plane[0], f.stride[0], 1, w, h};
        hasChroma = false;
        return f.plane[0] != nullptr;
    case PixelFormat::MJPEG:
    case PixelFormat::Invalid:
        break;
    }
    return false;
}

void buildColorLuts(const ColorParams &c, bool inputFullRange, uint8_t yLut[256], uint8_t uLut[256],
                    uint8_t vLut[256])
{
    const double contrast = std::clamp(c.contrast, 0.0, 4.0);
    const double brightness = std::clamp(c.brightness, -1.0, 1.0);
    const double gamma = std::clamp(c.gamma, 0.1, 10.0);
    const double invGamma = 1.0 / gamma;
    const double sat = std::clamp(c.saturation, 0.0, 4.0);
    const double black = std::clamp(c.blackPoint, 0.0, 0.5);
    const double white = std::max(black + 0.05, std::clamp(c.whitePoint, 0.5, 1.0));
    const double exposureGain = std::exp2(std::clamp(c.exposure, -4.0, 4.0));
    // Bumps peaking at 1/3 (shadows) and 2/3 (highlights). The amplitude keeps the
    // curve monotonic at the slider ends: max slope of x(1-x)^2 * 27/4 is 27/4.
    const double shadowAmt = std::clamp(c.shadows, -1.0, 1.0) * 0.14;
    const double highlightAmt = std::clamp(c.highlights, -1.0, 1.0) * 0.14;

    for (int i = 0; i < 256; ++i) {
        double x = inputFullRange ? i / 255.0 : (i - 16) / 219.0;
        x = std::clamp((x - black) / (white - black), 0.0, 1.0);
        if (exposureGain != 1.0)
            x = std::pow(std::min(1.0, std::pow(x, 2.2) * exposureGain), 1.0 / 2.2);
        x += shadowAmt * 6.75 * x * (1 - x) * (1 - x) + highlightAmt * 6.75 * x * x * (1 - x);
        x = (x - 0.5) * contrast + 0.5 + brightness;
        x = std::clamp(x, 0.0, 1.0);
        if (gamma != 1.0)
            x = std::pow(x, invGamma);
        yLut[i] = clamp8(int(std::lround(16.0 + 219.0 * x)));
    }

    // Warmth pushes Cr up and Cb down (amber); tint pushes both down (green) or up (magenta).
    const double warm = std::clamp(c.warmth, -1.0, 1.0) * 24.0;
    const double tint = std::clamp(c.tint, -1.0, 1.0) * 16.0;
    const double shiftU = -warm + tint;
    const double shiftV = warm + tint;
    for (int i = 0; i < 256; ++i) {
        double cc = inputFullRange ? (i - 128) / 127.5 : (i - 128) / 112.0;
        double u = cc * sat * 112.0 + 128.0 + shiftU;
        double v = cc * sat * 112.0 + 128.0 + shiftV;
        uLut[i] = clamp8(int(std::lround(std::clamp(u, 16.0, 240.0))));
        vLut[i] = clamp8(int(std::lround(std::clamp(v, 16.0, 240.0))));
    }
}

bool buildChromaMap(const ColorParams &c, uint16_t *map)
{
    if (!c.hasChromaMap())
        return false;
    const double vib = std::clamp(c.vibrance, -1.0, 1.0);
    constexpr double kPi = 3.14159265358979323846;
    const double rot = std::clamp(c.hue, -180.0, 180.0) * kPi / 180.0;
    const double cr = std::cos(rot), sr = std::sin(rot);
    // Typical skin sits around this angle in the Cb/Cr plane (Cb low, Cr high).
    constexpr double kSkinAngle = 2.25, kSkinWidth = 0.45;
    for (int u = 0; u < 256; ++u) {
        for (int v = 0; v < 256; ++v) {
            double cu = (u - 128) / 112.0, cv = (v - 128) / 112.0;
            if (vib != 0.0) {
                const double m = std::hypot(cu, cv);
                const double t = std::min(1.0, m / 0.7); // 0 = grey, 1 = strongly colored
                double gain;
                if (vib > 0) {
                    double d = std::atan2(cv, cu) - kSkinAngle;
                    d = std::remainder(d, 2 * kPi);
                    const double skin = std::exp(-(d * d) / (kSkinWidth * kSkinWidth));
                    gain = 1.0 + vib * 1.5 * (1 - t) * (1 - t) * (1 - 0.6 * skin);
                } else {
                    gain = 1.0 + vib * (0.5 + 0.5 * t); // strong colors fade first
                }
                cu *= gain;
                cv *= gain;
            }
            if (rot != 0.0) {
                const double ru = cu * cr - cv * sr;
                cv = cu * sr + cv * cr;
                cu = ru;
            }
            const int ou = clamp8(int(std::lround(std::clamp(128.0 + cu * 112.0, 16.0, 240.0))));
            const int ov = clamp8(int(std::lround(std::clamp(128.0 + cv * 112.0, 16.0, 240.0))));
            map[(u << 8) | v] = uint16_t((ou << 8) | ov);
        }
    }
    return true;
}

void packI420ToYuyv(const Frame &src, uint8_t *dst, int dstStride)
{
    const int w = src.width, h = src.height;
    for (int y = 0; y < h; ++y) {
        const uint8_t *yp = src.plane[0] + size_t(y) * src.stride[0];
        const uint8_t *up = src.plane[1] + size_t(y / 2) * src.stride[1];
        const uint8_t *vp = src.plane[2] + size_t(y / 2) * src.stride[2];
        uint8_t *d = dst + size_t(y) * dstStride;
        for (int x = 0; x < w / 2; ++x) {
            d[4 * x + 0] = yp[2 * x];
            d[4 * x + 1] = up[x];
            d[4 * x + 2] = yp[2 * x + 1];
            d[4 * x + 3] = vp[x];
        }
    }
}

void renderTestPattern(Frame &dst, uint64_t frameIndex)
{
    const int w = dst.width, h = dst.height;
    // 75% color bars in limited-range BT.601: white, yellow, cyan, green, magenta, red, blue, black.
    static const uint8_t bars[8][3] = {{180, 128, 128}, {162, 44, 142}, {131, 156, 44}, {112, 72, 58},
                                       {84, 184, 198},  {65, 100, 212}, {35, 212, 114}, {16, 128, 128}};
    const int barH = h * 2 / 3;
    for (int y = 0; y < h; ++y) {
        uint8_t *yp = dst.plane[0] + size_t(y) * dst.stride[0];
        for (int x = 0; x < w; ++x) {
            if (y < barH)
                yp[x] = bars[std::min(7, x * 8 / w)][0];
            else
                yp[x] = uint8_t(16 + (x * 219) / std::max(1, w - 1)); // luma ramp
        }
    }
    for (int y = 0; y < h / 2; ++y) {
        uint8_t *up = dst.plane[1] + size_t(y) * dst.stride[1];
        uint8_t *vp = dst.plane[2] + size_t(y) * dst.stride[2];
        for (int x = 0; x < w / 2; ++x) {
            if (y * 2 < barH) {
                int b = std::min(7, (x * 2) * 8 / w);
                up[x] = bars[b][1];
                vp[x] = bars[b][2];
            } else {
                up[x] = vp[x] = 128;
            }
        }
    }
    // A moving square makes dropped or repeated frames easy to spot.
    const int sq = std::max(8, h / 8) & ~1;
    const int range = std::max(1, w - sq);
    int pos = int((frameIndex * 8) % uint64_t(2 * range));
    if (pos > range)
        pos = 2 * range - pos;
    const int top = (barH - sq) / 2 & ~1;
    for (int y = top; y < top + sq && y < h; ++y)
        std::memset(dst.plane[0] + size_t(y) * dst.stride[0] + (pos & ~1), 235, size_t(sq));
    for (int y = top / 2; y < (top + sq) / 2 && y < h / 2; ++y) {
        std::memset(dst.plane[1] + size_t(y) * dst.stride[1] + pos / 2, 128, size_t(sq / 2));
        std::memset(dst.plane[2] + size_t(y) * dst.stride[2] + pos / 2, 128, size_t(sq / 2));
    }
}

Processor::Processor(int threads) : m_pool(threads) {}

void Processor::setColor(const ColorParams &c)
{
    if (m_lutValid && c == m_color)
        return;
    m_color = c;
    m_lutValid = false;
}

void Processor::setEffects(const EffectParams &e) { m_effects.setParams(e); }

void Processor::fillPlane(uint8_t *dst, int stride, int w, int h, uint8_t value)
{
    for (int y = 0; y < h; ++y)
        std::memset(dst + size_t(y) * stride, value, size_t(w));
}

// Bilinear resample of one plane through an affine map given in plane index
// space: source position = (ax*i + bx*j + cx, ay*i + by*j + cy). Positions
// outside the source by more than half a pixel produce `border`.
void Processor::samplePlane(const PlaneView &src, uint8_t *dst, int dstStride, int dstW, int dstH,
                            double ax, double bx, double cx, double ay, double by, double cy,
                            const uint8_t *lut, uint8_t border)
{
    if (src.width <= 0 || src.height <= 0) {
        fillPlane(dst, dstStride, dstW, dstH, border);
        return;
    }
    const int sw = src.width, sh = src.height;
    const int step = src.step;
    const int64_t loX = -kOne / 2, hiX = int64_t(sw) * kOne - kOne / 2;
    const int64_t loY = -kOne / 2, hiY = int64_t(sh) * kOne - kOne / 2;
    // Tolerance for rounding at exact edges (e.g. fit mode).
    const int64_t tol = kOne / 64;

    const bool axisAligned = std::fabs(bx) < 1e-12 && std::fabs(ay) < 1e-12;

    if (axisAligned) {
        // Separable: precompute per-column sample offsets and weights once.
        m_colOff0.resize(size_t(dstW));
        m_colOff1.resize(size_t(dstW));
        m_colFrac.resize(size_t(dstW));
        m_colInside.resize(size_t(dstW));
        bool allIntegerX = true;
        for (int i = 0; i < dstW; ++i) {
            int64_t px = int64_t(std::llround((ax * i + cx) * kOne));
            bool inside = px >= loX - tol && px <= hiX + tol;
            int x0 = int(px >> kFracBits);
            int frac = int((px & (kOne - 1)) >> 8);
            int x1 = x0 + 1;
            if (x0 < 0) {
                x0 = x1 = 0;
                frac = 0;
            } else if (x0 >= sw - 1) {
                x0 = x1 = sw - 1;
                frac = 0;
            }
            m_colOff0[size_t(i)] = x0 * step;
            m_colOff1[size_t(i)] = x1 * step;
            m_colFrac[size_t(i)] = int16_t(frac);
            m_colInside[size_t(i)] = inside;
            if (frac != 0 || !inside)
                allIntegerX = false;
        }
        const int32_t *off0 = m_colOff0.data();
        const int32_t *off1 = m_colOff1.data();
        const int16_t *fracs = m_colFrac.data();
        const uint8_t *insideCol = m_colInside.data();

        auto rows = [&](int jb, int je) {
            for (int j = jb; j < je; ++j) {
                uint8_t *out = dst + size_t(j) * dstStride;
                int64_t py = int64_t(std::llround((by * j + cy) * kOne));
                if (py < loY - tol || py > hiY + tol) {
                    std::memset(out, border, size_t(dstW));
                    continue;
                }
                int y0 = int(py >> kFracBits);
                int fy = int((py & (kOne - 1)) >> 8);
                int y1 = y0 + 1;
                if (y0 < 0) {
                    y0 = y1 = 0;
                    fy = 0;
                } else if (y0 >= sh - 1) {
                    y0 = y1 = sh - 1;
                    fy = 0;
                }
                const uint8_t *r0 = src.data + size_t(y0) * src.stride;
                const uint8_t *r1 = src.data + size_t(y1) * src.stride;
                if (fy == 0 && allIntegerX) {
                    // 1:1 copy (with color LUT), the common "no zoom" case.
                    for (int i = 0; i < dstW; ++i)
                        out[i] = lut[r0[off0[i]]];
                    continue;
                }
                const int wy1 = fy, wy0 = 256 - fy;
                for (int i = 0; i < dstW; ++i) {
                    if (!insideCol[i]) {
                        out[i] = border;
                        continue;
                    }
                    const int fx = fracs[i];
                    const int a = r0[off0[i]] * (256 - fx) + r0[off1[i]] * fx;
                    const int b = r1[off0[i]] * (256 - fx) + r1[off1[i]] * fx;
                    out[i] = lut[(a * wy0 + b * wy1 + 32768) >> 16];
                }
            }
        };
        m_pool.parallelFor(dstH, rows, 16);
        return;
    }

    // General affine (rotation) path, incremental fixed point.
    const int64_t dxi = int64_t(std::llround(ax * kOne));
    const int64_t dyi = int64_t(std::llround(ay * kOne));
    auto rows = [&](int jb, int je) {
        for (int j = jb; j < je; ++j) {
            uint8_t *out = dst + size_t(j) * dstStride;
            int64_t px = int64_t(std::llround((bx * j + cx) * kOne));
            int64_t py = int64_t(std::llround((by * j + cy) * kOne));
            for (int i = 0; i < dstW; ++i, px += dxi, py += dyi) {
                if (px < loX - tol || px > hiX + tol || py < loY - tol || py > hiY + tol) {
                    out[i] = border;
                    continue;
                }
                int x0 = int(px >> kFracBits), y0 = int(py >> kFracBits);
                int fx = int((px & (kOne - 1)) >> 8), fy = int((py & (kOne - 1)) >> 8);
                int x1 = x0 + 1, y1 = y0 + 1;
                if (x0 < 0) {
                    x0 = x1 = 0;
                    fx = 0;
                } else if (x0 >= sw - 1) {
                    x0 = x1 = sw - 1;
                    fx = 0;
                }
                if (y0 < 0) {
                    y0 = y1 = 0;
                    fy = 0;
                } else if (y0 >= sh - 1) {
                    y0 = y1 = sh - 1;
                    fy = 0;
                }
                const uint8_t *r0 = src.data + size_t(y0) * src.stride;
                const uint8_t *r1 = src.data + size_t(y1) * src.stride;
                const int a = r0[x0 * step] * (256 - fx) + r0[x1 * step] * fx;
                const int b = r1[x0 * step] * (256 - fx) + r1[x1 * step] * fx;
                out[i] = lut[(a * (256 - fy) + b * fy + 32768) >> 16];
            }
        }
    };
    m_pool.parallelFor(dstH, rows, 8);
}

bool Processor::process(const Frame &src, Frame &dst, const FramingParams &framing)
{
    PlaneView views[3];
    bool hasChroma = false;
    if (!planeViews(src, views, hasChroma) || dst.format != PixelFormat::I420 || dst.width <= 0)
        return false;

    if (!m_lutValid || m_lutFullRange != src.fullRange) {
        buildColorLuts(m_color, src.fullRange, m_yLut, m_uLut, m_vLut);
        m_chromaMap.resize(65536);
        m_chromaMapActive = buildChromaMap(m_color, m_chromaMap.data());
        m_lutFullRange = src.fullRange;
        m_lutValid = true;
    }

    const Affine m = computeFramingTransform(src.width, src.height, dst.width, dst.height, framing);
    const bool doSharpen = m_color.sharpness > 0.01;

    uint8_t *yOut = dst.plane[0];
    int yStride = dst.stride[0];
    if (doSharpen) {
        m_sharpenTmp.resize(size_t(dst.width) * dst.height);
        yOut = m_sharpenTmp.data();
        yStride = dst.width;
    }

    for (int p = 0; p < 3; ++p) {
        // Output plane geometry (I420).
        const int ssx = p == 0 ? 1 : 2, ssy = p == 0 ? 1 : 2;
        const int dw = p == 0 ? dst.width : dst.width / 2;
        const int dh = p == 0 ? dst.height : dst.height / 2;
        uint8_t *out = p == 0 ? yOut : dst.plane[p];
        const int outStride = p == 0 ? yStride : dst.stride[p];
        const uint8_t *lut = p == 0 ? m_yLut : (p == 1 ? m_uLut : m_vLut);
        const uint8_t border = p == 0 ? 16 : 128;

        if (p > 0 && !hasChroma) {
            fillPlane(out, outStride, dw, dh, lut[128]);
            continue;
        }
        const PlaneView &sv = views[p];
        // Source plane scale relative to luma.
        const double kx = double(sv.width) / src.width;
        const double ky = double(sv.height) / src.height;
        // Plane pixel i -> luma coordinate (i + 0.5) * ss -> source luma -> source plane index.
        const double ax = m.a * ssx * kx, bx = m.b * ssy * kx;
        const double cx = (m.a * 0.5 * ssx + m.b * 0.5 * ssy + m.c) * kx - 0.5;
        const double ay = m.d * ssx * ky, by = m.e * ssy * ky;
        const double cy = (m.d * 0.5 * ssx + m.e * 0.5 * ssy + m.f) * ky - 0.5;
        samplePlane(sv, out, outStride, dw, dh, ax, bx, cx, ay, by, cy, lut, border);
    }

    if (m_chromaMapActive && hasChroma)
        applyChromaMap(dst);
    if (doSharpen)
        sharpen(dst);

    m_effects.apply(dst, m_pool);
    return true;
}

void Processor::applyChromaMap(Frame &dst)
{
    const int cw = dst.width / 2, ch = dst.height / 2;
    const uint16_t *map = m_chromaMap.data();
    auto rows = [&](int jb, int je) {
        for (int j = jb; j < je; ++j) {
            uint8_t *u = dst.plane[1] + size_t(j) * dst.stride[1];
            uint8_t *v = dst.plane[2] + size_t(j) * dst.stride[2];
            for (int i = 0; i < cw; ++i) {
                const uint16_t m = map[(u[i] << 8) | v[i]];
                u[i] = uint8_t(m >> 8);
                v[i] = uint8_t(m);
            }
        }
    };
    m_pool.parallelFor(ch, rows, 16);
}

// Unsharp mask on luma: out = y + amount * (y - blur3x3(y)).
void Processor::sharpen(Frame &dst)
{
    const int w = dst.width, h = dst.height;
    const uint8_t *src = m_sharpenTmp.data();
    const int amount = int(std::lround(std::clamp(m_color.sharpness, 0.0, 4.0) * 256));
    auto rows = [&](int jb, int je) {
        for (int j = jb; j < je; ++j) {
            const uint8_t *rm = src + size_t(std::max(0, j - 1)) * w;
            const uint8_t *r0 = src + size_t(j) * w;
            const uint8_t *rp = src + size_t(std::min(h - 1, j + 1)) * w;
            uint8_t *out = dst.plane[0] + size_t(j) * dst.stride[0];
            out[0] = r0[0];
            out[w - 1] = r0[w - 1];
            for (int i = 1; i < w - 1; ++i) {
                int blur = (rm[i - 1] + 2 * rm[i] + rm[i + 1] + 2 * r0[i - 1] + 4 * r0[i] +
                            2 * r0[i + 1] + rp[i - 1] + 2 * rp[i] + rp[i + 1] + 8) >>
                           4;
                int v = r0[i] + (((r0[i] - blur) * amount) >> 8);
                out[i] = clamp8(v);
            }
        }
    };
    m_pool.parallelFor(h, rows, 16);
}

} // namespace cam
