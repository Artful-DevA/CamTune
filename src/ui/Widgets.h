// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QPushButton>
#include <QWidget>

#include <functional>

class QDoubleSpinBox;
class QHBoxLayout;
class QLabel;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace ui {

// Width of the label column in the inspector, so all properties line up.
constexpr int kLabelWidth = 96;

// One inspector property: label · slider · value field · reset.
//
// Values are stored in model units; `displayScale` converts them for display
// (e.g. 0..2.5 shown as 0..250 %). The reset button only shows once the value
// differs from its default.
class SliderRow : public QWidget {
    Q_OBJECT
public:
    SliderRow(const QString &label, double min, double max, double defaultValue, int decimals,
              const QString &suffix = QString(), double displayScale = 1.0, QWidget *parent = nullptr);

    double value() const { return m_value; }
    // Updates the widgets without emitting valueChanged (ignored while dragging).
    void setValue(double v);
    bool isDragging() const;
    void setHint(const QString &tip);

Q_SIGNALS:
    void valueChanged(double value);

protected:
    void changeEvent(QEvent *e) override;

private:
    int toSlider(double v) const;
    double fromSlider(int s) const;
    void apply(double v, bool emitSignal);
    void updateReset();

    QLabel *m_label;
    QSlider *m_slider;
    QDoubleSpinBox *m_spin;
    QToolButton *m_reset;
    double m_min, m_max, m_default, m_value, m_scale, m_displayScale;
    bool m_updating = false;
};

// A label in the property column next to any field widget (combo, checkbox...).
QWidget *propertyRow(const QString &label, QWidget *field, QWidget *extra = nullptr);

// A collapsible inspector group with a header bar.
class Section : public QWidget {
    Q_OBJECT
public:
    explicit Section(const QString &title, bool expanded = true, QWidget *parent = nullptr);
    QVBoxLayout *contentLayout() const { return m_layout; }
    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }
    // Adds a small button at the right of the header (e.g. "reset group").
    QToolButton *addHeaderAction(const QIcon &icon, const QString &tooltip, std::function<void()> fn);

private:
    QToolButton *m_header;
    QHBoxLayout *m_headerLayout;
    QWidget *m_body;
    QVBoxLayout *m_layout;
    bool m_expanded = true;
};

// A button showing and picking a color.
class ColorButton : public QPushButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget *parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor &c);

Q_SIGNALS:
    void colorChanged(const QColor &c);

private:
    QColor m_color;
};

// Small muted explanatory text.
QLabel *hintLabel(const QString &text);

// A compact icon-only tool button.
QToolButton *iconButton(const QIcon &icon, const QString &tooltip, bool checkable = false);

} // namespace ui
