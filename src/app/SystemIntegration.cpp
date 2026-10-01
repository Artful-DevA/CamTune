// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemIntegration.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace app {

SleepMonitor::SleepMonitor(QObject *parent) : QObject(parent)
{
    m_connected = QDBusConnection::systemBus().connect(
        QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"), this,
        SLOT(onPrepareForSleep(bool)));
}

void SleepMonitor::onPrepareForSleep(bool sleeping)
{
    if (sleeping)
        Q_EMIT aboutToSleep();
    else
        Q_EMIT resumed();
}

namespace autostart {

static QString desktopFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/autostart/io.github.LinuxCameraAdjust.desktop");
}

bool isEnabled()
{
    QFile f(desktopFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    // Respect a user who disabled the entry via their desktop's settings.
    const QByteArray content = f.readAll();
    return !content.contains("Hidden=true") && !content.contains("X-GNOME-Autostart-enabled=false");
}

bool setEnabled(bool enabled, QString *error)
{
    const QString path = desktopFilePath();
    if (!enabled) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            if (error)
                *error = QObject::tr("Could not remove %1").arg(path);
            return false;
        }
        return true;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QString exec = QCoreApplication::applicationFilePath();
    // Prefer the PATH name when installed system-wide so updates keep working.
    if (exec.startsWith(QStringLiteral("/usr/")))
        exec = QFileInfo(exec).fileName();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("Could not write %1").arg(path);
        return false;
    }
    f.write(QStringLiteral("[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=Camera Adjust\n"
                           "Comment=Webcam controls and virtual camera\n"
                           "Exec=\"%1\" --minimized\n"
                           "Icon=io.github.LinuxCameraAdjust\n"
                           "Terminal=false\n"
                           "X-GNOME-Autostart-enabled=true\n"
                           "X-GNOME-Autostart-Delay=2\n")
                .arg(exec)
                .toUtf8());
    if (!f.commit()) {
        if (error)
            *error = QObject::tr("Could not write %1").arg(path);
        return false;
    }
    return true;
}

} // namespace autostart

QString setupHelperPath()
{
    const QString name = QStringLiteral("camadjust-setup-v4l2loopback");
    const QStringList candidates = {
        QStringLiteral(CAMADJUST_LIBEXECDIR "/") + name,
        QStringLiteral("/usr/libexec/") + name,
        QStringLiteral("/usr/lib/camadjust/") + name,
        // Running from a build directory inside the source tree.
        QCoreApplication::applicationDirPath() + QStringLiteral("/../scripts/") + name,
        QCoreApplication::applicationDirPath() + QStringLiteral("/scripts/") + name,
    };
    for (const QString &c : candidates)
        if (QFileInfo(c).isExecutable())
            return QFileInfo(c).canonicalFilePath();
    return {};
}

} // namespace app
