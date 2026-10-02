// SPDX-License-Identifier: GPL-3.0-or-later
#include "Effects.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cam {

namespace {

double smoothstep(double e0, double e1, double x)
{
    if (e1 <= e0)
        return x < e0 ? 0.0 : 1.0;
    double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

} // namespace

void argbToYuv(uint32_t argb, uint8_t &y, uint8_t &u, uint8_t &v)
{
    const double r = (argb >> 16) & 0xff, g = (argb >> 8) & 0xff, b = argb & 0xff;
    y = uint8_t(std::clamp(std::lround(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255.0), 16L, 235L));
    u = uint8_t(std::clamp(std::lround(128 + (-37.797 * r - 74.203 * g + 112.0 * b) / 255.0), 16L, 240L));
    v = uint8_t(std::clamp(std::lround(128 + (112.0 * r - 93.786 * g - 18.214 * b) / 255.0), 16L, 240L));
}

void EffectsRenderer::setParams(const EffectParams &p)
{
    const EffectParams &o = m_params;
    bool shapeChanged = p.mode != o.mode || !(p.foreground == o.foreground) ||
                        p.foregroundEllipse != o.foregroundEllipse || p.feather != o.feather ||
                        p.blurRegions != o.blurRegions || p.maskImage != o.maskImage ||
                        p.maskWidth != o.maskWidth || p.maskHeight != o.maskHeight;
    m_params = p;
    if (shapeChanged)
        m_maskDirty = true;
}

void EffectsRenderer::buildStaticMask(int w, int h)
{
    m_mask.assign(size_t(w) * h, 255);
    const double feather = std::max(1.0, m_params.feather * std::min(w, h));

    auto shapeAlpha = [&](const RectF &rc, bool ellipse, int x, int y) -> double {
        const double cx = (rc.x + rc.w * 0.5) * w, cy = (rc.y + rc.h * 0.5) * h;
        const double rx = std::max(1.0, rc.w * 0.5 * w), ry = std::max(1.0, rc.h * 0.5 * h);
        const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
        double dist; // signed distance in pixels, negative inside
        if (ellipse) {
            double n = std::sqrt((dx * dx) / (rx * rx) + (dy * dy) / (ry * ry));
            dist = (n - 1.0) * std::min(rx, ry);
        } else {
            dist = std::max(std::fabs(dx) - rx, std::fabs(dy) - ry);
        }
        return 1.0 - smoothstep(-feather * 0.5, feather * 0.5, dist);
    };

    switch (m_params.mode) {
    case EffectMode::Foreground:
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                m_mask[size_t(y) * w + x] =
                    uint8_t(std::lround(255 * shapeAlpha(m_params.foreground, m_params.foregroundEllipse, x, y)));
        break;
    case EffectMode::BlurRegions: {
        const double regionFeather = std::max(1.0, 0.01 * std::min(w, h));
        for (const RectF &rc : m_params.blurRegions) {
            int x0 = std::max(0, int((rc.x * w) - regionFeather * 2));
            int x1 = std::min(w, int((rc.x + rc.w) * w + regionFeather * 2) + 1);
            int y0 = std::max(0, int((rc.y * h) - regionFeather * 2));
            int y1 = std::min(h, int((rc.y + rc.h) * h + regionFeather * 2) + 1);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    const double cx = (rc.x + rc.w * 0.5) * w, cy = (rc.y + rc.h * 0.5) * h;
                    double dist = std::max(std::fabs(x + 0.5 - cx) - rc.w * 0.5 * w,
                                           std::fabs(y + 0.5 - cy) - rc.h * 0.5 * h);
                    double a = 1.0 - smoothstep(-regionFeather, regionFeather, dist);
                    uint8_t &m = m_mask[size_t(y) * w + x];
                    m = std::min<uint8_t>(m, uint8_t(std::lround(255 * (1.0 - a))));
                }
        }
        break;
    }
    case EffectMode::MaskImage:
        if (m_params.maskImage && m_params.maskWidth > 0 && m_params.maskHeight > 0 &&
            m_params.maskImage->size() >= size_t(m_params.maskWidth) * m_params.maskHeight) {
            const auto &src = *m_params.maskImage;
            for (int y = 0; y < h; ++y) {
                int sy = std::min(m_params.maskHeight - 1, y * m_params.maskHeight / h);
                for (int x = 0; x < w; ++x) {
                    int sx = std::min(m_params.maskWidth - 1, x * m_params.maskWidth / w);
                    m_mask[size_t(y) * w + x] = src[size_t(sy) * m_params.maskWidth + sx];
                }
            }
        }
        break;
    default:
        break;
    }

    makeChromaMask(w, h);
    m_maskW = w;
    m_maskH = h;
    m_maskDirty = false;
}

void EffectsRenderer::makeChromaMask(int w, int h)
{
    // Chroma-resolution mask: 2x2 average.
    const int cw = w / 2, ch = h / 2;
    m_cmask.resize(size_t(cw) * ch);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            const uint8_t *a = &m_mask[size_t(2 * y) * w + 2 * x];
            const uint8_t *b = a + w;
            m_cmask[size_t(y) * cw + x] = uint8_t((a[0] + a[1] + b[0] + b[1] + 2) >> 2);
        }
}

void EffectsRenderer::buildChromaKeyMask(const Frame &frame)
{
    const int w = frame.width, h = frame.height, cw = w / 2, ch = h / 2;
    uint8_t ky, ku, kv;
    argbToYuv(m_params.keyColor, ky, ku, kv);
    const double sim = std::clamp(m_params.keySimilarity, 0.0, 1.0) * 0.5;
    const double smooth = std::clamp(m_params.keySmoothness, 0.0, 1.0) * 0.5;

    // Distance in the UV plane maps through a 64-entry-per-axis table would be
    // overkill; a squared-distance LUT keeps it to integer math per pixel.
    static thread_local std::vector<uint8_t> lut;
    lut.resize(2 * 128 * 128 + 1);
    const double maxd = 112.0 * std::sqrt(2.0);
    for (size_t i = 0; i < lut.size(); ++i) {
        double d = std::sqrt(double(i)) / maxd;
        lut[i] = uint8_t(std::lround(255 * smoothstep(sim, sim + smooth + 1e-6, d)));
    }

    m_cmask.resize(size_t(cw) * ch);
    for (int y = 0; y < ch; ++y) {
        const uint8_t *up = frame.plane[1] + size_t(y) * frame.stride[1];
        const uint8_t *vp = frame.plane[2] + size_t(y) * frame.stride[2];
        uint8_t *mp = &m_cmask[size_t(y) * cw];
        for (int x = 0; x < cw; ++x) {
            int du = std::min(127, std::abs(up[x] - ku));
            int dv = std::min(127, std::abs(vp[x] - kv));
            mp[x] = lut[size_t(du * du + dv * dv)];
        }
    }
    m_mask.resize(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const uint8_t *mp = &m_cmask[size_t(std::min(ch - 1, y / 2)) * cw];
        uint8_t *out = &m_mask[size_t(y) * w];
        for (int x = 0; x < w; ++x)
            out[x] = mp[std::min(cw - 1, x / 2)];
    }
    m_maskW = w;
    m_maskH = h;
}

void EffectsRenderer::buildBackground(const Frame &frame, ThreadPool *pool)
{
    const int w = frame.width, h = frame.height;
    const int dims[3][2] = {{w, h}, {w / 2, h / 2}, {w / 2, h / 2}};
    for (int p = 0; p < 3; ++p)
        m_bg[p].resize(size_t(dims[p][0]) * dims[p][1]);

    const EffectParams &e = m_params;
    if (e.fill == BackgroundFill::Image && e.backgroundImage && e.backgroundImage->width == w &&
        e.backgroundImage->height == h && e.backgroundImage->format == PixelFormat::I420) {
        for (int p = 0; p < 3; ++p)
            for (int y = 0; y < dims[p][1]; ++y)
                std::memcpy(&m_bg[p][size_t(y) * dims[p][0]],
                            e.backgroundImage->plane[p] + size_t(y) * e.backgroundImage->stride[p],
                            size_t(dims[p][0]));
        return;
    }
    if (e.fill == BackgroundFill::Blur) {
        // Masked blur: only background pixels feed the blur, so the person's
        // colors do not bleed into it as a halo. Regions use a plain blur.
        const bool masked = e.mode != EffectMode::BlurRegions;
        if (masked) {
            m_weight.resize(m_mask.size());
            m_cweight.resize(m_cmask.size());
            for (size_t i = 0; i < m_mask.size(); ++i)
                m_weight[i] = uint8_t(255 - m_mask[i]);
            for (size_t i = 0; i < m_cmask.size(); ++i)
                m_cweight[i] = uint8_t(255 - m_cmask[i]);
        }
        const double sigma = blurSigma(w, h);
        for (int p = 0; p < 3; ++p) {
            const uint8_t *wt = masked ? (p == 0 ? m_weight.data() : m_cweight.data()) : nullptr;
            m_blur.blur(frame.plane[p], frame.stride[p], m_bg[p].data(), dims[p][0], dims[p][0], dims[p][1],
                        p == 0 ? sigma : sigma * 0.5, pool, wt, dims[p][0]);
        }
        return;
    }
    uint8_t yuv[3];
    argbToYuv(e.fillColor, yuv[0], yuv[1], yuv[2]);
    for (int p = 0; p < 3; ++p)
        std::fill(m_bg[p].begin(), m_bg[p].end(), yuv[p]);
}

void EffectsRenderer::composite(Frame &frame, ThreadPool &pool)
{
    const int w = frame.width, h = frame.height;
    for (int p = 0; p < 3; ++p) {
        const int pw = p == 0 ? w : w / 2, ph = p == 0 ? h : h / 2;
        const uint8_t *mask = p == 0 ? m_mask.data() : m_cmask.data();
        const uint8_t *bg = m_bg[p].data();
        uint8_t *base = frame.plane[p];
        const int stride = frame.stride[p];
        pool.parallelFor(ph, [&](int jb, int je) {
            for (int y = jb; y < je; ++y) {
                uint8_t *out = base + size_t(y) * stride;
                const uint8_t *m = mask + size_t(y) * pw;
                const uint8_t *b = bg + size_t(y) * pw;
                const int width = pw; // local trip count keeps the loop vectorizable
                for (int x = 0; x < width; ++x) {
                    const int a = m[x];
                    out[x] = uint8_t(((out[x] * a + b[x] * (255 - a)) * 257 + 32768) >> 16);
                }
            }
        });
    }
}

double EffectsRenderer::blurSigma(int w, int h) const
{
    // Strength 0..1 maps to a Gaussian sigma of ~0.4%..4% of the picture size.
    return 1.5 + std::clamp(m_params.blurStrength, 0.0, 1.0) * std::min(w, h) * 0.04;
}

void EffectsRenderer::apply(Frame &frame, ThreadPool &pool)
{
    if (m_params.mode == EffectMode::Off || frame.format != PixelFormat::I420)
        return;
    if (m_params.mode == EffectMode::BlurRegions && m_params.blurRegions.empty())
        return; // nothing to blur
    if (m_params.mode == EffectMode::MaskImage && !m_params.maskImage)
        return; // no mask loaded yet
    const int w = frame.width, h = frame.height;

    if (m_params.mode == EffectMode::BlurAll) {
        const double sigma = blurSigma(w, h);
        for (int p = 0; p < 3; ++p) {
            const int pw = p == 0 ? w : w / 2, ph = p == 0 ? h : h / 2;
            m_blur.blur(frame.plane[p], frame.stride[p], frame.plane[p], frame.stride[p], pw, ph,
                        p == 0 ? sigma : sigma * 0.5, &pool);
        }
        return;
    }

    if (m_params.mode == EffectMode::Person) {
        // Softness slider is 0..30 %.
        const double softness = std::clamp(m_params.feather / 0.3, 0.0, 1.0);
        if (!m_segmenter.compute(frame, pool, softness, m_mask))
            return; // model unavailable: leave the picture untouched
        makeChromaMask(w, h);
        m_maskW = w;
        m_maskH = h;
        m_maskDirty = true; // the static mask cache no longer holds a static mask
    } else if (m_params.mode == EffectMode::ChromaKey) {
        buildChromaKeyMask(frame);
    } else if (m_maskDirty || m_maskW != w || m_maskH != h) {
        buildStaticMask(w, h);
    }

    // Blur regions always use a blurred background regardless of the fill setting.
    if (m_params.mode == EffectMode::BlurRegions) {
        BackgroundFill saved = m_params.fill;
        m_params.fill = BackgroundFill::Blur;
        buildBackground(frame, &pool);
        m_params.fill = saved;
    } else {
        buildBackground(frame, &pool);
    }
    composite(frame, pool);
}

bool EffectsRenderer::personDetectionAvailable() { return m_segmenter.available(); }

} // namespace cam
