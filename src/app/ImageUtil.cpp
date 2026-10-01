// SPDX-License-Identifier: GPL-3.0-or-later
#include "ImageUtil.h"

#include <QFont>
#include <QLinearGradient>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace app {

cam::FramePtr imageToI420(const QImage &image, int width, int height)
{
    if (image.isNull() || width <= 0 || height <= 0)
        return nullptr;
    QImage scaled = image.scaled(width, height, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int ox = (scaled.width() - width) / 2, oy = (scaled.height() - height) / 2;
    QImage rgb = scaled.copy(ox, oy, width, height).convertToFormat(QImage::Format_RGB32);

    auto frame = std::make_shared<cam::Frame>();
    if (!frame->allocI420(width, height))
        return nullptr;
    for (int y = 0; y < frame->height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(rgb.constScanLine(y));
        uint8_t *yp = frame->plane[0] + size_t(y) * frame->stride[0];
        for (int x = 0; x < frame->width; ++x) {
            const int r = qRed(line[x]), g = qGreen(line[x]), b = qBlue(line[x]);
            yp[x] = uint8_t((66 * r + 129 * g + 25 * b + 128) / 256 + 16);
        }
    }
    for (int y = 0; y < frame->height / 2; ++y) {
        const QRgb *l0 = reinterpret_cast<const QRgb *>(rgb.constScanLine(2 * y));
        const QRgb *l1 = reinterpret_cast<const QRgb *>(rgb.constScanLine(2 * y + 1));
        uint8_t *up = frame->plane[1] + size_t(y) * frame->stride[1];
        uint8_t *vp = frame->plane[2] + size_t(y) * frame->stride[2];
        for (int x = 0; x < frame->width / 2; ++x) {
            int r = 0, g = 0, b = 0;
            for (QRgb c : {l0[2 * x], l0[2 * x + 1], l1[2 * x], l1[2 * x + 1]}) {
                r += qRed(c);
                g += qGreen(c);
                b += qBlue(c);
            }
            r /= 4;
            g /= 4;
            b /= 4;
            up[x] = uint8_t(std::clamp((-38 * r - 74 * g + 112 * b + 128) / 256 + 128, 16, 240));
            vp[x] = uint8_t(std::clamp((112 * r - 94 * g - 18 * b + 128) / 256 + 128, 16, 240));
        }
    }
    return frame;
}

std::shared_ptr<std::vector<uint8_t>> imageToMask(const QImage &image, int &width, int &height)
{
    if (image.isNull())
        return nullptr;
    // Masks are resampled to the output size per use; cap the stored size.
    QImage img = image;
    if (img.width() > 1920 || img.height() > 1080)
        img = img.scaled(1920, 1080, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const bool useAlpha = img.hasAlphaChannel();
    QImage argb = img.convertToFormat(QImage::Format_ARGB32);
    width = argb.width();
    height = argb.height();
    auto mask = std::make_shared<std::vector<uint8_t>>(size_t(width) * height);
    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < width; ++x)
            (*mask)[size_t(y) * width + x] = uint8_t(useAlpha ? qAlpha(line[x]) : qGray(line[x]));
    }
    return mask;
}

cam::FramePtr renderPlaceholder(int width, int height, const QString &text)
{
    if (width <= 0 || height <= 0)
        return nullptr;
    QImage img(width, height, QImage::Format_RGB32);
    QPainter p(&img);
    QLinearGradient g(0, 0, 0, height);
    g.setColorAt(0, QColor(38, 42, 50));
    g.setColorAt(1, QColor(22, 24, 30));
    p.fillRect(img.rect(), g);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // Simple camera glyph.
    const double s = height / 9.0;
    const QPointF c(width / 2.0, height / 2.0 - s * 0.6);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(90, 98, 112));
    p.drawRoundedRect(QRectF(c.x() - s, c.y() - s * 0.6, s * 1.6, s * 1.2), s * 0.15, s * 0.15);
    QPolygonF lens;
    lens << QPointF(c.x() + s * 0.65, c.y() - s * 0.2) << QPointF(c.x() + s * 1.1, c.y() - s * 0.5)
         << QPointF(c.x() + s * 1.1, c.y() + s * 0.5) << QPointF(c.x() + s * 0.65, c.y() + s * 0.2);
    p.drawPolygon(lens);

    QFont f = p.font();
    f.setPixelSize(std::max(12, height / 22));
    p.setFont(f);
    p.setPen(QColor(210, 214, 222));
    QRectF textRect(width * 0.08, c.y() + s * 0.9, width * 0.84, height * 0.3);
    p.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, text);
    p.end();
    return imageToI420(img, width, height);
}

QRgb sampleI420(const cam::Frame &frame, double nx, double ny)
{
    if (frame.format != cam::PixelFormat::I420 || frame.width <= 1 || frame.height <= 1)
        return qRgb(0, 0, 0);
    const int x = std::clamp(int(nx * frame.width), 0, frame.width - 1);
    const int y = std::clamp(int(ny * frame.height), 0, frame.height - 1);
    const double Y = frame.plane[0][size_t(y) * frame.stride[0] + x] - 16.0;
    const double U = frame.plane[1][size_t(y / 2) * frame.stride[1] + x / 2] - 128.0;
    const double V = frame.plane[2][size_t(y / 2) * frame.stride[2] + x / 2] - 128.0;
    auto c = [](double v) { return std::clamp(int(std::lround(v)), 0, 255); };
    return qRgb(c(1.164 * Y + 1.596 * V), c(1.164 * Y - 0.392 * U - 0.813 * V), c(1.164 * Y + 2.017 * U));
}

} // namespace app
