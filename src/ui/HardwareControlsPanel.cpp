// SPDX-License-Identifier: GPL-3.0-or-later
#include "HardwareControlsPanel.h"

#include "Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <linux/videodev2.h>

#include <climits>
#include <cmath>

namespace ui {

using cam::v4l2::ControlInfo;

namespace {

// Display order inside the Camera group.
int cameraOrder(uint32_t id)
{
    static const uint32_t order[] = {
        V4L2_CID_EXPOSURE_AUTO,      V4L2_CID_EXPOSURE_ABSOLUTE, V4L2_CID_EXPOSURE,
        V4L2_CID_EXPOSURE_AUTO_PRIORITY, V4L2_CID_AUTO_EXPOSURE_BIAS, V4L2_CID_AUTOGAIN,
        V4L2_CID_GAIN,               V4L2_CID_FOCUS_AUTO,        V4L2_CID_FOCUS_ABSOLUTE,
        V4L2_CID_AUTO_WHITE_BALANCE, V4L2_CID_WHITE_BALANCE_TEMPERATURE, V4L2_CID_BACKLIGHT_COMPENSATION,
        V4L2_CID_POWER_LINE_FREQUENCY, V4L2_CID_ZOOM_ABSOLUTE,   V4L2_CID_PAN_ABSOLUTE,
        V4L2_CID_TILT_ABSOLUTE,      V4L2_CID_BRIGHTNESS,        V4L2_CID_CONTRAST,
        V4L2_CID_SATURATION,         V4L2_CID_HUE,               V4L2_CID_GAMMA,
        V4L2_CID_SHARPNESS,
    };
    for (size_t i = 0; i < sizeof order / sizeof order[0]; ++i)
        if (order[i] == id)
            return int(i);
    return 1000;
}

QString friendlyName(const ControlInfo &c)
{
    switch (c.id) {
    case V4L2_CID_EXPOSURE_AUTO: return QObject::tr("Exposure");
    case V4L2_CID_EXPOSURE_ABSOLUTE: return QObject::tr("Exposure time");
    case V4L2_CID_EXPOSURE_AUTO_PRIORITY: return QObject::tr("Lower frame rate in dim light");
    case V4L2_CID_FOCUS_AUTO: return QObject::tr("Autofocus");
    case V4L2_CID_FOCUS_ABSOLUTE: return QObject::tr("Focus");
    case V4L2_CID_AUTO_WHITE_BALANCE: return QObject::tr("Auto white balance");
    case V4L2_CID_WHITE_BALANCE_TEMPERATURE: return QObject::tr("White balance (K)");
    case V4L2_CID_AUTOGAIN: return QObject::tr("Auto gain");
    case V4L2_CID_POWER_LINE_FREQUENCY: return QObject::tr("Anti-flicker (mains)");
    case V4L2_CID_BACKLIGHT_COMPENSATION: return QObject::tr("Backlight compensation");
    case V4L2_CID_ZOOM_ABSOLUTE: return QObject::tr("Optical zoom");
    default: return QString::fromStdString(c.name);
    }
}

} // namespace

HardwareControlsPanel::HardwareControlsPanel(QWidget *parent) : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_empty = new QLabel(tr("This camera has no adjustable settings, or none is connected."));
    m_empty->setObjectName(QStringLiteral("Muted"));
    m_empty->setWordWrap(true);
    m_layout->addWidget(m_empty);
}

void HardwareControlsPanel::setControls(const std::vector<ControlInfo> &controls)
{
    // The structure only changes when a different camera is connected.
    QString sig;
    for (const auto &c : controls)
        sig += QString::number(c.id) + QLatin1Char(':') + QString::number(int(c.type)) + QLatin1Char(':') +
               QString::number(c.minimum) + QLatin1Char(':') + QString::number(c.maximum) + QLatin1Char(';');
    if (sig != m_signature) {
        m_signature = sig;
        rebuild(controls);
        return;
    }
    for (const auto &c : controls) {
        auto it = m_entries.find(c.id);
        if (it != m_entries.end())
            updateEntry(*it, c);
    }
}

void HardwareControlsPanel::rebuild(const std::vector<ControlInfo> &controls)
{
    delete m_content;
    m_content = nullptr;
    m_entries.clear();
    m_empty->setVisible(controls.empty());
    if (controls.empty())
        return;

    m_content = new QWidget;
    auto *v = new QVBoxLayout(m_content);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    std::vector<ControlInfo> camera, image, advanced;
    for (const auto &c : controls) {
        switch (cam::v4l2::controlGroup(c.id)) {
        case cam::v4l2::ControlGroup::Camera: camera.push_back(c); break;
        case cam::v4l2::ControlGroup::Color: image.push_back(c); break;
        default: advanced.push_back(c); break;
        }
    }
    auto byOrder = [](const ControlInfo &a, const ControlInfo &b) { return cameraOrder(a.id) < cameraOrder(b.id); };
    std::stable_sort(camera.begin(), camera.end(), byOrder);
    std::stable_sort(image.begin(), image.end(), byOrder);

    auto addGroup = [&](const QString &title, const std::vector<ControlInfo> &list, bool collapsible) {
        if (list.empty())
            return;
        auto *sec = new Section(title, collapsible);
        for (const auto &c : list)
            addControl(sec->contentLayout(), c);
        v->addWidget(sec);
    };
    addGroup(tr("Exposure, focus & white balance"), camera, false);
    addGroup(tr("Image (set in the camera)"), image, false);
    addGroup(tr("More camera settings"), advanced, true);

    auto *reset = new QPushButton(tr("Reset camera settings"));
    connect(reset, &QPushButton::clicked, this, &HardwareControlsPanel::resetRequested);
    v->addSpacing(6);
    v->addWidget(reset);
    m_layout->addWidget(m_content);
}

namespace {

// Driver menu entries are often cryptic ("Aperture Priority Mode").
QString friendlyMenuItem(uint32_t id, const QString &name)
{
    if (id == V4L2_CID_EXPOSURE_AUTO) {
        if (name.startsWith(QLatin1String("Manual")))
            return QObject::tr("Manual");
        if (name.startsWith(QLatin1String("Aperture")) || name.startsWith(QLatin1String("Auto")))
            return QObject::tr("Automatic");
        if (name.startsWith(QLatin1String("Shutter")))
            return QObject::tr("Automatic (fixed shutter)");
    }
    return name;
}

} // namespace

void HardwareControlsPanel::addControl(QVBoxLayout *layout, const ControlInfo &c)
{
    Entry e;
    e.info = c;
    const QString name = friendlyName(c);
    const quint32 id = c.id;
    switch (c.type) {
    case ControlInfo::Type::Integer: {
        // Very large ranges still work because the slider uses raw integers.
        const double lo = double(std::max<int64_t>(c.minimum, INT32_MIN / 2));
        const double hi = double(std::min<int64_t>(c.maximum, INT32_MAX / 2));
        e.slider = new SliderRow(name, lo, hi, double(c.defaultValue), 0);
        layout->addWidget(e.slider);
        connect(e.slider, &SliderRow::valueChanged, this, [this, id, step = std::max<int64_t>(1, c.step),
                                                          min = c.minimum](double v) {
            // Snap to the control's step so drivers don't reject the value.
            qint64 iv = qint64(std::llround(v));
            iv = min + ((iv - min) / step) * step;
            Q_EMIT controlChanged(id, iv);
        });
        break;
    }
    case ControlInfo::Type::Boolean:
        e.toggle = new ToggleRow(name);
        layout->addWidget(e.toggle);
        connect(e.toggle, &ToggleRow::toggled, this, [this, id](bool on) { Q_EMIT controlChanged(id, on ? 1 : 0); });
        break;
    case ControlInfo::Type::Menu:
    case ControlInfo::Type::IntegerMenu: {
        auto *row = new QWidget;
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 2, 0, 2);
        e.label = new QLabel(name);
        e.combo = new QComboBox;
        for (const auto &m : c.menu)
            e.combo->addItem(friendlyMenuItem(c.id, QString::fromStdString(m.second)), qint64(m.first));
        h->addWidget(e.label, 1);
        h->addWidget(e.combo);
        layout->addWidget(row);
        connect(e.combo, qOverload<int>(&QComboBox::activated), this, [this, id, combo = e.combo](int idx) {
            Q_EMIT controlChanged(id, combo->itemData(idx).toLongLong());
        });
        break;
    }
    case ControlInfo::Type::Button:
        e.button = new QPushButton(name);
        layout->addWidget(e.button);
        connect(e.button, &QPushButton::clicked, this, [this, id] { Q_EMIT controlChanged(id, 1); });
        break;
    }
    m_entries.insert(id, e);
    updateEntry(m_entries[id], c);
}

void HardwareControlsPanel::updateEntry(Entry &e, const ControlInfo &c)
{
    e.info = c;
    const bool enabled = !c.readOnly && !c.inactive;
    const QString tip = c.inactive ? tr("Set automatically by the camera — switch its automatic mode off to adjust")
                                   : QString();
    if (e.slider) {
        e.slider->setValue(double(c.value));
        e.slider->setEnabled(enabled);
        e.slider->setHint(tip);
    }
    if (e.toggle) {
        e.toggle->setChecked(c.value != 0);
        e.toggle->setEnabled(enabled);
        e.toggle->setToolTip(tip);
    }
    if (e.combo) {
        QSignalBlocker b(e.combo);
        int idx = e.combo->findData(qint64(c.value));
        if (idx >= 0)
            e.combo->setCurrentIndex(idx);
        e.combo->setEnabled(enabled);
        e.label->setEnabled(enabled);
    }
    if (e.button)
        e.button->setEnabled(enabled);
}

} // namespace ui
