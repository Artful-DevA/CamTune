// SPDX-License-Identifier: GPL-3.0-or-later
#include "BackgroundPanel.h"

#include "PreviewWidget.h"
#include "Widgets.h"
#include "app/CameraController.h"
#include "app/ImageUtil.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace ui {

namespace {

QWidget *wrap(QLayout *layout)
{
    auto *w = new QWidget;
    layout->setContentsMargins(0, 0, 0, 0);
    w->setLayout(layout);
    return w;
}

enum ManualType { ManualRegions = 0, ManualShape = 1, ManualMask = 2 };

} // namespace

BackgroundPanel::BackgroundPanel(app::CameraController &controller, PreviewWidget *preview, QWidget *parent)
    : QWidget(parent), m_controller(controller), m_preview(preview)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    // --- The choices -------------------------------------------------------
    m_choices = new QButtonGroup(this);
    m_choices->setExclusive(true);
    const struct {
        Choice id;
        const char *title;
        const char *desc;
    } cards[] = {
        {None, QT_TR_NOOP("None"), QT_TR_NOOP("Show the camera as it is")},
        {BlurBehind, QT_TR_NOOP("Blur background"), QT_TR_NOOP("Finds you and blurs what is behind you")},
        {ReplaceBehind, QT_TR_NOOP("Replace background"), QT_TR_NOOP("Puts a picture or color behind you")},
        {BlurAll, QT_TR_NOOP("Blur everything"), QT_TR_NOOP("Softens the whole picture")},
        {GreenScreen, QT_TR_NOOP("Green screen"), QT_TR_NOOP("Removes a solid-colored backdrop")},
        {Manual, QT_TR_NOOP("Manual areas"), QT_TR_NOOP("Blur or keep areas you draw yourself")},
    };
    for (const auto &c : cards) {
        auto *card = new OptionCard(tr(c.title), tr(c.desc));
        m_cards[c.id] = card;
        m_choices->addButton(card, c.id);
        v->addWidget(card);
    }
    connect(m_choices, &QButtonGroup::idClicked, this, [this](int id) { choose(Choice(id)); });

    auto *options = new QVBoxLayout;
    options->setSpacing(6);
    v->addSpacing(4);

    // --- Manual areas --------------------------------------------------------
    {
        auto *l = new QVBoxLayout;
        m_manualType = new QComboBox;
        m_manualType->addItem(tr("Blur areas I draw"), ManualRegions);
        m_manualType->addItem(tr("Keep a shape I draw sharp"), ManualShape);
        m_manualType->addItem(tr("Use a mask image"), ManualMask);
        l->addWidget(m_manualType);

        auto *rl = new QVBoxLayout;
        m_regions = new QListWidget;
        m_regions->setMaximumHeight(96);
        rl->addWidget(m_regions);
        auto *rb = new QHBoxLayout;
        auto *draw = new QPushButton(tr("Draw area"));
        draw->setObjectName(QStringLiteral("Primary"));
        auto *remove = new QPushButton(tr("Remove"));
        auto *clear = new QPushButton(tr("Clear all"));
        rb->addWidget(draw);
        rb->addWidget(remove);
        rb->addWidget(clear);
        rl->addLayout(rb);
        m_regionsBox = wrap(rl);
        l->addWidget(m_regionsBox);

        auto *sl = new QHBoxLayout;
        m_shape = new SegmentedControl;
        m_shape->addSegment(tr("Oval"), 1);
        m_shape->addSegment(tr("Rectangle"), 0);
        auto *drawShape = new QPushButton(tr("Draw on preview"));
        drawShape->setObjectName(QStringLiteral("Primary"));
        sl->addWidget(m_shape, 1);
        sl->addWidget(drawShape);
        m_shapeBox = wrap(sl);
        l->addWidget(m_shapeBox);

        m_maskBox = imagePicker(m_maskImage, tr("Mask image: white = keep, black = replace"),
                                tr("Choose mask image"));
        l->addWidget(m_maskBox);

        m_manualBox = wrap(l);
        options->addWidget(m_manualBox);

        connect(m_manualType, qOverload<int>(&QComboBox::activated), this, &BackgroundPanel::push);
        connect(m_shape, &SegmentedControl::changed, this, &BackgroundPanel::push);
        connect(draw, &QPushButton::clicked, this, [this] {
            m_pending = Pending::Region;
            m_preview->setInteraction(PreviewWidget::Interaction::DrawRect);
            m_hint->setText(tr("Drag on the preview over the area to blur. Esc cancels."));
            m_hint->show();
        });
        connect(drawShape, &QPushButton::clicked, this, [this] {
            m_pending = Pending::Foreground;
            m_preview->setInteraction(PreviewWidget::Interaction::DrawRect);
            m_hint->setText(tr("Drag on the preview around the area to keep. Esc cancels."));
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

    // --- Green screen --------------------------------------------------------
    {
        auto *l = new QVBoxLayout;
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(tr("Backdrop color")));
        row->addStretch(1);
        m_keyColor = new ColorButton;
        row->addWidget(m_keyColor);
        auto *pick = new QPushButton(tr("Pick"));
        pick->setToolTip(tr("Click the backdrop in the preview"));
        row->addWidget(pick);
        l->addLayout(row);
        m_similarity = new SliderRow(tr("Tolerance"), 0, 1, 0.25, 0, QStringLiteral(" %"), 100);
        m_smoothness = new SliderRow(tr("Edge smoothness"), 0, 1, 0.08, 0, QStringLiteral(" %"), 100);
        l->addWidget(m_similarity);
        l->addWidget(m_smoothness);
        m_keyBox = wrap(l);
        options->addWidget(m_keyBox);
        connect(m_keyColor, &ColorButton::colorChanged, this, &BackgroundPanel::push);
        connect(m_similarity, &SliderRow::valueChanged, this, &BackgroundPanel::push);
        connect(m_smoothness, &SliderRow::valueChanged, this, &BackgroundPanel::push);
        connect(pick, &QPushButton::clicked, this, [this] {
            m_pending = Pending::KeyColor;
            m_preview->setInteraction(PreviewWidget::Interaction::PickPoint);
            m_hint->setText(tr("Click the backdrop color in the preview. Esc cancels."));
            m_hint->show();
        });
    }

    // --- What to put behind ---------------------------------------------------
    {
        auto *l = new QVBoxLayout;
        m_replaceLabel = new QLabel(tr("Replace with"));
        l->addWidget(m_replaceLabel);
        m_fill = new SegmentedControl;
        m_fill->addSegment(tr("Blur"), int(cam::BackgroundFill::Blur));
        m_fill->addSegment(tr("Picture"), int(cam::BackgroundFill::Image));
        m_fill->addSegment(tr("Color"), int(cam::BackgroundFill::Color));
        l->addWidget(m_fill);
        auto *cr = new QHBoxLayout;
        cr->addWidget(new QLabel(tr("Color")));
        cr->addStretch(1);
        m_fillColor = new ColorButton;
        cr->addWidget(m_fillColor);
        m_colorRow = wrap(cr);
        l->addWidget(m_colorRow);
        m_imageRow = imagePicker(m_bgImage, tr("Choose a picture…"), tr("Choose background picture"));
        l->addWidget(m_imageRow);
        m_replaceBox = wrap(l);
        options->addWidget(m_replaceBox);
        connect(m_fill, &SegmentedControl::changed, this, &BackgroundPanel::push);
        connect(m_fillColor, &ColorButton::colorChanged, this, &BackgroundPanel::push);
    }

    m_strength = new SliderRow(tr("Blur strength"), 0, 1, 0.5, 0, QStringLiteral(" %"), 100);
    m_softness = new SliderRow(tr("Edge softness"), 0, 0.3, 0.08, 0, QStringLiteral(" %"), 100 / 0.3);
    m_softness->setHint(tr("How gradually you blend into the background"));
    options->addWidget(m_strength);
    options->addWidget(m_softness);
    connect(m_strength, &SliderRow::valueChanged, this, &BackgroundPanel::push);
    connect(m_softness, &SliderRow::valueChanged, this, &BackgroundPanel::push);

    m_hint = new QLabel;
    m_hint->setObjectName(QStringLiteral("Banner"));
    m_hint->setProperty("level", QStringLiteral("info"));
    m_hint->setWordWrap(true);
    m_hint->hide();
    options->addWidget(m_hint);

    m_cpuNote = hintLabel(tr("Runs entirely on this computer with a small built-in model. It uses noticeably "
                             "more CPU than other settings — prefer 720p output on older computers."));
    options->addWidget(m_cpuNote);
    v->addLayout(options);
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

QWidget *BackgroundPanel::imagePicker(QLineEdit *&edit, const QString &placeholder, const QString &dialogTitle)
{
    auto *h = new QHBoxLayout;
    edit = new QLineEdit;
    edit->setPlaceholderText(placeholder);
    edit->setReadOnly(true);
    auto *browse = new QPushButton(tr("Browse…"));
    h->addWidget(edit, 1);
    h->addWidget(browse);
    QLineEdit *target = edit;
    connect(browse, &QPushButton::clicked, this, [this, target, dialogTitle] {
        const QString f = chooseImage(dialogTitle);
        if (!f.isEmpty()) {
            target->setText(f);
            target->setToolTip(f);
            push();
        }
    });
    return wrap(h);
}

QString BackgroundPanel::chooseImage(const QString &title)
{
    return QFileDialog::getOpenFileName(this, title,
                                        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                        tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
}

void BackgroundPanel::cancelInteraction()
{
    m_pending = Pending::None;
    m_preview->setInteraction(PreviewWidget::Interaction::Navigate);
    m_hint->hide();
}

BackgroundPanel::Choice BackgroundPanel::choiceFromModel() const
{
    const cam::EffectParams &e = m_controller.effects();
    switch (e.mode) {
    case cam::EffectMode::Off: return None;
    case cam::EffectMode::Person: return e.fill == cam::BackgroundFill::Blur ? BlurBehind : ReplaceBehind;
    case cam::EffectMode::BlurAll: return BlurAll;
    case cam::EffectMode::ChromaKey: return GreenScreen;
    default: return Manual;
    }
}

void BackgroundPanel::choose(Choice c)
{
    cancelInteraction();
    cam::EffectParams e = m_controller.effects();
    switch (c) {
    case None:
        e.mode = cam::EffectMode::Off;
        break;
    case BlurBehind:
        e.mode = cam::EffectMode::Person;
        e.fill = cam::BackgroundFill::Blur;
        break;
    case ReplaceBehind:
        e.mode = cam::EffectMode::Person;
        if (e.fill == cam::BackgroundFill::Blur)
            e.fill = e.backgroundImagePath.empty() ? cam::BackgroundFill::Color : cam::BackgroundFill::Image;
        break;
    case BlurAll:
        e.mode = cam::EffectMode::BlurAll;
        break;
    case GreenScreen:
        e.mode = cam::EffectMode::ChromaKey;
        break;
    case Manual:
        if (e.mode != cam::EffectMode::BlurRegions && e.mode != cam::EffectMode::Foreground &&
            e.mode != cam::EffectMode::MaskImage)
            e.mode = cam::EffectMode::BlurRegions;
        break;
    case ChoiceCount:
        return;
    }
    m_controller.setEffects(e);
}

void BackgroundPanel::push()
{
    if (m_syncing)
        return;
    cam::EffectParams e = m_controller.effects();
    if (choiceFromModel() == Manual) {
        const int t = m_manualType->currentData().toInt();
        e.mode = t == ManualShape ? cam::EffectMode::Foreground
                                  : (t == ManualMask ? cam::EffectMode::MaskImage : cam::EffectMode::BlurRegions);
    }
    e.fill = cam::BackgroundFill(m_fill->currentData());
    if (choiceFromModel() == ReplaceBehind && e.fill == cam::BackgroundFill::Blur)
        e.fill = cam::BackgroundFill::Color;
    e.blurStrength = m_strength->value();
    e.feather = m_softness->value();
    e.fillColor = m_fillColor->color().rgba();
    e.backgroundImagePath = m_bgImage->text().trimmed().toStdString();
    e.foregroundEllipse = m_shape->currentData() == 1;
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
    const Choice choice = choiceFromModel();
    if (auto *b = m_choices->button(choice))
        b->setChecked(true);
    m_manualType->setCurrentIndex(e.mode == cam::EffectMode::Foreground ? ManualShape
                                  : e.mode == cam::EffectMode::MaskImage ? ManualMask
                                                                         : ManualRegions);
    m_fill->setCurrentData(int(e.fill));
    m_strength->setValue(e.blurStrength);
    m_softness->setValue(e.feather);
    m_fillColor->setColor(QColor::fromRgba(e.fillColor));
    m_bgImage->setText(QString::fromStdString(e.backgroundImagePath));
    m_shape->setCurrentData(e.foregroundEllipse ? 1 : 0);
    m_maskImage->setText(QString::fromStdString(e.maskImagePath));
    m_keyColor->setColor(QColor::fromRgba(e.keyColor));
    m_similarity->setValue(e.keySimilarity);
    m_smoothness->setValue(e.keySmoothness);
    m_regions->clear();
    for (size_t i = 0; i < e.blurRegions.size(); ++i)
        m_regions->addItem(tr("Area %1").arg(i + 1));
    if (e.blurRegions.empty())
        m_regions->addItem(tr("No areas yet — click “Draw area”"));
    m_syncing = false;
    updateVisibility();
    updateOverlay();
}

void BackgroundPanel::updateVisibility()
{
    const Choice c = choiceFromModel();
    const cam::EffectParams &e = m_controller.effects();
    const bool manual = c == Manual;
    m_manualBox->setVisible(manual);
    m_regionsBox->setVisible(manual && e.mode == cam::EffectMode::BlurRegions);
    m_shapeBox->setVisible(manual && e.mode == cam::EffectMode::Foreground);
    m_maskBox->setVisible(manual && e.mode == cam::EffectMode::MaskImage);
    m_keyBox->setVisible(c == GreenScreen);

    const bool replaces = c == ReplaceBehind || c == GreenScreen ||
                          (manual && e.mode != cam::EffectMode::BlurRegions);
    m_replaceBox->setVisible(replaces);
    m_fill->setSegmentVisible(int(cam::BackgroundFill::Blur), c != ReplaceBehind);
    m_replaceLabel->setText(c == ReplaceBehind ? tr("Put behind you") : tr("Replace the background with"));
    m_colorRow->setVisible(replaces && e.fill == cam::BackgroundFill::Color);
    m_imageRow->setVisible(replaces && e.fill == cam::BackgroundFill::Image);

    const bool blurs = c == BlurBehind || c == BlurAll || (manual && e.mode == cam::EffectMode::BlurRegions) ||
                       (replaces && c != ReplaceBehind && e.fill == cam::BackgroundFill::Blur);
    m_strength->setVisible(blurs);
    m_softness->setVisible(c == BlurBehind || c == ReplaceBehind || (manual && e.mode == cam::EffectMode::Foreground));
    m_cpuNote->setVisible(c == BlurBehind || c == ReplaceBehind);
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
