// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include "BackgroundPanel.h"
#include "HardwareControlsPanel.h"
#include "Icons.h"
#include "PresetsPanel.h"
#include "PreviewWidget.h"
#include "Theme.h"
#include "Widgets.h"
#include "app/Application.h"
#include "app/GlobalShortcuts.h"
#include "app/SystemIntegration.h"

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QToolBar>
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
    case V4L2_PIX_FMT_JPEG: return QStringLiteral("MJPEG");
    case V4L2_PIX_FMT_YUYV: return QStringLiteral("YUYV");
    case V4L2_PIX_FMT_NV12: return QStringLiteral("NV12");
    case V4L2_PIX_FMT_YUV420: return QStringLiteral("I420");
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

// Wraps an inspector page in a scroll area.
QWidget *scrollPage(QWidget *content)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

// A label that draws nothing rather than a clipped fragment when squeezed.
class FitLabel : public QLabel {
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent *e) override
    {
        if (width() >= QLabel::sizeHint().width())
            QLabel::paintEvent(e);
    }
};

QFrame *statusSeparator()
{
    auto *f = new QFrame;
    f->setObjectName(QStringLiteral("StatusSep"));
    f->setFixedSize(1, 14);
    return f;
}

} // namespace

MainWindow::MainWindow(app::Application &app, QWidget *parent)
    : QMainWindow(parent), m_app(app), m_ctl(app.controller())
{
    setWindowTitle(QStringLiteral("CamTune"));
    setWindowIcon(appIcon());
    m_previewPaused = m_app.settings().previewPaused();

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setHandleWidth(1);
    m_splitter->addWidget(buildViewer());
    m_splitter->addWidget(buildInspector());
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setChildrenCollapsible(false);
    setCentralWidget(m_splitter);

    buildMenus();
    buildToolBar();
    buildStatusBar();
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
        resize(1280, 780);
    if (!m_splitter->restoreState(m_app.settings().splitterState()))
        m_splitter->setSizes({900, 380});
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Menus, toolbar, status bar
// ---------------------------------------------------------------------------

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    QAction *prefs = file->addAction(tr("&Preferences…"), this, &MainWindow::showPreferences);
    prefs->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    file->addSeparator();
    QAction *quit = file->addAction(tr("&Quit"), this, [this] { m_app.quit(); });
    quit->setShortcut(QKeySequence::Quit);

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addAction(icons::get(icons::Name::ZoomIn), tr("Zoom &In"), this, [this] { m_app.adjustZoom(0.1); })
        ->setShortcut(QKeySequence::ZoomIn);
    view->addAction(icons::get(icons::Name::ZoomOut), tr("Zoom &Out"), this, [this] { m_app.adjustZoom(-0.1); })
        ->setShortcut(QKeySequence::ZoomOut);
    view->addAction(icons::get(icons::Name::Fit), tr("&Reset View"), this, [this] { m_app.resetFraming(); })
        ->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    view->addSeparator();
    m_pauseAction = view->addAction(tr("&Pause Preview"));
    m_pauseAction->setCheckable(true);
    m_pauseAction->setChecked(m_previewPaused);
    m_pauseAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    m_pauseAction->setStatusTip(tr("Stop drawing the preview; the virtual camera keeps running"));
    connect(m_pauseAction, &QAction::toggled, this, [this](bool on) {
        m_previewPaused = on;
        m_app.settings().setPreviewPaused(on);
        updatePreviewWanted();
    });

    m_presetsMenu = menuBar()->addMenu(tr("P&resets"));

    QMenu *tools = menuBar()->addMenu(tr("&Tools"));
    m_vcamAction = tools->addAction(tr("&Virtual Camera"));
    m_vcamAction->setCheckable(true);
    m_vcamAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    connect(m_vcamAction, &QAction::triggered, this, [this](bool on) { m_ctl.setVirtualCameraEnabled(on); });
    tools->addAction(tr("&Set Up Virtual Camera…"), this, &MainWindow::runVirtualCameraSetup);
    tools->addSeparator();
    tools->addAction(tr("&Rescan Cameras"), &m_ctl, &app::CameraController::refreshDevices)
        ->setShortcut(QKeySequence(Qt::Key_F5));

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&Command Line && Automation…"), this, &MainWindow::showAutomationHelp);
    help->addAction(tr("&About CamTune"), this, &MainWindow::showAbout);
}

void MainWindow::buildToolBar()
{
    auto *tb = new QToolBar(tr("Main Toolbar"));
    tb->setObjectName(QStringLiteral("MainToolBar"));
    tb->setMovable(false);
    tb->setFloatable(false);
    tb->setIconSize(QSize(18, 18));
    tb->toggleViewAction()->setEnabled(false);
    addToolBar(Qt::TopToolBarArea, tb);

    tb->addWidget(new QLabel(tr("Camera")));
    m_cameraCombo = new QComboBox;
    m_cameraCombo->setMinimumWidth(220);
    m_cameraCombo->setToolTip(tr("Video source"));
    tb->addWidget(m_cameraCombo);
    tb->addSeparator();

    tb->addWidget(new QLabel(tr("Preset")));
    m_presetCombo = new QComboBox;
    m_presetCombo->setMinimumWidth(170);
    m_presetCombo->setPlaceholderText(tr("Custom"));
    m_presetCombo->setToolTip(tr("Apply a saved preset (Ctrl+1…9)"));
    tb->addWidget(m_presetCombo);
    tb->addAction(icons::get(icons::Name::Save), tr("Save current settings as a preset (Ctrl+S)"), this,
                  &MainWindow::savePreset);
    tb->addAction(icons::get(icons::Name::Manage), tr("Manage presets"), this, &MainWindow::managePresets);

    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);

    m_vcamButton = new QToolButton;
    m_vcamButton->setObjectName(QStringLiteral("VirtualCamera"));
    m_vcamButton->setCheckable(true);
    m_vcamButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_vcamButton->setIconSize(QSize(12, 12));
    m_vcamButton->setToolTip(tr("Send the picture to video-call apps as the “CamTune” camera (Ctrl+Shift+V)"));
    tb->addWidget(m_vcamButton);

    connect(m_cameraCombo, qOverload<int>(&QComboBox::activated), this, [this](int idx) {
        const int i = m_cameraCombo->itemData(idx).toInt();
        if (i >= 0 && i < m_cameraEntries.size())
            m_ctl.setCamera(m_cameraEntries[i]);
    });
    connect(m_presetCombo, qOverload<int>(&QComboBox::activated), this,
            [this](int idx) { m_app.applyPresetIndex(idx); });
    connect(m_vcamButton, &QToolButton::toggled, this, [this](bool on) {
        if (!m_syncing)
            m_ctl.setVirtualCameraEnabled(on);
    });
}

void MainWindow::buildStatusBar()
{
    QStatusBar *sb = statusBar();
    sb->setSizeGripEnabled(false);
    auto *left = new QWidget;
    auto *lh = new QHBoxLayout(left);
    lh->setContentsMargins(8, 0, 0, 0);
    lh->setSpacing(6);
    m_statusLed = new QLabel;
    m_statusText = new QLabel;
    m_statusText->setStyleSheet(QStringLiteral("padding: 0;"));
    lh->addWidget(m_statusLed);
    lh->addWidget(m_statusText);
    lh->addStretch(1);
    sb->addWidget(left, 1);
    m_captureInfo = new QLabel;
    m_outputInfo = new QLabel;
    m_perfInfo = new QLabel;
    m_perfInfo->setToolTip(tr("Processing time per frame and delay from camera to output"));
    sb->addPermanentWidget(statusSeparator());
    sb->addPermanentWidget(m_captureInfo);
    sb->addPermanentWidget(statusSeparator());
    sb->addPermanentWidget(m_outputInfo);
    sb->addPermanentWidget(statusSeparator());
    sb->addPermanentWidget(m_perfInfo);
}

// ---------------------------------------------------------------------------
// Viewer
// ---------------------------------------------------------------------------

QWidget *MainWindow::buildViewer()
{
    auto *col = new QWidget;
    col->setObjectName(QStringLiteral("Viewer"));
    col->setAttribute(Qt::WA_StyledBackground);
    auto *v = new QVBoxLayout(col);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    m_banner = new QLabel;
    m_banner->setObjectName(QStringLiteral("Banner"));
    m_banner->setWordWrap(true);
    m_banner->hide();
    v->addWidget(m_banner);

    m_preview = new PreviewWidget;
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    v->addWidget(m_preview, 1);

    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("ViewerBar"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 3, 8, 3);
    h->setSpacing(2);
    auto *zoomOut = iconButton(icons::get(icons::Name::ZoomOut), tr("Zoom out (Ctrl+−)"));
    auto *zoomIn = iconButton(icons::get(icons::Name::ZoomIn), tr("Zoom in (Ctrl++)"));
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(100, 400);
    m_zoomSlider->setFixedWidth(140);
    m_zoomSlider->setToolTip(tr("Zoom (scroll on the picture to zoom, drag to move)"));
    m_zoomLabel = new QLabel(QStringLiteral("100%"));
    m_zoomLabel->setObjectName(QStringLiteral("Hint"));
    m_zoomLabel->setFixedWidth(44);
    m_zoomLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *fit = iconButton(icons::get(icons::Name::Fit), tr("Reset view (Ctrl+0)"));
    m_mirrorButton = iconButton(icons::get(icons::Name::Mirror), tr("Mirror left–right"), true);
    m_flipButton = iconButton(icons::get(icons::Name::Flip), tr("Flip upside down"), true);
    h->addWidget(zoomOut);
    h->addWidget(m_zoomSlider);
    h->addWidget(zoomIn);
    h->addWidget(m_zoomLabel);
    h->addSpacing(10);
    h->addWidget(fit);
    h->addSpacing(10);
    for (QToolButton *b : {m_mirrorButton, m_flipButton})
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_mirrorButton->setText(tr("Mirror"));
    m_flipButton->setText(tr("Flip"));
    h->addWidget(m_mirrorButton);
    h->addWidget(m_flipButton);
    auto *hint = new FitLabel(tr("Scroll to zoom · drag to move · double-click to reset"));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // Shrinks away instead of crowding the buttons in narrow windows.
    hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    h->addWidget(hint, 1);
    v->addWidget(bar);

    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int z) {
        m_zoomLabel->setText(QStringLiteral("%1%").arg(z));
        if (m_syncing)
            return;
        cam::FramingParams f = m_ctl.framing();
        f.zoom = z / 100.0;
        m_ctl.setFraming(f, app::CameraController::kSliderTransitionMs);
    });
    connect(zoomIn, &QToolButton::clicked, this, [this] { m_app.adjustZoom(0.1); });
    connect(zoomOut, &QToolButton::clicked, this, [this] { m_app.adjustZoom(-0.1); });
    connect(fit, &QToolButton::clicked, this, [this] { m_app.resetFraming(); });
    connect(m_mirrorButton, &QToolButton::toggled, this, [this](bool on) {
        if (m_syncing)
            return;
        cam::FramingParams f = m_ctl.framing();
        f.mirror = on;
        m_ctl.setFraming(f, 0);
    });
    connect(m_flipButton, &QToolButton::toggled, this, [this](bool on) {
        if (m_syncing)
            return;
        cam::FramingParams f = m_ctl.framing();
        f.flip = on;
        m_ctl.setFraming(f, 0);
    });
    connect(m_preview, &PreviewWidget::zoomRequested, this, [this](double steps) {
        cam::FramingParams f = m_ctl.framing();
        f.zoom = std::clamp(f.zoom * std::pow(1.08, steps), 1.0, 8.0);
        m_ctl.setFraming(f, 120);
    });
    connect(m_preview, &PreviewWidget::panRequested, this, &MainWindow::panBy);
    connect(m_preview, &PreviewWidget::doubleClicked, this, [this] { m_app.resetFraming(); });
    return col;
}

// ---------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------

QWidget *MainWindow::buildInspector()
{
    auto *panel = new QWidget;
    panel->setObjectName(QStringLiteral("Inspector"));
    panel->setAttribute(Qt::WA_StyledBackground);
    panel->setMinimumWidth(330);
    panel->setMaximumWidth(560);
    auto *h = new QHBoxLayout(panel);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    // Vertical page rail: never scrolls, never truncates.
    auto *rail = new QWidget;
    rail->setObjectName(QStringLiteral("InspectorRail"));
    rail->setAttribute(Qt::WA_StyledBackground);
    auto *rl = new QVBoxLayout(rail);
    rl->setContentsMargins(0, 4, 0, 4);
    rl->setSpacing(0);
    m_rail = new QButtonGroup(this);
    m_rail->setExclusive(true);

    auto *right = new QWidget;
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);
    m_pageTitle = new QLabel;
    m_pageTitle->setObjectName(QStringLiteral("InspectorTitle"));
    rv->addWidget(m_pageTitle);
    m_pages = new QStackedWidget;
    rv->addWidget(m_pages, 1);

    auto *bg = new QWidget;
    auto *bgl = new QVBoxLayout(bg);
    bgl->setContentsMargins(0, 0, 0, 0);
    m_background = new BackgroundPanel(m_ctl, m_preview);
    bgl->addWidget(m_background);

    const struct {
        icons::Name icon;
        QString title;
        QWidget *page;
    } pages[] = {
        {icons::Name::Picture, tr("Picture"), buildPicturePage()},
        {icons::Name::Framing, tr("Framing"), buildFramingPage()},
        {icons::Name::Background, tr("Background"), bg},
        {icons::Name::Camera, tr("Camera"), buildCameraPage()},
        {icons::Name::Output, tr("Output"), buildOutputPage()},
    };
    int index = 0;
    for (const auto &p : pages) {
        auto *b = new QToolButton;
        b->setObjectName(QStringLiteral("RailButton"));
        b->setIcon(icons::get(p.icon));
        b->setIconSize(QSize(22, 22));
        b->setCheckable(true);
        b->setToolTip(p.title);
        b->setFocusPolicy(Qt::TabFocus);
        m_rail->addButton(b, index++);
        rl->addWidget(b);
        m_pages->addWidget(scrollPage(p.page));
        b->setProperty("title", p.title);
    }
    rl->addStretch(1);
    h->addWidget(rail);
    h->addWidget(right, 1);

    const int bgIndex = 2;
    auto select = [this, bgIndex](int i) {
        m_pages->setCurrentIndex(i);
        if (auto *b = m_rail->button(i)) {
            b->setChecked(true);
            m_pageTitle->setText(b->property("title").toString());
        }
        // Outlines and drawing modes only make sense while that page is open.
        if (i == bgIndex) {
            m_background->syncFromModel();
        } else {
            m_background->cancelInteraction();
            m_preview->setOverlayRects({});
        }
        m_app.settings().setLastTab(i);
    };
    connect(m_rail, &QButtonGroup::idClicked, this, select);
    select(std::clamp(m_app.settings().lastTab(), 0, m_pages->count() - 1));
    return panel;
}

QWidget *MainWindow::buildPicturePage()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *tone = new Section(tr("Light"));
    m_exposure = new SliderRow(tr("Exposure"), -2, 2, 0, 2, QStringLiteral(" EV"));
    m_exposure->setHint(tr("Overall brightness in camera stops, like opening the lens"));
    m_contrast = new SliderRow(tr("Contrast"), 0.2, 2.5, 1, 0, QStringLiteral(" %"), 100);
    m_highlights = new SliderRow(tr("Highlights"), -1, 1, 0, 0, QString(), 100);
    m_highlights->setHint(tr("Recover a bright window or lamp (−) or brighten light areas (+)"));
    m_shadows = new SliderRow(tr("Shadows"), -1, 1, 0, 0, QString(), 100);
    m_shadows->setHint(tr("Lift a dark face or background (+) or deepen dark areas (−)"));
    m_whitePoint = new SliderRow(tr("White point"), 0.5, 1, 1, 0, QStringLiteral(" %"), 100);
    m_whitePoint->setHint(tr("The brightness that becomes pure white. Lower it if the image looks dull"));
    m_blackPoint = new SliderRow(tr("Black point"), 0, 0.5, 0, 0, QStringLiteral(" %"), 100);
    m_blackPoint->setHint(tr("The darkness that becomes pure black. Raise it if blacks look washed out"));
    m_brightness = new SliderRow(tr("Brightness"), -0.5, 0.5, 0, 0, QString(), 200);
    m_gamma = new SliderRow(tr("Gamma"), 0.3, 3.0, 1, 2);
    m_gamma->setHint(tr("Brightens or darkens the midtones without clipping highlights"));
    for (auto *s : {m_exposure, m_contrast, m_highlights, m_shadows, m_whitePoint, m_blackPoint, m_brightness,
                    m_gamma})
        tone->contentLayout()->addWidget(s);
    tone->addHeaderAction(icons::get(icons::Name::Reset), tr("Reset light"), [this] {
        cam::ColorParams c = m_ctl.color();
        const cam::ColorParams d;
        c.exposure = d.exposure;
        c.contrast = d.contrast;
        c.highlights = d.highlights;
        c.shadows = d.shadows;
        c.whitePoint = d.whitePoint;
        c.blackPoint = d.blackPoint;
        c.brightness = d.brightness;
        c.gamma = d.gamma;
        m_ctl.setColor(c);
    });
    v->addWidget(tone);

    auto *color = new Section(tr("Color"));
    m_warmth = new SliderRow(tr("Temperature"), -1, 1, 0, 0, QString(), 100);
    m_warmth->setHint(tr("Cooler (blue) ← → warmer (amber)"));
    m_tint = new SliderRow(tr("Tint"), -1, 1, 0, 0, QString(), 100);
    m_tint->setHint(tr("Green ← → magenta"));
    m_vibrance = new SliderRow(tr("Vibrance"), -1, 1, 0, 0, QString(), 100);
    m_vibrance->setHint(tr("Boosts muted colors more than strong ones and keeps skin tones natural"));
    m_saturation = new SliderRow(tr("Saturation"), 0, 2.5, 1, 0, QStringLiteral(" %"), 100);
    m_hue = new SliderRow(tr("Hue"), -180, 180, 0, 0, QStringLiteral("°"));
    m_hue->setHint(tr("Rotates every color around the color wheel"));
    for (auto *s : {m_warmth, m_tint, m_vibrance, m_saturation, m_hue})
        color->contentLayout()->addWidget(s);
    color->addHeaderAction(icons::get(icons::Name::Reset), tr("Reset color"), [this] {
        cam::ColorParams c = m_ctl.color();
        const cam::ColorParams d;
        c.warmth = d.warmth;
        c.tint = d.tint;
        c.vibrance = d.vibrance;
        c.saturation = d.saturation;
        c.hue = d.hue;
        m_ctl.setColor(c);
    });
    v->addWidget(color);

    auto *detail = new Section(tr("Detail"));
    m_sharpness = new SliderRow(tr("Sharpness"), 0, 2, 0, 0, QStringLiteral(" %"), 50);
    detail->contentLayout()->addWidget(m_sharpness);
    v->addWidget(detail);
    v->addStretch(1);

    for (SliderRow *s : {m_brightness, m_contrast, m_saturation, m_gamma, m_sharpness, m_warmth, m_tint, m_exposure,
                         m_highlights, m_shadows, m_whitePoint, m_blackPoint, m_vibrance, m_hue})
        connect(s, &SliderRow::valueChanged, this, &MainWindow::pushColor);
    return w;
}

QWidget *MainWindow::buildFramingPage()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *transform = new Section(tr("Transform"));
    m_zoom = new SliderRow(tr("Zoom"), 1, 8, 1, 2, QStringLiteral("×"));
    m_panX = new SliderRow(tr("Position X"), -1, 1, 0, 0, QStringLiteral(" %"), 100);
    m_panY = new SliderRow(tr("Position Y"), -1, 1, 0, 0, QStringLiteral(" %"), 100);
    m_panX->setHint(tr("Moves the view when zoomed in (you can also drag the picture)"));
    m_panY->setHint(tr("Moves the view when zoomed in (you can also drag the picture)"));
    for (auto *s : {m_zoom, m_panX, m_panY})
        transform->contentLayout()->addWidget(s);
    transform->addHeaderAction(icons::get(icons::Name::Reset), tr("Reset view"), [this] { m_app.resetFraming(); });
    v->addWidget(transform);

    auto *orient = new Section(tr("Orientation"));
    m_rotation = new QComboBox;
    for (int deg : {0, 90, 180, 270})
        m_rotation->addItem(QStringLiteral("%1°").arg(deg), deg);
    orient->contentLayout()->addWidget(propertyRow(tr("Rotation"), m_rotation));
    m_straighten = new SliderRow(tr("Straighten"), -15, 15, 0, 1, QStringLiteral("°"));
    m_straighten->setHint(tr("Corrects a slightly tilted camera"));
    orient->contentLayout()->addWidget(m_straighten);
    m_mirror = new QCheckBox(tr("Mirror left–right"));
    m_mirror->setToolTip(tr("Call apps mirror your own self-view anyway; this changes what others see"));
    m_flip = new QCheckBox(tr("Upside down"));
    orient->contentLayout()->addWidget(propertyRow(tr("Flip"), m_mirror));
    orient->contentLayout()->addWidget(propertyRow(QString(), m_flip));
    v->addWidget(orient);

    auto *aspect = new Section(tr("Aspect"));
    m_aspect = new QComboBox;
    m_aspect->addItem(tr("Fill (crop edges)"), int(cam::AspectMode::Fill));
    m_aspect->addItem(tr("Fit (letterbox)"), int(cam::AspectMode::Fit));
    m_aspect->addItem(tr("Stretch"), int(cam::AspectMode::Stretch));
    m_aspect->setToolTip(tr("Used when the camera's aspect ratio differs from the output"));
    aspect->contentLayout()->addWidget(propertyRow(tr("Scaling"), m_aspect));
    v->addWidget(aspect);

    auto *crop = new Section(tr("Crop"), false);
    m_cropL = new SliderRow(tr("Left"), 0, 0.45, 0, 0, QStringLiteral(" %"), 100);
    m_cropR = new SliderRow(tr("Right"), 0, 0.45, 0, 0, QStringLiteral(" %"), 100);
    m_cropT = new SliderRow(tr("Top"), 0, 0.45, 0, 0, QStringLiteral(" %"), 100);
    m_cropB = new SliderRow(tr("Bottom"), 0, 0.45, 0, 0, QStringLiteral(" %"), 100);
    for (auto *s : {m_cropL, m_cropR, m_cropT, m_cropB})
        crop->contentLayout()->addWidget(s);
    v->addWidget(crop);
    v->addStretch(1);

    for (SliderRow *s : {m_zoom, m_panX, m_panY, m_straighten, m_cropL, m_cropR, m_cropT, m_cropB})
        connect(s, &SliderRow::valueChanged, this, [this] { pushFraming(); });
    connect(m_rotation, qOverload<int>(&QComboBox::activated), this, [this] { pushFraming(250); });
    connect(m_aspect, qOverload<int>(&QComboBox::activated), this, [this] { pushFraming(0); });
    connect(m_mirror, &QCheckBox::toggled, this, [this] { pushFraming(0); });
    connect(m_flip, &QCheckBox::toggled, this, [this] { pushFraming(0); });
    return w;
}

QWidget *MainWindow::buildCameraPage()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *capture = new Section(tr("Capture"));
    m_modeCombo = new QComboBox;
    m_modeCombo->setToolTip(tr("Resolution, frame rate and format requested from the camera"));
    capture->contentLayout()->addWidget(propertyRow(tr("Format"), m_modeCombo));
    v->addWidget(capture);
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

QWidget *MainWindow::buildOutputPage()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *vcam = new Section(tr("Virtual Camera"));
    m_outEnabled = new QCheckBox(tr("Enabled"));
    vcam->contentLayout()->addWidget(propertyRow(tr("Output"), m_outEnabled));
    m_outStatus = hintLabel(QString());
    m_outStatus->setContentsMargins(kLabelWidth + 6, 0, 0, 4);
    vcam->contentLayout()->addWidget(m_outStatus);
    m_setupButton = new QPushButton(tr("Set Up Virtual Camera…"));
    m_setupButton->setObjectName(QStringLiteral("Primary"));
    vcam->contentLayout()->addWidget(propertyRow(QString(), m_setupButton));
    m_outResolution = new QComboBox;
    for (auto &r : kResolutions)
        m_outResolution->addItem(QStringLiteral("%1 × %2").arg(r[0]).arg(r[1]), QSize(r[0], r[1]));
    vcam->contentLayout()->addWidget(propertyRow(tr("Resolution"), m_outResolution));
    m_outFps = new QComboBox;
    for (int f : kFrameRates)
        m_outFps->addItem(tr("%1 fps").arg(f), f);
    vcam->contentLayout()->addWidget(propertyRow(tr("Frame rate"), m_outFps));
    v->addWidget(vcam);

    auto *adv = new Section(tr("Compatibility"), false);
    m_outDevice = new QComboBox;
    adv->contentLayout()->addWidget(propertyRow(tr("Device"), m_outDevice));
    m_outFormat = new QComboBox;
    m_outFormat->addItem(tr("I420 (recommended)"), int(cam::OutputPixelFormat::I420));
    m_outFormat->addItem(tr("YUYV"), int(cam::OutputPixelFormat::YUYV));
    adv->contentLayout()->addWidget(propertyRow(tr("Pixel format"), m_outFormat));
    auto *note = hintLabel(tr("Change only if an application does not list or accept the virtual camera."));
    note->setContentsMargins(kLabelWidth + 6, 2, 0, 0);
    adv->contentLayout()->addWidget(note);
    v->addWidget(adv);
    v->addStretch(1);

    connect(m_outEnabled, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing)
            m_ctl.setVirtualCameraEnabled(on);
    });
    for (QComboBox *c : {m_outDevice, m_outResolution, m_outFps, m_outFormat})
        connect(c, qOverload<int>(&QComboBox::activated), this, &MainWindow::pushOutput);
    connect(m_setupButton, &QPushButton::clicked, this, &MainWindow::runVirtualCameraSetup);
    return w;
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(appIcon(), this);
    m_tray->setToolTip(QStringLiteral("CamTune"));
    m_trayMenu = new QMenu(this);
    m_trayShow = m_trayMenu->addAction(tr("Show CamTune"));
    connect(m_trayShow, &QAction::triggered, this, [this] {
        if (isVisible() && !isMinimized())
            hideToTray();
        else
            showAndRaise();
    });
    m_trayVcam = m_trayMenu->addAction(tr("Virtual Camera"));
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
        m_trayShow->setText(isVisible() && !isMinimized() ? tr("Hide CamTune") : tr("Show CamTune"));
    });
    m_tray->show();
}

void MainWindow::buildShortcuts()
{
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
        m_cameraCombo->addItem(tr("%1 (disconnected)").arg(QString::fromStdString(current.card)),
                               m_cameraEntries.size() - 1);
    }
    if (m_cameraCombo->count() > 0)
        m_cameraCombo->insertSeparator(m_cameraCombo->count());
    cam::CameraSelection test;
    test.testPattern = true;
    if (current.testPattern)
        currentIndex = m_cameraEntries.size();
    m_cameraEntries.append(test);
    m_cameraCombo->addItem(tr("Test Pattern"), m_cameraEntries.size() - 1);
    cam::CameraSelection none;
    if (current.empty())
        currentIndex = m_cameraEntries.size();
    m_cameraEntries.append(none);
    m_cameraCombo->addItem(tr("None"), m_cameraEntries.size() - 1);
    m_cameraCombo->setCurrentIndex(m_cameraCombo->findData(currentIndex));
    m_syncing = false;
}

void MainWindow::syncModes()
{
    m_syncing = true;
    m_modeCombo->clear();
    m_modeEntries.clear();
    const auto &active = m_ctl.activeMode();
    m_modeEntries.append(cam::CaptureRequest());
    QString autoText = tr("Automatic");
    if (active.width > 0 && m_ctl.captureRequest().automatic)
        autoText = tr("Automatic (%1×%2 @ %3)").arg(active.width).arg(active.height).arg(fpsText(active.fps));
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
            m_modeCombo->addItem(QStringLiteral("%1×%2 @ %3  %4")
                                     .arg(m.width)
                                     .arg(m.height)
                                     .arg(fpsText(r.fps()))
                                     .arg(formatName(m.fourcc)),
                                 m_modeEntries.size() - 1);
        }
    }
    if (!req.automatic && currentIndex == 0 && m_ctl.modes().empty()) {
        m_modeEntries.append(req);
        m_modeCombo->addItem(QStringLiteral("%1×%2 @ %3").arg(req.width).arg(req.height).arg(fpsText(req.rate.fps())),
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
    m_exposure->setValue(c.exposure);
    m_highlights->setValue(c.highlights);
    m_shadows->setValue(c.shadows);
    m_whitePoint->setValue(c.whitePoint);
    m_blackPoint->setValue(c.blackPoint);
    m_vibrance->setValue(c.vibrance);
    m_hue->setValue(c.hue);
}

void MainWindow::syncFraming()
{
    m_syncing = true;
    const cam::FramingParams &f = m_ctl.framing();
    m_zoom->setValue(f.zoom);
    if (!m_zoomSlider->isSliderDown())
        m_zoomSlider->setValue(int(std::lround(std::min(f.zoom, 4.0) * 100)));
    m_zoomLabel->setText(QStringLiteral("%1%").arg(int(std::lround(f.zoom * 100))));
    m_panX->setValue(f.panX);
    m_panY->setValue(f.panY);
    double rot = std::fmod(f.rotation, 360.0);
    if (rot < 0)
        rot += 360;
    const int quarter = int(std::floor((rot + 45) / 90)) % 4;
    double fine = rot - quarter * 90;
    if (fine > 180)
        fine -= 360;
    m_rotation->setCurrentIndex(quarter);
    m_straighten->setValue(fine);
    {
        QSignalBlocker b1(m_mirror), b2(m_flip), b3(m_mirrorButton), b4(m_flipButton);
        m_mirror->setChecked(f.mirror);
        m_flip->setChecked(f.flip);
        m_mirrorButton->setChecked(f.mirror);
        m_flipButton->setChecked(f.flip);
    }
    m_aspect->setCurrentIndex(std::max(0, m_aspect->findData(int(f.aspect))));
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
    {
        QSignalBlocker b1(m_outEnabled), b2(m_vcamButton);
        m_outEnabled->setChecked(o.enabled);
        m_vcamButton->setChecked(o.enabled);
    }
    m_vcamAction->setChecked(o.enabled);
    if (m_trayVcam)
        m_trayVcam->setChecked(o.enabled);

    m_outDevice->clear();
    m_outDevice->addItem(tr("Automatic"), QString());
    int devIndex = 0;
    for (const auto &d : m_ctl.loopbackDevices()) {
        const QString path = QString::fromStdString(d.path);
        m_outDevice->addItem(QStringLiteral("%1 (%2)").arg(QString::fromStdString(d.card), path), path);
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

    const bool noDevice = m_ctl.loopbackDevices().isEmpty();
    QString detail;
    QColor led = theme::kMuted;
    QString button = tr("Start Virtual Camera");
    switch (m_ctl.outputState()) {
    case cam::OutputState::Disabled:
        detail = noDevice ? tr("Not set up yet. A one-time setup is needed.") : tr("Off.");
        break;
    case cam::OutputState::Active:
        detail = m_ctl.outputMessage().isEmpty() ? tr("Running. Select “CamTune” as the camera in your call app.")
                                                 : m_ctl.outputMessage();
        led = theme::kSuccess;
        button = tr("Virtual Camera On");
        break;
    case cam::OutputState::NoDevice:
        detail = tr("Not set up yet. A one-time setup is needed.");
        led = theme::kWarning;
        button = tr("Virtual Camera: Setup Needed");
        break;
    case cam::OutputState::Error:
        detail = m_ctl.outputMessage();
        led = theme::kDanger;
        button = tr("Virtual Camera: Error");
        break;
    }
    m_vcamButton->setText(button);
    m_vcamButton->setIcon(icons::led(led));
    m_outStatus->setText(detail);
    m_setupButton->parentWidget()->setVisible(noDevice);
    if (m_tray)
        m_tray->setToolTip(m_ctl.outputState() == cam::OutputState::Active ? tr("CamTune — virtual camera on")
                                                                           : QStringLiteral("CamTune"));
    m_syncing = false;
    updateBanner();
    updateStatus();
}

void MainWindow::syncCameraState()
{
    switch (m_ctl.cameraState()) {
    case cam::CameraState::Streaming:
        m_preview->setMessage(m_previewPaused ? tr("Preview paused (Ctrl+P) — the virtual camera keeps running")
                                              : QString());
        break;
    case cam::CameraState::Opening:
        m_preview->setMessage(tr("Starting camera…"));
        break;
    case cam::CameraState::NoCamera:
        m_preview->clearFrame();
        m_preview->setMessage(tr("No camera selected"));
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

    m_presetCombo->clear();
    int currentIndex = -1;
    for (int i = 0; i < list.size(); ++i) {
        m_presetCombo->addItem(list[i].shortcut ? QStringLiteral("%1\t(Ctrl+%2)").arg(list[i].name).arg(list[i].shortcut)
                                                : list[i].name);
        if (list[i].name == current)
            currentIndex = i;
    }
    m_presetCombo->setCurrentIndex(currentIndex); // -1 shows "Custom"
    // Show the plain name in the closed combo.
    for (int i = 0; i < list.size(); ++i)
        m_presetCombo->setItemText(i, list[i].name);

    m_presetsMenu->clear();
    m_presetsMenu->addAction(icons::get(icons::Name::Save), tr("&Save Current Settings…"), this, &MainWindow::savePreset)
        ->setShortcut(QKeySequence::Save);
    m_presetsMenu->actions().last()->setShortcutContext(Qt::WidgetShortcut); // handled by the QShortcut
    m_presetsMenu->addAction(icons::get(icons::Name::Manage), tr("&Manage Presets…"), this, &MainWindow::managePresets);
    m_presetsMenu->addSeparator();
    for (int i = 0; i < list.size(); ++i) {
        QAction *a = m_presetsMenu->addAction(list[i].name);
        a->setCheckable(true);
        a->setChecked(list[i].name == current);
        if (list[i].shortcut)
            a->setShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_0 + list[i].shortcut)));
        connect(a, &QAction::triggered, this, [this, i] { m_app.applyPresetIndex(i); });
    }

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
                tr("The virtual camera is not set up yet. Use Tools → Set Up Virtual Camera.");
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
    QString text;
    QColor led = theme::kMuted;
    const QString name = QString::fromStdString(m_ctl.camera().displayName());
    const bool live = m_ctl.cameraState() == cam::CameraState::Streaming;
    switch (m_ctl.cameraState()) {
    case cam::CameraState::Streaming:
        led = theme::kSuccess;
        text = s.overloaded ? tr("%1 — system busy, quality reduced").arg(name) : name;
        break;
    case cam::CameraState::Opening:
        led = theme::kWarning;
        text = tr("Starting %1…").arg(name);
        break;
    case cam::CameraState::Waiting:
        led = theme::kWarning;
        text = tr("%1 disconnected — waiting").arg(name);
        break;
    case cam::CameraState::Busy:
    case cam::CameraState::Error:
        led = theme::kDanger;
        text = tr("Camera unavailable — retrying");
        break;
    case cam::CameraState::Suspended:
        text = tr("Paused for system sleep");
        break;
    case cam::CameraState::NoCamera:
        text = tr("No camera");
        break;
    }
    m_statusLed->setPixmap(icons::led(led).pixmap(12, 12));
    m_statusText->setText(text);

    const auto &m = m_ctl.activeMode();
    m_captureInfo->setText(live ? tr("Input %1×%2 · %3 fps").arg(m.width).arg(m.height).arg(s.captureFps, 0, 'f', 1)
                                : tr("Input —"));
    if (m_ctl.outputState() == cam::OutputState::Active) {
        int w, h;
        m_ctl.renderSize(w, h);
        m_outputInfo->setText(tr("Virtual camera %1×%2 · %3 fps").arg(w).arg(h).arg(s.outputFps, 0, 'f', 1));
    } else {
        m_outputInfo->setText(tr("Virtual camera off"));
    }
    m_perfInfo->setText(live ? tr("Process %1 ms · Latency %2 ms").arg(s.processMs, 0, 'f', 1).arg(s.latencyMs, 0, 'f', 0)
                             : QString());
}

void MainWindow::updatePreviewWanted()
{
    const bool visible = isVisible() && !isMinimized();
    m_ctl.setPreviewWanted(visible && !m_previewPaused);
    if (m_previewPaused)
        m_preview->setMessage(tr("Preview paused (Ctrl+P) — the virtual camera keeps running"));
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
    c.exposure = m_exposure->value();
    c.highlights = m_highlights->value();
    c.shadows = m_shadows->value();
    c.whitePoint = m_whitePoint->value();
    c.blackPoint = m_blackPoint->value();
    c.vibrance = m_vibrance->value();
    c.hue = m_hue->value();
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
    f.rotation = m_rotation->currentData().toInt() + m_straighten->value();
    f.mirror = m_mirror->isChecked();
    f.flip = m_flip->isChecked();
    f.aspect = cam::AspectMode(m_aspect->currentData().toInt());
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
    // Hidden child so the name dialog is centred on the main window.
    PresetsPanel panel(m_app, this);
    panel.hide();
    panel.saveNew();
}

void MainWindow::managePresets()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Manage Presets"));
    dlg.resize(440, 460);
    auto *v = new QVBoxLayout(&dlg);
    v->addWidget(new PresetsPanel(m_app));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(buttons);
    dlg.exec();
}

void MainWindow::showPreferences()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Preferences"));
    dlg.setMinimumWidth(460);
    auto *v = new QVBoxLayout(&dlg);

    auto *startup = new QGroupBox(tr("Startup"));
    auto *sl = new QVBoxLayout(startup);
    auto *login = new QCheckBox(tr("Start CamTune when I log in"));
    login->setChecked(app::autostart::isEnabled());
    auto *hidden = new QCheckBox(tr("Start hidden in the system tray"));
    hidden->setChecked(m_app.settings().startMinimized());
    sl->addWidget(login);
    sl->addWidget(hidden);
    v->addWidget(startup);

    auto *behaviour = new QGroupBox(tr("Behaviour"));
    auto *bl = new QVBoxLayout(behaviour);
    auto *tray = new QCheckBox(tr("Keep running in the tray when the window is closed"));
    tray->setChecked(m_app.settings().closeToTray());
    auto *global = new QCheckBox(tr("Global shortcuts — Ctrl+Alt+1…9 apply presets from any app"));
    global->setChecked(m_app.settings().globalShortcuts());
    auto *globalStatus = hintLabel(m_app.settings().globalShortcuts() ? m_app.globalShortcuts()->statusText()
                                                                      : QString());
    globalStatus->setVisible(!globalStatus->text().isEmpty());
    auto *form = new QFormLayout;
    auto *transition = new QDoubleSpinBox;
    transition->setRange(0, 3);
    transition->setSingleStep(0.05);
    transition->setDecimals(2);
    transition->setSuffix(tr(" s"));
    transition->setValue(m_app.settings().presetTransitionMs() / 1000.0);
    form->addRow(tr("Preset transition:"), transition);
    bl->addWidget(tray);
    bl->addWidget(global);
    bl->addWidget(globalStatus);
    bl->addLayout(form);
    v->addWidget(behaviour);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(buttons);

    connect(login, &QCheckBox::toggled, &dlg, [this, login](bool on) {
        QString err;
        if (!app::autostart::setEnabled(on, &err)) {
            QMessageBox::warning(this, tr("Preferences"), err);
            QSignalBlocker b(login);
            login->setChecked(!on);
        }
    });
    connect(hidden, &QCheckBox::toggled, &dlg, [this](bool on) { m_app.settings().setStartMinimized(on); });
    connect(tray, &QCheckBox::toggled, &dlg, [this](bool on) { m_app.settings().setCloseToTray(on); });
    connect(global, &QCheckBox::toggled, &dlg, [this](bool on) { m_app.setGlobalShortcutsEnabled(on); });
    connect(m_app.globalShortcuts(), &app::GlobalShortcuts::statusChanged, globalStatus, [this, globalStatus] {
        const QString s = m_app.settings().globalShortcuts() ? m_app.globalShortcuts()->statusText() : QString();
        globalStatus->setText(s);
        globalStatus->setVisible(!s.isEmpty());
    });
    connect(transition, qOverload<double>(&QDoubleSpinBox::valueChanged), &dlg,
            [this](double s) { m_app.settings().setPresetTransitionMs(int(s * 1000)); });
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
    dlg.setWindowTitle(tr("Set Up Virtual Camera"));
    dlg.resize(620, 440);
    auto *v = new QVBoxLayout(&dlg);
    auto *intro = new QLabel(
        tr("<p>The virtual camera uses the <b>v4l2loopback</b> kernel module. Setup loads it with settings that "
           "work with Zoom, Teams, Meet, Discord and browsers, names it “CamTune”, and loads it at every boot. "
           "You will be asked for your password.</p>"
           "<p>If the module is not installed yet, choose <b>Install &amp; Set Up</b>. Fedora needs the free "
           "RPM Fusion repository enabled first. With Secure Boot, the installer may ask you to enroll a key on "
           "the next reboot.</p>"));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    v->addWidget(intro);
    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    log->setPlaceholderText(tr("Output will appear here."));
    v->addWidget(log, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *install = buttons->addButton(tr("Install && Set Up"), QDialogButtonBox::ActionRole);
    auto *run = buttons->addButton(tr("Set Up"), QDialogButtonBox::ActionRole);
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
            log->appendPlainText(tr("\nThe module is not installed. Click “Install & Set Up”."));
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
    box.setWindowTitle(tr("Command Line & Automation"));
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
                   "<p><b>Shortcuts during a call:</b> enable global shortcuts in File → Preferences, or bind "
                   "keys to the commands above in your desktop's keyboard settings (Wayland and X11).</p>"
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
                   "<p>Webcam controls, framing, background effects and a low-latency virtual camera for "
                   "video calls. Everything runs on this computer.</p>"
                   "<p>License: GPL-3.0-or-later. Person detection uses Google's MediaPipe selfie "
                   "segmentation model (Apache-2.0).</p>")
                    .arg(QApplication::applicationVersion()));
    box.exec();
}

} // namespace ui
