// SPDX-License-Identifier: GPL-3.0-or-later
#include "Blur.h"

#include "ThreadPool.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cam {

namespace {

// Radii of n box filters whose combination approximates a Gaussian of sigma.
void boxRadiiForGauss(double sigma, int n, int *radii)
{
    const double wIdeal = std::sqrt(12.0 * sigma * sigma / n + 1.0);
    int wl = int(std::floor(wIdeal));
    if (wl % 2 == 0)
        --wl;
    const int wu = wl + 2;
    const double mIdeal = (12.0 * sigma * sigma - n * wl * wl - 4.0 * n * wl - 3.0 * n) / (-4.0 * wl - 4.0);
    const int m = int(std::lround(mIdeal));
    for (int i = 0; i < n; ++i)
        radii[i] = std::max(0, ((i < m ? wl : wu) - 1) / 2);
}

// Horizontal box blur of every row, edge pixels replicated.
void boxRows(float *buf, int w, int h, int r, std::vector<float> &line)
{
    if (r <= 0)
        return;
    line.resize(size_t(w));
    const float inv = 1.0f / float(2 * r + 1);
    for (int y = 0; y < h; ++y) {
        float *row = buf + size_t(y) * w;
        std::memcpy(line.data(), row, size_t(w) * sizeof(float));
        float sum = 0;
        for (int k = -r; k <= r; ++k)
            sum += line[size_t(std::clamp(k, 0, w - 1))];
        for (int x = 0; x < w; ++x) {
            row[x] = sum * inv;
            sum += line[size_t(std::min(w - 1, x + r + 1))] - line[size_t(std::max(0, x - r))];
        }
    }
}

// Vertical box blur, processed row-wise so the inner loop is contiguous.
void boxCols(float *buf, int w, int h, int r, std::vector<float> &copy, std::vector<float> &acc)
{
    if (r <= 0)
        return;
    copy.assign(buf, buf + size_t(w) * h);
    acc.assign(size_t(w), 0.0f);
    const float inv = 1.0f / float(2 * r + 1);
    auto row = [&](int y) { return copy.data() + size_t(std::clamp(y, 0, h - 1)) * w; };
    for (int k = -r; k <= r; ++k) {
        const float *s = row(k);
        for (int x = 0; x < w; ++x)
            acc[size_t(x)] += s[x];
    }
    for (int y = 0; y < h; ++y) {
        float *out = buf + size_t(y) * w;
        const float *add = row(y + r + 1), *sub = row(y - r);
        for (int x = 0; x < w; ++x) {
            out[x] = acc[size_t(x)] * inv;
            acc[size_t(x)] += add[x] - sub[x];
        }
    }
}

void gaussLowRes(float *buf, int w, int h, double sigma, std::vector<float> &a, std::vector<float> &b)
{
    if (sigma < 0.3)
        return;
    int radii[3];
    boxRadiiForGauss(sigma, 3, radii);
    for (int r : radii) {
        boxRows(buf, w, h, r, a);
        boxCols(buf, w, h, r, a, b);
    }
}

// Cubic B-spline weights for fractional position t in [0, 1).
inline void bspline(float t, float *wt)
{
    const float t2 = t * t, t3 = t2 * t;
    wt[0] = (1 - t) * (1 - t) * (1 - t) / 6.0f;
    wt[1] = (3 * t3 - 6 * t2 + 4) / 6.0f;
    wt[2] = (-3 * t3 + 3 * t2 + 3 * t + 1) / 6.0f;
    wt[3] = t3 / 6.0f;
}

inline uint8_t toByte(float v)
{
    v = v * 255.0f + 0.5f;
    return uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v));
}

} // namespace

void GaussianBlur::blur(const uint8_t *src, int srcStride, uint8_t *dst, int dstStride, int w, int h,
                        double sigma, ThreadPool *pool, const uint8_t *weight, int weightStride)
{
    if (w <= 0 || h <= 0)
        return;
    if (!(sigma >= 0.5)) {
        if (src != dst)
            for (int y = 0; y < h; ++y)
                std::memcpy(dst + size_t(y) * dstStride, src + size_t(y) * srcStride, size_t(w));
        return;
    }
    // Work at 1/f resolution so the low-resolution sigma stays around 1.5-3 px.
    int f = 1;
    while (f < 32 && f * 2 <= sigma / 1.5)
        f *= 2;
    const int sw = (w + f - 1) / f, sh = (h + f - 1) / f;
    const bool weighted = weight != nullptr;
    const size_t lowSize = size_t(sw) * sh;
    // Planes: U = plain average, A = weighted sum, W = weight average.
    m_lowA.resize(lowSize * (weighted ? 3 : 1));
    float *lowU = m_lowA.data();
    float *lowA = weighted ? lowU + lowSize : nullptr;
    float *lowW = weighted ? lowU + 2 * lowSize : nullptr;

    // Box downsample (reads the whole source before anything is written, so
    // src == dst is fine).
    auto down = [&](int yb, int ye) {
        std::vector<float> su(static_cast<size_t>(sw)), sa, sww;
        if (weighted) {
            sa.resize(size_t(sw));
            sww.resize(size_t(sw));
        }
        for (int sy = yb; sy < ye; ++sy) {
            std::fill(su.begin(), su.end(), 0.0f);
            if (weighted) {
                std::fill(sa.begin(), sa.end(), 0.0f);
                std::fill(sww.begin(), sww.end(), 0.0f);
            }
            const int y0 = sy * f, y1 = std::min(h, y0 + f);
            for (int y = y0; y < y1; ++y) {
                const uint8_t *row = src + size_t(y) * srcStride;
                if (!weighted) {
                    for (int sx = 0, x = 0; sx < sw; ++sx) {
                        const int xe = std::min(w, x + f);
                        unsigned s = 0;
                        for (; x < xe; ++x)
                            s += row[x];
                        su[size_t(sx)] += float(s);
                    }
                    continue;
                }
                const uint8_t *wrow = weight + size_t(y) * weightStride;
                for (int sx = 0, x = 0; sx < sw; ++sx) {
                    const int xe = std::min(w, x + f);
                    unsigned s = 0, sAw = 0, sW = 0;
                    for (; x < xe; ++x) {
                        s += row[x];
                        sAw += unsigned(row[x]) * wrow[x];
                        sW += wrow[x];
                    }
                    su[size_t(sx)] += float(s);
                    sa[size_t(sx)] += float(sAw);
                    sww[size_t(sx)] += float(sW);
                }
            }
            for (int sx = 0; sx < sw; ++sx) {
                const int n = (std::min(w, (sx + 1) * f) - sx * f) * (y1 - y0);
                const float inv = 1.0f / (255.0f * n);
                lowU[size_t(sy) * sw + sx] = su[size_t(sx)] * inv;
                if (weighted) {
                    lowA[size_t(sy) * sw + sx] = sa[size_t(sx)] * inv / 255.0f;
                    lowW[size_t(sy) * sw + sx] = sww[size_t(sx)] * inv;
                }
            }
        }
    };
    if (pool)
        pool->parallelFor(sh, down, 4);
    else
        down(0, sh);

    // Gaussian at low resolution. Box averaging already contributed
    // (f^2-1)/12 of variance and the B-spline upsampler adds about 1/3 px^2.
    double var = sigma * sigma - (double(f) * f - 1.0) / 12.0;
    double sl = std::sqrt(std::max(var, 0.25)) / f;
    if (f > 1)
        sl = std::sqrt(std::max(sl * sl - 1.0 / 3.0, 0.1));
    std::vector<float> t1, t2;
    gaussLowRes(lowU, sw, sh, sl, t1, t2);
    if (weighted) {
        gaussLowRes(lowA, sw, sh, sl, t1, t2);
        gaussLowRes(lowW, sw, sh, sl, t1, t2);
    }

    // Masked blur: normalise at low resolution. Where almost nothing
    // contributes (deep inside the masked area) blend smoothly towards the
    // plain blur instead of dividing by ~0. Upsampling the ratio instead of
    // its three parts is visually identical for a blur and 3x cheaper.
    if (weighted) {
        const float eps = 0.03f;
        for (size_t i = 0; i < lowSize; ++i)
            lowU[i] = (lowA[i] + lowU[i] * eps) / (std::max(lowW[i], 0.0f) + eps);
    }

    if (f == 1) {
        for (int y = 0; y < h; ++y) {
            uint8_t *out = dst + size_t(y) * dstStride;
            const float *in = lowU + size_t(y) * sw;
            const uint8_t *wrow = weighted ? weight + size_t(y) * weightStride : nullptr;
            for (int x = 0; x < w; ++x)
                if (!wrow || wrow[x])
                    out[x] = toByte(in[x]);
        }
        return;
    }

    // Separable cubic B-spline upsampling: rows first (only sh rows), then columns.
    m_cx.resize(size_t(w) * 4);
    m_cw.resize(size_t(w) * 4);
    for (int x = 0; x < w; ++x) {
        const float s = (x + 0.5f) / f - 0.5f;
        const int i = int(std::floor(s));
        bspline(s - i, &m_cw[size_t(x) * 4]);
        for (int k = 0; k < 4; ++k)
            m_cx[size_t(x) * 4 + k] = std::clamp(i - 1 + k, 0, sw - 1);
    }
    const int planes = 1;
    m_rowsA.resize(size_t(sh) * w * planes);
    auto horiz = [&](int yb, int ye) {
        for (int p = 0; p < planes; ++p) {
            const float *lowP = lowU + size_t(p) * lowSize;
            float *rowsP = m_rowsA.data() + size_t(p) * sh * w;
            for (int sy = yb; sy < ye; ++sy) {
                const float *in = lowP + size_t(sy) * sw;
                float *out = rowsP + size_t(sy) * w;
                for (int x = 0; x < w; ++x) {
                    const int32_t *ix = &m_cx[size_t(x) * 4];
                    const float *wx = &m_cw[size_t(x) * 4];
                    out[x] = in[ix[0]] * wx[0] + in[ix[1]] * wx[1] + in[ix[2]] * wx[2] + in[ix[3]] * wx[3];
                }
            }
        }
    };
    if (pool)
        pool->parallelFor(sh, horiz, 4);
    else
        horiz(0, sh);

    const float *rowsU = m_rowsA.data();
    auto vert = [&](int yb, int ye) {
        for (int y = yb; y < ye; ++y) {
            const float s = (y + 0.5f) / f - 0.5f;
            const int i = int(std::floor(s));
            float wy[4];
            bspline(s - i, wy);
            const float *r0 = rowsU + size_t(std::clamp(i - 1, 0, sh - 1)) * w;
            const float *r1 = rowsU + size_t(std::clamp(i, 0, sh - 1)) * w;
            const float *r2 = rowsU + size_t(std::clamp(i + 1, 0, sh - 1)) * w;
            const float *r3 = rowsU + size_t(std::clamp(i + 2, 0, sh - 1)) * w;
            uint8_t *out = dst + size_t(y) * dstStride;
            const int width = w; // local trip count keeps the loop vectorizable
            const float w0 = wy[0], w1 = wy[1], w2 = wy[2], w3 = wy[3];
            if (!weighted) {
                for (int x = 0; x < width; ++x)
                    out[x] = toByte(r0[x] * w0 + r1[x] * w1 + r2[x] * w2 + r3[x] * w3);
            } else {
                // Zero weight = fully foreground: the composite never shows the
                // background there, so it is left as is.
                const uint8_t *wrow = weight + size_t(y) * weightStride;
                for (int x = 0; x < width; ++x) {
                    const uint8_t v = toByte(r0[x] * w0 + r1[x] * w1 + r2[x] * w2 + r3[x] * w3);
                    out[x] = wrow[x] ? v : out[x];
                }
            }
        }
    };
    if (pool)
        pool->parallelFor(h, vert, 16);
    else
        vert(0, h);
}

} // namespace cam
