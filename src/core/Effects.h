// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Frame.h"
#include "Params.h"
#include "ThreadPool.h"

#include <cstdint>
#include <vector>

namespace cam {

// Fast approximate Gaussian blur of one 8-bit plane: box-downsample, two box
// blur passes at low resolution, bilinear upsample. Cost is dominated by the
// final upsample, so it stays cheap even for large radii.
void blurPlane(const uint8_t *src, int srcStride, uint8_t *dst, int dstStride, int w, int h,
               double radiusPx, std::vector<uint8_t> &scratchA, std::vector<uint16_t> &scratchB,
               ThreadPool *pool = nullptr);

// Converts an ARGB color to limited-range BT.601 Y, U, V.
void argbToYuv(uint32_t argb, uint8_t &y, uint8_t &u, uint8_t &v);

// Background effects without segmentation models: whole-frame blur, blurred
// regions, manual foreground shape, fixed mask image and chroma key.
class EffectsRenderer {
public:
    void setParams(const EffectParams &p);
    void apply(Frame &frame, ThreadPool &pool);

private:
    bool needsBackground() const;
    void buildBackground(const Frame &frame, ThreadPool *pool);
    void buildStaticMask(int w, int h);
    void buildChromaKeyMask(const Frame &frame);
    void composite(Frame &frame, ThreadPool &pool);

    EffectParams m_params;
    bool m_maskDirty = true;
    int m_maskW = 0, m_maskH = 0;
    std::vector<uint8_t> m_mask;  // luma resolution, 255 = keep foreground
    std::vector<uint8_t> m_cmask; // chroma resolution
    std::vector<uint8_t> m_bg[3];
    std::vector<uint8_t> m_scratchA;
    std::vector<uint16_t> m_scratchB;
};

} // namespace cam
