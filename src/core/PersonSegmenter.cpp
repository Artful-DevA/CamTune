// SPDX-License-Identifier: GPL-3.0-or-later
#include "PersonSegmenter.h"

#include "Clock.h"
#include "ThreadPool.h"

#include <algorithm>
#include <cmath>

namespace cam {

// Generated at build time from resources/models/selfie_segmentation_landscape.camnn.
extern const unsigned char kSelfieModel[];
extern const size_t kSelfieModelSize;

namespace {

// Box mean with radius r; near the edges only the available pixels count.
void boxMean(const float *src, float *dst, int w, int h, int r, std::vector<float> &tmp)
{
    tmp.resize(size_t(w) * h + size_t(w));
    float *rows = tmp.data();
    float *acc = tmp.data() + size_t(w) * h;
    for (int y = 0; y < h; ++y) {
        const float *s = src + size_t(y) * w;
        float *t = rows + size_t(y) * w;
        float sum = 0;
        for (int x = 0; x <= std::min(r, w - 1); ++x)
            sum += s[x];
        for (int x = 0; x < w; ++x) {
            const int n = std::min(w - 1, x + r) - std::max(0, x - r) + 1;
            t[x] = sum / float(n);
            if (x + r + 1 < w)
                sum += s[x + r + 1];
            if (x - r >= 0)
                sum -= s[x - r];
        }
    }
    // Vertical pass row by row so memory is read sequentially.
    std::fill(acc, acc + w, 0.0f);
    for (int y = 0; y <= std::min(r, h - 1); ++y)
        for (int x = 0; x < w; ++x)
            acc[x] += rows[size_t(y) * w + x];
    for (int y = 0; y < h; ++y) {
        const float inv = 1.0f / float(std::min(h - 1, y + r) - std::max(0, y - r) + 1);
        float *d = dst + size_t(y) * w;
        for (int x = 0; x < w; ++x)
            d[x] = acc[x] * inv;
        if (y + r + 1 < h) {
            const float *add = rows + size_t(y + r + 1) * w;
            for (int x = 0; x < w; ++x)
                acc[x] += add[x];
        }
        if (y - r >= 0) {
            const float *sub = rows + size_t(y - r) * w;
            for (int x = 0; x < w; ++x)
                acc[x] -= sub[x];
        }
    }
}

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

} // namespace

bool PersonSegmenter::available()
{
    if (!m_tried) {
        m_tried = true;
        m_ok = m_net.load(kSelfieModel, kSelfieModelSize, m_error) && m_net.inputChannels() == 3 &&
               m_net.outputWidth() == m_net.inputWidth() && m_net.outputHeight() == m_net.inputHeight();
        if (!m_ok && m_error.empty())
            m_error = "unexpected model shape";
        m_nw = m_net.inputWidth();
        m_nh = m_net.inputHeight();
    }
    return m_ok;
}

// Area-averages the frame down to the network input (RGB 0..1) and keeps the
// matching luma as the guide image for edge refinement.
void PersonSegmenter::prepareInput(const Frame &f, ThreadPool &pool)
{
    const int W = f.width, H = f.height, nw = m_nw, nh = m_nh;
    float *in = m_net.input();
    m_guide.resize(size_t(nw) * nh);
    pool.parallelFor(nh, [&](int tb, int te) {
        for (int ty = tb; ty < te; ++ty) {
            const int y0 = ty * H / nh, y1 = std::max(y0 + 1, (ty + 1) * H / nh);
            for (int tx = 0; tx < nw; ++tx) {
                const int x0 = tx * W / nw, x1 = std::max(x0 + 1, (tx + 1) * W / nw);
                unsigned sy = 0;
                for (int y = y0; y < y1; ++y) {
                    const uint8_t *row = f.plane[0] + size_t(y) * f.stride[0];
                    for (int x = x0; x < x1; ++x)
                        sy += row[x];
                }
                unsigned su = 0, sv = 0, nc = 0;
                for (int y = y0 / 2; y <= (y1 - 1) / 2; ++y) {
                    const uint8_t *ur = f.plane[1] + size_t(y) * f.stride[1];
                    const uint8_t *vr = f.plane[2] + size_t(y) * f.stride[2];
                    for (int x = x0 / 2; x <= (x1 - 1) / 2; ++x, ++nc) {
                        su += ur[x];
                        sv += vr[x];
                    }
                }
                const float Y = float(sy) / float((y1 - y0) * (x1 - x0));
                const float U = float(su) / float(nc) - 128.0f, V = float(sv) / float(nc) - 128.0f;
                const float yl = 1.164f * (Y - 16.0f);
                float *px = in + (size_t(ty) * nw + tx) * 3;
                px[0] = clamp01((yl + 1.596f * V) / 255.0f);
                px[1] = clamp01((yl - 0.392f * U - 0.813f * V) / 255.0f);
                px[2] = clamp01((yl + 2.017f * U) / 255.0f);
                m_guide[size_t(ty) * nw + tx] = clamp01((Y - 16.0f) / 219.0f);
            }
        }
    }, 8);
}

// Fast guided filter (He & Sun) coefficients at low resolution:
// q = a * I + b, with a, b box-averaged.
void PersonSegmenter::guidedCoefficients()
{
    const int w = m_nw, h = m_nh, r = 2;
    const float eps = 0.004f;
    const size_t n = size_t(w) * h;
    m_t1.resize(n); // mean I
    m_t2.resize(n); // mean p
    m_t3.resize(n); // mean I*I, then mean I*p
    m_t4.resize(n);
    m_a.resize(n);
    m_b.resize(n);
    boxMean(m_guide.data(), m_t1.data(), w, h, r, m_line);
    boxMean(m_prob.data(), m_t2.data(), w, h, r, m_line);
    for (size_t i = 0; i < n; ++i)
        m_a[i] = m_guide[i] * m_guide[i];
    boxMean(m_a.data(), m_t3.data(), w, h, r, m_line);
    for (size_t i = 0; i < n; ++i)
        m_b[i] = m_guide[i] * m_prob[i];
    boxMean(m_b.data(), m_t4.data(), w, h, r, m_line);
    for (size_t i = 0; i < n; ++i) {
        const float varI = m_t3[i] - m_t1[i] * m_t1[i];
        const float covIp = m_t4[i] - m_t1[i] * m_t2[i];
        m_a[i] = covIp / (varI + eps);
        m_b[i] = m_t2[i] - m_a[i] * m_t1[i];
    }
    boxMean(m_a.data(), m_t3.data(), w, h, r, m_line);
    boxMean(m_b.data(), m_t4.data(), w, h, r, m_line);
    m_a.swap(m_t3);
    m_b.swap(m_t4);
}

bool PersonSegmenter::compute(const Frame &f, ThreadPool &pool, double softness, std::vector<uint8_t> &mask)
{
    if (!available() || f.format != PixelFormat::I420 || f.width < 16 || f.height < 16)
        return false;
    prepareInput(f, pool);
    const size_t n = size_t(m_nw) * m_nh;
    // The network runs at most ~20 times a second; in between, the previous
    // result is reused but still refined against the current frame below, so
    // the outline stays locked to the picture.
    const int64_t now = monotonicNs();
    if (m_prev.size() != n || now - m_lastRunNs >= 45000000) {
        m_lastRunNs = now;
        m_net.run(&pool);
        const float *out = m_net.output();
        m_prob.assign(out, out + n);
        // Light temporal smoothing against flicker; large changes (movement)
        // follow almost immediately so the mask does not trail behind.
        if (m_prev.size() == n) {
            for (size_t i = 0; i < n; ++i) {
                const float d = std::fabs(m_prob[i] - m_prev[i]);
                const float keep = d > 0.4f ? 0.1f : 0.4f;
                m_prob[i] = m_prob[i] * (1.0f - keep) + m_prev[i] * keep;
            }
        }
        m_prev = m_prob;
    } else {
        m_prob = m_prev;
    }
    guidedCoefficients();

    // Upsample a, b to full resolution and apply them to the full-resolution
    // luma, then shape the edge.
    const int W = f.width, H = f.height, nw = m_nw, nh = m_nh;
    const float s = float(std::clamp(0.03 + softness * 0.42, 0.03, 0.45));
    const float lo = 0.5f - s, inv = 1.0f / (2.0f * s);
    m_cx0.resize(size_t(W));
    m_cx1.resize(size_t(W));
    m_cfx.resize(size_t(W));
    for (int x = 0; x < W; ++x) {
        const float sx = (x + 0.5f) * nw / W - 0.5f;
        const int x0 = int(std::floor(sx));
        m_cfx[size_t(x)] = sx - x0;
        m_cx0[size_t(x)] = std::clamp(x0, 0, nw - 1);
        m_cx1[size_t(x)] = std::clamp(x0 + 1, 0, nw - 1);
    }
    // Horizontal upsampling of a and b once per low-resolution row.
    m_rowA.resize(size_t(nh) * W);
    m_rowB.resize(size_t(nh) * W);
    pool.parallelFor(nh, [&](int rb, int re) {
        for (int r = rb; r < re; ++r) {
            const float *a = m_a.data() + size_t(r) * nw, *b = m_b.data() + size_t(r) * nw;
            float *ra = m_rowA.data() + size_t(r) * W, *rbuf = m_rowB.data() + size_t(r) * W;
            for (int x = 0; x < W; ++x) {
                const int32_t x0 = m_cx0[size_t(x)], x1 = m_cx1[size_t(x)];
                const float fx = m_cfx[size_t(x)];
                ra[x] = a[x0] + (a[x1] - a[x0]) * fx;
                rbuf[x] = b[x0] + (b[x1] - b[x0]) * fx;
            }
        }
    }, 8);
    mask.resize(size_t(W) * H);
    pool.parallelFor(H, [&](int yb, int ye) {
        for (int y = yb; y < ye; ++y) {
            const float sy = (y + 0.5f) * nh / H - 0.5f;
            const int y0 = int(std::floor(sy));
            const float fy = sy - y0, gy = 1.0f - fy;
            const size_t r0 = size_t(std::clamp(y0, 0, nh - 1)) * W, r1 = size_t(std::clamp(y0 + 1, 0, nh - 1)) * W;
            const float *a0 = m_rowA.data() + r0, *a1 = m_rowA.data() + r1;
            const float *b0 = m_rowB.data() + r0, *b1 = m_rowB.data() + r1;
            const uint8_t *luma = f.plane[0] + size_t(y) * f.stride[0];
            uint8_t *mo = mask.data() + size_t(y) * W;
            // Branch-free with a local trip count (uint8_t stores may alias a
            // captured W) so the compiler vectorizes it: this runs per pixel.
            const int width = W;
            const float lo_ = lo, inv_ = inv;
            for (int x = 0; x < width; ++x) {
                const float A = a0[x] * gy + a1[x] * fy, B = b0[x] * gy + b1[x] * fy;
                const float I = float(luma[x]) * (1.0f / 219.0f) - (16.0f / 219.0f);
                float q = (A * I + B - lo_) * inv_;
                q = q < 0.0f ? 0.0f : q;
                q = q > 1.0f ? 1.0f : q;
                q = q * q * (3.0f - 2.0f * q);
                mo[x] = uint8_t(int(q * 255.0f + 0.5f));
            }
        }
    }, 16);
    return true;
}

} // namespace cam
