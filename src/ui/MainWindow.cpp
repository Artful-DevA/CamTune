// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "BackgroundPanel.h"
#include "HardwareControlsPanel.h"
#include "PresetsPanel.h"
#include "PreviewWidget.h"
#include "Theme.h"
#include "Widgets.h"
#include "app/Application.h"
#include "app/GlobalShortcuts.h"
#include "app/SystemIntegration.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <linux/videodev2.h>

#include <cmath>

namespace ui {

namespace {

QString fpsText(double fps)
{
    return std::fabs(fps - std::round(fps)) < 0.05 ? QString::number(int(std::round(fps)))
                                                   : QString::number(fps, 'f', 1);
}

QString formatName(uint32_t f)
{
    switch (f) {
    case V4L2_PIX_FMT_MJPEG:
    case V4L2_PIX_FMT_JPEG: return QObject::tr("compressed");
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_YUV420: return QObject::tr("uncompressed");
    default: return QString::fromStdString(cam::v4l2::fourccToString(f));
    }
}

const int kResolutions[][2] = {{640, 360},  {640, 480},   {960, 540},  {1280, 720},
                               {1280, 960}, {1920, 1080}, {2560, 1440}};
const int kFrameRates[] = {15, 24, 25, 30, 50, 60};

QIcon appIcon()
{
    QIcon icon = QIcon::fromTheme(QStringLiteral("io.github.CamTune"));
    if (icon.isNull()) {
        icon.addFile(QStringLiteral(":/icons/hicolor/camtune-32.png"));
        icon.addFile(QStringLiteral(":/icons/hicolor/camtune-64.png"));
        icon.addFile(QStringLiteral(":/icons/camtune.png"));
    }
    return icon;
}

// A labelled combo box on one row.
QWidget *comboRow(const QString &label, QComboBox *combo)
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 2, 0, 2);
    h->addWidget(new QLabel(label), 1);
    combo->setMinimumWidth(170);
    h->addWidget(combo);
    return w;
}

// A scrollable tab page.
QWidget *page(QWidget *content)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    content->setContentsMargins(16, 10, 18, 16);
    scroll->setWidget(content);
    return scroll;
}

} // namespace

MainWindow::MainWindow(app::Application &app, QWidget *parent)
    : QMainWindow(parent), m_app(app), m_ctl(app.controller())
{
    setWindowTitle(QStringLiteral("CamTune"));
    setWindowIcon(appIcon());
    m_previewPaused = m_app.settings().previewPaused();
    m_showPerformance = m_app.settings().showPerformance();

    auto *central = new QWidget;
    auto *v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    v->addWidget(buildHeader());

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setHandleWidth(1);
    m_splitter->addWidget(buildPreviewColumn());
    m_splitter->addWidget(buildSidebar());
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setChildrenCollapsible(false);
    v->addWidget(m_splitter, 1);
    setCentralWidget(central);

    buildTray();
    buildShortcuts();

    // Model -> view
    connect(&m_ctl, &app::CameraController::previewFrameReady, this, [this] {
        if (auto f = m_ctl.takePreviewFrame())
            m_preview->setFrame(std::move(f));
    });
    connect(&m_ctl, &app::CameraController::devicesChanged, this, [this] {
        syncCameraList();
        syncOutput();
    });
    connect(&m_ctl, &app::CameraController::cameraChanged, this, &MainWindow::syncCameraList);
    connect(&m_ctl, &app::CameraController::modesChanged, this, &MainWindow::syncModes);
    connect(&m_ctl, &app::CameraController::captureRequestChanged, this, &MainWindow::syncModes);
    connect(&m_ctl, &app::CameraController::controlsChanged, this,
            [this] { m_hwPanel->setControls(m_ctl.controls()); });
    connect(&m_ctl, &app::CameraController::controlError, this, [this](const QString &msg) {
        m_controlErrorText = msg;
        m_controlErrorTimer.start();
        updateBanner();
    });
    connect(&m_ctl, &app::CameraController::cameraStateChanged, this, &MainWindow::syncCameraState);
    connect(&m_ctl, &app::CameraController::outputStateChanged, this, &MainWindow::syncOutput);
    connect(&m_ctl, &app::CameraController::outputChanged, this, &MainWindow::syncOutput);
    connect(&m_ctl, &app::CameraController::colorChanged, this, &MainWindow::syncColor);
    connect(&m_ctl, &app::CameraController::framingChanged, this, &MainWindow::syncFraming);
    connect(&m_ctl, &app::CameraController::effectsChanged, m_background, &BackgroundPanel::syncFromModel);
    connect(&m_app.presets(), &app::PresetStore::changed, this, &MainWindow::syncPresets);
    connect(&m_app, &app::Application::currentPresetChanged, this, &MainWindow::syncPresets);

    m_controlErrorTimer.setSingleShot(true);
    m_controlErrorTimer.setInterval(6000);
    connect(&m_controlErrorTimer, &QTimer::timeout, this, [this] {
        m_controlErrorText.clear();
        updateBanner();
    });
    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    m_statsTimer.start();

    syncCameraList();
    syncModes();
    syncColor();
    syncFraming();
    syncOutput();
    syncCameraState();
    syncPresets();
    m_hwPanel->setControls(m_ctl.controls());

    m_preview->setFocus(); // nothing highlighted at start
    if (!restoreGeometry(m_app.settings().windowGeometry()))
        resize(1240, 760);
    if (!m_splitter->restoreState(m_app.settings().splitterState()))
        m_splitter->setSizes({860, 380});
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

QWidget *MainWindow::buildHeader()
{
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("Header"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(14, 8, 10, 8);
    h->setSpacing(10);

    auto *logo = new QLabel;
    logo->setPixmap(appIcon().pixmap(28, 28));
    h->addWidget(logo);
    auto *title = new QLabel(QStringLiteral("CamTune"));
    title->setObjectName(QStringLiteral("AppTitle"));
    h->addWidget(title);
    h->addSpacing(18);

    m_cameraCombo = new QComboBox;
    m_cameraCombo->setMinimumWidth(240);
    m_cameraCombo->setToolTip(tr("Which webcam to use"));
    h->addWidget(m_cameraCombo);
    h->addStretch(1);

    m_presetsButton = new QToolButton;
    m_presetsButton->setObjectName(QStringLiteral("HeaderButton"));
    m_presetsButton->setText(tr("Presets  ▾"));
    m_presetsButton->setPopupMode(QToolButton::InstantPopup);
    m_presetsMenu = new QMenu(m_presetsButton);
    m_presetsButton->setMenu(m_presetsMenu);
    h->addWidget(m_presetsButton);
    h->addSpacing(8);

    // Virtual camera: the one switch that matters most.
    auto *vcam = new QWidget;
    auto *vh = new QHBoxLayout(vcam);
    vh->setContentsMargins(0, 0, 0, 0);
    vh->setSpacing(8);
    m_vcamSwitch = new ToggleSwitch;
    m_vcamSwitch->setToolTip(tr("Send the picture to video-call apps (Ctrl+Shift+V)"));
    vh->addWidget(m_vcamSwitch);
    auto *vt = new QVBoxLayout;
    vt->setSpacing(0);
    auto *vl = new QLabel(tr("Virtual camera"));
    QFont bf = vl->font();
    bf.setWeight(QFont::DemiBold);
    vl->setFont(bf);
    vt->addWidget(vl);
    m_vcamState = new QLabel;
    m_vcamState->setObjectName(QStringLiteral("Muted"));
    QFont sf = m_vcamState->font();
    sf.setPointSizeF(sf.pointSizeF() * 0.88);
    m_vcamState->setFont(sf);
    vt->addWidget(m_vcamState);
    vh->addLayout(vt);
    h->addWidget(vcam);
    h->addSpacing(6);

    auto *menuButton = new QToolButton;
    menuButton->setObjectName(QStringLiteral("HeaderButton"));
    menuButton->setText(QStringLiteral("☰"));
    menuButton->setToolTip(tr("Menu"));
    menuButton->setPopupMode(QToolButton::InstantPopup);
    menuButton->setMenu(buildMainMenu());
    h->addWidget(menuButton);

    connect(m_cameraCombo, qOverload<int>(&QComboBox::activated), this, [this](int idx) {
        const int i = m_cameraCombo->itemData(idx).toInt();
        if (i >= 0 && i < m_cameraEntries.size())
            m_ctl.setCamera(m_cameraEntries[i]);
    });
    connect(m_vcamSwitch, &ToggleSwitch::toggled, this, [this](bool on) {
        if (!m_syncing)
            m_ctl.setVirtualCameraEnabled(on);
    });
    return bar;
}

QWidget *MainWindow::buildPreviewColumn()
{
    auto *col = new QWidget;
    col->setObjectName(QStringLiteral("PreviewColumn"));
    col->setAttribute(Qt::WA_StyledBackground);
    auto *v = new QVBoxLayout(col);
    v->setContentsMargins(14, 12, 14, 10);
    v->setSpacing(10);

    m_banner = new QLabel;
    m_banner->setObjectName(QStringLiteral("Banner"));
    m_banner->setWordWrap(true);
    m_banner->hide();
    v->addWidget(m_banner);

    m_preview = new PreviewWidget;
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    v->addWidget(m_preview, 1);

    // Quick controls under the picture.
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("PreviewBar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(10, 6, 10, 6);
    h->setSpacing(6);
    auto *zoomOut = new QToolButton;
    zoomOut->setText(QStringLiteral("−"));
    zoomOut->setToolTip(tr("Zoom out (Ctrl+−)"));
    auto *zoomIn = new QToolButton;
    zoomIn->setText(QStringLiteral("+"));
    zoomIn->setToolTip(tr("Zoom in (Ctrl++)"));
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(100, 400);
    m_zoomSlider->setFixedWidth(150);
    m_zoomSlider->setToolTip(tr("Zoom — you can also scroll on the picture"));
    m_zoomLabel = new QLabel(QStringLiteral("1.0×"));
    m_zoomLabel->setObjectName(QStringLiteral("ValueText"));
    m_zoomLabel->setFixedWidth(38);
    h->addWidget(zoomOut);
    h->addWidget(m_zoomSlider);
    h->addWidget(zoomIn);
    h->addWidget(m_zoomLabel);
    m_mirrorButton = new QPushButton(tr("Mirror"));
    m_mirrorButton->setObjectName(QStringLiteral("Chip"));
    m_mirrorButton->setCheckable(true);
    m_mirrorButton->setToolTip(tr("Flip the picture left–right"));
    h->addWidget(m_mirrorButton);
    auto *reset = new QPushButton(tr("Reset view"));
    reset->setObjectName(QStringLiteral("Chip"));
    reset->setToolTip(tr("Reset zoom and position (Ctrl+0, or double-click the picture)"));
    h->addWidget(reset);
    h->addStretch(1);
    m_chipLayout = new QHBoxLayout;
    m_chipLayout->setSpacing(6);
    h->addLayout(m_chipLayout);
    v->addWidget(bar);

    // Status line.
    auto *status = new QHBoxLayout;
    status->setContentsMargins(4, 0, 4, 0);
    m_statusDot = new QLabel(QStringLiteral("●"));
    m_statusText = new QLabel;
    m_statusText->setObjectName(QStringLiteral("StatusText"));
    m_perfText = new QLabel;
    m_perfText->setObjectName(QStringLiteral("StatusText"));
    status->addWidget(m_statusDot);
    status->addWidget(m_statusText, 1);
    status->addWidget(m_perfText);
    v->addLayout(status);

    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int z) {
        m_zoomLabel->setText(QStringLiteral("%1×").arg(z / 100.0, 0, 'f', 1));
        if (m_syncing)
            return;
        cam::FramingParams f = m_ctl.framing();
        f.zoom = z / 100.0;
        m_ctl.setFraming(f, app::CameraController::kSliderTransitionMs);
    });
    connect(zoomIn, &QToolButton::clicked, this, [this] { m_app.adjustZoom(0.1); });
    connect(zoomOut, &QToolButton::clicked, this, [this] { m_app.adjustZoom(-0.1); });
    connect(m_mirrorButton, &QPushButton::toggled, this, [this](bool on) {
        if (m_syncing)
            return;
        cam::FramingParams f = m_ctl.framing();
        f.mirror = on;
        m_ctl.setFraming(f, 0);
    });
    connect(reset, &QPushButton::clicked, this, [this] { m_app.resetFraming(); });
    connect(m_preview, &PreviewWidget::zoomRequested, this, [this](double steps) {
        cam::FramingParams f = m_ctl.framing();
        f.zoom = std::clamp(f.zoom * std::pow(1.08, steps), 1.0, 8.0);
        m_ctl.setFraming(f, 120);
    });
    connect(m_preview, &PreviewWidget::panRequested, this, &MainWindow::panBy);
    connect(m_preview, &PreviewWidget::doubleClicked, this, [this] { m_app.resetFraming(); });
    return col;
}

QWidget *MainWindow::buildSidebar()
{
    auto *side = new QWidget;
    side->setObjectName(QStringLiteral("Sidebar"));
    side->setAttribute(Qt::WA_StyledBackground);
    side->setMinimumWidth(340);
    side->setMaximumWidth(520);
    auto *v = new QVBoxLayout(side);
    v->setContentsMargins(0, 0, 0, 0);
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(true);
    m_tabs->tabBar()->setExpanding(true);
    m_tabs->addTab(page(buildPictureTab()), tr("Picture"));
    m_tabs->addTab(page(buildFramingTab()), tr("Framing"));
    auto *bg = new QWidget;
    auto *bgl = new QVBoxLayout(bg);
    bgl->setContentsMargins(0, 0, 0, 0);
    m_background = new BackgroundPanel(m_ctl, m_preview);
    bgl->addWidget(m_background);
    m_tabs->addTab(page(bg), tr("Background"));
    m_tabs->addTab(page(buildCameraTab()), tr("Camera"));
    m_tabs->addTab(page(buildOutputTab()), tr("Output"));
    v->addWidget(m_tabs);

    const int bgIndex = 2;
    connect(m_tabs, &QTabWidget::currentChanged, this, [this, bgIndex](int i) {
        // Outlines and drawing modes only make sense while the tab is open.
        if (i == bgIndex) {
            m_background->syncFromModel();
        } else {
            m_background->cancelInteraction();
            m_preview->setOverlayRects({});
        }
        m_app.settings().setLastTab(i);
    });
    m_tabs->setCurrentIndex(std::clamp(m_app.settings().lastTab(), 0, m_tabs->count() - 1));
    if (m_tabs->currentIndex() != bgIndex)
        m_preview->setOverlayRects({});
    return side;
}

QWidget *MainWindow::buildPictureTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    auto *light = new Section(tr("Light"));
    m_brightness = new SliderRow(tr("Brightness"), -0.5, 0.5, 0, 0, QString(), 200);
    m_contrast = new SliderRow(tr("Contrast"), 0.2, 2.5, 1, 0, QStringLiteral(" %"), 100);
    m_gamma = new SliderRow(tr("Midtones"), 0.3, 3.0, 1, 2);
    m_gamma->setHint(tr("Brightens or darkens the middle tones without blowing out highlights (gamma)"));
    for (auto *s : {m_brightness, m_contrast, m_gamma})
        light->contentLayout()->addWidget(s);
    v->addWidget(light);

    auto *color = new Section(tr("Color"));
    m_saturation = new SliderRow(tr("Saturation"), 0, 2.5, 1, 0, QStringLiteral(" %"), 100);
    m_warmth = new SliderRow(tr("Warmth"), -1, 1, 0, 0, QString(), 100);
    m_warmth->setHint(tr("Cooler (blue) ← → warmer (amber)"));
    m_tint = new SliderRow(tr("Tint"), -1, 1, 0, 0, QString(), 100);
    m_tint->setHint(tr("Greener ← → more magenta"));
    for (auto *s : {m_saturation, m_warmth, m_tint})
        color->contentLayout()->addWidget(s);
    v->addWidget(color);

    auto *detail = new Section(tr("Detail"));
    m_sharpness = new SliderRow(tr("Sharpness"), 0, 2, 0, 0, QStringLiteral(" %"), 50);
    detail->contentLayout()->addWidget(m_sharpness);
    v->addWidget(detail);

    for (SliderRow *s : {m_brightness, m_contrast, m_saturation, m_gamma, m_sharpness, m_warmth, m_tint})
        connect(s, &SliderRow::valueChanged, this, &MainWindow::pushColor);
    auto *reset = new QPushButton(tr("Reset picture"));
    connect(reset, &QPushButton::clicked, this, [this] { m_ctl.setColor(cam::ColorParams()); });
    v->addSpacing(6);
    v->addWidget(reset);
    v->addWidget(hintLabel(tr("These adjust the picture in CamTune. Settings stored in the camera itself "
                              "are on the Camera tab.")));
    v->addStretch(1);
    return w;
}

QWidget *MainWindow::buildFramingTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    auto *view = new Section(tr("Zoom & position"));
    m_zoom = new SliderRow(tr("Zoom"), 1, 8, 1, 2, QStringLiteral("×"));
    m_panX = new SliderRow(tr("Left ↔ right"), -1, 1, 0, 0, QStringLiteral(" %"), 100);
    m_panY = new SliderRow(tr("Up ↕ down"), -1, 1, 0, 0, QStringLiteral(" %"), 100);
    m_panX->setHint(tr("Moves the view when zoomed in. You can also drag the picture."));
    m_panY->setHint(tr("Moves the view when zoomed in. You can also drag the picture."));
    for (auto *s : {m_zoom, m_panX, m_panY})
        view->contentLayout()->addWidget(s);
    v->addWidget(view);

    auto *orient = new Section(tr("Orientation"));
    m_rotation = new SegmentedControl;
    for (int deg : {0, 90, 180, 270})
        m_rotation->addSegment(QStringLiteral("%1°").arg(deg), deg);
    orient->contentLayout()->addWidget(new QLabel(tr("Rotate")));
    orient->contentLayout()->addWidget(m_rotation);
    m_straighten = new SliderRow(tr("Straighten"), -15, 15, 0, 1, QStringLiteral("°"));
    m_straighten->setHint(tr("Fixes a slightly tilted camera"));
    orient->contentLayout()->addWidget(m_straighten);
    m_mirror = new ToggleRow(tr("Mirror"), tr("Others see you flipped left–right"));
    m_flip = new ToggleRow(tr("Upside down"), tr("For cameras mounted upside down"));
    orient->contentLayout()->addWidget(m_mirror);
    orient->contentLayout()->addWidget(m_flip);
    v->addWidget(orient);

    auto *fit = new Section(tr("Shape"));
    m_aspect = new SegmentedControl;
    m_aspect->addSegment(tr("Fill"), int(cam::AspectMode::Fill), tr("Crop the edges so the picture fills the frame"));
    m_aspect->addSegment(tr("Fit"), int(cam::AspectMode::Fit), tr("Show everything, with bars at the sides"));
    m_aspect->addSegment(tr("Stretch"), int(cam::AspectMode::Stretch), tr("Squash the picture to fit"));
    fit->contentLayout()->addWidget(m_aspect);
    fit->contentLayout()->addWidget(
        hintLabel(tr("Used when the camera's shape differs from the output (e.g. 4:3 vs 16:9).")));
    v->addWidget(fit);

    auto *crop = new Section(tr("Crop edges"), true);
    m_cropL = new SliderRow(tr("Left"), 0, 0.45, 0, 0, QStringLiteral(" %"), 100);
    m_cropR = new SliderRow(tr("Right"), 0, 0.45, 0, 0, QStringLiteral(" %"), 100);
    m_cropT = new SliderRow(tr("Top"), 0, 0.45, 0, 0, QStringLiteral(" %"), 100);
    m_cropB = new SliderRow(tr("Bottom"), 0, 0.45, 0, 0, QStringLiteral(" %"), 100);
    for (auto *s : {m_cropL, m_cropR, m_cropT, m_cropB})
        crop->contentLayout()->addWidget(s);
    v->addWidget(crop);

    auto *reset = new QPushButton(tr("Reset framing"));
    connect(reset, &QPushButton::clicked, this, [this] { m_app.resetFraming(); });
    v->addSpacing(6);
    v->addWidget(reset);
    v->addStretch(1);

    for (SliderRow *s : {m_zoom, m_panX, m_panY, m_straighten, m_cropL, m_cropR, m_cropT, m_cropB})
        connect(s, &SliderRow::valueChanged, this, [this] { pushFraming(); });
    connect(m_rotation, &SegmentedControl::changed, this, [this] { pushFraming(250); });
    connect(m_aspect, &SegmentedControl::changed, this, [this] { pushFraming(0); });
    connect(m_mirror, &ToggleRow::toggled, this, [this] { pushFraming(0); });
    connect(m_flip, &ToggleRow::toggled, this, [this] { pushFraming(0); });
    return w;
}

QWidget *MainWindow::buildCameraTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    auto *mode = new Section(tr("Camera resolution"));
    m_modeCombo = new QComboBox;
    mode->contentLayout()->addWidget(m_modeCombo);
    mode->contentLayout()->addWidget(
        hintLabel(tr("“Automatic” picks the best mode for your output resolution and frame rate.")));
    v->addWidget(mode);
    connect(m_modeCombo, qOverload<int>(&QComboBox::activated), this, [this](int idx) {
        const int i = m_modeCombo->itemData(idx).toInt();
        if (i >= 0 && i < m_modeEntries.size())
            m_ctl.setCaptureRequest(m_modeEntries[i]);
    });

    m_hwPanel = new HardwareControlsPanel;
    v->addWidget(m_hwPanel);
    connect(m_hwPanel, &HardwareControlsPanel::controlChanged, &m_ctl, &app::CameraController::setHardwareControl);
    connect(m_hwPanel, &HardwareControlsPanel::resetRequested, &m_ctl,
            &app::CameraController::resetHardwareControls);
    v->addStretch(1);
    return w;
}

QWidget *MainWindow::buildOutputTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    auto *vcam = new Section(tr("Virtual camera"));
    m_outEnabled = new ToggleRow(tr("Send to video calls"),
                                 tr("Then choose “CamTune” as the camera in Zoom, Meet, Teams, Discord…"));
    vcam->contentLayout()->addWidget(m_outEnabled);
    m_outStatus = new QLabel;
    m_outStatus->setWordWrap(true);
    vcam->contentLayout()->addWidget(m_outStatus);
    m_setupButton = new QPushButton(tr("Set up virtual camera…"));
    m_setupButton->setObjectName(QStringLiteral("Primary"));
    vcam->contentLayout()->addWidget(m_setupButton);
    m_outResolution = new QComboBox;
    for (auto &r : kResolutions)
        m_outResolution->addItem(QStringLiteral("%1 × %2").arg(r[0]).arg(r[1]), QSize(r[0], r[1]));
    vcam->contentLayout()->addWidget(comboRow(tr("Resolution"), m_outResolution));
    m_outFps = new QComboBox;
    for (int f : kFrameRates)
        m_outFps->addItem(tr("%1 fps").arg(f), f);
    vcam->contentLayout()->addWidget(comboRow(tr("Frame rate"), m_outFps));
    v->addWidget(vcam);

    auto *compat = new Section(tr("Compatibility"), true);
    m_outDevice = new QComboBox;
    compat->contentLayout()->addWidget(comboRow(tr("Device"), m_outDevice));
    m_outFormat = new QComboBox;
    m_outFormat->addItem(tr("Standard (recommended)"), int(cam::OutputPixelFormat::I420));
    m_outFormat->addItem(tr("Alternative (YUYV)"), int(cam::OutputPixelFormat::YUYV));
    compat->contentLayout()->addWidget(comboRow(tr("Video format"), m_outFormat));
    compat->contentLayout()->addWidget(
        hintLabel(tr("Only change these if an app does not show or accept the virtual camera.")));
    v->addWidget(compat);

    auto *appSec = new Section(tr("App"));
    auto *login = new ToggleRow(tr("Start when I log in"), tr("So the virtual camera is always ready"));
    login->setChecked(app::autostart::isEnabled());
    auto *minimized = new ToggleRow(tr("Start hidden"), tr("Open in the system tray instead of a window"));
    minimized->setChecked(m_app.settings().startMinimized());
    auto *tray = new ToggleRow(tr("Keep running when closed"), tr("Closing the window keeps the virtual camera on"));
    tray->setChecked(m_app.settings().closeToTray());
    auto *global = new ToggleRow(tr("Shortcuts from any app"), tr("Ctrl+Alt+1…9 apply presets even while in a call"));
    global->setChecked(m_app.settings().globalShortcuts());
    auto *globalStatus = hintLabel(QString());
    globalStatus->hide();
    auto *perf = new ToggleRow(tr("Show performance info"), tr("Processing time and delay under the preview"));
    perf->setChecked(m_showPerformance);
    for (QWidget *x : {static_cast<QWidget *>(login), static_cast<QWidget *>(minimized),
                       static_cast<QWidget *>(tray), static_cast<QWidget *>(global),
                       static_cast<QWidget *>(globalStatus), static_cast<QWidget *>(perf)})
        appSec->contentLayout()->addWidget(x);
    v->addWidget(appSec);
    v->addStretch(1);

    connect(m_outEnabled, &ToggleRow::toggled, this, [this](bool on) {
        if (!m_syncing)
            m_ctl.setVirtualCameraEnabled(on);
    });
    for (QComboBox *c : {m_outDevice, m_outResolution, m_outFps, m_outFormat})
        connect(c, qOverload<int>(&QComboBox::activated), this, &MainWindow::pushOutput);
    connect(m_setupButton, &QPushButton::clicked, this, &MainWindow::runVirtualCameraSetup);
    connect(login, &ToggleRow::toggled, this, [this, login](bool on) {
        QString err;
        if (!app::autostart::setEnabled(on, &err)) {
            QMessageBox::warning(this, tr("Start when I log in"), err);
            login->setChecked(!on);
        }
    });
    connect(minimized, &ToggleRow::toggled, this, [this](bool on) { m_app.settings().setStartMinimized(on); });
    connect(tray, &ToggleRow::toggled, this, [this](bool on) { m_app.settings().setCloseToTray(on); });
    connect(global, &ToggleRow::toggled, this, [this](bool on) { m_app.setGlobalShortcutsEnabled(on); });
    connect(m_app.globalShortcuts(), &app::GlobalShortcuts::statusChanged, globalStatus, [this, globalStatus] {
        // Only explain the portal state to someone who asked for global shortcuts.
        const QString s = m_app.settings().globalShortcuts() ? m_app.globalShortcuts()->statusText() : QString();
        globalStatus->setText(s);
        globalStatus->setVisible(!s.isEmpty());
    });
    connect(perf, &ToggleRow::toggled, this, [this](bool on) {
        m_showPerformance = on;
        m_app.settings().setShowPerformance(on);
        updateStatus();
    });
    return w;
}

QMenu *MainWindow::buildMainMenu()
{
    auto *menu = new QMenu(this);
    m_pauseAction = menu->addAction(tr("Pause preview"));
    m_pauseAction->setCheckable(true);
    m_pauseAction->setChecked(m_previewPaused);
    m_pauseAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    connect(m_pauseAction, &QAction::toggled, this, [this](bool on) {
        m_previewPaused = on;
        m_app.settings().setPreviewPaused(on);
        updatePreviewWanted();
    });
    menu->addSeparator();
    connect(menu->addAction(tr("Set up virtual camera…")), &QAction::triggered, this,
            &MainWindow::runVirtualCameraSetup);
    connect(menu->addAction(tr("Command line && automation…")), &QAction::triggered, this,
            &MainWindow::showAutomationHelp);
    connect(menu->addAction(tr("About CamTune")), &QAction::triggered, this, &MainWindow::showAbout);
    menu->addSeparator();
    QAction *quit = menu->addAction(tr("Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, [this] { m_app.quit(); });
    // Make the shortcuts work while the menu is closed.
    addAction(m_pauseAction);
    addAction(quit);
    return menu;
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(appIcon(), this);
    m_tray->setToolTip(QStringLiteral("CamTune"));
    m_trayMenu = new QMenu(this);
    m_trayShow = m_trayMenu->addAction(tr("Show window"));
    connect(m_trayShow, &QAction::triggered, this, [this] {
        if (isVisible() && !isMinimized())
            hideToTray();
        else
            showAndRaise();
    });
    m_trayVcam = m_trayMenu->addAction(tr("Virtual camera"));
    m_trayVcam->setCheckable(true);
    connect(m_trayVcam, &QAction::triggered, this, [this](bool on) { m_ctl.setVirtualCameraEnabled(on); });
    m_trayPresets = m_trayMenu->addMenu(tr("Presets"));
    m_trayMenu->addSeparator();
    connect(m_trayMenu->addAction(tr("Quit")), &QAction::triggered, this, [this] { m_app.quit(); });
    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger) {
            if (isVisible() && !isMinimized())
                hideToTray();
            else
                showAndRaise();
        }
    });
    connect(m_trayMenu, &QMenu::aboutToShow, this, [this] {
        m_trayShow->setText(isVisible() && !isMinimized() ? tr("Hide window") : tr("Show window"));
    });
    m_tray->show();
}

void MainWindow::buildShortcuts()
{
    for (int i = 1; i <= 9; ++i) {
        auto *sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_0 + i)), this);
        connect(sc, &QShortcut::activated, this, [this, i] {
            const int idx = m_app.presets().indexForShortcut(i);
            if (idx >= 0)
                m_app.applyPresetIndex(idx);
        });
    }
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this), &QShortcut::activated, this,
            [this] { m_app.resetFraming(); });
    connect(new QShortcut(QKeySequence::ZoomIn, this), &QShortcut::activated, this, [this] { m_app.adjustZoom(0.1); });
    connect(new QShortcut(QKeySequence::ZoomOut, this), &QShortcut::activated, this,
            [this] { m_app.adjustZoom(-0.1); });
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), this), &QShortcut::activated, this,
            [this] { m_ctl.setVirtualCameraEnabled(!m_ctl.output().enabled); });
    connect(new QShortcut(QKeySequence::Save, this), &QShortcut::activated, this, &MainWindow::savePreset);
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, m_background,
            &BackgroundPanel::cancelInteraction);
}

// ---------------------------------------------------------------------------
// Model -> view
// ---------------------------------------------------------------------------

void MainWindow::syncCameraList()
{
    m_syncing = true;
    m_cameraCombo->clear();
    m_cameraEntries.clear();
    const cam::CameraSelection &current = m_ctl.camera();
    int currentIndex = -1;
    for (const auto &d : m_ctl.cameras()) {
        cam::CameraSelection s;
        s.byIdPath = d.byIdPath;
        s.card = d.card;
        s.busInfo = d.busInfo;
        s.path = d.path;
        const bool isCurrent = !current.testPattern &&
                               ((!current.byIdPath.empty() && current.byIdPath == s.byIdPath) ||
                                (current.card == s.card && current.busInfo == s.busInfo));
        if (isCurrent)
            currentIndex = m_cameraEntries.size();
        m_cameraEntries.append(isCurrent ? current : s);
        m_cameraCombo->addItem(QString::fromStdString(d.card), m_cameraEntries.size() - 1);
        m_cameraCombo->setItemData(m_cameraCombo->count() - 1, QString::fromStdString(d.path), Qt::ToolTipRole);
    }
    if (!current.empty() && !current.testPattern && currentIndex < 0) {
        // Keep showing an unplugged camera: it is restored when plugged back in.
        currentIndex = m_cameraEntries.size();
        m_cameraEntries.append(current);
        m_cameraCombo->addItem(tr("%1 (unplugged)").arg(QString::fromStdString(current.card)),
                               m_cameraEntries.size() - 1);
    }
    cam::CameraSelection test;
    test.testPattern = true;
    if (current.testPattern)
        currentIndex = m_cameraEntries.size();
    m_cameraEntries.append(test);
    m_cameraCombo->addItem(tr("Test pattern"), m_cameraEntries.size() - 1);
    cam::CameraSelection none;
    if (current.empty())
        currentIndex = m_cameraEntries.size();
    m_cameraEntries.append(none);
    m_cameraCombo->addItem(tr("No camera"), m_cameraEntries.size() - 1);
    m_cameraCombo->setCurrentIndex(currentIndex);
    m_syncing = false;
}

void MainWindow::syncModes()
{
    m_syncing = true;
    m_modeCombo->clear();
    m_modeEntries.clear();
    const auto &active = m_ctl.activeMode();
    m_modeEntries.append(cam::CaptureRequest());
    QString autoText = tr("Automatic (recommended)");
    if (active.width > 0 && m_ctl.captureRequest().automatic)
        autoText = tr("Automatic — %1×%2, %3 fps").arg(active.width).arg(active.height).arg(fpsText(active.fps));
    m_modeCombo->addItem(autoText, 0);
    int currentIndex = 0;
    const cam::CaptureRequest &req = m_ctl.captureRequest();
    for (const auto &m : m_ctl.modes()) {
        for (const auto &r : m.rates) {
            cam::CaptureRequest q;
            q.automatic = false;
            q.fourcc = m.fourcc;
            q.width = m.width;
            q.height = m.height;
            q.rate = r;
            if (!req.automatic && req == q)
                currentIndex = m_modeEntries.size();
            m_modeEntries.append(q);
            m_modeCombo->addItem(tr("%1×%2, %3 fps (%4)")
                                     .arg(m.width)
                                     .arg(m.height)
                                     .arg(fpsText(r.fps()))
                                     .arg(formatName(m.fourcc)),
                                 m_modeEntries.size() - 1);
        }
    }
    if (!req.automatic && currentIndex == 0 && m_ctl.modes().empty()) {
        m_modeEntries.append(req);
        m_modeCombo->addItem(tr("%1×%2, %3 fps").arg(req.width).arg(req.height).arg(fpsText(req.rate.fps())),
                             m_modeEntries.size() - 1);
        currentIndex = m_modeEntries.size() - 1;
    }
    m_modeCombo->setCurrentIndex(currentIndex);
    m_syncing = false;
}

void MainWindow::syncColor()
{
    const cam::ColorParams &c = m_ctl.color();
    m_brightness->setValue(c.brightness);
    m_contrast->setValue(c.contrast);
    m_saturation->setValue(c.saturation);
    m_gamma->setValue(c.gamma);
    m_sharpness->setValue(c.sharpness);
    m_warmth->setValue(c.warmth);
    m_tint->setValue(c.tint);
}

void MainWindow::syncFraming()
{
    m_syncing = true;
    const cam::FramingParams &f = m_ctl.framing();
    m_zoom->setValue(f.zoom);
    if (!m_zoomSlider->isSliderDown())
        m_zoomSlider->setValue(int(std::lround(std::min(f.zoom, 4.0) * 100)));
    m_zoomLabel->setText(QStringLiteral("%1×").arg(f.zoom, 0, 'f', 1));
    m_panX->setValue(f.panX);
    m_panY->setValue(f.panY);
    double rot = std::fmod(f.rotation, 360.0);
    if (rot < 0)
        rot += 360;
    const int quarter = int(std::floor((rot + 45) / 90)) % 4;
    double fine = rot - quarter * 90;
    if (fine > 180)
        fine -= 360;
    m_rotation->setCurrentData(quarter * 90);
    m_straighten->setValue(fine);
    m_mirror->setChecked(f.mirror);
    m_flip->setChecked(f.flip);
    m_mirrorButton->setChecked(f.mirror);
    m_aspect->setCurrentData(int(f.aspect));
    m_cropL->setValue(f.cropLeft);
    m_cropR->setValue(f.cropRight);
    m_cropT->setValue(f.cropTop);
    m_cropB->setValue(f.cropBottom);
    m_syncing = false;
}

void MainWindow::syncOutput()
{
    m_syncing = true;
    const cam::OutputConfig &o = m_ctl.output();
    m_outEnabled->setChecked(o.enabled);
    {
        QSignalBlocker b(m_vcamSwitch);
        m_vcamSwitch->setChecked(o.enabled);
        m_vcamSwitch->update();
    }
    if (m_trayVcam)
        m_trayVcam->setChecked(o.enabled);

    m_outDevice->clear();
    m_outDevice->addItem(tr("Automatic"), QString());
    int devIndex = 0;
    for (const auto &d : m_ctl.loopbackDevices()) {
        const QString path = QString::fromStdString(d.path);
        m_outDevice->addItem(QString::fromStdString(d.card), path);
        m_outDevice->setItemData(m_outDevice->count() - 1, path, Qt::ToolTipRole);
        if (path.toStdString() == o.devicePath)
            devIndex = m_outDevice->count() - 1;
    }
    if (!o.devicePath.empty() && devIndex == 0) {
        m_outDevice->addItem(tr("%1 (missing)").arg(QString::fromStdString(o.devicePath)),
                             QString::fromStdString(o.devicePath));
        devIndex = m_outDevice->count() - 1;
    }
    m_outDevice->setCurrentIndex(devIndex);
    int resIndex = m_outResolution->findData(QSize(o.width, o.height));
    if (resIndex < 0) {
        m_outResolution->addItem(QStringLiteral("%1 × %2").arg(o.width).arg(o.height), QSize(o.width, o.height));
        resIndex = m_outResolution->count() - 1;
    }
    m_outResolution->setCurrentIndex(resIndex);
    int fpsIndex = m_outFps->findData(o.fps);
    if (fpsIndex < 0) {
        m_outFps->addItem(tr("%1 fps").arg(o.fps), o.fps);
        fpsIndex = m_outFps->count() - 1;
    }
    m_outFps->setCurrentIndex(fpsIndex);
    m_outFormat->setCurrentIndex(std::max(0, m_outFormat->findData(int(o.pixelFormat))));

    QString state, detail, color = QStringLiteral("#8a93a6");
    const bool noDevice = m_ctl.loopbackDevices().isEmpty();
    switch (m_ctl.outputState()) {
    case cam::OutputState::Disabled:
        state = noDevice ? tr("Not set up") : tr("Off");
        detail = noDevice ? tr("The virtual camera needs a one-time setup.") : QString();
        break;
    case cam::OutputState::Active: {
        int w, h;
        m_ctl.renderSize(w, h);
        state = tr("On · %1×%2").arg(w).arg(h);
        detail = m_ctl.outputMessage().isEmpty() ? tr("Active. Choose “CamTune” as the camera in your call app.")
                                                 : m_ctl.outputMessage();
        color = QStringLiteral("#34c77b");
        break;
    }
    case cam::OutputState::NoDevice:
        state = tr("Not set up");
        detail = tr("The virtual camera needs a one-time setup.");
        color = QStringLiteral("#ffb454");
        break;
    case cam::OutputState::Error:
        state = tr("Problem");
        detail = m_ctl.outputMessage();
        color = QStringLiteral("#ff5d5d");
        break;
    }
    m_vcamState->setText(state);
    m_vcamState->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    m_outStatus->setText(detail);
    m_outStatus->setVisible(!detail.isEmpty());
    m_outStatus->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    m_setupButton->setVisible(noDevice);
    if (m_tray)
        m_tray->setToolTip(m_ctl.outputState() == cam::OutputState::Active ? tr("CamTune — virtual camera on")
                                                                           : QStringLiteral("CamTune"));
    m_syncing = false;
    updateBanner();
}

void MainWindow::syncCameraState()
{
    switch (m_ctl.cameraState()) {
    case cam::CameraState::Streaming:
        m_preview->setMessage(m_previewPaused ? tr("Preview paused — the virtual camera keeps running") : QString());
        break;
    case cam::CameraState::Opening:
        m_preview->setMessage(tr("Starting camera…"));
        break;
    case cam::CameraState::NoCamera:
        m_preview->clearFrame();
        m_preview->setMessage(tr("Choose a camera at the top left"));
        break;
    default:
        m_preview->clearFrame();
        m_preview->setMessage(m_ctl.cameraMessage());
        break;
    }
    syncModes();
    updateBanner();
    updateStatus();
}

void MainWindow::syncPresets()
{
    const auto &list = m_app.presets().presets();
    const QString current = m_app.currentPresetName();

    // Header menu.
    m_presetsMenu->clear();
    QAction *save = m_presetsMenu->addAction(tr("Save current settings as preset…"));
    save->setShortcut(QKeySequence::Save);
    save->setShortcutContext(Qt::WidgetShortcut); // the window-wide QShortcut handles the key
    connect(save, &QAction::triggered, this, &MainWindow::savePreset);
    m_presetsMenu->addSeparator();
    for (int i = 0; i < list.size(); ++i) {
        QAction *a = m_presetsMenu->addAction(list[i].name);
        a->setCheckable(true);
        a->setChecked(list[i].name == current);
        if (list[i].shortcut) {
            a->setShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_0 + list[i].shortcut)));
            a->setShortcutContext(Qt::WidgetShortcut);
        }
        connect(a, &QAction::triggered, this, [this, i] { m_app.applyPresetIndex(i); });
    }
    m_presetsMenu->addSeparator();
    connect(m_presetsMenu->addAction(tr("Manage presets…")), &QAction::triggered, this, &MainWindow::managePresets);

    // One-click chips under the preview.
    while (QLayoutItem *item = m_chipLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const int shown = std::min<int>(list.size(), 5);
    for (int i = 0; i < shown; ++i) {
        auto *chip = new QPushButton(list[i].name);
        chip->setObjectName(QStringLiteral("Chip"));
        chip->setCheckable(true);
        chip->setChecked(list[i].name == current);
        chip->setToolTip(list[i].shortcut ? tr("Apply preset (Ctrl+%1)").arg(list[i].shortcut) : tr("Apply preset"));
        connect(chip, &QPushButton::clicked, this, [this, i] { m_app.applyPresetIndex(i); });
        m_chipLayout->addWidget(chip);
    }
    auto *add = new QPushButton(QStringLiteral("+"));
    add->setObjectName(QStringLiteral("Chip"));
    add->setToolTip(tr("Save the current settings as a preset (Ctrl+S)"));
    connect(add, &QPushButton::clicked, this, &MainWindow::savePreset);
    m_chipLayout->addWidget(add);

    // Tray.
    if (m_trayPresets) {
        m_trayPresets->clear();
        for (int i = 0; i < list.size(); ++i)
            connect(m_trayPresets->addAction(list[i].name), &QAction::triggered, this,
                    [this, i] { m_app.applyPresetIndex(i); });
    }
}

void MainWindow::updateBanner()
{
    QString text, level = QStringLiteral("warn");
    switch (m_ctl.cameraState()) {
    case cam::CameraState::Busy:
    case cam::CameraState::Error:
        text = m_ctl.cameraMessage();
        level = QStringLiteral("error");
        break;
    case cam::CameraState::Waiting:
        text = m_ctl.cameraMessage();
        break;
    default:
        break;
    }
    if (m_ctl.output().enabled && m_ctl.outputState() == cam::OutputState::NoDevice) {
        text += (text.isEmpty() ? QString() : QStringLiteral("\n")) +
                tr("The virtual camera is not set up yet — open the Output tab and click “Set up virtual camera”.");
    } else if (m_ctl.output().enabled && m_ctl.outputState() == cam::OutputState::Error) {
        text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + m_ctl.outputMessage();
        level = QStringLiteral("error");
    }
    if (!m_controlErrorText.isEmpty())
        text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + m_controlErrorText;
    m_banner->setVisible(!text.isEmpty());
    m_banner->setText(text);
    if (m_banner->property("level").toString() != level) {
        m_banner->setProperty("level", level);
        m_banner->style()->unpolish(m_banner);
        m_banner->style()->polish(m_banner);
    }
}

void MainWindow::updateStatus()
{
    const cam::EngineStats s = m_ctl.stats();
    QString text, color;
    const QString name = QString::fromStdString(m_ctl.camera().displayName());
    switch (m_ctl.cameraState()) {
    case cam::CameraState::Streaming: {
        const auto &m = m_ctl.activeMode();
        color = QStringLiteral("#34c77b");
        text = tr("Live · %1 · %2×%3 · %4 fps").arg(name).arg(m.width).arg(m.height).arg(fpsText(s.captureFps));
        if (s.overloaded)
            text += tr(" · computer is busy, quality reduced");
        break;
    }
    case cam::CameraState::Opening:
        color = QStringLiteral("#ffb454");
        text = tr("Starting %1…").arg(name);
        break;
    case cam::CameraState::Waiting:
        color = QStringLiteral("#ffb454");
        text = tr("%1 is unplugged — waiting for it").arg(name);
        break;
    case cam::CameraState::Busy:
    case cam::CameraState::Error:
        color = QStringLiteral("#ff5d5d");
        text = tr("Camera unavailable — retrying");
        break;
    case cam::CameraState::Suspended:
        color = QStringLiteral("#8a93a6");
        text = tr("Paused while the computer sleeps");
        break;
    case cam::CameraState::NoCamera:
        color = QStringLiteral("#8a93a6");
        text = tr("No camera selected");
        break;
    }
    m_statusDot->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    m_statusText->setText(text);
    m_perfText->setVisible(m_showPerformance);
    if (m_showPerformance)
        m_perfText->setText(tr("processing %1 ms · delay %2 ms%3")
                                .arg(s.processMs, 0, 'f', 1)
                                .arg(s.latencyMs, 0, 'f', 0)
                                .arg(m_ctl.outputState() == cam::OutputState::Active
                                         ? tr(" · sending %1 fps").arg(fpsText(s.outputFps))
                                         : QString()));
}

void MainWindow::updatePreviewWanted()
{
    const bool visible = isVisible() && !isMinimized();
    m_ctl.setPreviewWanted(visible && !m_previewPaused);
    if (m_previewPaused)
        m_preview->setMessage(tr("Preview paused — the virtual camera keeps running"));
    else if (m_ctl.cameraState() == cam::CameraState::Streaming)
        m_preview->setMessage(QString());
}

// ---------------------------------------------------------------------------
// View -> model
// ---------------------------------------------------------------------------

void MainWindow::pushColor()
{
    cam::ColorParams c;
    c.brightness = m_brightness->value();
    c.contrast = m_contrast->value();
    c.saturation = m_saturation->value();
    c.gamma = m_gamma->value();
    c.sharpness = m_sharpness->value();
    c.warmth = m_warmth->value();
    c.tint = m_tint->value();
    m_ctl.setColor(c);
}

void MainWindow::pushFraming(int transitionMs)
{
    if (m_syncing)
        return;
    cam::FramingParams f = m_ctl.framing();
    f.zoom = m_zoom->value();
    f.panX = m_panX->value();
    f.panY = m_panY->value();
    f.rotation = std::max(0, m_rotation->currentData()) + m_straighten->value();
    f.mirror = m_mirror->isChecked();
    f.flip = m_flip->isChecked();
    f.aspect = cam::AspectMode(std::max(0, m_aspect->currentData()));
    f.cropLeft = m_cropL->value();
    f.cropRight = m_cropR->value();
    f.cropTop = m_cropT->value();
    f.cropBottom = m_cropB->value();
    m_ctl.setFraming(f, transitionMs < 0 ? app::CameraController::kSliderTransitionMs : transitionMs);
}

void MainWindow::pushOutput()
{
    if (m_syncing)
        return;
    cam::OutputConfig o = m_ctl.output();
    o.devicePath = m_outDevice->currentData().toString().toStdString();
    const QSize sz = m_outResolution->currentData().toSize();
    o.width = sz.width();
    o.height = sz.height();
    o.fps = m_outFps->currentData().toInt();
    o.pixelFormat = cam::OutputPixelFormat(m_outFormat->currentData().toInt());
    m_ctl.setOutput(o);
}

void MainWindow::panBy(double dxOut, double dyOut)
{
    // Convert a movement in output-picture units to pan units (fractions of
    // the available travel in each direction).
    cam::FramingParams f = m_ctl.framing();
    const auto &m = m_ctl.activeMode();
    int ow, oh;
    m_ctl.renderSize(ow, oh);
    if (m.width <= 0 || m.height <= 0 || ow <= 0 || oh <= 0)
        return;
    double cw = m.width * (1 - f.cropLeft - f.cropRight), ch = m.height * (1 - f.cropTop - f.cropBottom);
    const double rot = std::fmod(std::fabs(f.rotation) + 45, 180);
    if (rot >= 90)
        std::swap(cw, ch);
    const double aspect = double(ow) / oh;
    double vw = cw, vh = ch;
    if (f.aspect == cam::AspectMode::Fill) {
        if (cw / ch > aspect)
            vw = ch * aspect;
        else
            vh = cw / aspect;
    }
    vw /= f.zoom;
    vh /= f.zoom;
    const double travelX = (cw - vw) / 2, travelY = (ch - vh) / 2;
    if (travelX > 0.5)
        f.panX = std::clamp(f.panX + dxOut * vw / travelX * (f.mirror ? -1 : 1), -1.0, 1.0);
    if (travelY > 0.5)
        f.panY = std::clamp(f.panY + dyOut * vh / travelY * (f.flip ? -1 : 1), -1.0, 1.0);
    m_ctl.setFraming(f, 40);
}

void MainWindow::savePreset()
{
    PresetsPanel panel(m_app);
    panel.saveNew();
}

void MainWindow::managePresets()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Presets"));
    dlg.resize(420, 460);
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(new PresetsPanel(m_app));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(buttons);
    dlg.exec();
}

// ---------------------------------------------------------------------------
// Window behaviour
// ---------------------------------------------------------------------------

bool MainWindow::hasTray() const { return m_tray && m_tray->isVisible(); }

void MainWindow::hideToTray()
{
    m_background->cancelInteraction();
    hide();
    updatePreviewWanted();
}

void MainWindow::showAndRaise()
{
    if (isMinimized())
        setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
    updatePreviewWanted();
}

void MainWindow::prepareQuit()
{
    m_quitting = true;
    m_app.settings().setWindowGeometry(saveGeometry());
    m_app.settings().setSplitterState(m_splitter->saveState());
    if (m_tray)
        m_tray->hide();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    m_app.settings().setWindowGeometry(saveGeometry());
    m_app.settings().setSplitterState(m_splitter->saveState());
    if (!m_quitting && hasTray() && m_app.settings().closeToTray()) {
        e->ignore();
        hideToTray();
        static bool told = false;
        if (!told) {
            told = true;
            m_tray->showMessage(tr("CamTune is still running"),
                                tr("The virtual camera keeps working. Use the tray icon to open CamTune or quit."),
                                QSystemTrayIcon::Information, 4000);
        }
        return;
    }
    e->accept();
    if (!m_quitting)
        QTimer::singleShot(0, &m_app, [app = &m_app] { app->quit(); });
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange)
        updatePreviewWanted();
}

void MainWindow::showEvent(QShowEvent *e)
{
    QMainWindow::showEvent(e);
    updatePreviewWanted();
}

void MainWindow::hideEvent(QHideEvent *e)
{
    QMainWindow::hideEvent(e);
    updatePreviewWanted();
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

void MainWindow::runVirtualCameraSetup()
{
    const QString helper = app::setupHelperPath();
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Set up the virtual camera"));
    dlg.resize(620, 440);
    auto *v = new QVBoxLayout(&dlg);
    auto *intro = new QLabel(
        tr("<p>CamTune's virtual camera uses the <b>v4l2loopback</b> kernel module. Setup loads it with settings "
           "that work with Zoom, Teams, Meet, Discord and browsers, names it “CamTune”, and loads it at every "
           "start. You will be asked for your password.</p>"
           "<p>If the module is not installed yet, choose <b>Install &amp; set up</b>. "
           "Fedora needs the free RPM Fusion repository enabled first. With Secure Boot, the installer may ask "
           "you to enroll a key on the next reboot.</p>"));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    v->addWidget(intro);
    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    log->setPlaceholderText(tr("Progress will appear here."));
    v->addWidget(log, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *install = buttons->addButton(tr("Install && set up"), QDialogButtonBox::ActionRole);
    auto *run = buttons->addButton(tr("Set up"), QDialogButtonBox::ActionRole);
    run->setObjectName(QStringLiteral("Primary"));
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (helper.isEmpty()) {
        log->setPlainText(tr("The setup helper (camtune-setup-v4l2loopback) was not found. Run it from the "
                             "source tree's scripts/ directory with sudo, or load the module manually:\n\n"
                             "sudo modprobe v4l2loopback devices=1 video_nr=42 card_label=\"CamTune\" "
                             "exclusive_caps=1 max_buffers=2"));
        run->setEnabled(false);
        install->setEnabled(false);
    }

    auto *proc = new QProcess(&dlg);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::readyRead, log, [proc, log] {
        log->appendPlainText(QString::fromLocal8Bit(proc->readAll()).trimmed());
    });
    connect(proc, &QProcess::finished, &dlg, [this, log, run, install](int code, QProcess::ExitStatus) {
        run->setEnabled(true);
        install->setEnabled(true);
        if (code == 0) {
            log->appendPlainText(tr("\nDone — the virtual camera is ready."));
            m_ctl.refreshDevices();
        } else if (code == 126 || code == 127) {
            log->appendPlainText(tr("\nCancelled, or the password prompt (pkexec) is not available."));
        } else if (code == 2) {
            log->appendPlainText(tr("\nThe module is not installed. Click “Install & set up”."));
        } else {
            log->appendPlainText(tr("\nSetup failed (code %1).").arg(code));
        }
    });
    auto start = [proc, helper, log, run, install](bool withInstall) {
        log->clear();
        run->setEnabled(false);
        install->setEnabled(false);
        QStringList args{helper};
        if (withInstall)
            args << QStringLiteral("--install");
        proc->start(QStringLiteral("pkexec"), args);
    };
    connect(run, &QPushButton::clicked, &dlg, [start] { start(false); });
    connect(install, &QPushButton::clicked, &dlg, [start] { start(true); });
    dlg.exec();
    if (proc->state() != QProcess::NotRunning)
        proc->waitForFinished(30000);
}

void MainWindow::showAutomationHelp()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Command line & automation"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<p>Control a running CamTune from scripts, hotkey tools or a Stream Deck:</p>"
                   "<pre>camtune --preset 2            # by number or name\n"
                   "camtune --preset \"Close-up\"\n"
                   "camtune --zoom 1.5\n"
                   "camtune --zoom-in | --zoom-out\n"
                   "camtune --pan 0.2,-0.3\n"
                   "camtune --reset-framing\n"
                   "camtune --virtual-camera on|off|toggle\n"
                   "camtune --list-presets | --status | --show | --quit</pre>"
                   "<p><b>Shortcuts while in a call:</b> turn on <i>Shortcuts from any app</i> on the Output tab, "
                   "or bind keys to the commands above in your desktop's keyboard settings (works on Wayland and "
                   "X11).</p>"
                   "<p><b>D-Bus:</b> <code>io.github.CamTune</code>, object <code>/io/github/CamTune</code>, "
                   "interface <code>io.github.CamTune1</code>.</p>"));
    box.exec();
}

void MainWindow::showAbout()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("About CamTune"));
    box.setIconPixmap(appIcon().pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText(tr("<h3>CamTune %1</h3>"
                   "<p>Webcam controls, framing, background blur and a low-latency virtual camera for "
                   "video calls. Everything runs on this computer.</p>"
                   "<p>License: GPL-3.0-or-later. Person detection uses Google's MediaPipe selfie "
                   "segmentation model (Apache-2.0).</p>")
                    .arg(QApplication::applicationVersion()));
    box.exec();
}

} // namespace ui
