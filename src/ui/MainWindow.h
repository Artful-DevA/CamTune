// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pipeline/Types.h"

#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QVector>

class QAction;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QMenu;
class QPushButton;
class QSlider;
class QSplitter;
class QSystemTrayIcon;
class QTabWidget;
class QToolButton;

namespace app {
class Application;
class CameraController;
}

namespace ui {

class BackgroundPanel;
class HardwareControlsPanel;
class PreviewWidget;
class SegmentedControl;
class SliderRow;
class ToggleRow;
class ToggleSwitch;

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
    QWidget *buildHeader();
    QWidget *buildPreviewColumn();
    QWidget *buildSidebar();
    QWidget *buildPictureTab();
    QWidget *buildFramingTab();
    QWidget *buildCameraTab();
    QWidget *buildOutputTab();
    QMenu *buildMainMenu();
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
    void runVirtualCameraSetup();
    void showAutomationHelp();
    void showAbout();

    app::Application &m_app;
    app::CameraController &m_ctl;
    bool m_quitting = false;
    bool m_syncing = false;
    bool m_previewPaused = false;
    bool m_showPerformance = false;

    // Header
    QComboBox *m_cameraCombo = nullptr;
    QVector<cam::CameraSelection> m_cameraEntries;
    ToggleSwitch *m_vcamSwitch = nullptr;
    QLabel *m_vcamState = nullptr;
    QToolButton *m_presetsButton = nullptr;
    QMenu *m_presetsMenu = nullptr;

    // Preview column
    PreviewWidget *m_preview = nullptr;
    QLabel *m_banner = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QPushButton *m_mirrorButton = nullptr;
    QHBoxLayout *m_chipLayout = nullptr;
    QLabel *m_statusDot = nullptr;
    QLabel *m_statusText = nullptr;
    QLabel *m_perfText = nullptr;

    // Sidebar
    QTabWidget *m_tabs = nullptr;
    QSplitter *m_splitter = nullptr;
    HardwareControlsPanel *m_hwPanel = nullptr;
    BackgroundPanel *m_background = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QVector<cam::CaptureRequest> m_modeEntries;

    SliderRow *m_brightness, *m_contrast, *m_saturation, *m_gamma, *m_sharpness, *m_warmth, *m_tint;
    SliderRow *m_zoom, *m_panX, *m_panY, *m_straighten;
    SliderRow *m_cropL, *m_cropT, *m_cropR, *m_cropB;
    SegmentedControl *m_rotation = nullptr;
    SegmentedControl *m_aspect = nullptr;
    ToggleRow *m_mirror = nullptr;
    ToggleRow *m_flip = nullptr;

    ToggleRow *m_outEnabled = nullptr;
    QLabel *m_outStatus = nullptr;
    QPushButton *m_setupButton = nullptr;
    QComboBox *m_outResolution = nullptr;
    QComboBox *m_outFps = nullptr;
    QComboBox *m_outDevice = nullptr;
    QComboBox *m_outFormat = nullptr;

    // Tray & menus
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    QMenu *m_trayPresets = nullptr;
    QAction *m_trayVcam = nullptr;
    QAction *m_trayShow = nullptr;
    QAction *m_pauseAction = nullptr;

    QTimer m_statsTimer;
    QString m_controlErrorText;
    QTimer m_controlErrorTimer;
};

} // namespace ui
