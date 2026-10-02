// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QWidget>

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;

namespace app {
class CameraController;
}

namespace ui {

class ColorButton;
class OptionCard;
class PreviewWidget;
class SegmentedControl;
class SliderRow;

// "Background" tab: a short list of choices (blur behind you, replace
// behind you, blur everything, green screen, manual areas), each showing
// only the settings it needs.
class BackgroundPanel : public QWidget {
    Q_OBJECT
public:
    BackgroundPanel(app::CameraController &controller, PreviewWidget *preview, QWidget *parent = nullptr);

    void syncFromModel();
    // Leaves any drawing/picking mode on the preview.
    void cancelInteraction();

private:
    enum Choice { None, BlurBehind, ReplaceBehind, BlurAll, GreenScreen, Manual, ChoiceCount };

    Choice choiceFromModel() const;
    void choose(Choice c);
    void push();
    void updateVisibility();
    void updateOverlay();
    QString chooseImage(const QString &title);
    QWidget *imagePicker(QLineEdit *&edit, const QString &placeholder, const QString &dialogTitle);

    app::CameraController &m_controller;
    PreviewWidget *m_preview;
    bool m_syncing = false;
    enum class Pending { None, Region, Foreground, KeyColor } m_pending = Pending::None;

    QButtonGroup *m_choices;
    OptionCard *m_cards[ChoiceCount] = {};

    QWidget *m_manualBox;
    QComboBox *m_manualType;
    QWidget *m_regionsBox;
    QListWidget *m_regions;
    QWidget *m_shapeBox;
    SegmentedControl *m_shape;
    QWidget *m_maskBox;
    QLineEdit *m_maskImage;

    QWidget *m_keyBox;
    ColorButton *m_keyColor;
    SliderRow *m_similarity;
    SliderRow *m_smoothness;

    QWidget *m_replaceBox;
    QLabel *m_replaceLabel;
    SegmentedControl *m_fill;
    QWidget *m_colorRow;
    ColorButton *m_fillColor;
    QWidget *m_imageRow;
    QLineEdit *m_bgImage;

    SliderRow *m_strength;
    SliderRow *m_softness;
    QLabel *m_hint;
    QLabel *m_cpuNote;
};

} // namespace ui
