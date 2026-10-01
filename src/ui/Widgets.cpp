// SPDX-License-Identifier: GPL-3.0-or-later
#include "Widgets.h"

#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace ui {

SliderRow::SliderRow(QGridLayout *grid, int row, const QString &label, double min, double max, double def,
                     int decimals, const QString &suffix, QWidget *parent)
    : QObject(parent ? parent : grid->parentWidget()), m_min(min), m_max(max), m_default(def), m_value(def)
{
    m_scale = std::pow(10.0, decimals);
    m_label = new QLabel(label);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(toSlider(min), toSlider(max));
    m_slider->setValue(toSlider(def));
    m_slider->setMinimumWidth(120);
    m_slider->setFocusPolicy(Qt::StrongFocus);
    // Page steps of ~5% make keyboard adjustment useful.
    m_slider->setPageStep(std::max(1, (toSlider(max) - toSlider(min)) / 20));
    m_spin = new QDoubleSpinBox;
    m_spin->setRange(min, max);
    m_spin->setDecimals(decimals);
    m_spin->setSingleStep(decimals > 0 ? std::pow(10.0, -std::min(decimals, 2)) : 1.0);
    m_spin->setValue(def);
    m_spin->setSuffix(suffix);
    m_spin->setKeyboardTracking(false);
    m_spin->setMinimumWidth(78);
    m_reset = new QToolButton;
    m_reset->setText(QStringLiteral("↺"));
    m_reset->setToolTip(tr("Reset to default"));
    m_reset->setAutoRaise(true);
    m_label->setBuddy(m_slider);

    grid->addWidget(m_label, row, 0);
    grid->addWidget(m_slider, row, 1);
    grid->addWidget(m_spin, row, 2);
    grid->addWidget(m_reset, row, 3);

    connect(m_slider, &QSlider::valueChanged, this, [this](int s) {
        if (!m_updating)
            apply(fromSlider(s), true);
    });
    connect(m_spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (!m_updating)
            apply(v, true);
    });
    connect(m_reset, &QToolButton::clicked, this, [this] { apply(m_default, true); });
}

int SliderRow::toSlider(double v) const { return int(std::lround(v * m_scale)); }
double SliderRow::fromSlider(int s) const { return s / m_scale; }

void SliderRow::apply(double v, bool emitSignal)
{
    v = std::clamp(v, m_min, m_max);
    m_updating = true;
    if (m_slider->value() != toSlider(v))
        m_slider->setValue(toSlider(v));
    if (std::fabs(m_spin->value() - v) > 0.5 / m_scale)
        m_spin->setValue(v);
    m_updating = false;
    const bool changed = v != m_value;
    m_value = v;
    if (emitSignal && changed)
        Q_EMIT valueChanged(v);
}

void SliderRow::setValue(double v)
{
    // Never fight the user while they are dragging.
    if (isDragging())
        return;
    apply(v, false);
}

void SliderRow::setEnabled(bool enabled)
{
    m_slider->setEnabled(enabled);
    m_spin->setEnabled(enabled);
    m_reset->setEnabled(enabled);
    m_label->setEnabled(enabled);
}

void SliderRow::setToolTip(const QString &tip)
{
    m_label->setToolTip(tip);
    m_slider->setToolTip(tip);
}

void SliderRow::setVisible(bool visible)
{
    m_label->setVisible(visible);
    m_slider->setVisible(visible);
    m_spin->setVisible(visible);
    m_reset->setVisible(visible);
}

bool SliderRow::isDragging() const { return m_slider->isSliderDown(); }

Section::Section(const QString &title, QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(2);
    m_header = new QHBoxLayout;
    m_toggle = new QToolButton;
    m_toggle->setText(title);
    m_toggle->setCheckable(true);
    m_toggle->setChecked(true);
    m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggle->setArrowType(Qt::DownArrow);
    m_toggle->setAutoRaise(true);
    QFont f = m_toggle->font();
    f.setBold(true);
    m_toggle->setFont(f);
    m_header->addWidget(m_toggle);
    m_header->addStretch(1);
    outer->addLayout(m_header);

    m_body = new QWidget;
    m_layout = new QVBoxLayout(m_body);
    m_layout->setContentsMargins(12, 0, 4, 8);
    outer->addWidget(m_body);

    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    outer->addWidget(line);

    connect(m_toggle, &QToolButton::toggled, this, &Section::setExpanded);
}

void Section::setExpanded(bool expanded)
{
    m_toggle->setChecked(expanded);
    m_toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    m_body->setVisible(expanded);
}

bool Section::isExpanded() const { return m_toggle->isChecked(); }

void Section::addHeaderWidget(QWidget *w) { m_header->addWidget(w); }

ColorButton::ColorButton(QWidget *parent) : QPushButton(parent)
{
    setColor(Qt::black);
    connect(this, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(m_color, this, tr("Choose color"));
        if (c.isValid()) {
            setColor(c);
            Q_EMIT colorChanged(c);
        }
    });
}

void ColorButton::setColor(const QColor &c)
{
    m_color = c;
    QPixmap pm(28, 14);
    pm.fill(c);
    QPainter p(&pm);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    p.end();
    setIcon(QIcon(pm));
    setText(c.name().toUpper());
}

} // namespace ui
