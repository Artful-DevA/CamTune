// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Effects.h"
#include "Frame.h"
#include "Framing.h"
#include "Params.h"
#include "ThreadPool.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cam {

// A read-only view of one image plane. `step` is the byte distance between
// horizontally adjacent samples, which lets packed formats (YUYV, NV12...) be
// sampled in place without first being converted to planar.
struct PlaneView {
    const uint8_t *data = nullptr;
    int stride = 0;
    int step = 1;
    int width = 0;
    int height = 0;
};

// Returns false for formats that cannot be sampled directly (e.g. MJPEG).
bool planeViews(const Frame &f, PlaneView views[3], bool &hasChroma);

// Builds the 8-bit lookup tables for a color setting. Input range is
// normalized and the output is always limited-range BT.601 video.
void buildColorLuts(const ColorParams &c, bool inputFullRange, uint8_t yLut[256], uint8_t uLut[256],
                    uint8_t vLut[256]);

// Builds the 2D chroma table (index (u << 8) | v, value (u' << 8) | v') for
// vibrance and hue on limited-range output. Returns false, leaving the table
// untouched, when the setting needs no chroma pass.
bool buildChromaMap(const ColorParams &c, uint16_t *map);

// Converts an I420 frame to packed YUYV (chroma rows are shared between line pairs).
void packI420ToYuyv(const Frame &src, uint8_t *dst, int dstStride);

// Fills an I420 frame with an animated test pattern.
void renderTestPattern(Frame &dst, uint64_t frameIndex);

// The image-processing stage: one fused resample + color pass from any
// supported input layout into an I420 output frame, then optional sharpening
// and background effects. Not thread-safe; owned by the processing thread.
class Processor {
public:
    explicit Processor(int threads = ThreadPool::defaultThreadCount());

    void setColor(const ColorParams &c);
    void setEffects(const EffectParams &e);

    // dst must already be allocated as I420 at the desired output size.
    bool process(const Frame &src, Frame &dst, const FramingParams &framing);

    int threadCount() const { return m_pool.size(); }

private:
    void samplePlane(const PlaneView &src, uint8_t *dst, int dstStride, int dstW, int dstH, double ax,
                     double bx, double cx, double ay, double by, double cy, const uint8_t *lut,
                     uint8_t border);
    void fillPlane(uint8_t *dst, int stride, int w, int h, uint8_t value);
    void sharpen(Frame &dst);
    void applyChromaMap(Frame &dst);

    ThreadPool m_pool;
    ColorParams m_color;
    bool m_lutFullRange = false;
    bool m_lutValid = false;
    uint8_t m_yLut[256];
    uint8_t m_uLut[256];
    uint8_t m_vLut[256];
    std::vector<uint16_t> m_chromaMap;
    bool m_chromaMapActive = false;

    // Per-call scratch, reused across frames to avoid allocation.
    std::vector<int32_t> m_colOff0, m_colOff1;
    std::vector<int16_t> m_colFrac;
    std::vector<uint8_t> m_colInside;
    std::vector<uint8_t> m_sharpenTmp;

    EffectsRenderer m_effects;
};

} // namespace cam
