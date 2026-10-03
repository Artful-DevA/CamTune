// SPDX-License-Identifier: GPL-3.0-or-later
#include "Icons.h"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace ui::icons {

namespace {

const QColor kNormal(0xb4, 0xb4, 0xbc);
const QColor kActive(0xf2, 0xf2, 0xf5);
const QColor kChecked(0x6a, 0xa8, 0xff);
const QColor kDisabled(0x5c, 0x5c, 0x64);

// Draws one icon on a 24x24 design grid.
void draw(QPainter &p, Name n, const QColor &c)
{
    QPen pen(c, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    switch (n) {
    case Name::Picture: // half-filled circle (tone / adjust)
        p.drawEllipse(QPointF(12, 12), 8, 8);
        path.moveTo(12, 4);
        path.arcTo(QRectF(4, 4, 16, 16), 90, 180);
        path.closeSubpath();
        p.setBrush(c);
        p.drawPath(path);
        break;
    case Name::Framing: // crop marks
        path.moveTo(7, 3);
        path.lineTo(7, 17);
        path.lineTo(21, 17);
        path.moveTo(3, 7);
        path.lineTo(17, 7);
        path.lineTo(17, 21);
        p.drawPath(path);
        break;
    case Name::Background: // person in front of a frame
        p.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
        p.drawEllipse(QPointF(12, 10), 3, 3);
        path.moveTo(6.5, 21);
        path.cubicTo(6.5, 16.5, 9, 14.8, 12, 14.8);
        path.cubicTo(15, 14.8, 17.5, 16.5, 17.5, 21);
        p.drawPath(path);
        break;
    case Name::Camera: // webcam
        p.drawEllipse(QPointF(12, 10), 6.5, 6.5);
        p.drawEllipse(QPointF(12, 10), 2.4, 2.4);
        path.moveTo(12, 16.5);
        path.lineTo(12, 19.5);
        path.moveTo(8, 20.5);
        path.lineTo(16, 20.5);
        p.drawPath(path);
        break;
    case Name::Output: // broadcast
        p.setBrush(c);
        p.drawEllipse(QPointF(12, 12), 1.8, 1.8);
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(7, 7, 10, 10), 135 * 16, 90 * 16);
        p.drawArc(QRectF(7, 7, 10, 10), -45 * 16, 90 * 16);
        p.drawArc(QRectF(3, 3, 18, 18), 135 * 16, 90 * 16);
        p.drawArc(QRectF(3, 3, 18, 18), -45 * 16, 90 * 16);
        break;
    case Name::Mirror:
    case Name::Flip: {
        p.save();
        if (n == Name::Flip) {
            p.translate(12, 12);
            p.rotate(90);
            p.translate(-12, -12);
        }
        QPen dash = pen;
        dash.setDashPattern({1.2, 1.6});
        p.setPen(dash);
        p.drawLine(QPointF(12, 3), QPointF(12, 21));
        p.setPen(pen);
        path.moveTo(9.5, 6);
        path.lineTo(3.5, 18);
        path.lineTo(9.5, 18);
        path.closeSubpath();
        p.drawPath(path);
        QPainterPath right;
        right.moveTo(14.5, 6);
        right.lineTo(20.5, 18);
        right.lineTo(14.5, 18);
        right.closeSubpath();
        p.setBrush(c);
        p.drawPath(right);
        p.restore();
        break;
    }
    case Name::ZoomIn:
    case Name::ZoomOut:
        p.drawEllipse(QPointF(10.5, 10.5), 6.5, 6.5);
        path.moveTo(15.3, 15.3);
        path.lineTo(20.5, 20.5);
        path.moveTo(7.5, 10.5);
        path.lineTo(13.5, 10.5);
        if (n == Name::ZoomIn) {
            path.moveTo(10.5, 7.5);
            path.lineTo(10.5, 13.5);
        }
        p.drawPath(path);
        break;
    case Name::Fit: // corner brackets
        for (int sx : {-1, 1})
            for (int sy : {-1, 1}) {
                const double x = 12 + sx * 8.5, y = 12 + sy * 7;
                path.moveTo(x, y - sy * 4);
                path.lineTo(x, y);
                path.lineTo(x - sx * 4, y);
            }
        p.drawPath(path);
        p.drawRect(QRectF(8.5, 9, 7, 6));
        break;
    case Name::Save: // arrow into tray
        path.moveTo(12, 3.5);
        path.lineTo(12, 14);
        path.moveTo(7.5, 9.5);
        path.lineTo(12, 14);
        path.lineTo(16.5, 9.5);
        path.moveTo(4, 15);
        path.lineTo(4, 20);
        path.lineTo(20, 20);
        path.lineTo(20, 15);
        p.drawPath(path);
        break;
    case Name::Manage: // slider list
        for (int i = 0; i < 3; ++i) {
            const double y = 6 + i * 6;
            const double knob = i == 1 ? 15 : (i == 0 ? 8 : 11);
            path.moveTo(3.5, y);
            path.lineTo(20.5, y);
            p.drawPath(path);
            path = QPainterPath();
            p.setBrush(QColor(0x26, 0x26, 0x2b));
            p.drawEllipse(QPointF(knob, y), 2.2, 2.2);
            p.setBrush(Qt::NoBrush);
        }
        break;
    case Name::Reset: // counter-clockwise arrow
        p.drawArc(QRectF(5, 5, 14, 14), 100 * 16, 290 * 16);
        path.moveTo(9.8, 2.6);
        path.lineTo(10.6, 5.6);
        path.lineTo(7.6, 6.6);
        p.drawPath(path);
        break;
    case Name::ChevronRight:
        path.moveTo(9.5, 6);
        path.lineTo(15.5, 12);
        path.lineTo(9.5, 18);
        p.drawPath(path);
        break;
    case Name::ChevronDown:
        path.moveTo(6, 9.5);
        path.lineTo(12, 15.5);
        path.lineTo(18, 9.5);
        p.drawPath(path);
        break;
    case Name::Draw: // dashed selection rectangle
    {
        QPen dash = pen;
        dash.setDashPattern({2.0, 1.6});
        p.setPen(dash);
        p.drawRect(QRectF(4, 5, 16, 14));
        break;
    }
    case Name::Pick: // eyedropper
        path.moveTo(14.5, 5.5);
        path.lineTo(18.5, 9.5);
        path.moveTo(16.5, 7.5);
        path.lineTo(6.5, 17.5);
        path.lineTo(4.5, 19.5);
        p.drawPath(path);
        p.drawEllipse(QPointF(17.5, 6.5), 2.2, 2.2);
        break;
    case Name::Remove:
        path.moveTo(6, 12);
        path.lineTo(18, 12);
        p.drawPath(path);
        break;
    }
}

class LineIconEngine : public QIconEngine {
public:
    explicit LineIconEngine(Name n) : m_name(n) {}

    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        QColor c = kNormal;
        if (mode == QIcon::Disabled)
            c = kDisabled;
        else if (state == QIcon::On)
            c = kChecked;
        else if (mode == QIcon::Active || mode == QIcon::Selected)
            c = kActive;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const qreal s = std::min(rect.width(), rect.height()) / 24.0;
        p->translate(rect.x() + (rect.width() - 24 * s) / 2, rect.y() + (rect.height() - 24 * s) / 2);
        p->scale(s, s);
        draw(*p, m_name, c);
        p->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pm(size * scale);
        pm.fill(Qt::transparent);
        pm.setDevicePixelRatio(scale);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine *clone() const override { return new LineIconEngine(m_name); }

private:
    Name m_name;
};

} // namespace

QIcon get(Name name) { return QIcon(new LineIconEngine(name)); }

QIcon led(const QColor &color)
{
    QIcon icon;
    for (int s : {12, 16, 24, 32}) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        QColor glow = color;
        glow.setAlpha(70);
        p.setBrush(glow);
        p.drawEllipse(QRectF(0.5, 0.5, s - 1, s - 1));
        p.setBrush(color);
        p.drawEllipse(QRectF(s * 0.22, s * 0.22, s * 0.56, s * 0.56));
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace ui::icons
