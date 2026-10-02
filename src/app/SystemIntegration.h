// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

namespace app {

// Watches logind's PrepareForSleep signal so the camera can be released
// before suspend and reopened cleanly after resume.
class SleepMonitor : public QObject {
    Q_OBJECT
public:
    explicit SleepMonitor(QObject *parent = nullptr);
    bool isConnected() const { return m_connected; }

Q_SIGNALS:
    void aboutToSleep();
    void resumed();

private Q_SLOTS:
    void onPrepareForSleep(bool sleeping);

private:
    bool m_connected = false;
};

namespace autostart {
bool isEnabled();
bool setEnabled(bool enabled, QString *error = nullptr);
} // namespace autostart

// One-time move of settings, presets and autostart from the app's previous
// name ("Camera Adjust", ~/.config/LinuxCameraAdjust).
void migrateLegacyConfig();

// Locates the privileged v4l2loopback setup helper.
QString setupHelperPath();

} // namespace app
