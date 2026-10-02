// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.h"

#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QPalette>
#include <QScrollBar>
#include <QStyleFactory>
#include <QWheelEvent>

namespace ui::theme {

namespace {

const char *kStyleSheet = R"(
QWidget { color: #e7eaf0; }
QMainWindow, QDialog { background: #10141b; }
QToolTip { background: #202737; color: #e7eaf0; border: 1px solid #2a3243; padding: 4px 6px; }

#Header { background: #171c26; border-bottom: 1px solid #2a3243; }
#AppTitle { font-size: 13pt; font-weight: 700; }
#Sidebar { background: #171c26; border-left: 1px solid #2a3243; }
#PreviewColumn { background: #10141b; }
#PreviewBar { background: #171c26; border: 1px solid #2a3243; border-radius: 10px; }

QLabel#Muted, QLabel#Hint { color: #8a93a6; }
QLabel#ValueText { color: #8a93a6; }
QLabel#SectionTitle { color: #8a93a6; font-weight: 600; font-size: 8.5pt; }
QLabel#StatusText { color: #8a93a6; }

QLabel#Banner { border-radius: 8px; padding: 9px 12px; }
QLabel#Banner[level="error"] { background: #3a1d24; color: #ffc2c2; border: 1px solid #6b2a35; }
QLabel#Banner[level="warn"] { background: #3a2f1a; color: #ffe0a6; border: 1px solid #6b5426; }
QLabel#Banner[level="info"] { background: #1a2a42; color: #cfe1ff; border: 1px solid #2c4a7a; }

QPushButton {
    background: #202737; border: 1px solid #2a3243; border-radius: 8px;
    padding: 6px 12px; min-height: 18px;
}
QPushButton:hover { background: #273044; border-color: #36405a; }
QPushButton:pressed { background: #2d3850; }
QPushButton:disabled { color: #596174; background: #1a202c; }
QPushButton#Primary { background: #3d8bff; border-color: #3d8bff; color: white; font-weight: 600; }
QPushButton#Primary:hover { background: #5a9cff; }
QPushButton#Chip { border-radius: 14px; padding: 4px 12px; background: #1d2331; }
QPushButton#Chip:hover { background: #273044; }
QPushButton#Chip:checked { background: #1d3456; border-color: #3d8bff; color: #d6e6ff; }
#Segmented { background: #1d2331; border: 1px solid #2a3243; border-radius: 9px; }
QPushButton#Segment { background: transparent; border: none; border-radius: 7px; padding: 5px 8px; color: #b9c0cf; }
QPushButton#Segment:hover { background: #273044; color: #e7eaf0; }
QPushButton#Segment:checked { background: #3d8bff; color: white; font-weight: 600; }

QToolButton { background: transparent; border: none; border-radius: 6px; padding: 4px 6px; }
QToolButton:hover { background: #273044; }
QToolButton:pressed, QToolButton:checked { background: #2d3850; }
QToolButton#HeaderButton { border: 1px solid #2a3243; background: #202737; padding: 5px 12px; border-radius: 8px; }
QToolButton#HeaderButton:hover { background: #273044; }
QToolButton#HeaderButton::menu-indicator { image: none; width: 0; }
QToolButton#ResetButton { color: #8a93a6; padding: 0 4px; }
QToolButton#ResetButton:hover { color: #e7eaf0; }

QComboBox, QLineEdit {
    background: #202737; border: 1px solid #2a3243; border-radius: 8px; padding: 5px 10px;
    selection-background-color: #3d8bff;
}
QComboBox:hover, QLineEdit:hover { border-color: #36405a; }
QComboBox:focus, QLineEdit:focus { border-color: #3d8bff; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox::down-arrow { image: url(:/icons/arrow-down.png); width: 10px; height: 6px; margin-right: 8px; }
QComboBox::down-arrow:hover, QComboBox::down-arrow:on { image: url(:/icons/arrow-down-hover.png); }
QComboBox QAbstractItemView {
    background: #202737; border: 1px solid #2a3243; selection-background-color: #3d8bff; outline: 0; padding: 4px;
}

QDoubleSpinBox#Value {
    background: transparent; border: 1px solid transparent; border-radius: 6px; color: #b9c0cf; padding: 1px 4px;
}
QDoubleSpinBox#Value:hover { border-color: #2a3243; }
QDoubleSpinBox#Value:focus { border-color: #3d8bff; background: #202737; color: #e7eaf0; }

QSlider::groove:horizontal { height: 4px; background: #2a3243; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #3d8bff; border-radius: 2px; }
QSlider::handle:horizontal {
    background: #e7eaf0; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px;
}
QSlider::handle:horizontal:hover { background: #ffffff; }
QSlider::sub-page:horizontal:disabled { background: #3a4252; }
QSlider::handle:horizontal:disabled { background: #596174; }

QTabWidget::pane { border: none; }
QTabBar { qproperty-drawBase: 0; }
QTabBar::tab {
    background: transparent; color: #8a93a6; padding: 10px 9px 8px 9px; border: none;
    border-bottom: 2px solid transparent; font-weight: 600;
}
QTabBar::tab:hover { color: #c9cfdb; }
QTabBar::tab:selected { color: #e7eaf0; border-bottom-color: #3d8bff; }

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #2f3849; border-radius: 3px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #3b4559; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QMenu { background: #202737; border: 1px solid #2a3243; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 6px 28px 6px 12px; border-radius: 6px; }
QMenu::item:selected { background: #3d8bff; color: white; }
QMenu::item:disabled { color: #596174; }
QMenu::separator { height: 1px; background: #2a3243; margin: 5px 6px; }
QMenu::indicator { width: 14px; height: 14px; left: 6px; }

QListWidget { background: #202737; border: 1px solid #2a3243; border-radius: 8px; padding: 4px; outline: 0; }
QListWidget::item { padding: 6px 8px; border-radius: 6px; }
QListWidget::item:selected { background: #3d8bff; color: white; }

QCheckBox { spacing: 8px; }
QPlainTextEdit { background: #0d1117; border: 1px solid #2a3243; border-radius: 8px; }
QSplitter::handle { background: #2a3243; }
)";

} // namespace

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    p.setColor(QPalette::Window, kWindow);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, kRaised);
    p.setColor(QPalette::AlternateBase, kSurface);
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, kRaised);
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::ToolTipBase, kRaised);
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, kAccent);
    p.setColor(QPalette::PlaceholderText, kMuted);
    p.setColor(QPalette::Mid, kBorder);
    p.setColor(QPalette::Dark, kWindow);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(0x59, 0x61, 0x74));
    app.setPalette(p);
    app.setStyleSheet(QString::fromLatin1(kStyleSheet));
    app.installEventFilter(new WheelGuard(&app));
}

bool WheelGuard::eventFilter(QObject *obj, QEvent *event)
{
    const auto type = event->type();
    if (type != QEvent::Wheel && type != QEvent::Polish)
        return false;
    auto *w = qobject_cast<QWidget *>(obj);
    if (!w || !(qobject_cast<QAbstractSlider *>(w) || qobject_cast<QAbstractSpinBox *>(w) ||
                qobject_cast<QComboBox *>(w)) ||
        qobject_cast<QScrollBar *>(w))
        return false;
    if (type == QEvent::Polish) {
        // Never take focus just because the wheel passed over the control.
        if (w->focusPolicy() & Qt::WheelFocus)
            w->setFocusPolicy(Qt::StrongFocus);
        return false;
    }
    if (w->hasFocus())
        return false; // the user clicked it: the wheel adjusts it
    // Otherwise scroll the panel the control sits in.
    for (QWidget *p = w->parentWidget(); p; p = p->parentWidget()) {
        if (auto *area = qobject_cast<QAbstractScrollArea *>(p)) {
            QCoreApplication::sendEvent(area->verticalScrollBar(), event);
            return true;
        }
    }
    event->ignore();
    return true;
}

} // namespace ui::theme
