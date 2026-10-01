// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "pipeline/Types.h"

#include <QMainWindow>
#include <QPointer>
#include <QTimer>
#include <QVector>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QMenu;
class QPushButton;
class QSplitter;
class QSystemTrayIcon;
class QToolButton;

namespace app {
class Application;
class CameraController;
}

namespace ui {

class EffectsPanel;
class HardwareControlsPanel;
class PreviewWidget;
class PresetsPanel;
class SliderRow;

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
    QWidget *buildTopBar();
    QWidget *buildPreviewArea();
    QWidget *buildSidePanel();
    QWidget *buildColorSection();
    QWidget *buildFramingSection();
    QWidget *buildOutputSection();
    void buildMenus();
    void buildTray();
    void buildShortcuts();

    void syncCameraList();
    void syncModes();
    void syncColor();
    void syncFraming();
    void syncOutput();
    void syncCameraState();
    void updateStats();
    void updatePreviewWanted();
    void updateBanner();
    void rebuildTrayPresets();

    void pushColor();
    void pushFraming(int transitionMs = -1);
    void pushOutput();
    void panBy(double dxOut, double dyOut);
    void runVirtualCameraSetup();
    void showAutomationHelp();
    void showAbout();

    app::Application &m_app;
    app::CameraController &m_ctl;
    bool m_quitting = false;
    bool m_syncing = false;
    bool m_previewPaused = false;

    // Top bar
    QComboBox *m_cameraCombo = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QToolButton *m_vcamButton = nullptr;
    QVector<cam::CameraSelection> m_cameraEntries;
    QVector<cam::CaptureRequest> m_modeEntries;

    // Preview
    PreviewWidget *m_preview = nullptr;
    QLabel *m_banner = nullptr;
    QLabel *m_stats = nullptr;
    QSplitter *m_splitter = nullptr;

    // Panels
    HardwareControlsPanel *m_hwPanel = nullptr;
    EffectsPanel *m_effects = nullptr;
    PresetsPanel *m_presets = nullptr;

    SliderRow *m_brightness, *m_contrast, *m_saturation, *m_gamma, *m_sharpness, *m_warmth, *m_tint;
    SliderRow *m_zoom, *m_panX, *m_panY, *m_fineRotation;
    SliderRow *m_cropL, *m_cropT, *m_cropR, *m_cropB;
    QComboBox *m_rotation = nullptr;
    QComboBox *m_aspect = nullptr;
    QCheckBox *m_mirror = nullptr;
    QCheckBox *m_flip = nullptr;

    QCheckBox *m_outEnabled = nullptr;
    QComboBox *m_outDevice = nullptr;
    QComboBox *m_outResolution = nullptr;
    QComboBox *m_outFps = nullptr;
    QComboBox *m_outFormat = nullptr;
    QLabel *m_outStatus = nullptr;
    QPushButton *m_setupButton = nullptr;

    // Tray & menus
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    QMenu *m_trayPresets = nullptr;
    QAction *m_trayVcam = nullptr;
    QAction *m_trayShow = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_statsAction = nullptr;

    QTimer m_statsTimer;
    QString m_controlErrorText;
    QTimer m_controlErrorTimer;
};

} // namespace ui
