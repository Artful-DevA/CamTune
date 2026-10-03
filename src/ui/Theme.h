// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QObject>

class QApplication;

namespace ui::theme {

// Neutral studio palette; the logo blue is used only as the accent.
inline const QColor kWindow{0x1e, 0x1e, 0x22};
inline const QColor kPanel{0x26, 0x26, 0x2b};
inline const QColor kHeader{0x2d, 0x2d, 0x33};
inline const QColor kInput{0x19, 0x19, 0x1d};
inline const QColor kBorder{0x3a, 0x3a, 0x42};
inline const QColor kText{0xdc, 0xdc, 0xe0};
inline const QColor kMuted{0x96, 0x96, 0xa0};
inline const QColor kAccent{0x3d, 0x8b, 0xff};
inline const QColor kSuccess{0x3f, 0xc0, 0x7a};
inline const QColor kWarning{0xf0, 0xb0, 0x45};
inline const QColor kDanger{0xf0, 0x5a, 0x5a};

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
