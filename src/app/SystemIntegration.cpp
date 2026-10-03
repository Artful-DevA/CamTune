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
           QStringLiteral("/autostart/io.github.CamTune.desktop");
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
                           "Name=CamTune\n"
                           "Comment=Webcam controls and virtual camera\n"
                           "Exec=\"%1\" --minimized\n"
                           "Icon=io.github.CamTune\n"
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

void migrateLegacyConfig()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString oldDir = base + QStringLiteral("/LinuxCameraAdjust");
    const QString newDir = base + QStringLiteral("/CamTune");
    if (QFileInfo::exists(oldDir) && !QFileInfo::exists(newDir)) {
        QDir().mkpath(newDir);
        QFile::copy(oldDir + QStringLiteral("/camadjust.conf"), newDir + QStringLiteral("/camtune.conf"));
        QFile::copy(oldDir + QStringLiteral("/presets.json"), newDir + QStringLiteral("/presets.json"));
    }
    const QString oldAutostart = base + QStringLiteral("/autostart/io.github.LinuxCameraAdjust.desktop");
    if (QFileInfo::exists(oldAutostart)) {
        QFile::remove(oldAutostart);
        autostart::setEnabled(true);
    }
}

QString setupHelperPath()
{
    const QString name = QStringLiteral("camtune-setup-v4l2loopback");
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QStringLiteral(CAMTUNE_LIBEXECDIR "/") + name,
        QStringLiteral("/usr/libexec/") + name,
        QStringLiteral("/usr/lib/camtune/") + name,
        // Bundled next to the binary (AppImage: usr/bin + usr/libexec).
        appDir + QStringLiteral("/../libexec/") + name,
        // Running from a build directory inside the source tree.
        appDir + QStringLiteral("/../scripts/") + name,
        appDir + QStringLiteral("/scripts/") + name,
    };
    QString found;
    for (const QString &c : candidates) {
        if (QFileInfo(c).isExecutable()) {
            found = QFileInfo(c).canonicalFilePath();
            break;
        }
    }
    if (found.isEmpty())
        return {};

    // An AppImage is a FUSE mount that root cannot read, so pkexec could not
    // start the helper from there. Hand it a private copy instead.
    const QString appImageDir = qEnvironmentVariable("APPDIR");
    if (!appImageDir.isEmpty() && found.startsWith(QDir(appImageDir).canonicalPath() + QLatin1Char('/'))) {
        QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (runtime.isEmpty())
            runtime = QDir::tempPath();
        const QString copy = runtime + QLatin1Char('/') + name;
        QFile::remove(copy);
        if (!QFile::copy(found, copy))
            return {};
        QFile::setPermissions(copy, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        return copy;
    }
    return found;
}

} // namespace app
