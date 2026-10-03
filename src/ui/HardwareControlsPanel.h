// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "v4l2/CameraControls.h"

#include <QHash>
#include <QWidget>

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace ui {

class Section;
class SliderRow;

// Generated UI for whatever V4L2/UVC controls the camera exposes. Known
// controls are grouped (exposure, focus, gain, white balance, image); the rest
// go to "More controls". Controls governed by an automatic mode are disabled
// while that mode is on.
class HardwareControlsPanel : public QWidget {
    Q_OBJECT
public:
    explicit HardwareControlsPanel(QWidget *parent = nullptr);

    // Rebuilds when the set of controls changes; otherwise only updates values.
    void setControls(const std::vector<cam::v4l2::ControlInfo> &controls);

Q_SIGNALS:
    void controlChanged(quint32 id, qint64 value);
    void resetRequested();

private:
    struct Entry {
        cam::v4l2::ControlInfo info;
        SliderRow *slider = nullptr;
        QCheckBox *toggle = nullptr;
        QComboBox *combo = nullptr;
        QPushButton *button = nullptr;
        QWidget *row = nullptr; // whole property row (enabled/disabled together)
    };
    void rebuild(const std::vector<cam::v4l2::ControlInfo> &controls);
    void addControl(QVBoxLayout *layout, const cam::v4l2::ControlInfo &c);
    void updateEntry(Entry &e, const cam::v4l2::ControlInfo &c);

    QVBoxLayout *m_layout;
    QWidget *m_content = nullptr;
    QLabel *m_empty;
    QHash<quint32, Entry> m_entries;
    QString m_signature;
};

} // namespace ui
