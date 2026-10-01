// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "EffectsPanel.h"
#include "HardwareControlsPanel.h"
#include "PresetsPanel.h"
#include "PreviewWidget.h"
#include "Widgets.h"
#include "app/Application.h"
#include "app/GlobalShortcuts.h"
#include "app/SystemIntegration.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QVBoxLayout>

#include <linux/videodev2.h>

#include <cmath>

namespace ui {

namespace {

QString fourccName(uint32_t f)
{
    switch (f) {
    case V4L2_PIX_FMT_MJPEG:
    case V4L2_PIX_FMT_JPEG: return QStringLiteral("MJPEG");
    case V4L2_PIX_FMT_YUYV: return QStringLiteral("YUYV");
    case V4L2_PIX_FMT_NV12: return QStringLiteral("NV12");
    case V4L2_PIX_FMT_YUV420: return QStringLiteral("I420");
    default: return QString::fromStdString(cam::v4l2::fourccToString(f));
    }
}

QString fpsText(double fps)
{
    return std::fabs(fps - std::round(fps)) < 0.01 ? QString::number(int(std::round(fps)))
                                                  : QString::number(fps, 'f', 2);
}

const int kResolutions[][2] = {{640, 360},  {640, 480},  {800, 600},  {960, 540},   {1024, 576},
                               {1280, 720}, {1280, 960}, {1600, 900}, {1920, 1080}, {2560, 1440}};
const int kFrameRates[] = {15, 20, 24, 25, 30, 50, 60};

QIcon appIcon()
{
    QIcon icon = QIcon::fromTheme(QStringLiteral("io.github.LinuxCameraAdjust"));
    if (icon.isNull())
        icon = QIcon(QStringLiteral(":/icons/camadjust.svg"));
    return icon;
}

} // namespace

MainWindow::MainWindow(app::Application &app, QWidget *parent)
    : QMainWindow(parent), m_app(app), m_ctl(app.controller())
{
    setWindowTitle(tr("Camera Adjust"));
    setWindowIcon(appIcon());
    m_previewPaused = m_app.settings().previewPaused();

    auto *central = new QWidget;
    auto *v = new QVBoxLayout(central);
    v->setContentsMargins(8, 6, 8, 4);
    v->addWidget(buildTopBar());

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->addWidget(buildPreviewArea());
    m_splitter->addWidget(buildSidePanel());
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setChildrenCollapsible(false);
    v->addWidget(m_splitter, 1);
    setCentralWidget(central);

    buildMenus();
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
    connect(&m_ctl, &app::CameraController::effectsChanged, m_effects, &EffectsPanel::syncFromModel);
    connect(&m_app.presets(), &app::PresetStore::changed, this, &MainWindow::rebuildTrayPresets);

    m_controlErrorTimer.setSingleShot(true);
    m_controlErrorTimer.setInterval(6000);
    connect(&m_controlErrorTimer, &QTimer::timeout, this, [this] {
        m_controlErrorText.clear();
        updateBanner();
    });

    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, &MainWindow::updateStats);
    m_statsTimer.start();

    syncCameraList();
    syncModes();
    syncColor();
    syncFraming();
    syncOutput();
    syncCameraState();
    m_hwPanel->setControls(m_ctl.controls());
    rebuildTrayPresets();

    if (!restoreGeometry(m_app.settings().windowGeometry()))
        resize(1180, 720);
    m_splitter->restoreState(m_app.settings().splitterState());
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

QWidget *MainWindow::buildTopBar()
{
    auto *bar = new QWidget;
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(0, 0, 0, 0);

    h->addWidget(new QLabel(tr("Camera:")));
    m_cameraCombo = new QComboBox;
    m_cameraCombo->setMinimumWidth(220);
    m_cameraCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    h->addWidget(m_cameraCombo);
    auto *refresh = new QToolButton;
    refresh->setText(QStringLiteral("⟳"));
    refresh->setToolTip(tr("Rescan cameras"));
    h->addWidget(refresh);

    h->addSpacing(12);
    h->addWidget(new QLabel(tr("Format:")));
    m_modeCombo = new QComboBox;
    m_modeCombo->setMinimumWidth(200);
    m_modeCombo->setToolTip(tr("Capture format of the physical camera. “Automatic” picks the best match "
                               "for the output resolution and frame rate."));
    h->addWidget(m_modeCombo);
    h->addStretch(1);

    m_vcamButton = new QToolButton;
    m_vcamButton->setCheckable(true);
    m_vcamButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_vcamButton->setMinimumWidth(180);
    m_vcamButton->setToolTip(tr("Send the processed picture to the virtual camera (Ctrl+Shift+V)"));
    h->addWidget(m_vcamButton);

    connect(refresh, &QToolButton::clicked, &m_ctl, &app::CameraController::refreshDevices);
    connect(m_cameraCombo, qOverload<int>(&QComboBox::activated), this, [this](int idx) {
        int i = m_cameraCombo->itemData(idx).toInt();
        if (i >= 0 && i < m_cameraEntries.size())
            m_ctl.setCamera(m_cameraEntries[i]);
    });
    connect(m_modeCombo, qOverload<int>(&QComboBox::activated), this, [this](int idx) {
        int i = m_modeCombo->itemData(idx).toInt();
        if (i >= 0 && i < m_modeEntries.size())
            m_ctl.setCaptureRequest(m_modeEntries[i]);
    });
    connect(m_vcamButton, &QToolButton::toggled, this, [this](bool on) {
        if (!m_syncing)
            m_ctl.setVirtualCameraEnabled(on);
    });
    return bar;
}

QWidget *MainWindow::buildPreviewArea()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    m_banner = new QLabel;
    m_banner->setWordWrap(true);
    m_banner->setMargin(8);
    m_banner->hide();
    v->addWidget(m_banner);

    m_preview = new PreviewWidget;
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    v->addWidget(m_preview, 1);

    m_stats = new QLabel;
    m_stats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont f = m_stats->font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    m_stats->setFont(f);
    v->addWidget(m_stats);

    connect(m_preview, &PreviewWidget::zoomRequested, this, [this](double steps) {
        cam::FramingParams f = m_ctl.framing();
        f.zoom = std::clamp(f.zoom * std::pow(1.08, steps), 1.0, 8.0);
        m_ctl.setFraming(f, 120);
    });
    connect(m_preview, &PreviewWidget::panRequested, this, &MainWindow::panBy);
    connect(m_preview, &PreviewWidget::doubleClicked, this, [this] { m_app.resetFraming(); });
    return w;
}

QWidget *MainWindow::buildSidePanel()
{
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(4, 0, 8, 0);

    auto *camera = new Section(tr("Camera"));
    m_hwPanel = new HardwareControlsPanel;
    camera->contentLayout()->addWidget(m_hwPanel);
    connect(m_hwPanel, &HardwareControlsPanel::controlChanged, &m_ctl, &app::CameraController::setHardwareControl);
    connect(m_hwPanel, &HardwareControlsPanel::resetRequested, &m_ctl,
            &app::CameraController::resetHardwareControls);
    v->addWidget(camera);

    auto *color = new Section(tr("Color"));
    color->contentLayout()->addWidget(buildColorSection());
    v->addWidget(color);

    auto *framing = new Section(tr("Framing"));
    framing->contentLayout()->addWidget(buildFramingSection());
    v->addWidget(framing);

    auto *effects = new Section(tr("Background effects"));
    m_effects = new EffectsPanel(m_ctl, m_preview);
    effects->contentLayout()->addWidget(m_effects);
    effects->setExpanded(m_ctl.effects().mode != cam::EffectMode::Off);
    connect(effects->findChild<QToolButton *>(), &QToolButton::toggled, this, [this](bool open) {
        if (!open)
            m_effects->cancelInteraction();
    });
    v->addWidget(effects);

    auto *output = new Section(tr("Output"));
    output->contentLayout()->addWidget(buildOutputSection());
    v->addWidget(output);

    auto *presets = new Section(tr("Presets"));
    m_presets = new PresetsPanel(m_app);
    presets->contentLayout()->addWidget(m_presets);
    v->addWidget(presets);
    v->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(380);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

QWidget *MainWindow::buildColorSection()
{
    auto *w = new QWidget;
    auto *g = new QGridLayout(w);
    g->setContentsMargins(0, 0, 0, 0);
    g->setColumnStretch(1, 1);
    int r = 0;
    m_brightness = new SliderRow(g, r++, tr("Brightness"), -0.5, 0.5, 0, 2);
    m_contrast = new SliderRow(g, r++, tr("Contrast"), 0.2, 2.5, 1, 2);
    m_saturation = new SliderRow(g, r++, tr("Saturation"), 0, 2.5, 1, 2);
    m_gamma = new SliderRow(g, r++, tr("Gamma"), 0.3, 3.0, 1, 2);
    m_sharpness = new SliderRow(g, r++, tr("Sharpness"), 0, 2, 0, 2);
    m_warmth = new SliderRow(g, r++, tr("Warmth"), -1, 1, 0, 2);
    m_tint = new SliderRow(g, r++, tr("Tint"), -1, 1, 0, 2);
    m_gamma->setToolTip(tr("Values above 1 brighten shadows and midtones"));
    m_sharpness->setToolTip(tr("Software sharpening (costs a little CPU). Hardware sharpness is under Camera."));
    m_warmth->setToolTip(tr("Software white balance: negative = cooler, positive = warmer"));
    m_tint->setToolTip(tr("Negative = greener, positive = more magenta"));
    for (SliderRow *s : {m_brightness, m_contrast, m_saturation, m_gamma, m_sharpness, m_warmth, m_tint})
        connect(s, &SliderRow::valueChanged, this, &MainWindow::pushColor);
    auto *reset = new QPushButton(tr("Reset color"));
    connect(reset, &QPushButton::clicked, this, [this] { m_ctl.setColor(cam::ColorParams()); });
    g->addWidget(reset, r, 1, 1, 3, Qt::AlignRight);
    return w;
}

QWidget *MainWindow::buildFramingSection()
{
    auto *w = new QWidget;
    auto *g = new QGridLayout(w);
    g->setContentsMargins(0, 0, 0, 0);
    g->setColumnStretch(1, 1);
    int r = 0;
    m_zoom = new SliderRow(g, r++, tr("Zoom"), 1, 8, 1, 2, QStringLiteral("×"));
    m_panX = new SliderRow(g, r++, tr("Pan X"), -100, 100, 0, 0, QStringLiteral(" %"));
    m_panY = new SliderRow(g, r++, tr("Pan Y"), -100, 100, 0, 0, QStringLiteral(" %"));
    m_panX->setToolTip(tr("Move the view horizontally (needs zoom or a different output aspect ratio)"));
    m_panY->setToolTip(tr("Move the view vertically"));

    g->addWidget(new QLabel(tr("Rotation")), r, 0);
    m_rotation = new QComboBox;
    m_rotation->addItem(QStringLiteral("0°"), 0);
    m_rotation->addItem(QStringLiteral("90°"), 90);
    m_rotation->addItem(QStringLiteral("180°"), 180);
    m_rotation->addItem(QStringLiteral("270°"), 270);
    g->addWidget(m_rotation, r++, 1, 1, 3);
    m_fineRotation = new SliderRow(g, r++, tr("Level"), -15, 15, 0, 1, QStringLiteral("°"));
    m_fineRotation->setToolTip(tr("Fine rotation to straighten a tilted camera"));

    auto *flags = new QHBoxLayout;
    m_mirror = new QCheckBox(tr("Mirror"));
    m_mirror->setToolTip(tr("Flip horizontally. Note: most call apps mirror only your local "
                            "self-view; others see the picture as sent."));
    m_flip = new QCheckBox(tr("Flip vertically"));
    flags->addWidget(m_mirror);
    flags->addWidget(m_flip);
    flags->addStretch(1);
    g->addLayout(flags, r++, 0, 1, 4);

    g->addWidget(new QLabel(tr("Aspect")), r, 0);
    m_aspect = new QComboBox;
    m_aspect->addItem(tr("Fill (crop to fit)"), int(cam::AspectMode::Fill));
    m_aspect->addItem(tr("Fit (letterbox)"), int(cam::AspectMode::Fit));
    m_aspect->addItem(tr("Stretch"), int(cam::AspectMode::Stretch));
    g->addWidget(m_aspect, r++, 1, 1, 3);

    auto *cropLabel = new QLabel(tr("Crop edges"));
    QFont f = cropLabel->font();
    f.setItalic(true);
    cropLabel->setFont(f);
    g->addWidget(cropLabel, r++, 0, 1, 4);
    m_cropL = new SliderRow(g, r++, tr("Left"), 0, 45, 0, 0, QStringLiteral(" %"));
    m_cropR = new SliderRow(g, r++, tr("Right"), 0, 45, 0, 0, QStringLiteral(" %"));
    m_cropT = new SliderRow(g, r++, tr("Top"), 0, 45, 0, 0, QStringLiteral(" %"));
    m_cropB = new SliderRow(g, r++, tr("Bottom"), 0, 45, 0, 0, QStringLiteral(" %"));

    auto *reset = new QPushButton(tr("Reset framing"));
    reset->setToolTip(tr("Reset zoom, pan and crop (Ctrl+0)"));
    connect(reset, &QPushButton::clicked, this, [this] { m_app.resetFraming(); });
    g->addWidget(reset, r, 1, 1, 3, Qt::AlignRight);

    for (SliderRow *s : {m_zoom, m_panX, m_panY, m_fineRotation, m_cropL, m_cropR, m_cropT, m_cropB})
        connect(s, &SliderRow::valueChanged, this, [this] { pushFraming(); });
    connect(m_rotation, qOverload<int>(&QComboBox::activated), this, [this] { pushFraming(250); });
    connect(m_aspect, qOverload<int>(&QComboBox::activated), this, [this] { pushFraming(0); });
    connect(m_mirror, &QCheckBox::toggled, this, [this] { pushFraming(0); });
    connect(m_flip, &QCheckBox::toggled, this, [this] { pushFraming(0); });
    return w;
}

QWidget *MainWindow::buildOutputSection()
{
    auto *w = new QWidget;
    auto *form = new QFormLayout(w);
    form->setContentsMargins(0, 0, 0, 0);
    m_outEnabled = new QCheckBox(tr("Virtual camera enabled"));
    form->addRow(m_outEnabled);
    m_outDevice = new QComboBox;
    form->addRow(tr("Device:"), m_outDevice);
    m_outResolution = new QComboBox;
    for (auto &r : kResolutions)
        m_outResolution->addItem(QStringLiteral("%1 × %2").arg(r[0]).arg(r[1]), QSize(r[0], r[1]));
    form->addRow(tr("Resolution:"), m_outResolution);
    m_outFps = new QComboBox;
    for (int f : kFrameRates)
        m_outFps->addItem(tr("%1 fps").arg(f), f);
    form->addRow(tr("Frame rate:"), m_outFps);
    m_outFormat = new QComboBox;
    m_outFormat->addItem(tr("I420 / YU12 (recommended)"), int(cam::OutputPixelFormat::I420));
    m_outFormat->addItem(tr("YUYV (compatibility)"), int(cam::OutputPixelFormat::YUYV));
    m_outFormat->setToolTip(tr("I420 is written without any conversion. Use YUYV only if an application "
                               "does not accept the virtual camera."));
    form->addRow(tr("Pixel format:"), m_outFormat);
    m_outStatus = new QLabel;
    m_outStatus->setWordWrap(true);
    form->addRow(m_outStatus);
    m_setupButton = new QPushButton(tr("Set up virtual camera…"));
    m_setupButton->setToolTip(tr("Loads the v4l2loopback kernel module (asks for your password)"));
    form->addRow(m_setupButton);

    connect(m_outEnabled, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing)
            m_ctl.setVirtualCameraEnabled(on);
    });
    for (QComboBox *c : {m_outDevice, m_outResolution, m_outFps, m_outFormat})
        connect(c, qOverload<int>(&QComboBox::activated), this, &MainWindow::pushOutput);
    connect(m_setupButton, &QPushButton::clicked, this, &MainWindow::runVirtualCameraSetup);
    return w;
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    QAction *quit = file->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, [this] { m_app.quit(); });

    QMenu *view = menuBar()->addMenu(tr("&View"));
    m_pauseAction = view->addAction(tr("&Pause preview"));
    m_pauseAction->setCheckable(true);
    m_pauseAction->setChecked(m_previewPaused);
    m_pauseAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    m_pauseAction->setToolTip(tr("Stop drawing the preview to save resources; the virtual camera keeps running"));
    connect(m_pauseAction, &QAction::toggled, this, [this](bool on) {
        m_previewPaused = on;
        m_app.settings().setPreviewPaused(on);
        updatePreviewWanted();
    });
    m_statsAction = view->addAction(tr("Show &statistics"));
    m_statsAction->setCheckable(true);
    m_statsAction->setChecked(true);
    connect(m_statsAction, &QAction::toggled, m_stats, &QWidget::setVisible);

    QMenu *settings = menuBar()->addMenu(tr("&Settings"));
    QAction *autostart = settings->addAction(tr("Start automatically on &login"));
    autostart->setCheckable(true);
    autostart->setChecked(app::autostart::isEnabled());
    connect(autostart, &QAction::toggled, this, [this, autostart](bool on) {
        QString err;
        if (!app::autostart::setEnabled(on, &err)) {
            QMessageBox::warning(this, tr("Autostart"), err);
            QSignalBlocker b(autostart);
            autostart->setChecked(!on);
        }
    });
    QAction *minimized = settings->addAction(tr("Start &minimized to tray"));
    minimized->setCheckable(true);
    minimized->setChecked(m_app.settings().startMinimized());
    connect(minimized, &QAction::toggled, this, [this](bool on) { m_app.settings().setStartMinimized(on); });
    QAction *closeTray = settings->addAction(tr("&Close button keeps running in tray"));
    closeTray->setCheckable(true);
    closeTray->setChecked(m_app.settings().closeToTray());
    connect(closeTray, &QAction::toggled, this, [this](bool on) { m_app.settings().setCloseToTray(on); });
    settings->addSeparator();
    QAction *global = settings->addAction(tr("&Global shortcuts (Ctrl+Alt+1…9)"));
    global->setCheckable(true);
    global->setChecked(m_app.settings().globalShortcuts());
    connect(global, &QAction::toggled, this, [this](bool on) { m_app.setGlobalShortcutsEnabled(on); });
    connect(m_app.globalShortcuts(), &app::GlobalShortcuts::statusChanged, this,
            [this] { statusBar()->showMessage(m_app.globalShortcuts()->statusText(), 8000); });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    connect(help->addAction(tr("Virtual camera &setup…")), &QAction::triggered, this,
            &MainWindow::runVirtualCameraSetup);
    connect(help->addAction(tr("Command line && &automation…")), &QAction::triggered, this,
            &MainWindow::showAutomationHelp);
    connect(help->addAction(tr("&About")), &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(appIcon(), this);
    m_tray->setToolTip(tr("Camera Adjust"));
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
    // Ctrl+1..9 apply presets while the window is focused.
    for (int i = 1; i <= 9; ++i) {
        auto *sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_0 + i)), this);
        connect(sc, &QShortcut::activated, this, [this, i] {
            int idx = m_app.presets().indexForShortcut(i);
            if (idx >= 0)
                m_app.applyPresetIndex(idx);
        });
    }
    auto *reset = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this);
    connect(reset, &QShortcut::activated, this, [this] { m_app.resetFraming(); });
    auto *zin = new QShortcut(QKeySequence::ZoomIn, this);
    connect(zin, &QShortcut::activated, this, [this] { m_app.adjustZoom(0.1); });
    auto *zout = new QShortcut(QKeySequence::ZoomOut, this);
    connect(zout, &QShortcut::activated, this, [this] { m_app.adjustZoom(-0.1); });
    auto *vcam = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), this);
    connect(vcam, &QShortcut::activated, this,
            [this] { m_ctl.setVirtualCameraEnabled(!m_ctl.output().enabled); });
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, m_effects, &EffectsPanel::cancelInteraction);
}

// ---------------------------------------------------------------------------
// Model -> view synchronisation
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
        m_cameraCombo->addItem(QStringLiteral("%1  (%2)").arg(QString::fromStdString(d.card),
                                                                QString::fromStdString(d.path)),
                               m_cameraEntries.size() - 1);
    }
    if (!current.empty() && !current.testPattern && currentIndex < 0) {
        // Selected camera is unplugged: keep showing it so it is restored on replug.
        currentIndex = m_cameraEntries.size();
        m_cameraEntries.append(current);
        m_cameraCombo->addItem(tr("%1 (disconnected)").arg(QString::fromStdString(current.card)),
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
    m_cameraCombo->addItem(tr("None (release camera)"), m_cameraEntries.size() - 1);
    m_cameraCombo->setCurrentIndex(currentIndex);
    m_syncing = false;
}

void MainWindow::syncModes()
{
    m_syncing = true;
    m_modeCombo->clear();
    m_modeEntries.clear();
    const auto &active = m_ctl.activeMode();
    cam::CaptureRequest autoReq;
    m_modeEntries.append(autoReq);
    QString autoText = tr("Automatic");
    if (active.width > 0)
        autoText += QStringLiteral("  (%1×%2 %3 %4 fps)")
                        .arg(active.width)
                        .arg(active.height)
                        .arg(fourccName(active.fourcc))
                        .arg(fpsText(active.fps));
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
            m_modeCombo->addItem(QStringLiteral("%1×%2  %3  %4 fps")
                                     .arg(m.width)
                                     .arg(m.height)
                                     .arg(fourccName(m.fourcc))
                                     .arg(fpsText(r.fps())),
                                 m_modeEntries.size() - 1);
        }
    }
    if (!req.automatic && currentIndex == 0 && m_ctl.modes().empty()) {
        // Modes not known yet (camera still opening); show the saved choice.
        m_modeEntries.append(req);
        m_modeCombo->addItem(QStringLiteral("%1×%2  %3  %4 fps")
                                 .arg(req.width)
                                 .arg(req.height)
                                 .arg(fourccName(req.fourcc))
                                 .arg(fpsText(req.rate.fps())),
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
    m_panX->setValue(f.panX * 100);
    m_panY->setValue(f.panY * 100);
    double rot = std::fmod(f.rotation, 360.0);
    if (rot < 0)
        rot += 360;
    int quarter = int(std::floor((rot + 45) / 90)) % 4;
    double fine = rot - quarter * 90;
    if (fine > 180)
        fine -= 360;
    m_rotation->setCurrentIndex(quarter);
    m_fineRotation->setValue(fine);
    {
        QSignalBlocker b1(m_mirror), b2(m_flip);
        m_mirror->setChecked(f.mirror);
        m_flip->setChecked(f.flip);
    }
    m_aspect->setCurrentIndex(qMax(0, m_aspect->findData(int(f.aspect))));
    m_cropL->setValue(f.cropLeft * 100);
    m_cropR->setValue(f.cropRight * 100);
    m_cropT->setValue(f.cropTop * 100);
    m_cropB->setValue(f.cropBottom * 100);
    m_syncing = false;
}

void MainWindow::syncOutput()
{
    m_syncing = true;
    const cam::OutputConfig &o = m_ctl.output();
    m_outEnabled->setChecked(o.enabled);
    m_vcamButton->setChecked(o.enabled);
    if (m_trayVcam)
        m_trayVcam->setChecked(o.enabled);

    m_outDevice->clear();
    m_outDevice->addItem(tr("Automatic"), QString());
    int devIndex = 0;
    for (const auto &d : m_ctl.loopbackDevices()) {
        const QString path = QString::fromStdString(d.path);
        m_outDevice->addItem(QStringLiteral("%1  (%2)").arg(QString::fromStdString(d.card), path), path);
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
    m_outFormat->setCurrentIndex(qMax(0, m_outFormat->findData(int(o.pixelFormat))));

    QString status;
    QString style;
    switch (m_ctl.outputState()) {
    case cam::OutputState::Disabled:
        status = tr("Off. Enable it, then choose “Camera Adjust” as the camera in your call application.");
        break;
    case cam::OutputState::Active: {
        int w, h;
        m_ctl.renderSize(w, h);
        status = tr("Active: %1×%2 @ %3 fps.").arg(w).arg(h).arg(o.fps);
        if (!m_ctl.outputMessage().isEmpty())
            status += QLatin1Char(' ') + m_ctl.outputMessage();
        style = QStringLiteral("color: #2e9d4f;");
        break;
    }
    case cam::OutputState::NoDevice:
    case cam::OutputState::Error:
        status = m_ctl.outputMessage();
        style = QStringLiteral("color: #c0392b;");
        break;
    }
    m_outStatus->setText(status);
    m_outStatus->setStyleSheet(style);
    m_setupButton->setVisible(m_ctl.loopbackDevices().isEmpty());

    const bool on = o.enabled && m_ctl.outputState() == cam::OutputState::Active;
    m_vcamButton->setText(on ? tr("● Virtual camera ON") : (o.enabled ? tr("● Virtual camera …") : tr("○ Virtual camera OFF")));
    m_vcamButton->setStyleSheet(on ? QStringLiteral("QToolButton { color: white; background: #2e9d4f; "
                                                    "border-radius: 4px; padding: 4px 10px; font-weight: bold; }")
                                   : QStringLiteral("QToolButton { padding: 4px 10px; }"));
    if (m_tray)
        m_tray->setToolTip(on ? tr("Camera Adjust — virtual camera on") : tr("Camera Adjust"));
    m_syncing = false;
    updateBanner();
}

void MainWindow::syncCameraState()
{
    switch (m_ctl.cameraState()) {
    case cam::CameraState::Streaming:
        m_preview->setMessage(m_previewPaused ? tr("Preview paused (Ctrl+P)") : QString());
        break;
    case cam::CameraState::Opening:
        m_preview->setMessage(m_ctl.cameraMessage());
        break;
    default:
        m_preview->clearFrame();
        m_preview->setMessage(m_ctl.cameraMessage());
        break;
    }
    syncModes();
    updateBanner();
}

void MainWindow::updateBanner()
{
    QString text;
    bool error = false;
    switch (m_ctl.cameraState()) {
    case cam::CameraState::Busy:
    case cam::CameraState::Error:
        text = m_ctl.cameraMessage();
        error = true;
        break;
    case cam::CameraState::Waiting:
        text = m_ctl.cameraMessage();
        break;
    default:
        break;
    }
    if (m_ctl.output().enabled &&
        (m_ctl.outputState() == cam::OutputState::NoDevice || m_ctl.outputState() == cam::OutputState::Error)) {
        if (!text.isEmpty())
            text += QStringLiteral("\n");
        text += m_ctl.outputMessage();
        error = true;
    }
    if (!m_controlErrorText.isEmpty()) {
        if (!text.isEmpty())
            text += QStringLiteral("\n");
        text += m_controlErrorText;
    }
    m_banner->setVisible(!text.isEmpty());
    m_banner->setText(text);
    m_banner->setStyleSheet(error ? QStringLiteral("background: #fdecea; color: #8a1c13; border-radius: 4px;")
                                  : QStringLiteral("background: #fff4d6; color: #6b4e00; border-radius: 4px;"));
}

void MainWindow::updateStats()
{
    if (!m_stats->isVisible())
        return;
    const cam::EngineStats s = m_ctl.stats();
    const auto &m = m_ctl.activeMode();
    int w, h;
    m_ctl.renderSize(w, h);
    QStringList parts;
    if (m_ctl.cameraState() == cam::CameraState::Streaming) {
        parts << tr("Camera %1×%2 %3 · %4 fps")
                     .arg(m.width)
                     .arg(m.height)
                     .arg(fourccName(m.fourcc))
                     .arg(s.captureFps, 0, 'f', 1);
        parts << tr("processing %1 ms").arg(s.processMs, 0, 'f', 1);
        if (m_ctl.outputState() == cam::OutputState::Active)
            parts << tr("virtual camera %1×%2 · %3 fps").arg(w).arg(h).arg(s.outputFps, 0, 'f', 1);
        parts << tr("latency %1 ms").arg(s.latencyMs, 0, 'f', 0);
        if (s.decodeScale > 1)
            parts << tr("decoding at 1/%1 size").arg(s.decodeScale);
        if (s.overloaded)
            parts << tr("⚠ CPU overloaded — reducing quality");
    } else {
        parts << tr("Camera not streaming");
        if (m_ctl.outputState() == cam::OutputState::Active)
            parts << tr("virtual camera shows a placeholder");
    }
    m_stats->setText(parts.join(QStringLiteral("  ·  ")));
}

void MainWindow::updatePreviewWanted()
{
    const bool visible = isVisible() && !isMinimized();
    m_ctl.setPreviewWanted(visible && !m_previewPaused);
    if (m_previewPaused)
        m_preview->setMessage(tr("Preview paused (Ctrl+P)"));
    else if (m_ctl.cameraState() == cam::CameraState::Streaming)
        m_preview->setMessage(QString());
}

void MainWindow::rebuildTrayPresets()
{
    if (!m_trayPresets)
        return;
    m_trayPresets->clear();
    const auto &list = m_app.presets().presets();
    for (int i = 0; i < list.size(); ++i) {
        QString text = list[i].name;
        if (list[i].shortcut)
            text += QStringLiteral("\tCtrl+%1").arg(list[i].shortcut);
        connect(m_trayPresets->addAction(text), &QAction::triggered, this, [this, i] { m_app.applyPresetIndex(i); });
    }
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
    f.panX = m_panX->value() / 100.0;
    f.panY = m_panY->value() / 100.0;
    f.rotation = m_rotation->currentData().toInt() + m_fineRotation->value();
    f.mirror = m_mirror->isChecked();
    f.flip = m_flip->isChecked();
    f.aspect = cam::AspectMode(m_aspect->currentData().toInt());
    f.cropLeft = m_cropL->value() / 100.0;
    f.cropRight = m_cropR->value() / 100.0;
    f.cropTop = m_cropT->value() / 100.0;
    f.cropBottom = m_cropB->value() / 100.0;
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
    // Convert a movement in output-picture units to pan units, which are
    // fractions of the available travel in each direction.
    cam::FramingParams f = m_ctl.framing();
    const auto &m = m_ctl.activeMode();
    int ow, oh;
    m_ctl.renderSize(ow, oh);
    if (m.width <= 0 || m.height <= 0 || ow <= 0 || oh <= 0)
        return;
    double cw = m.width * (1 - f.cropLeft - f.cropRight), ch = m.height * (1 - f.cropTop - f.cropBottom);
    double rot = std::fmod(std::fabs(f.rotation) + 45, 180);
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

// ---------------------------------------------------------------------------
// Window behaviour
// ---------------------------------------------------------------------------

bool MainWindow::hasTray() const { return m_tray && m_tray->isVisible(); }

void MainWindow::hideToTray()
{
    m_effects->cancelInteraction();
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
        // Keep the virtual camera running in the background.
        e->ignore();
        hideToTray();
        static bool told = false;
        if (!told) {
            told = true;
            m_tray->showMessage(tr("Camera Adjust is still running"),
                                tr("The virtual camera keeps working. Use the tray icon to show the window or quit."),
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
    dlg.setWindowTitle(tr("Virtual camera setup"));
    dlg.resize(620, 420);
    auto *v = new QVBoxLayout(&dlg);
    auto *intro = new QLabel(
        tr("The virtual camera uses the <b>v4l2loopback</b> kernel module. Setup loads it with options that "
           "work with Zoom, Teams, Meet, Discord, Chromium and Firefox (<code>exclusive_caps=1</code>), names it "
           "“Camera Adjust”, and makes it load at boot.<br><br>"
           "Install the module first if needed:<br>"
           "&nbsp;&nbsp;Ubuntu/Debian: <code>sudo apt install v4l2loopback-dkms</code><br>"
           "&nbsp;&nbsp;Fedora: enable RPM Fusion, then <code>sudo dnf install v4l2loopback</code><br>"
           "With Secure Boot, DKMS/akmods may ask you to enroll a signing key."));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    v->addWidget(intro);
    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    v->addWidget(log, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *run = buttons->addButton(tr("Load module"), QDialogButtonBox::ActionRole);
    auto *install = buttons->addButton(tr("Install && load"), QDialogButtonBox::ActionRole);
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (helper.isEmpty()) {
        log->setPlainText(tr("The setup helper (camadjust-setup-v4l2loopback) was not found. Run it from the "
                             "source tree's scripts/ directory with sudo, or load the module manually:\n\n"
                             "sudo modprobe v4l2loopback devices=1 video_nr=42 card_label=\"Camera Adjust\" "
                             "exclusive_caps=1 max_buffers=2"));
        run->setEnabled(false);
        install->setEnabled(false);
    }

    auto *proc = new QProcess(&dlg);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::readyRead, log, [proc, log] {
        log->appendPlainText(QString::fromLocal8Bit(proc->readAll()).trimmed());
    });
    connect(proc, &QProcess::finished, &dlg, [this, proc, log, run, install](int code, QProcess::ExitStatus) {
        run->setEnabled(true);
        install->setEnabled(true);
        if (code == 0) {
            log->appendPlainText(tr("\nDone. The virtual camera is ready."));
            m_ctl.refreshDevices();
        } else if (code == 126 || code == 127) {
            log->appendPlainText(tr("\nAuthorization was cancelled or pkexec is not available."));
        } else {
            log->appendPlainText(tr("\nSetup failed (exit code %1).").arg(code));
        }
        (void)proc;
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
    box.setText(tr(
        "<p>A running instance can be controlled from scripts, hotkey daemons or a Stream Deck:</p>"
        "<pre>camadjust --preset 2            # by number or name\n"
        "camadjust --preset \"Close-up\"\n"
        "camadjust --zoom 1.5\n"
        "camadjust --zoom-in | --zoom-out\n"
        "camadjust --pan 0.2,-0.3\n"
        "camadjust --reset-framing\n"
        "camadjust --virtual-camera on|off|toggle\n"
        "camadjust --list-presets | --status | --show | --quit</pre>"
        "<p><b>Global hotkeys:</b> with <i>Settings → Global shortcuts</i> the desktop portal provides "
        "Ctrl+Alt+1…9 for presets (adjustable in your desktop's settings). Where the portal is not available, "
        "bind keys to the commands above in your desktop's keyboard settings — this works on both Wayland and "
        "X11, including while Zoom has focus.</p>"
        "<p><b>D-Bus:</b> service <code>io.github.LinuxCameraAdjust</code>, object "
        "<code>/io/github/LinuxCameraAdjust</code>, interface <code>io.github.LinuxCameraAdjust1</code> "
        "(ApplyPreset, ListPresets, SetZoom, AdjustZoom, SetPan, ResetFraming, SetVirtualCamera, "
        "ToggleVirtualCamera, ShowWindow, Status, Quit).</p>"));
    box.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About Camera Adjust"),
                       tr("<h3>Camera Adjust %1</h3>"
                          "<p>Webcam controls, framing and a low-latency virtual camera for video calls.</p>"
                          "<p>Pipeline: V4L2 capture → YUV processing (no RGB round trip) → v4l2loopback, "
                          "with the preview rendered on the GPU.</p>"
                          "<p>License: GPL-3.0-or-later</p>")
                           .arg(QApplication::applicationVersion()));
}

} // namespace ui
