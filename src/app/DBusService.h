// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QStringList>

namespace app {

class Application;

inline constexpr const char *kDBusService = "io.github.CamTune";
inline constexpr const char *kDBusPath = "/io/github/CamTune";
inline constexpr const char *kDBusInterface = "io.github.CamTune1";

// Session-bus automation interface. Also used by the command line to talk
// to an already running instance, e.g. `camtune --preset 2`.
class DBusAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.CamTune1")
public:
    explicit DBusAdaptor(Application *app);

public Q_SLOTS:
    bool ApplyPreset(const QString &nameOrNumber);
    QStringList ListPresets();
    void SetZoom(double zoom);
    void AdjustZoom(double delta);
    void SetPan(double x, double y);
    void ResetFraming();
    void SetVirtualCamera(bool enabled);
    void ToggleVirtualCamera();
    void ShowWindow();
    void Quit();
    QString Status();

private:
    Application *m_app;
};

} // namespace app
