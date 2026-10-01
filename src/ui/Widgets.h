// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QPushButton>
#include <QWidget>

class QDoubleSpinBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace ui {

// Label + slider + spin box + reset button on one grid row.
//
// The slider emits continuously while dragging; the pipeline picks up the
// latest value on the next frame, so dragging is smooth and never queues.
class SliderRow : public QObject {
    Q_OBJECT
public:
    SliderRow(QGridLayout *grid, int row, const QString &label, double min, double max, double defaultValue,
              int decimals, const QString &suffix = QString(), QWidget *parent = nullptr);

    double value() const { return m_value; }
    // Updates the widgets without emitting valueChanged.
    void setValue(double v);
    void setEnabled(bool enabled);
    void setToolTip(const QString &tip);
    bool isDragging() const;
    void setVisible(bool visible);

Q_SIGNALS:
    void valueChanged(double value);

private:
    int toSlider(double v) const;
    double fromSlider(int s) const;
    void apply(double v, bool emitSignal);

    QLabel *m_label;
    QSlider *m_slider;
    QDoubleSpinBox *m_spin;
    QToolButton *m_reset;
    double m_min, m_max, m_default, m_value;
    double m_scale;
    bool m_updating = false;
};

// A titled section whose content can be collapsed.
class Section : public QWidget {
    Q_OBJECT
public:
    explicit Section(const QString &title, QWidget *parent = nullptr);
    QVBoxLayout *contentLayout() const { return m_layout; }
    void setExpanded(bool expanded);
    bool isExpanded() const;
    void addHeaderWidget(QWidget *w);

private:
    QToolButton *m_toggle;
    QWidget *m_body;
    QVBoxLayout *m_layout;
    QHBoxLayout *m_header;
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

} // namespace ui
