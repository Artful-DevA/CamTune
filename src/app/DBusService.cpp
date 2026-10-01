// SPDX-License-Identifier: GPL-3.0-or-later
#include "DBusService.h"

#include "Application.h"

#include <QTimer>

namespace app {

DBusAdaptor::DBusAdaptor(Application *app) : QDBusAbstractAdaptor(app), m_app(app) {}

bool DBusAdaptor::ApplyPreset(const QString &nameOrNumber) { return m_app->applyPreset(nameOrNumber); }

QStringList DBusAdaptor::ListPresets()
{
    QStringList out;
    for (const auto &p : m_app->presets().presets())
        out << (p.shortcut ? QStringLiteral("%1\tCtrl+%2").arg(p.name).arg(p.shortcut) : p.name);
    return out;
}

void DBusAdaptor::SetZoom(double zoom) { m_app->setZoom(zoom); }
void DBusAdaptor::AdjustZoom(double delta) { m_app->adjustZoom(delta); }
void DBusAdaptor::SetPan(double x, double y) { m_app->setPan(x, y); }
void DBusAdaptor::ResetFraming() { m_app->resetFraming(); }
void DBusAdaptor::SetVirtualCamera(bool enabled) { m_app->controller().setVirtualCameraEnabled(enabled); }

void DBusAdaptor::ToggleVirtualCamera()
{
    m_app->controller().setVirtualCameraEnabled(!m_app->controller().output().enabled);
}

void DBusAdaptor::ShowWindow() { m_app->showWindow(); }

void DBusAdaptor::Quit()
{
    // Reply to the caller before tearing down.
    QTimer::singleShot(0, m_app, [app = m_app] { app->quit(); });
}

QString DBusAdaptor::Status() { return m_app->statusText(); }

} // namespace app
