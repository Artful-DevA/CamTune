// SPDX-License-Identifier: GPL-3.0-or-later
#include "Framing.h"

#include <algorithm>
#include <cmath>

namespace cam {

namespace {

double clampd(double v, double lo, double hi)
{
    if (!(v >= lo)) // also catches NaN
        return lo;
    return v > hi ? hi : v;
}

} // namespace

Affine computeFramingTransform(int srcW, int srcH, int outW, int outH, const FramingParams &pIn)
{
    Affine m;
    if (srcW <= 0 || srcH <= 0 || outW <= 0 || outH <= 0)
        return m;

    FramingParams p = pIn;
    p.zoom = clampd(p.zoom, 0.1, 16.0);
    p.panX = clampd(p.panX, -1.0, 1.0);
    p.panY = clampd(p.panY, -1.0, 1.0);
    p.cropLeft = clampd(p.cropLeft, 0.0, 0.9);
    p.cropRight = clampd(p.cropRight, 0.0, 0.9);
    p.cropTop = clampd(p.cropTop, 0.0, 0.9);
    p.cropBottom = clampd(p.cropBottom, 0.0, 0.9);
    if (p.cropLeft + p.cropRight > 0.95)
        p.cropRight = 0.95 - p.cropLeft;
    if (p.cropTop + p.cropBottom > 0.95)
        p.cropBottom = 0.95 - p.cropTop;
    if (!std::isfinite(p.rotation))
        p.rotation = 0;

    // Crop rectangle in source pixels.
    const double cx0 = p.cropLeft * srcW;
    const double cx1 = (1.0 - p.cropRight) * srcW;
    const double cy0 = p.cropTop * srcH;
    const double cy1 = (1.0 - p.cropBottom) * srcH;
    const double cropW = cx1 - cx0;
    const double cropH = cy1 - cy0;
    const double cropCx = (cx0 + cx1) * 0.5;
    const double cropCy = (cy0 + cy1) * 0.5;

    // Rotation: split into quarter turns (which swap the effective aspect) and a
    // fine remainder in [-45, 45].
    double rot = std::fmod(p.rotation, 360.0);
    if (rot < 0)
        rot += 360.0;
    int quarter = int(std::floor((rot + 45.0) / 90.0)) & 3;
    double fine = rot - quarter * 90.0;
    if (fine > 180.0)
        fine -= 360.0;

    double effW = (quarter & 1) ? cropH : cropW;
    double effH = (quarter & 1) ? cropW : cropH;

    // View size (in source pixels) at zoom 1.
    const double outAspect = double(outW) / outH;
    double vw, vh;
    switch (p.aspect) {
    case AspectMode::Fit:
        if (effW / effH > outAspect) {
            vw = effW;
            vh = effW / outAspect;
        } else {
            vh = effH;
            vw = effH * outAspect;
        }
        break;
    case AspectMode::Stretch:
        vw = effW;
        vh = effH;
        break;
    case AspectMode::Fill:
    default:
        if (effW / effH > outAspect) {
            vh = effH;
            vw = effH * outAspect;
        } else {
            vw = effW;
            vh = effW / outAspect;
        }
        break;
    }

    vw /= p.zoom;
    vh /= p.zoom;

    const double rad = fine * M_PI / 180.0;
    const double cs = std::cos(rad), sn = std::sin(rad);

    // In fill mode, shrink a finely rotated view until it fits inside the crop so
    // no empty corners appear.
    if (p.aspect == AspectMode::Fill && std::fabs(fine) > 1e-6) {
        double bw = vw * std::fabs(cs) + vh * std::fabs(sn);
        double bh = vw * std::fabs(sn) + vh * std::fabs(cs);
        double s = std::min({1.0, effW / bw, effH / bh});
        vw *= s;
        vh *= s;
    }

    // Pan within the available travel. Mirroring is an output-space flip, so the
    // pan direction is flipped too in order to stay "what you see" consistent.
    double travelX = std::fabs(effW - vw) * 0.5;
    double travelY = std::fabs(effH - vh) * 0.5;
    double panX = p.mirror ? -p.panX : p.panX;
    double panY = p.flip ? -p.panY : p.panY;
    double offX = panX * travelX; // view-center offset in the rotated crop frame
    double offY = panY * travelY;

    // Output pixel (x, y) -> normalized u in [-0.5, 0.5].
    double su = 1.0 / outW, sv = 1.0 / outH;
    double u0 = -0.5, v0 = -0.5;
    if (p.mirror) {
        su = -su;
        u0 = 0.5;
    }
    if (p.flip) {
        sv = -sv;
        v0 = 0.5;
    }
    // Rotated-frame coordinates: rx = offX + u*vw, ry = offY + v*vh.
    const double rxx = su * vw, rx0 = offX + u0 * vw;
    const double ryy = sv * vh, ry0 = offY + v0 * vh;

    // Total rotation (clockwise in the image as seen by the viewer) maps the
    // rotated frame back to source: s = R(-theta) * r + cropCenter, where y points down.
    const double total = (quarter * 90.0 + fine) * M_PI / 180.0;
    const double c = std::cos(total), s = std::sin(total);
    // For an image rotated clockwise by theta, an output offset r corresponds to
    // a source offset R(theta)^-1 r; with y down, R(-theta) = [[c, s], [-s, c]].
    m.a = c * rxx;
    m.b = s * ryy;
    m.c = c * rx0 + s * ry0 + cropCx;
    m.d = -s * rxx;
    m.e = c * ryy;
    m.f = -s * rx0 + c * ry0 + cropCy;

    // Snap negligible terms so the fast axis-aligned path is used for 0/180 degrees.
    auto snap = [](double &v) {
        if (std::fabs(v) < 1e-12)
            v = 0;
    };
    snap(m.a);
    snap(m.b);
    snap(m.d);
    snap(m.e);
    return m;
}

namespace {

double smoothstep(double t)
{
    t = clampd(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

} // namespace

void FramingAnimator::setTarget(const FramingParams &target, int durationMs, int64_t nowNs)
{
    if (!m_initialized || durationMs <= 0) {
        m_from = m_to = m_current = target;
        m_animating = false;
        m_initialized = true;
        return;
    }
    // Start from wherever we are right now so retargeting mid-animation is seamless.
    m_from = current(nowNs);
    m_to = target;
    m_startNs = nowNs;
    m_durationNs = int64_t(durationMs) * 1000000;
    m_animating = true;
    // Discrete settings switch immediately.
    m_from.mirror = target.mirror;
    m_from.flip = target.flip;
    m_from.aspect = target.aspect;
}

FramingParams FramingAnimator::current(int64_t nowNs)
{
    if (!m_animating)
        return m_current;
    double t = m_durationNs > 0 ? double(nowNs - m_startNs) / double(m_durationNs) : 1.0;
    if (t >= 1.0) {
        m_current = m_to;
        m_animating = false;
        return m_current;
    }
    double k = smoothstep(t);
    FramingParams r = m_to;
    double z0 = std::log(std::max(1e-3, m_from.zoom));
    double z1 = std::log(std::max(1e-3, m_to.zoom));
    r.zoom = std::exp(lerp(z0, z1, k));
    r.panX = lerp(m_from.panX, m_to.panX, k);
    r.panY = lerp(m_from.panY, m_to.panY, k);
    // Rotate along the shortest arc.
    double dr = std::fmod(m_to.rotation - m_from.rotation, 360.0);
    if (dr > 180)
        dr -= 360;
    if (dr < -180)
        dr += 360;
    r.rotation = m_from.rotation + dr * k;
    r.cropLeft = lerp(m_from.cropLeft, m_to.cropLeft, k);
    r.cropTop = lerp(m_from.cropTop, m_to.cropTop, k);
    r.cropRight = lerp(m_from.cropRight, m_to.cropRight, k);
    r.cropBottom = lerp(m_from.cropBottom, m_to.cropBottom, k);
    m_current = r;
    return r;
}

} // namespace cam
