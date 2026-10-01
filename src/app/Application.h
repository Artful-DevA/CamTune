// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "AppSettings.h"
#include "CameraController.h"
#include "PresetStore.h"

#include <QObject>
#include <QPointer>

namespace ui {
class MainWindow;
}

namespace app {

class GlobalShortcuts;
class SleepMonitor;

// Wires together the model (CameraController), presets, settings, the main
// window and desktop integration (D-Bus, global shortcuts, suspend handling).
class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject *parent = nullptr);
    ~Application() override;

    // Returns false if the D-Bus name is already owned (another instance).
    bool registerDBus();
    void start(bool minimized);

    CameraController &controller() { return *m_controller; }
    PresetStore &presets() { return m_presets; }
    AppSettings &settings() { return m_settings; }
    GlobalShortcuts *globalShortcuts() { return m_shortcuts; }

    bool applyPreset(const QString &nameOrNumber);
    void applyPresetIndex(int index);
    void setZoom(double zoom);
    void adjustZoom(double delta);
    void setPan(double x, double y);
    void resetFraming();
    void showWindow();
    void quit();
    QString statusText() const;
    QString currentPresetName() const { return m_currentPreset; }
    void setGlobalShortcutsEnabled(bool enabled);

Q_SIGNALS:
    void presetApplied(const QString &name);

private:
    void onGlobalShortcut(const QString &id);

    AppSettings m_settings;
    PresetStore m_presets;
    CameraController *m_controller = nullptr;
    QPointer<ui::MainWindow> m_window;
    GlobalShortcuts *m_shortcuts = nullptr;
    SleepMonitor *m_sleep = nullptr;
    QString m_currentPreset;
};

} // namespace app
