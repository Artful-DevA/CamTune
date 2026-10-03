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
QWidget { color: #dcdce0; }
QMainWindow, QDialog { background: #1e1e22; }
QToolTip { background: #2d2d33; color: #dcdce0; border: 1px solid #3a3a42; padding: 3px 6px; }

QMenuBar { background: #26262b; border-bottom: 1px solid #141417; padding: 1px 2px; }
QMenuBar::item { padding: 4px 9px; background: transparent; border-radius: 3px; }
QMenuBar::item:selected { background: #3a3a42; }
QMenu { background: #2a2a30; border: 1px solid #3f3f47; padding: 4px 0; }
QMenu::item { padding: 5px 28px 5px 26px; }
QMenu::item:selected { background: #3d8bff; color: white; }
QMenu::item:disabled { color: #6a6a72; }
QMenu::separator { height: 1px; background: #3a3a42; margin: 4px 8px; }
QMenu::indicator { width: 13px; height: 13px; left: 7px; }

QToolBar#MainToolBar { background: #26262b; border: none; border-bottom: 1px solid #141417; padding: 4px 6px; spacing: 6px; }
QToolBar#MainToolBar QLabel { color: #96969f; padding: 0 2px 0 6px; }
QStatusBar { background: #26262b; border-top: 1px solid #141417; color: #96969f; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #96969f; padding: 0 8px; }
QFrame#StatusSep { background: #3a3a42; border: none; }

#Viewer { background: #141417; }
#ViewerBar { background: #26262b; border-top: 1px solid #141417; }
#Inspector { background: #26262b; }
#InspectorRail { background: #202025; border-right: 1px solid #141417; }
#InspectorTitle { background: #26262b; color: #dcdce0; font-weight: 600; padding: 9px 12px 7px 12px; border-bottom: 1px solid #1a1a1e; }
QSplitter::handle { background: #141417; }

QToolButton { background: transparent; border: 1px solid transparent; border-radius: 3px; padding: 3px; color: #b4b4bc; }
QToolButton:hover { background: #34343b; border-color: #44444c; }
QToolButton:pressed { background: #2a2a30; }
QToolButton:checked { background: #23324a; border-color: #36598f; }
QToolButton#RailButton { border: none; border-radius: 0; border-left: 2px solid transparent; padding: 9px; }
QToolButton#RailButton:hover { background: #2b2b31; }
QToolButton#RailButton:checked { background: #26262b; border-left: 2px solid #3d8bff; }
QToolButton#SectionHeader {
    background: #2d2d33; border: none; border-top: 1px solid #36363d; border-bottom: 1px solid #1a1a1e;
    border-radius: 0; padding: 5px 6px; font-weight: 600; text-align: left;
}
QToolButton#SectionHeader:hover { background: #33333a; }
QToolButton#SectionAction { padding: 2px; }
QToolButton#ResetButton { padding: 1px; }

QToolButton#VirtualCamera {
    background: #2f2f36; border: 1px solid #44444c; border-radius: 3px; padding: 4px 12px 4px 8px; font-weight: 600;
}
QToolButton#VirtualCamera:hover { background: #38383f; }
QToolButton#VirtualCamera:checked { background: #1d3a2a; border-color: #2f7a4d; color: #c9f2d9; }

QPushButton {
    background: #34343b; border: 1px solid #47474f; border-radius: 3px; padding: 4px 12px; min-height: 16px;
}
QPushButton:hover { background: #3c3c44; }
QPushButton:pressed { background: #2c2c32; }
QPushButton:disabled { color: #6a6a72; background: #2a2a30; border-color: #36363d; }
QPushButton:default, QPushButton#Primary { background: #2f6fd0; border-color: #3d8bff; color: white; }
QPushButton#Primary:hover { background: #3a7de0; }

QComboBox, QLineEdit, QAbstractSpinBox {
    background: #19191d; border: 1px solid #3a3a42; border-radius: 3px; padding: 3px 6px;
    selection-background-color: #3d8bff; min-height: 18px;
}
QComboBox:hover, QLineEdit:hover, QAbstractSpinBox:hover { border-color: #4a4a54; }
QComboBox:focus, QLineEdit:focus, QAbstractSpinBox:focus { border-color: #3d8bff; }
QComboBox:disabled, QLineEdit:disabled, QAbstractSpinBox:disabled { color: #6a6a72; }
QComboBox::drop-down { border: none; width: 20px; }
QComboBox::down-arrow { image: url(:/icons/arrow-down.png); width: 10px; height: 6px; margin-right: 6px; }
QComboBox::down-arrow:hover, QComboBox::down-arrow:on { image: url(:/icons/arrow-down-hover.png); }
QComboBox QAbstractItemView { background: #2a2a30; border: 1px solid #3f3f47; selection-background-color: #3d8bff; outline: 0; }
QDoubleSpinBox#Value { padding: 2px 4px; }

QSlider { min-height: 18px; }
QSlider::groove:horizontal { height: 3px; background: #3a3a42; border-radius: 1px; }
QSlider::sub-page:horizontal { background: #3d8bff; border-radius: 1px; }
QSlider::handle:horizontal { background: #d8d8de; width: 11px; height: 11px; margin: -4px 0; border-radius: 6px; }
QSlider::handle:horizontal:hover { background: #ffffff; }
QSlider::sub-page:horizontal:disabled { background: #4a4a52; }
QSlider::handle:horizontal:disabled { background: #6a6a72; }

QCheckBox { spacing: 7px; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #50505a; border-radius: 2px; background: #19191d; }
QCheckBox::indicator:hover { border-color: #6a6a74; }
QCheckBox::indicator:checked { background: #3d8bff; border-color: #3d8bff; image: url(:/icons/check.png); }
QCheckBox:disabled { color: #6a6a72; }

QLabel#Hint { color: #8a8a94; }
QLabel#PropertyLabel { color: #b4b4bc; }
QLabel#Banner { padding: 7px 10px; border-left: 3px solid; }
QLabel#Banner[level="error"] { background: #3a2226; color: #f3c1c1; border-left-color: #f05a5a; }
QLabel#Banner[level="warn"] { background: #3a3222; color: #f3dfb4; border-left-color: #f0b045; }
QLabel#Banner[level="info"] { background: #22303f; color: #c7dcf7; border-left-color: #3d8bff; }

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: #222227; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #44444c; border-radius: 4px; min-height: 30px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: #55555e; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QListWidget { background: #19191d; border: 1px solid #3a3a42; border-radius: 3px; outline: 0; }
QListWidget::item { padding: 4px 6px; }
QListWidget::item:selected { background: #3d8bff; color: white; }
QPlainTextEdit { background: #141417; border: 1px solid #3a3a42; }
QGroupBox { border: 1px solid #3a3a42; border-radius: 3px; margin-top: 14px; padding: 10px 8px 8px 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #b4b4bc; }
)";

} // namespace

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    p.setColor(QPalette::Window, kWindow);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, kInput);
    p.setColor(QPalette::AlternateBase, kPanel);
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, kHeader);
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::ToolTipBase, kHeader);
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, kAccent);
    p.setColor(QPalette::PlaceholderText, kMuted);
    p.setColor(QPalette::Mid, kBorder);
    p.setColor(QPalette::Dark, kWindow);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(0x6a, 0x6a, 0x72));
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
