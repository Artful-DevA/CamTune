// SPDX-License-Identifier: GPL-3.0-or-later
#include "Widgets.h"

#include "Icons.h"

#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace ui {

namespace {

// Label for the property column; long names are elided with the full text in
// the tooltip.
QLabel *propertyLabel(const QString &text)
{
    auto *l = new QLabel;
    l->setObjectName(QStringLiteral("PropertyLabel"));
    l->setFixedWidth(kLabelWidth);
    const QString elided = l->fontMetrics().elidedText(text, Qt::ElideRight, kLabelWidth - 4);
    l->setText(elided);
    if (elided != text)
        l->setToolTip(text);
    return l;
}

} // namespace

// ---------------------------------------------------------------------------
// SliderRow

SliderRow::SliderRow(const QString &label, double min, double max, double def, int decimals,
                     const QString &suffix, double displayScale, QWidget *parent)
    : QWidget(parent), m_min(min), m_max(max), m_default(def), m_value(def), m_displayScale(displayScale)
{
    // Slider resolution: fine enough for smooth dragging regardless of how the
    // value is displayed.
    m_scale = std::pow(10.0, decimals) * displayScale;
    if (m_scale * (max - min) < 200)
        m_scale = 200 / (max - min);

    m_label = propertyLabel(label);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(toSlider(min), toSlider(max));
    m_slider->setValue(toSlider(def));
    m_slider->setPageStep(std::max(1, (toSlider(max) - toSlider(min)) / 20));
    m_slider->setMinimumWidth(80);
    m_spin = new QDoubleSpinBox;
    m_spin->setObjectName(QStringLiteral("Value"));
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_spin->setRange(min * displayScale, max * displayScale);
    m_spin->setDecimals(decimals);
    m_spin->setSuffix(suffix);
    m_spin->setValue(def * displayScale);
    m_spin->setKeyboardTracking(false);
    m_spin->setFixedWidth(64);
    m_reset = new QToolButton;
    m_reset->setObjectName(QStringLiteral("ResetButton"));
    m_reset->setIcon(icons::get(icons::Name::Reset));
    m_reset->setIconSize(QSize(14, 14));
    m_reset->setToolTip(tr("Reset to default"));
    m_reset->setFocusPolicy(Qt::NoFocus);
    QSizePolicy sp = m_reset->sizePolicy();
    sp.setRetainSizeWhenHidden(true);
    m_reset->setSizePolicy(sp);
    m_label->setBuddy(m_slider);

    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 1, 0, 1);
    h->setSpacing(6);
    h->addWidget(m_label);
    h->addWidget(m_slider, 1);
    h->addWidget(m_spin);
    h->addWidget(m_reset);
    updateReset();

    connect(m_slider, &QSlider::valueChanged, this, [this](int s) {
        if (!m_updating)
            apply(fromSlider(s), true);
    });
    connect(m_spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (!m_updating)
            apply(v / m_displayScale, true);
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
    if (std::fabs(m_spin->value() - v * m_displayScale) > 0.5 * std::pow(10.0, -m_spin->decimals()))
        m_spin->setValue(v * m_displayScale);
    m_updating = false;
    const bool changed = v != m_value;
    m_value = v;
    updateReset();
    if (emitSignal && changed)
        Q_EMIT valueChanged(v);
}

void SliderRow::updateReset()
{
    m_reset->setVisible(isEnabled() && std::fabs(m_value - m_default) > 0.5 / m_scale);
}

void SliderRow::setValue(double v)
{
    if (isDragging())
        return; // never fight the user
    apply(v, false);
}

bool SliderRow::isDragging() const { return m_slider->isSliderDown(); }

void SliderRow::setHint(const QString &tip)
{
    if (!tip.isEmpty() || m_label->toolTip().isEmpty())
        m_label->setToolTip(tip);
    m_slider->setToolTip(tip);
}

void SliderRow::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::EnabledChange)
        updateReset();
}

// ---------------------------------------------------------------------------

QWidget *propertyRow(const QString &label, QWidget *field, QWidget *extra)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 1, 0, 1);
    h->setSpacing(6);
    auto *l = propertyLabel(label);
    l->setBuddy(field);
    h->addWidget(l);
    h->addWidget(field, 1);
    if (extra)
        h->addWidget(extra);
    return w;
}

// ---------------------------------------------------------------------------
// Section

Section::Section(const QString &title, bool expanded, QWidget *parent) : QWidget(parent), m_expanded(expanded)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *headerRow = new QWidget;
    m_headerLayout = new QHBoxLayout(headerRow);
    m_headerLayout->setContentsMargins(0, 0, 0, 0);
    m_headerLayout->setSpacing(0);
    m_header = new QToolButton;
    m_header->setObjectName(QStringLiteral("SectionHeader"));
    m_header->setText(title);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setIconSize(QSize(12, 12));
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_header->setFocusPolicy(Qt::TabFocus);
    m_headerLayout->addWidget(m_header, 1);
    outer->addWidget(headerRow);

    m_body = new QWidget;
    m_layout = new QVBoxLayout(m_body);
    m_layout->setContentsMargins(10, 6, 6, 8);
    m_layout->setSpacing(2);
    outer->addWidget(m_body);

    connect(m_header, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); });
    setExpanded(expanded);
}

void Section::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_header->setIcon(icons::get(expanded ? icons::Name::ChevronDown : icons::Name::ChevronRight));
    m_body->setVisible(expanded);
}

QToolButton *Section::addHeaderAction(const QIcon &icon, const QString &tooltip, std::function<void()> fn)
{
    auto *b = new QToolButton;
    b->setObjectName(QStringLiteral("SectionHeader"));
    b->setIcon(icon);
    b->setIconSize(QSize(14, 14));
    b->setToolTip(tooltip);
    b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_headerLayout->addWidget(b);
    connect(b, &QToolButton::clicked, this, [fn = std::move(fn)] { fn(); });
    return b;
}

// ---------------------------------------------------------------------------
// ColorButton

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
    QPixmap pm(16, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(QPen(QColor(255, 255, 255, 90), 1));
    p.setBrush(c);
    p.drawRect(QRectF(0.5, 0.5, 15, 11));
    p.end();
    setIcon(QIcon(pm));
    setText(c.name().toUpper());
}

QLabel *hintLabel(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("Hint"));
    l->setWordWrap(true);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * 0.92);
    l->setFont(f);
    return l;
}

QToolButton *iconButton(const QIcon &icon, const QString &tooltip, bool checkable)
{
    auto *b = new QToolButton;
    b->setIcon(icon);
    b->setIconSize(QSize(18, 18));
    b->setToolTip(tooltip);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    return b;
}

} // namespace ui
