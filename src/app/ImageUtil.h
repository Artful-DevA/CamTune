// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Frame.h"

#include <QImage>
#include <QString>

#include <memory>
#include <vector>

namespace app {

// Scales (cover + center crop) an image and converts it to limited-range I420.
cam::FramePtr imageToI420(const QImage &image, int width, int height);

// Converts an image to an 8-bit mask (luma or alpha; 255 = foreground).
std::shared_ptr<std::vector<uint8_t>> imageToMask(const QImage &image, int &width, int &height);

// Frame sent to the virtual camera while the physical camera is unavailable.
cam::FramePtr renderPlaceholder(int width, int height, const QString &text);

// Converts an I420 frame region to RGB (for color picking).
QRgb sampleI420(const cam::Frame &frame, double nx, double ny);

} // namespace app
