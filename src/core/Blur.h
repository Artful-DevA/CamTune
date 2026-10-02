// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace cam {

class ThreadPool;

// High-quality, fast Gaussian-like blur for 8-bit image planes.
//
// Large blurs are computed at reduced resolution (box downsample by a power
// of two, three box passes ~ Gaussian) and brought back with a cubic B-spline
// upsampler, which is smooth (C2) so no blocks, diamonds or streaks appear.
//
// With a weight plane the blur is "masked": only weighted pixels contribute,
// i.e. out = blur(src * w) / blur(w). Used with an inverted person mask this
// blurs the background without the person's colors smearing into it (no halo).
// Output pixels whose own weight is 0 are left untouched (they are hidden by
// the foreground in the composite).
class GaussianBlur {
public:
    void blur(const uint8_t *src, int srcStride, uint8_t *dst, int dstStride, int w, int h, double sigma,
              ThreadPool *pool, const uint8_t *weight = nullptr, int weightStride = 0);

private:
    std::vector<float> m_lowA, m_lowW, m_tmp, m_rowsA, m_rowsW;
    std::vector<int32_t> m_cx;
    std::vector<float> m_cw;
};

} // namespace cam
