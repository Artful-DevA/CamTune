// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Frame.h"

#include <memory>
#include <string>
#include <vector>

namespace cam {

// Software color adjustments, applied with lookup tables fused into the
// resampling pass (no extra pass over the image).
struct ColorParams {
    double brightness = 0.0; // -1 .. 1   (additive, in fraction of full scale)
    double contrast = 1.0;   //  0 .. 3
    double saturation = 1.0; //  0 .. 3
    double gamma = 1.0;      //  0.2 .. 5 (>1 brightens midtones)
    double sharpness = 0.0;  //  0 .. 2   (unsharp mask amount, extra pass when > 0)
    double warmth = 0.0;     // -1 .. 1   (blue <-> amber chroma shift)
    double tint = 0.0;       // -1 .. 1   (green <-> magenta chroma shift)

    bool isIdentity() const
    {
        return brightness == 0.0 && contrast == 1.0 && saturation == 1.0 && gamma == 1.0 &&
               warmth == 0.0 && tint == 0.0;
    }
    bool operator==(const ColorParams &o) const
    {
        return brightness == o.brightness && contrast == o.contrast && saturation == o.saturation &&
               gamma == o.gamma && sharpness == o.sharpness && warmth == o.warmth && tint == o.tint;
    }
    bool operator!=(const ColorParams &o) const { return !(*this == o); }
};

enum class AspectMode : int { Fill = 0, Fit = 1, Stretch = 2 };

struct FramingParams {
    double zoom = 1.0;     // 1 .. 8
    double panX = 0.0;     // -1 .. 1, fraction of available travel (+ = view moves right)
    double panY = 0.0;     // -1 .. 1 (+ = view moves down)
    double rotation = 0.0; // degrees, clockwise
    bool mirror = false;   // horizontal flip of the output
    bool flip = false;     // vertical flip of the output
    AspectMode aspect = AspectMode::Fill;
    // Manual crop, as fractions of the source removed from each edge.
    double cropLeft = 0.0, cropTop = 0.0, cropRight = 0.0, cropBottom = 0.0;

    bool operator==(const FramingParams &o) const
    {
        return zoom == o.zoom && panX == o.panX && panY == o.panY && rotation == o.rotation &&
               mirror == o.mirror && flip == o.flip && aspect == o.aspect && cropLeft == o.cropLeft &&
               cropTop == o.cropTop && cropRight == o.cropRight && cropBottom == o.cropBottom;
    }
    bool operator!=(const FramingParams &o) const { return !(*this == o); }
};

struct RectF {
    double x = 0, y = 0, w = 0, h = 0; // normalized 0..1 output coordinates
    bool operator==(const RectF &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

// Background effects. None of these need machine learning.
enum class EffectMode : int {
    Off = 0,
    BlurAll = 1,        // blur the entire picture
    BlurRegions = 2,    // blur only user-drawn rectangles
    Foreground = 3,     // keep a user-drawn ellipse/rectangle, replace the rest
    MaskImage = 4,      // keep where a fixed mask image is white, replace the rest
    ChromaKey = 5,      // green screen
};

enum class BackgroundFill : int { Blur = 0, Color = 1, Image = 2 };

struct EffectParams {
    EffectMode mode = EffectMode::Off;
    BackgroundFill fill = BackgroundFill::Blur;
    double blurStrength = 0.5;          // 0 .. 1
    uint32_t fillColor = 0xff202020;    // ARGB
    std::vector<RectF> blurRegions;
    RectF foreground{0.2, 0.05, 0.6, 0.95};
    bool foregroundEllipse = true;
    double feather = 0.08;              // fraction of the shorter output side
    uint32_t keyColor = 0xff00b140;     // ARGB, typical chroma green
    double keySimilarity = 0.25;        // 0 .. 1
    double keySmoothness = 0.08;        // 0 .. 1
    std::string backgroundImagePath;
    std::string maskImagePath;

    // Prepared by the UI layer at the current output size (I420 / 8-bit plane).
    std::shared_ptr<const Frame> backgroundImage;
    std::shared_ptr<const std::vector<uint8_t>> maskImage; // w*h, 255 = foreground
    int maskWidth = 0, maskHeight = 0;
};

enum class OutputPixelFormat : int { I420 = 0, YUYV = 1 };

struct OutputConfig {
    bool enabled = false;
    std::string devicePath; // empty = auto-detect a v4l2loopback device
    int width = 1280;
    int height = 720;
    int fps = 30;
    OutputPixelFormat pixelFormat = OutputPixelFormat::I420;
};

} // namespace cam
