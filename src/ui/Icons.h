// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QIcon>

namespace ui::icons {

enum class Name {
    Picture,
    Framing,
    Background,
    Camera,
    Output,
    Mirror,
    Flip,
    ZoomIn,
    ZoomOut,
    Fit,
    Save,
    Manage,
    Reset,
    ChevronRight,
    ChevronDown,
    Draw,
    Pick,
    Remove,
};

// Crisp line icons drawn at any size and device pixel ratio, coloured for the
// dark theme (normal / active / checked / disabled).
QIcon get(Name name);

// A small round status light.
QIcon led(const QColor &color);

} // namespace ui::icons
