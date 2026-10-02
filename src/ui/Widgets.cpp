// SPDX-License-Identifier: GPL-3.0-or-later
#include "Widgets.h"

#include "Theme.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QEnterEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace ui {

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

    m_label = new QLabel(label);
    m_spin = new QDoubleSpinBox;
    m_spin->setObjectName(QStringLiteral("Value"));
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_spin->setRange(min * displayScale, max * displayScale);
    m_spin->setDecimals(decimals);
    m_spin->setSuffix(suffix);
    m_spin->setValue(def * displayScale);
    m_spin->setKeyboardTracking(false);
    m_spin->setFixedWidth(76);
    m_spin->setToolTip(tr("Click to type a value"));
    m_reset = new QToolButton;
    m_reset->setObjectName(QStringLiteral("ResetButton"));
    m_reset->setText(QStringLiteral("↺"));
    m_reset->setToolTip(tr("Reset to default"));
    m_reset->setFocusPolicy(Qt::NoFocus);
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(toSlider(min), toSlider(max));
    m_slider->setValue(toSlider(def));
    m_slider->setPageStep(std::max(1, (toSlider(max) - toSlider(min)) / 20));
    m_label->setBuddy(m_slider);

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(0, 2, 0, 4);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);
    grid->addWidget(m_label, 0, 0);
    grid->addWidget(m_reset, 0, 1);
    grid->addWidget(m_spin, 0, 2);
    grid->addWidget(m_slider, 1, 0, 1, 3);
    grid->setColumnStretch(0, 1);
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
    const double eps = 0.5 / m_scale;
    // Keep the space so the layout does not jump.
    QSizePolicy sp = m_reset->sizePolicy();
    sp.setRetainSizeWhenHidden(true);
    m_reset->setSizePolicy(sp);
    m_reset->setVisible(isEnabled() && std::fabs(m_value - m_default) > eps);
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
// ToggleSwitch

ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

QSize ToggleSwitch::sizeHint() const { return QSize(40, 22); }

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal h = std::min<qreal>(r.height(), 22);
    QRectF track(r.x(), r.center().y() - h / 2, std::min<qreal>(r.width(), 40), h);
    QColor trackColor = isChecked() ? theme::kAccent : QColor(0x34, 0x3d, 0x51);
    if (!isEnabled())
        trackColor = trackColor.darker(160);
    else if (m_hover)
        trackColor = trackColor.lighter(115);
    p.setPen(Qt::NoPen);
    p.setBrush(trackColor);
    p.drawRoundedRect(track, h / 2, h / 2);
    const qreal d = h - 6;
    const qreal x = isChecked() ? track.right() - 3 - d : track.left() + 3;
    p.setBrush(isEnabled() ? QColor(Qt::white) : QColor(0x8a, 0x93, 0xa6));
    p.drawEllipse(QRectF(x, track.top() + 3, d, d));
    if (hasFocus()) {
        p.setPen(QPen(theme::kAccent.lighter(130), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(track.adjusted(-1, -1, 1, 1), h / 2 + 1, h / 2 + 1);
    }
}

void ToggleSwitch::enterEvent(QEnterEvent *e)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(e);
}

void ToggleSwitch::leaveEvent(QEvent *e)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(e);
}

// ---------------------------------------------------------------------------
// ToggleRow

ToggleRow::ToggleRow(const QString &label, const QString &description, QWidget *parent) : QWidget(parent)
{
    auto *h = new QHBoxLayout(this);
    h->setContentsMargins(0, 3, 0, 3);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(1);
    auto *title = new QLabel(label);
    texts->addWidget(title);
    if (!description.isEmpty()) {
        auto *d = hintLabel(description);
        texts->addWidget(d);
    }
    h->addLayout(texts, 1);
    m_switch = new ToggleSwitch;
    h->addWidget(m_switch, 0, Qt::AlignVCenter);
    title->setBuddy(m_switch);
    connect(m_switch, &ToggleSwitch::toggled, this, &ToggleRow::toggled);
}

bool ToggleRow::isChecked() const { return m_switch->isChecked(); }

void ToggleRow::setChecked(bool on)
{
    QSignalBlocker b(m_switch);
    m_switch->setChecked(on);
    m_switch->update();
}

// ---------------------------------------------------------------------------
// SegmentedControl

SegmentedControl::SegmentedControl(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("Segmented"));
    setAttribute(Qt::WA_StyledBackground);
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(2, 2, 2, 2);
    m_layout->setSpacing(2);
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);
    connect(m_group, &QButtonGroup::idClicked, this, &SegmentedControl::changed);
}

void SegmentedControl::addSegment(const QString &text, int data, const QString &tooltip)
{
    auto *b = new QPushButton(text);
    b->setObjectName(QStringLiteral("Segment"));
    b->setCheckable(true);
    b->setToolTip(tooltip);
    b->setFocusPolicy(Qt::TabFocus);
    m_group->addButton(b, data);
    m_layout->addWidget(b, 1);
    updateShapes();
}

void SegmentedControl::setSegmentVisible(int data, bool visible)
{
    if (auto *b = m_group->button(data)) {
        b->setVisible(visible);
        updateShapes();
    }
}

void SegmentedControl::updateShapes()
{
    // The pill-style group needs no per-button shapes; kept for layout updates.
    updateGeometry();
}

int SegmentedControl::currentData() const { return m_group->checkedId(); }

void SegmentedControl::setCurrentData(int data)
{
    if (auto *b = m_group->button(data)) {
        QSignalBlocker block(m_group);
        b->setChecked(true);
    }
}

// ---------------------------------------------------------------------------
// OptionCard

OptionCard::OptionCard(const QString &title, const QString &description, QWidget *parent)
    : QAbstractButton(parent), m_description(description)
{
    setText(title);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize OptionCard::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(240, fm.height() * 2 + 22);
}

void OptionCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QColor bg = isChecked() ? QColor(0x1d, 0x34, 0x56) : (m_hover ? QColor(0x24, 0x2c, 0x3d) : theme::kRaised);
    p.setPen(QPen(isChecked() ? theme::kAccent : theme::kBorder, 1));
    p.setBrush(bg);
    p.drawRoundedRect(r, 9, 9);

    // Radio indicator.
    const qreal d = 14;
    QRectF dot(r.left() + 12, r.center().y() - d / 2, d, d);
    p.setPen(QPen(isChecked() ? theme::kAccent : theme::kMuted, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(dot);
    if (isChecked()) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme::kAccent);
        p.drawEllipse(dot.adjusted(3.5, 3.5, -3.5, -3.5));
    }

    const QFontMetrics fm(font());
    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    const qreal tx = dot.right() + 12;
    const qreal top = r.center().y() - fm.height();
    p.setFont(bold);
    p.setPen(theme::kText);
    p.drawText(QRectF(tx, top, r.width() - tx - 8, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, text());
    p.setFont(font());
    p.setPen(theme::kMuted);
    p.drawText(QRectF(tx, top + fm.height() + 1, r.width() - tx - 8, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(m_description, Qt::ElideRight, int(r.width() - tx - 8)));
}

void OptionCard::enterEvent(QEnterEvent *e)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(e);
}

void OptionCard::leaveEvent(QEvent *e)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(e);
}

// ---------------------------------------------------------------------------
// Section

Section::Section(const QString &title, bool collapsible, QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 6, 0, 6);
    outer->setSpacing(4);
    if (collapsible) {
        m_toggle = new QToolButton;
        m_toggle->setText(title);
        m_toggle->setCheckable(true);
        m_toggle->setChecked(false);
        m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_toggle->setArrowType(Qt::RightArrow);
        m_toggle->setStyleSheet(QStringLiteral("QToolButton { color: #8a93a6; font-weight: 600; padding-left: 0; }"
                                               "QToolButton:hover { color: #e7eaf0; background: transparent; }"
                                               "QToolButton:checked { background: transparent; }"));
        outer->addWidget(m_toggle, 0, Qt::AlignLeft);
        connect(m_toggle, &QToolButton::toggled, this, &Section::setExpanded);
    } else if (!title.isEmpty()) {
        auto *label = new QLabel(title.toUpper());
        label->setObjectName(QStringLiteral("SectionTitle"));
        outer->addWidget(label);
    }
    m_body = new QWidget;
    m_layout = new QVBoxLayout(m_body);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);
    outer->addWidget(m_body);
    if (collapsible)
        m_body->setVisible(false);
}

void Section::setExpanded(bool expanded)
{
    if (!m_toggle)
        return;
    {
        QSignalBlocker b(m_toggle);
        m_toggle->setChecked(expanded);
    }
    m_toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    m_body->setVisible(expanded);
    Q_EMIT expandedChanged(expanded);
}

bool Section::isExpanded() const { return !m_toggle || m_toggle->isChecked(); }

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
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(255, 255, 255, 80), 1));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(0.5, 0.5, 17, 17), 4, 4);
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

} // namespace ui
