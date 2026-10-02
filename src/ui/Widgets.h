// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QPushButton>
#include <QWidget>

class QButtonGroup;
class QDoubleSpinBox;
class QHBoxLayout;
class QLabel;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace ui {

// A labelled slider: name and value on one line, the slider below, and a
// small reset button that only appears once the value differs from its default.
//
// Values are stored in model units; `displayScale` converts them for display
// (e.g. 0..2.5 shown as 0..250 %).
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

// An on/off switch.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    bool m_hover = false;
};

// Label (with optional description) on the left, switch on the right.
class ToggleRow : public QWidget {
    Q_OBJECT
public:
    ToggleRow(const QString &label, const QString &description = QString(), QWidget *parent = nullptr);
    ToggleSwitch *toggle() const { return m_switch; }
    bool isChecked() const;
    // Updates the switch without emitting toggled.
    void setChecked(bool on);

Q_SIGNALS:
    void toggled(bool on);

private:
    ToggleSwitch *m_switch;
};

// A row of mutually exclusive buttons ("segmented control").
class SegmentedControl : public QWidget {
    Q_OBJECT
public:
    explicit SegmentedControl(QWidget *parent = nullptr);
    void addSegment(const QString &text, int data, const QString &tooltip = QString());
    void setSegmentVisible(int data, bool visible);
    int currentData() const;
    // Selects without emitting changed.
    void setCurrentData(int data);

Q_SIGNALS:
    void changed(int data);

private:
    void updateShapes();

    QHBoxLayout *m_layout;
    QButtonGroup *m_group;
};

// A selectable card with a title and a one-line description.
class OptionCard : public QAbstractButton {
    Q_OBJECT
public:
    OptionCard(const QString &title, const QString &description, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QString m_description;
    bool m_hover = false;
};

// A titled group whose content can be collapsed.
class Section : public QWidget {
    Q_OBJECT
public:
    explicit Section(const QString &title, bool collapsible = false, QWidget *parent = nullptr);
    QVBoxLayout *contentLayout() const { return m_layout; }
    void setExpanded(bool expanded);
    bool isExpanded() const;

Q_SIGNALS:
    void expandedChanged(bool expanded);

private:
    QToolButton *m_toggle = nullptr;
    QWidget *m_body;
    QVBoxLayout *m_layout;
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

} // namespace ui
