// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;

namespace app {
class CameraController;
}

namespace ui {

class ColorButton;
class PreviewWidget;
class SliderRow;

// Inspector page for background effects: one "Mode" choice, then only the
// properties that mode uses.
class BackgroundPanel : public QWidget {
    Q_OBJECT
public:
    BackgroundPanel(app::CameraController &controller, PreviewWidget *preview, QWidget *parent = nullptr);

    void syncFromModel();
    // Leaves any drawing/picking mode on the preview.
    void cancelInteraction();

private:
    enum Mode { None, BlurBehind, ReplaceBehind, BlurAll, GreenScreen, Regions, Shape, MaskImage };

    Mode modeFromModel() const;
    void setMode(Mode m);
    void push();
    void updateVisibility();
    void updateOverlay();
    QWidget *fileRow(const QString &label, QLineEdit *&edit, const QString &dialogTitle);

    app::CameraController &m_controller;
    PreviewWidget *m_preview;
    bool m_syncing = false;
    enum class Pending { None, Region, Foreground, KeyColor } m_pending = Pending::None;

    QComboBox *m_mode;
    QLabel *m_description;

    QWidget *m_fillRow;
    QComboBox *m_fill;
    QWidget *m_colorRow;
    ColorButton *m_fillColor;
    QWidget *m_imageRow;
    QLineEdit *m_bgImage;

    SliderRow *m_strength;
    SliderRow *m_softness;

    QWidget *m_keyRow;
    ColorButton *m_keyColor;
    SliderRow *m_similarity;
    SliderRow *m_smoothness;

    QWidget *m_regionsBox;
    QListWidget *m_regions;
    QWidget *m_shapeRow;
    QComboBox *m_shape;
    QWidget *m_maskRow;
    QLineEdit *m_maskImage;

    QLabel *m_hint;
};

} // namespace ui
