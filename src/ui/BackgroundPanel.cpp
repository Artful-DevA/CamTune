// SPDX-License-Identifier: GPL-3.0-or-later
#include "BackgroundPanel.h"

#include "Icons.h"
#include "PreviewWidget.h"
#include "Widgets.h"
#include "app/CameraController.h"
#include "app/ImageUtil.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

namespace ui {

BackgroundPanel::BackgroundPanel(app::CameraController &controller, PreviewWidget *preview, QWidget *parent)
    : QWidget(parent), m_controller(controller), m_preview(preview)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *sec = new Section(tr("Background"));
    auto *l = sec->contentLayout();
    v->addWidget(sec);

    m_mode = new QComboBox;
    m_mode->addItem(tr("None"), None);
    m_mode->insertSeparator(m_mode->count());
    m_mode->addItem(tr("Blur background"), BlurBehind);
    m_mode->addItem(tr("Replace background"), ReplaceBehind);
    m_mode->insertSeparator(m_mode->count());
    m_mode->addItem(tr("Blur entire image"), BlurAll);
    m_mode->addItem(tr("Green screen"), GreenScreen);
    m_mode->insertSeparator(m_mode->count());
    m_mode->addItem(tr("Blur selected areas"), Regions);
    m_mode->addItem(tr("Keep a drawn shape"), Shape);
    m_mode->addItem(tr("Mask image"), MaskImage);
    l->addWidget(propertyRow(tr("Mode"), m_mode));
    m_description = hintLabel(QString());
    m_description->setContentsMargins(kLabelWidth + 6, 0, 0, 4);
    l->addWidget(m_description);
    connect(m_mode, qOverload<int>(&QComboBox::activated), this,
            [this] { setMode(Mode(m_mode->currentData().toInt())); });

    // Green screen key.
    m_keyColor = new ColorButton;
    auto *pick = iconButton(icons::get(icons::Name::Pick), tr("Pick the backdrop color from the preview"));
    m_keyRow = propertyRow(tr("Key color"), m_keyColor, pick);
    l->addWidget(m_keyRow);
    m_similarity = new SliderRow(tr("Tolerance"), 0, 1, 0.25, 0, QStringLiteral(" %"), 100);
    m_smoothness = new SliderRow(tr("Edge blend"), 0, 1, 0.08, 0, QStringLiteral(" %"), 100);
    l->addWidget(m_similarity);
    l->addWidget(m_smoothness);
    connect(m_keyColor, &ColorButton::colorChanged, this, &BackgroundPanel::push);
    connect(m_similarity, &SliderRow::valueChanged, this, &BackgroundPanel::push);
    connect(m_smoothness, &SliderRow::valueChanged, this, &BackgroundPanel::push);
    connect(pick, &QToolButton::clicked, this, [this] {
        m_pending = Pending::KeyColor;
        m_preview->setInteraction(PreviewWidget::Interaction::PickPoint);
        m_hint->setText(tr("Click the backdrop in the preview. Esc cancels."));
        m_hint->show();
    });

    // Drawn areas.
    {
        auto *box = new QWidget;
        auto *bl = new QVBoxLayout(box);
        bl->setContentsMargins(0, 0, 0, 0);
        bl->setSpacing(4);
        m_regions = new QListWidget;
        m_regions->setMaximumHeight(90);
        auto *tools = new QHBoxLayout;
        auto *draw = new QPushButton(icons::get(icons::Name::Draw), tr("Draw Area"));
        auto *remove = new QPushButton(icons::get(icons::Name::Remove), tr("Remove"));
        auto *clear = new QPushButton(tr("Clear"));
        tools->addWidget(draw);
        tools->addWidget(remove);
        tools->addWidget(clear);
        tools->addStretch(1);
        auto *inner = new QWidget;
        auto *il = new QVBoxLayout(inner);
        il->setContentsMargins(0, 0, 0, 0);
        il->addWidget(m_regions);
        il->addLayout(tools);
        bl->addWidget(propertyRow(tr("Areas"), inner));
        m_regionsBox = box;
        l->addWidget(box);
        connect(draw, &QPushButton::clicked, this, [this] {
            m_pending = Pending::Region;
            m_preview->setInteraction(PreviewWidget::Interaction::DrawRect);
            m_hint->setText(tr("Drag over the area to blur in the preview. Esc cancels."));
            m_hint->show();
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            const int i = m_regions->currentRow();
            cam::EffectParams e = m_controller.effects();
            if (i >= 0 && i < int(e.blurRegions.size())) {
                e.blurRegions.erase(e.blurRegions.begin() + i);
                m_controller.setEffects(e);
            }
        });
        connect(clear, &QPushButton::clicked, this, [this] {
            cam::EffectParams e = m_controller.effects();
            e.blurRegions.clear();
            m_controller.setEffects(e);
        });
    }

    // Drawn shape.
    m_shape = new QComboBox;
    m_shape->addItem(tr("Oval"), 1);
    m_shape->addItem(tr("Rectangle"), 0);
    auto *drawShape = iconButton(icons::get(icons::Name::Draw), tr("Draw the shape to keep on the preview"));
    m_shapeRow = propertyRow(tr("Shape"), m_shape, drawShape);
    l->addWidget(m_shapeRow);
    connect(m_shape, qOverload<int>(&QComboBox::activated), this, &BackgroundPanel::push);
    connect(drawShape, &QToolButton::clicked, this, [this] {
        m_pending = Pending::Foreground;
        m_preview->setInteraction(PreviewWidget::Interaction::DrawRect);
        m_hint->setText(tr("Drag around the area to keep in the preview. Esc cancels."));
        m_hint->show();
    });

    m_maskRow = fileRow(tr("Mask file"), m_maskImage, tr("Choose Mask Image"));
    m_maskImage->setPlaceholderText(tr("White = keep, black = replace"));
    l->addWidget(m_maskRow);

    // What goes behind.
    m_fill = new QComboBox;
    m_fill->addItem(tr("Blur"), int(cam::BackgroundFill::Blur));
    m_fill->addItem(tr("Image"), int(cam::BackgroundFill::Image));
    m_fill->addItem(tr("Solid color"), int(cam::BackgroundFill::Color));
    m_fillRow = propertyRow(tr("Replace with"), m_fill);
    l->addWidget(m_fillRow);
    connect(m_fill, qOverload<int>(&QComboBox::activated), this, &BackgroundPanel::push);
    m_fillColor = new ColorButton;
    m_colorRow = propertyRow(tr("Color"), m_fillColor);
    l->addWidget(m_colorRow);
    connect(m_fillColor, &ColorButton::colorChanged, this, &BackgroundPanel::push);
    m_imageRow = fileRow(tr("Image file"), m_bgImage, tr("Choose Background Image"));
    l->addWidget(m_imageRow);

    m_strength = new SliderRow(tr("Blur amount"), 0, 1, 0.5, 0, QStringLiteral(" %"), 100);
    m_softness = new SliderRow(tr("Edge softness"), 0, 0.3, 0.08, 0, QStringLiteral(" %"), 100 / 0.3);
    m_softness->setHint(tr("How gradually the subject blends into the background"));
    l->addWidget(m_strength);
    l->addWidget(m_softness);
    connect(m_strength, &SliderRow::valueChanged, this, &BackgroundPanel::push);
    connect(m_softness, &SliderRow::valueChanged, this, &BackgroundPanel::push);

    m_hint = new QLabel;
    m_hint->setObjectName(QStringLiteral("Banner"));
    m_hint->setProperty("level", QStringLiteral("info"));
    m_hint->setWordWrap(true);
    m_hint->hide();
    l->addSpacing(4);
    l->addWidget(m_hint);
    v->addStretch(1);

    connect(m_preview, &PreviewWidget::rectDrawn, this, [this](const QRectF &r) {
        cam::EffectParams e = m_controller.effects();
        const cam::RectF rf{r.x(), r.y(), r.width(), r.height()};
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
        cancelInteraction();
        m_controller.setEffects(e);
    });
    connect(m_preview, &PreviewWidget::pointPicked, this, [this](const QPointF &p) {
        if (m_pending != Pending::KeyColor)
            return;
        if (cam::FramePtr f = m_controller.takePreviewFrame()) {
            m_keyColor->setColor(QColor(app::sampleI420(*f, p.x(), p.y())));
            push();
        }
        cancelInteraction();
    });

    syncFromModel();
}

QWidget *BackgroundPanel::fileRow(const QString &label, QLineEdit *&edit, const QString &dialogTitle)
{
    edit = new QLineEdit;
    edit->setReadOnly(true);
    auto *browse = new QPushButton(tr("Browse…"));
    QLineEdit *target = edit;
    connect(browse, &QPushButton::clicked, this, [this, target, dialogTitle] {
        const QString f = QFileDialog::getOpenFileName(
            this, dialogTitle, QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
            tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (!f.isEmpty()) {
            target->setText(f);
            target->setToolTip(f);
            push();
        }
    });
    return propertyRow(label, edit, browse);
}

void BackgroundPanel::cancelInteraction()
{
    m_pending = Pending::None;
    m_preview->setInteraction(PreviewWidget::Interaction::Navigate);
    m_hint->hide();
}

BackgroundPanel::Mode BackgroundPanel::modeFromModel() const
{
    const cam::EffectParams &e = m_controller.effects();
    switch (e.mode) {
    case cam::EffectMode::Off: return None;
    case cam::EffectMode::Person: return e.fill == cam::BackgroundFill::Blur ? BlurBehind : ReplaceBehind;
    case cam::EffectMode::BlurAll: return BlurAll;
    case cam::EffectMode::ChromaKey: return GreenScreen;
    case cam::EffectMode::BlurRegions: return Regions;
    case cam::EffectMode::Foreground: return Shape;
    case cam::EffectMode::MaskImage: return MaskImage;
    }
    return None;
}

void BackgroundPanel::setMode(Mode m)
{
    cancelInteraction();
    cam::EffectParams e = m_controller.effects();
    switch (m) {
    case None: e.mode = cam::EffectMode::Off; break;
    case BlurBehind:
        e.mode = cam::EffectMode::Person;
        e.fill = cam::BackgroundFill::Blur;
        break;
    case ReplaceBehind:
        e.mode = cam::EffectMode::Person;
        if (e.fill == cam::BackgroundFill::Blur)
            e.fill = e.backgroundImagePath.empty() ? cam::BackgroundFill::Color : cam::BackgroundFill::Image;
        break;
    case BlurAll: e.mode = cam::EffectMode::BlurAll; break;
    case GreenScreen: e.mode = cam::EffectMode::ChromaKey; break;
    case Regions: e.mode = cam::EffectMode::BlurRegions; break;
    case Shape: e.mode = cam::EffectMode::Foreground; break;
    case MaskImage: e.mode = cam::EffectMode::MaskImage; break;
    }
    m_controller.setEffects(e);
}

void BackgroundPanel::push()
{
    if (m_syncing)
        return;
    cam::EffectParams e = m_controller.effects();
    e.fill = cam::BackgroundFill(m_fill->currentData().toInt());
    if (modeFromModel() == ReplaceBehind && e.fill == cam::BackgroundFill::Blur)
        e.fill = cam::BackgroundFill::Color;
    e.blurStrength = m_strength->value();
    e.feather = m_softness->value();
    e.fillColor = m_fillColor->color().rgba();
    e.backgroundImagePath = m_bgImage->text().trimmed().toStdString();
    e.foregroundEllipse = m_shape->currentData().toInt() == 1;
    e.maskImagePath = m_maskImage->text().trimmed().toStdString();
    e.keyColor = m_keyColor->color().rgba();
    e.keySimilarity = m_similarity->value();
    e.keySmoothness = m_smoothness->value();
    m_controller.setEffects(e);
}

void BackgroundPanel::syncFromModel()
{
    m_syncing = true;
    const cam::EffectParams &e = m_controller.effects();
    m_mode->setCurrentIndex(std::max(0, m_mode->findData(modeFromModel())));
    m_fill->setCurrentIndex(std::max(0, m_fill->findData(int(e.fill))));
    m_strength->setValue(e.blurStrength);
    m_softness->setValue(e.feather);
    m_fillColor->setColor(QColor::fromRgba(e.fillColor));
    m_bgImage->setText(QString::fromStdString(e.backgroundImagePath));
    m_shape->setCurrentIndex(e.foregroundEllipse ? 0 : 1);
    m_maskImage->setText(QString::fromStdString(e.maskImagePath));
    m_keyColor->setColor(QColor::fromRgba(e.keyColor));
    m_similarity->setValue(e.keySimilarity);
    m_smoothness->setValue(e.keySmoothness);
    m_regions->clear();
    for (size_t i = 0; i < e.blurRegions.size(); ++i)
        m_regions->addItem(tr("Area %1").arg(i + 1));
    m_syncing = false;
    updateVisibility();
    updateOverlay();
}

void BackgroundPanel::updateVisibility()
{
    const Mode m = modeFromModel();
    const cam::EffectParams &e = m_controller.effects();
    static const char *descriptions[] = {
        QT_TR_NOOP("No background effect."),
        QT_TR_NOOP("Detects you and blurs everything behind you. Runs locally."),
        QT_TR_NOOP("Detects you and places an image or color behind you. Runs locally."),
        QT_TR_NOOP("Softens the whole picture."),
        QT_TR_NOOP("Removes a solid-colored backdrop."),
        QT_TR_NOOP("Blurs rectangles you draw on the preview."),
        QT_TR_NOOP("Keeps a shape you draw sharp and replaces the rest."),
        QT_TR_NOOP("Keeps the white parts of an image you provide."),
    };
    m_description->setText(tr(descriptions[m]));

    const bool replaces = m == ReplaceBehind || m == GreenScreen || m == Shape || m == MaskImage;
    m_fillRow->setVisible(replaces);
    // "Replace background" has its own way to blur ("Blur background").
    if (auto *view = qobject_cast<QListView *>(m_fill->view()))
        view->setRowHidden(m_fill->findData(int(cam::BackgroundFill::Blur)), m == ReplaceBehind);
    m_colorRow->setVisible(replaces && e.fill == cam::BackgroundFill::Color);
    m_imageRow->setVisible(replaces && e.fill == cam::BackgroundFill::Image);
    m_keyRow->setVisible(m == GreenScreen);
    m_similarity->setVisible(m == GreenScreen);
    m_smoothness->setVisible(m == GreenScreen);
    m_regionsBox->setVisible(m == Regions);
    m_shapeRow->setVisible(m == Shape);
    m_maskRow->setVisible(m == MaskImage);
    const bool blurs = m == BlurBehind || m == BlurAll || m == Regions ||
                       (replaces && m != ReplaceBehind && e.fill == cam::BackgroundFill::Blur);
    m_strength->setVisible(blurs);
    m_softness->setVisible(m == BlurBehind || m == ReplaceBehind || m == Shape);
}

void BackgroundPanel::updateOverlay()
{
    const cam::EffectParams &e = m_controller.effects();
    QList<QRectF> rects;
    if (e.mode == cam::EffectMode::Foreground)
        rects << QRectF(e.foreground.x, e.foreground.y, e.foreground.w, e.foreground.h);
    m_preview->setOverlayRects(rects, e.foregroundEllipse);
}

} // namespace ui
