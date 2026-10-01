// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Params.h"

#include <cstdint>

namespace cam {

// Maps continuous output coordinates (pixel centers at i + 0.5) to continuous
// source luma coordinates:  sx = a*x + b*y + c,  sy = d*x + e*y + f.
struct Affine {
    double a = 1, b = 0, c = 0;
    double d = 0, e = 1, f = 0;

    bool isAxisAligned() const { return b > -1e-9 && b < 1e-9 && d > -1e-9 && d < 1e-9; }
};

// Computes the output->source mapping for the given framing.
Affine computeFramingTransform(int srcW, int srcH, int outW, int outH, const FramingParams &p);

// Smoothly moves the framing used for rendering towards a target.
//
// Zoom is interpolated in log space so equal time gives equal perceived zoom
// change; the easing is a smoothstep so motion starts and stops gently.
class FramingAnimator {
public:
    void setTarget(const FramingParams &target, int durationMs, int64_t nowNs);
    // Returns the framing to use for a frame rendered at nowNs.
    FramingParams current(int64_t nowNs);
    bool animating() const { return m_animating; }

private:
    FramingParams m_from;
    FramingParams m_to;
    FramingParams m_current;
    int64_t m_startNs = 0;
    int64_t m_durationNs = 0;
    bool m_animating = false;
    bool m_initialized = false;
};

} // namespace cam
