// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Blur.h"
#include "Frame.h"
#include "Params.h"
#include "PersonSegmenter.h"
#include "ThreadPool.h"

#include <cstdint>
#include <vector>

namespace cam {

// Converts an ARGB color to limited-range BT.601 Y, U, V.
void argbToYuv(uint32_t argb, uint8_t &y, uint8_t &u, uint8_t &v);

// Background effects: whole-frame blur, blurred regions, manual foreground
// shape, fixed mask image, chroma key and (optional, local) person detection.
class EffectsRenderer {
public:
    void setParams(const EffectParams &p);
    void apply(Frame &frame, ThreadPool &pool);
    bool personDetectionAvailable();

private:
    double blurSigma(int w, int h) const;
    void buildBackground(const Frame &frame, ThreadPool *pool);
    void buildStaticMask(int w, int h);
    void makeChromaMask(int w, int h);
    void buildChromaKeyMask(const Frame &frame);
    void composite(Frame &frame, ThreadPool &pool);

    EffectParams m_params;
    bool m_maskDirty = true;
    int m_maskW = 0, m_maskH = 0;
    std::vector<uint8_t> m_mask;  // luma resolution, 255 = keep foreground
    std::vector<uint8_t> m_cmask; // chroma resolution
    std::vector<uint8_t> m_bg[3];
    std::vector<uint8_t> m_weight;  // background weight (255 - mask) for masked blur
    std::vector<uint8_t> m_cweight;
    GaussianBlur m_blur;
    PersonSegmenter m_segmenter;
};

} // namespace cam
