// SPDX-License-Identifier: GPL-3.0-or-later
#include "EffectsPanel.h"

#include "PreviewWidget.h"
#include "Widgets.h"
#include "app/CameraController.h"
#include "app/ImageUtil.h"

#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace ui {

namespace {
QWidget *box(QLayout *layout)
{
    auto *w = new QWidget;
    layout->setContentsMargins(0, 0, 0, 0);
    w->setLayout(layout);
    return w;
}
} // namespace

EffectsPanel::EffectsPanel(app::CameraController &controller, PreviewWidget *preview, QWidget *parent)
    : QWidget(parent), m_controller(controller), m_preview(preview)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);

    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(new QLabel(tr("Effect:")));
    m_mode = new QComboBox;
    m_mode->addItem(tr("Off"), int(cam::EffectMode::Off));
    m_mode->addItem(tr("Blur background (detects you)"), int(cam::EffectMode::Person));
    m_mode->addItem(tr("Blur entire image"), int(cam::EffectMode::BlurAll));
    m_mode->addItem(tr("Blur selected regions"), int(cam::EffectMode::BlurRegions));
    m_mode->addItem(tr("Keep a drawn shape sharp"), int(cam::EffectMode::Foreground));
    m_mode->addItem(tr("Fixed mask image"), int(cam::EffectMode::MaskImage));
    m_mode->addItem(tr("Chroma key (green screen)"), int(cam::EffectMode::ChromaKey));
    modeRow->addWidget(m_mode, 1);
    v->addLayout(modeRow);

    // Regions
    {
        auto *l = new QVBoxLayout;
        m_regions = new QListWidget;
        m_regions->setMaximumHeight(90);
        l->addWidget(m_regions);
        auto *h = new QHBoxLayout;
        auto *draw = new QPushButton(tr("Draw region"));
        auto *remove = new QPushButton(tr("Remove"));
        auto *clear = new QPushButton(tr("Clear"));
        h->addWidget(draw);
        h->addWidget(remove);
        h->addWidget(clear);
        l->addLayout(h);
        m_regionsBox = box(l);
        v->addWidget(m_regionsBox);
        connect(draw, &QPushButton::clicked, this, [this] {
            m_pending = Pending::Region;
            m_preview->setInteraction(PreviewWidget::Interaction::DrawRect);
            m_hint->setText(tr("Drag on the preview to mark an area to blur."));
            m_hint->show();
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            int i = m_regions->currentRow();
            cam::EffectParams e = m_controller.effects();
            if (i >= 0 && i < int(e.blurRegions.size())) {
                e.blurRegions.erase(e.blurRegions.begin() + i);
                m_controller.setEffects(e);
                syncFromModel();
            }
        });
        connect(clear, &QPushButton::clicked, this, [this] {
            cam::EffectParams e = m_controller.effects();
            e.blurRegions.clear();
            m_controller.setEffects(e);
            syncFromModel();
        });
    }

    // Foreground shape
    {
        auto *g = new QGridLayout;
        g->addWidget(new QLabel(tr("Shape:")), 0, 0);
        m_fgShape = new QComboBox;
        m_fgShape->addItem(tr("Ellipse"));
        m_fgShape->addItem(tr("Rectangle"));
        g->addWidget(m_fgShape, 0, 1);
        auto *draw = new QPushButton(tr("Draw on preview"));
        g->addWidget(draw, 0, 2, 1, 2);
        g->setColumnStretch(1, 1);
        m_fgBox = box(g);
        v->addWidget(m_fgBox);
        connect(m_fgShape, qOverload<int>(&QComboBox::activated), this, &EffectsPanel::push);

        auto *fg = new QGridLayout;
        m_feather = new SliderRow(fg, 0, tr("Soft edge"), 0, 30, 8, 0, QStringLiteral(" %"));
        m_feather->setToolTip(tr("How gradually you blend into the background"));
        fg->setColumnStretch(1, 1);
        m_featherBox = box(fg);
        v->addWidget(m_featherBox);
        connect(m_feather, &SliderRow::valueChanged, this, &EffectsPanel::push);
        connect(draw, &QPushButton::clicked, this, [this] {
            m_pending = Pending::Foreground;
            m_preview->setInteraction(PreviewWidget::Interaction::DrawRect);
            m_hint->setText(tr("Drag on the preview around the area to keep (you)."));
            m_hint->show();
        });
    }

    // Mask image
    {
        auto *h = new QHBoxLayout;
        m_maskImage = new QLineEdit;
        m_maskImage->setPlaceholderText(tr("White = keep, black = replace"));
        auto *browse = new QPushButton(tr("Browse…"));
        h->addWidget(m_maskImage, 1);
        h->addWidget(browse);
        m_maskBox = box(h);
        v->addWidget(m_maskBox);
        connect(m_maskImage, &QLineEdit::editingFinished, this, &EffectsPanel::push);
        connect(browse, &QPushButton::clicked, this, [this] {
            QString f = chooseImage(tr("Choose mask image"));
            if (!f.isEmpty()) {
                m_maskImage->setText(f);
                push();
            }
        });
    }

    // Chroma key
    {
        auto *g = new QGridLayout;
        g->addWidget(new QLabel(tr("Key color:")), 0, 0);
        m_keyColor = new ColorButton;
        g->addWidget(m_keyColor, 0, 1);
        auto *pick = new QPushButton(tr("Pick from preview"));
        g->addWidget(pick, 0, 2, 1, 2);
        m_similarity = new SliderRow(g, 1, tr("Similarity"), 0, 100, 25, 0, QStringLiteral(" %"));
        m_smoothness = new SliderRow(g, 2, tr("Smoothness"), 0, 100, 8, 0, QStringLiteral(" %"));
        g->setColumnStretch(1, 1);
        m_keyBox = box(g);
        v->addWidget(m_keyBox);
        connect(m_keyColor, &ColorButton::colorChanged, this, &EffectsPanel::push);
        connect(m_similarity, &SliderRow::valueChanged, this, &EffectsPanel::push);
        connect(m_smoothness, &SliderRow::valueChanged, this, &EffectsPanel::push);
        connect(pick, &QPushButton::clicked, this, [this] {
            m_pending = Pending::KeyColor;
            m_preview->setInteraction(PreviewWidget::Interaction::PickPoint);
            m_hint->setText(tr("Click on the background color in the preview."));
            m_hint->show();
        });
    }

    // Background fill
    {
        m_fillGrid = new QGridLayout;
        m_fillGrid->addWidget(new QLabel(tr("Background:")), 0, 0);
        m_fill = new QComboBox;
        m_fill->addItem(tr("Blurred"), int(cam::BackgroundFill::Blur));
        m_fill->addItem(tr("Solid color"), int(cam::BackgroundFill::Color));
        m_fill->addItem(tr("Image"), int(cam::BackgroundFill::Image));
        m_fillGrid->addWidget(m_fill, 0, 1, 1, 3);
        m_strength = new SliderRow(m_fillGrid, 1, tr("Blur strength"), 0, 100, 50, 0, QStringLiteral(" %"));
        m_fillGrid->addWidget(new QLabel(tr("Color:")), 2, 0);
        m_fillColor = new ColorButton;
        m_fillGrid->addWidget(m_fillColor, 2, 1, 1, 3);
        m_fillGrid->addWidget(new QLabel(tr("Image:")), 3, 0);
        m_bgImage = new QLineEdit;
        m_bgBrowse = new QPushButton(tr("Browse…"));
        m_fillGrid->addWidget(m_bgImage, 3, 1, 1, 2);
        m_fillGrid->addWidget(m_bgBrowse, 3, 3);
        m_fillGrid->setColumnStretch(1, 1);
        m_fillBox = box(m_fillGrid);
        v->addWidget(m_fillBox);
        connect(m_fill, qOverload<int>(&QComboBox::activated), this, &EffectsPanel::push);
        connect(m_strength, &SliderRow::valueChanged, this, &EffectsPanel::push);
        connect(m_fillColor, &ColorButton::colorChanged, this, &EffectsPanel::push);
        connect(m_bgImage, &QLineEdit::editingFinished, this, &EffectsPanel::push);
        connect(m_bgBrowse, &QPushButton::clicked, this, [this] {
            QString f = chooseImage(tr("Choose background image"));
            if (!f.isEmpty()) {
                m_bgImage->setText(f);
                push();
            }
        });
    }

    m_hint = new QLabel;
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color: palette(highlight);"));
    m_hint->hide();
    v->addWidget(m_hint);

    m_personNote = new QLabel(tr("Detection runs entirely on this computer using a small built-in model "
                                 "(Google MediaPipe selfie segmentation)."));
    m_personNote->setWordWrap(true);
    m_personNote->setEnabled(false);
    v->addWidget(m_personNote);

    auto *note = new QLabel(tr("Effects add some CPU load; keep them off when not needed."));
    note->setWordWrap(true);
    note->setEnabled(false);
    v->addWidget(note);

    connect(m_mode, qOverload<int>(&QComboBox::activated), this, &EffectsPanel::push);

    connect(m_preview, &PreviewWidget::rectDrawn, this, [this](const QRectF &r) {
        cam::EffectParams e = m_controller.effects();
        cam::RectF rf{r.x(), r.y(), r.width(), r.height()};
        if (m_pending == Pending::Region) {
            if (e.blurRegions.size() < 32)
                e.blurRegions.push_back(rf);
            e.mode = cam::EffectMode::BlurRegions;
        } else if (m_pending == Pending::Foreground) {
            e.foreground = rf;
            e.mode = cam::EffectMode::Foreground;
        } else {
            return;
        }
        m_controller.setEffects(e);
        cancelInteraction();
        syncFromModel();
    });
    connect(m_preview, &PreviewWidget::pointPicked, this, [this](const QPointF &p) {
        if (m_pending != Pending::KeyColor)
            return;
        cam::FramePtr f = m_controller.takePreviewFrame();
        if (f) {
            // Pick from the unkeyed picture would be ideal; the processed
            // frame is close enough while keying is still being set up.
            m_keyColor->setColor(QColor(app::sampleI420(*f, p.x(), p.y())));
            push();
        }
        cancelInteraction();
    });

    syncFromModel();
}

QString EffectsPanel::chooseImage(const QString &title)
{
    return QFileDialog::getOpenFileName(this, title,
                                        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                        tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
}

void EffectsPanel::cancelInteraction()
{
    m_pending = Pending::None;
    m_preview->setInteraction(PreviewWidget::Interaction::Navigate);
    m_hint->hide();
}

void EffectsPanel::push()
{
    if (m_syncing)
        return;
    cam::EffectParams e = m_controller.effects();
    e.mode = cam::EffectMode(m_mode->currentData().toInt());
    e.fill = cam::BackgroundFill(m_fill->currentData().toInt());
    e.blurStrength = m_strength->value() / 100.0;
    e.fillColor = m_fillColor->color().rgba();
    e.backgroundImagePath = m_bgImage->text().trimmed().toStdString();
    e.foregroundEllipse = m_fgShape->currentIndex() == 0;
    e.feather = m_feather->value() / 100.0;
    e.maskImagePath = m_maskImage->text().trimmed().toStdString();
    e.keyColor = m_keyColor->color().rgba();
    e.keySimilarity = m_similarity->value() / 100.0;
    e.keySmoothness = m_smoothness->value() / 100.0;
    if (e.mode == cam::EffectMode::Off || e.mode == cam::EffectMode::BlurAll)
        cancelInteraction();
    m_controller.setEffects(e);
    updateVisibility();
    updateOverlay();
}

void EffectsPanel::syncFromModel()
{
    m_syncing = true;
    const cam::EffectParams &e = m_controller.effects();
    m_mode->setCurrentIndex(qMax(0, m_mode->findData(int(e.mode))));
    m_fill->setCurrentIndex(qMax(0, m_fill->findData(int(e.fill))));
    m_strength->setValue(e.blurStrength * 100);
    m_fillColor->setColor(QColor::fromRgba(e.fillColor));
    m_bgImage->setText(QString::fromStdString(e.backgroundImagePath));
    m_fgShape->setCurrentIndex(e.foregroundEllipse ? 0 : 1);
    m_feather->setValue(e.feather * 100);
    m_maskImage->setText(QString::fromStdString(e.maskImagePath));
    m_keyColor->setColor(QColor::fromRgba(e.keyColor));
    m_similarity->setValue(e.keySimilarity * 100);
    m_smoothness->setValue(e.keySmoothness * 100);
    m_regions->clear();
    for (size_t i = 0; i < e.blurRegions.size(); ++i) {
        const auto &r = e.blurRegions[i];
        m_regions->addItem(tr("Region %1  (%2%, %3%  %4×%5%)")
                               .arg(i + 1)
                               .arg(int(r.x * 100))
                               .arg(int(r.y * 100))
                               .arg(int(r.w * 100))
                               .arg(int(r.h * 100)));
    }
    m_syncing = false;
    updateVisibility();
    updateOverlay();
}

void EffectsPanel::updateVisibility()
{
    const auto mode = cam::EffectMode(m_mode->currentData().toInt());
    const auto fill = cam::BackgroundFill(m_fill->currentData().toInt());
    const bool replaces = mode == cam::EffectMode::Foreground || mode == cam::EffectMode::MaskImage ||
                          mode == cam::EffectMode::ChromaKey || mode == cam::EffectMode::Person;
    m_regionsBox->setVisible(mode == cam::EffectMode::BlurRegions);
    m_fgBox->setVisible(mode == cam::EffectMode::Foreground);
    m_featherBox->setVisible(mode == cam::EffectMode::Foreground || mode == cam::EffectMode::Person);
    m_personNote->setVisible(mode == cam::EffectMode::Person);
    m_maskBox->setVisible(mode == cam::EffectMode::MaskImage);
    m_keyBox->setVisible(mode == cam::EffectMode::ChromaKey);
    m_fillBox->setVisible(mode != cam::EffectMode::Off);
    // Fill choice only matters when a background is replaced.
    for (int c = 0; c < 4; ++c) {
        if (auto *item = m_fillGrid->itemAtPosition(0, c); item && item->widget())
            item->widget()->setVisible(replaces);
        for (int r = 2; r <= 3; ++r)
            if (auto *it = m_fillGrid->itemAtPosition(r, c); it && it->widget())
                it->widget()->setVisible(replaces && ((r == 2 && fill == cam::BackgroundFill::Color) ||
                                                      (r == 3 && fill == cam::BackgroundFill::Image)));
    }
    m_strength->setVisible(!replaces || fill == cam::BackgroundFill::Blur);
}

void EffectsPanel::updateOverlay()
{
    const cam::EffectParams &e = m_controller.effects();
    QList<QRectF> rects;
    bool ellipse = false;
    if (e.mode == cam::EffectMode::BlurRegions) {
        for (const auto &r : e.blurRegions)
            rects << QRectF(r.x, r.y, r.w, r.h);
    } else if (e.mode == cam::EffectMode::Foreground && !isHidden() && m_fgBox->isVisible()) {
        // Show the shape only while the panel is open, to help placing it.
        rects << QRectF(e.foreground.x, e.foreground.y, e.foreground.w, e.foreground.h);
        ellipse = e.foregroundEllipse;
    }
    m_preview->setOverlayRects(e.mode == cam::EffectMode::BlurRegions ? QList<QRectF>() : rects, ellipse);
}

} // namespace ui
