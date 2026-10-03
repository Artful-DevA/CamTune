// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pipeline/Types.h"

#include <QMainWindow>
#include <QTimer>
#include <QVector>

class QAction;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QSlider;
class QSplitter;
class QStackedWidget;
class QSystemTrayIcon;
class QToolButton;

namespace app {
class Application;
class CameraController;
}

namespace ui {

class BackgroundPanel;
class HardwareControlsPanel;
class PreviewWidget;
class SliderRow;

// Layout follows desktop studio apps: menu bar, a toolbar for source / preset
// / output, the viewer with its own tool row, an inspector with a vertical
// page rail on the right, and a status bar with live figures.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(app::Application &app, QWidget *parent = nullptr);
    ~MainWindow() override;

    bool hasTray() const;
    void hideToTray();
    void showAndRaise();
    void prepareQuit();

protected:
    void closeEvent(QCloseEvent *e) override;
    void changeEvent(QEvent *e) override;
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void buildMenus();
    void buildToolBar();
    QWidget *buildViewer();
    QWidget *buildInspector();
    QWidget *buildPicturePage();
    QWidget *buildFramingPage();
    QWidget *buildCameraPage();
    QWidget *buildOutputPage();
    void buildStatusBar();
    void buildTray();
    void buildShortcuts();

    void syncCameraList();
    void syncModes();
    void syncColor();
    void syncFraming();
    void syncOutput();
    void syncCameraState();
    void syncPresets();
    void updateStatus();
    void updatePreviewWanted();
    void updateBanner();

    void pushColor();
    void pushFraming(int transitionMs = -1);
    void pushOutput();
    void panBy(double dxOut, double dyOut);
    void savePreset();
    void managePresets();
    void showPreferences();
    void runVirtualCameraSetup();
    void showAutomationHelp();
    void showAbout();

    app::Application &m_app;
    app::CameraController &m_ctl;
    bool m_quitting = false;
    bool m_syncing = false;
    bool m_previewPaused = false;

    // Toolbar
    QComboBox *m_cameraCombo = nullptr;
    QVector<cam::CameraSelection> m_cameraEntries;
    QComboBox *m_presetCombo = nullptr;
    QToolButton *m_vcamButton = nullptr;

    // Menus
    QMenu *m_presetsMenu = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_vcamAction = nullptr;

    // Viewer
    PreviewWidget *m_preview = nullptr;
    QLabel *m_banner = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QToolButton *m_mirrorButton = nullptr;
    QToolButton *m_flipButton = nullptr;

    // Inspector
    QSplitter *m_splitter = nullptr;
    QStackedWidget *m_pages = nullptr;
    QButtonGroup *m_rail = nullptr;
    QLabel *m_pageTitle = nullptr;
    HardwareControlsPanel *m_hwPanel = nullptr;
    BackgroundPanel *m_background = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QVector<cam::CaptureRequest> m_modeEntries;

    SliderRow *m_brightness, *m_contrast, *m_saturation, *m_gamma, *m_sharpness, *m_warmth, *m_tint;
    SliderRow *m_zoom, *m_panX, *m_panY, *m_straighten;
    SliderRow *m_cropL, *m_cropT, *m_cropR, *m_cropB;
    QComboBox *m_rotation = nullptr;
    QComboBox *m_aspect = nullptr;
    QCheckBox *m_mirror = nullptr;
    QCheckBox *m_flip = nullptr;

    QCheckBox *m_outEnabled = nullptr;
    QLabel *m_outStatus = nullptr;
    QPushButton *m_setupButton = nullptr;
    QComboBox *m_outResolution = nullptr;
    QComboBox *m_outFps = nullptr;
    QComboBox *m_outDevice = nullptr;
    QComboBox *m_outFormat = nullptr;

    // Status bar
    QLabel *m_statusLed = nullptr;
    QLabel *m_statusText = nullptr;
    QLabel *m_captureInfo = nullptr;
    QLabel *m_outputInfo = nullptr;
    QLabel *m_perfInfo = nullptr;

    // Tray
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    QMenu *m_trayPresets = nullptr;
    QAction *m_trayVcam = nullptr;
    QAction *m_trayShow = nullptr;

    QTimer m_statsTimer;
    QString m_controlErrorText;
    QTimer m_controlErrorTimer;
};

} // namespace ui
