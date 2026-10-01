// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace app {
class CameraController;
}

namespace ui {

class ColorButton;
class PreviewWidget;
class SliderRow;

// Background effects that need no machine learning: blur, blurred regions,
// a manual foreground shape, a fixed mask image and chroma key.
class EffectsPanel : public QWidget {
    Q_OBJECT
public:
    EffectsPanel(app::CameraController &controller, PreviewWidget *preview, QWidget *parent = nullptr);

    void syncFromModel();
    // Leaves any drawing/picking mode on the preview.
    void cancelInteraction();

private:
    void push();
    void updateVisibility();
    void updateOverlay();
    QString chooseImage(const QString &title);

    app::CameraController &m_controller;
    PreviewWidget *m_preview;
    bool m_syncing = false;
    enum class Pending { None, Region, Foreground, KeyColor } m_pending = Pending::None;

    QComboBox *m_mode;
    QWidget *m_fillBox;
    QComboBox *m_fill;
    SliderRow *m_strength;
    ColorButton *m_fillColor;
    QLineEdit *m_bgImage;
    QPushButton *m_bgBrowse;
    QWidget *m_regionsBox;
    QListWidget *m_regions;
    QWidget *m_fgBox;
    QComboBox *m_fgShape;
    SliderRow *m_feather;
    QWidget *m_maskBox;
    QLineEdit *m_maskImage;
    QWidget *m_keyBox;
    ColorButton *m_keyColor;
    SliderRow *m_similarity;
    SliderRow *m_smoothness;
    QLabel *m_hint;
    QWidget *m_strengthRow;
    QWidget *m_colorRow;
    QWidget *m_imageRow;
    QGridLayout *m_fillGrid;
};

} // namespace ui
