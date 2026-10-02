// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>

class QApplication;

namespace ui::theme {

// Palette taken from the CamTune logo.
inline const QColor kWindow{0x10, 0x14, 0x1b};
inline const QColor kSurface{0x17, 0x1c, 0x26};
inline const QColor kRaised{0x20, 0x27, 0x37};
inline const QColor kBorder{0x2a, 0x32, 0x43};
inline const QColor kText{0xe7, 0xea, 0xf0};
inline const QColor kMuted{0x8a, 0x93, 0xa6};
inline const QColor kAccent{0x3d, 0x8b, 0xff};
inline const QColor kOrange{0xff, 0x8a, 0x2b};
inline const QColor kSuccess{0x34, 0xc7, 0x7b};
inline const QColor kDanger{0xff, 0x5d, 0x5d};

// Applies the Fusion style, a dark palette and the stylesheet, and installs
// the scroll-wheel guard.
void apply(QApplication &app);

// Stops the mouse wheel from changing sliders, spin boxes and combo boxes the
// user has not clicked: the wheel scrolls the surrounding panel instead.
class WheelGuard : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

} // namespace ui::theme
