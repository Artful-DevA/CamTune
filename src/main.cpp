// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/Application.h"
#include "app/DBusService.h"
#include "app/PresetStore.h"
#include "app/SystemIntegration.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QTimer>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <functional>

namespace {

struct Commands {
    bool minimized = false;
    bool show = false;
    bool listPresets = false;
    bool status = false;
    bool quit = false;
    bool resetFraming = false;
    bool zoomIn = false, zoomOut = false;
    QString preset;
    QString zoom;
    QString pan;
    QString virtualCamera;

    bool remote() const
    {
        return show || listPresets || status || quit || resetFraming || zoomIn || zoomOut || !preset.isEmpty() ||
               !zoom.isEmpty() || !pan.isEmpty() || !virtualCamera.isEmpty();
    }
};

void out(const QString &s) { std::fprintf(stdout, "%s\n", s.toLocal8Bit().constData()); }
void err(const QString &s) { std::fprintf(stderr, "%s\n", s.toLocal8Bit().constData()); }

bool parsePan(const QString &s, double &x, double &y)
{
    const QStringList parts = s.split(QLatin1Char(','));
    if (parts.size() != 2)
        return false;
    bool ok1 = false, ok2 = false;
    x = parts[0].toDouble(&ok1);
    y = parts[1].toDouble(&ok2);
    return ok1 && ok2;
}

// Sends the commands to a running instance. Returns the process exit code.
int runRemote(const Commands &c)
{
    QDBusInterface iface(QString::fromLatin1(app::kDBusService), QString::fromLatin1(app::kDBusPath),
                         QString::fromLatin1(app::kDBusInterface));
    if (!iface.isValid()) {
        err(QStringLiteral("camtune is running but its D-Bus interface is not reachable."));
        return 1;
    }
    int rc = 0;
    auto check = [&](const QDBusMessage &m) {
        if (m.type() == QDBusMessage::ErrorMessage) {
            err(m.errorMessage());
            rc = 1;
        }
        return m;
    };
    if (c.listPresets) {
        QDBusReply<QStringList> r = iface.call(QStringLiteral("ListPresets"));
        if (r.isValid())
            for (const QString &s : r.value())
                out(s);
        else
            rc = 1;
    }
    if (!c.preset.isEmpty()) {
        QDBusReply<bool> r = iface.call(QStringLiteral("ApplyPreset"), c.preset);
        if (!r.isValid() || !r.value()) {
            err(QStringLiteral("No preset named or numbered “%1”.").arg(c.preset));
            rc = 2;
        }
    }
    if (!c.zoom.isEmpty()) {
        bool ok = false;
        double z = c.zoom.toDouble(&ok);
        if (ok)
            check(iface.call(QStringLiteral("SetZoom"), z));
        else {
            err(QStringLiteral("Invalid zoom value."));
            rc = 2;
        }
    }
    if (c.zoomIn)
        check(iface.call(QStringLiteral("AdjustZoom"), 0.1));
    if (c.zoomOut)
        check(iface.call(QStringLiteral("AdjustZoom"), -0.1));
    if (!c.pan.isEmpty()) {
        double x, y;
        if (parsePan(c.pan, x, y))
            check(iface.call(QStringLiteral("SetPan"), x, y));
        else {
            err(QStringLiteral("Pan must be given as X,Y with values from -1 to 1."));
            rc = 2;
        }
    }
    if (c.resetFraming)
        check(iface.call(QStringLiteral("ResetFraming")));
    if (!c.virtualCamera.isEmpty()) {
        const QString v = c.virtualCamera.toLower();
        if (v == QStringLiteral("toggle"))
            check(iface.call(QStringLiteral("ToggleVirtualCamera")));
        else if (v == QStringLiteral("on") || v == QStringLiteral("off"))
            check(iface.call(QStringLiteral("SetVirtualCamera"), v == QStringLiteral("on")));
        else {
            err(QStringLiteral("--virtual-camera expects on, off or toggle."));
            rc = 2;
        }
    }
    if (c.show)
        check(iface.call(QStringLiteral("ShowWindow")));
    if (c.status) {
        QDBusReply<QString> r = iface.call(QStringLiteral("Status"));
        if (r.isValid())
            out(r.value());
        else
            rc = 1;
    }
    if (c.quit)
        check(iface.call(QStringLiteral("Quit")));
    return rc;
}

bool instanceRunning()
{
    auto bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.interface()->isServiceRegistered(QString::fromLatin1(app::kDBusService));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("CamTune"));
    QCoreApplication::setApplicationName(QStringLiteral("camtune"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CAMTUNE_VERSION));

    QStringList args;
    for (int i = 0; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Webcam controls and low-latency virtual camera for video calls.\n"
                       "Without options, starts the application (or shows the running one).\n"
                       "Options that change settings are sent to the running instance."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption minimizedOpt({QStringLiteral("minimized"), QStringLiteral("hidden")},
                                    QStringLiteral("Start hidden in the system tray."));
    QCommandLineOption presetOpt({QStringLiteral("p"), QStringLiteral("preset")},
                                 QStringLiteral("Apply a preset by name or number."), QStringLiteral("name|N"));
    QCommandLineOption listOpt(QStringLiteral("list-presets"), QStringLiteral("List presets."));
    QCommandLineOption zoomOpt(QStringLiteral("zoom"), QStringLiteral("Set digital zoom (1 to 8)."),
                               QStringLiteral("factor"));
    QCommandLineOption zoomInOpt(QStringLiteral("zoom-in"), QStringLiteral("Zoom in a step."));
    QCommandLineOption zoomOutOpt(QStringLiteral("zoom-out"), QStringLiteral("Zoom out a step."));
    QCommandLineOption panOpt(QStringLiteral("pan"), QStringLiteral("Set pan position, each -1 to 1."),
                              QStringLiteral("x,y"));
    QCommandLineOption resetOpt(QStringLiteral("reset-framing"), QStringLiteral("Reset zoom and pan."));
    QCommandLineOption vcamOpt(QStringLiteral("virtual-camera"), QStringLiteral("Turn the virtual camera on/off."),
                               QStringLiteral("on|off|toggle"));
    QCommandLineOption showOpt(QStringLiteral("show"), QStringLiteral("Show the window of the running instance."));
    QCommandLineOption statusOpt(QStringLiteral("status"), QStringLiteral("Print the running instance's status."));
    QCommandLineOption quitOpt(QStringLiteral("quit"), QStringLiteral("Quit the running instance."));
    QCommandLineOption guiStartOpt(QStringLiteral("gui-start"), QStringLiteral("internal"));
    guiStartOpt.setFlags(QCommandLineOption::HiddenFromHelp);
    for (auto *o : {&guiStartOpt, &minimizedOpt, &presetOpt, &listOpt, &zoomOpt, &zoomInOpt, &zoomOutOpt, &panOpt, &resetOpt,
                    &vcamOpt, &showOpt, &statusOpt, &quitOpt})
        parser.addOption(*o);

    if (!parser.parse(args)) {
        err(parser.errorText());
        return 2;
    }
    if (parser.isSet(QStringLiteral("help")) || parser.isSet(QStringLiteral("version"))) {
        QCoreApplication core(argc, argv);
        parser.process(core); // prints and exits
        return 0;
    }

    Commands cmd;
    cmd.minimized = parser.isSet(minimizedOpt);
    cmd.show = parser.isSet(showOpt);
    cmd.listPresets = parser.isSet(listOpt);
    cmd.status = parser.isSet(statusOpt);
    cmd.quit = parser.isSet(quitOpt);
    cmd.resetFraming = parser.isSet(resetOpt);
    cmd.zoomIn = parser.isSet(zoomInOpt);
    cmd.zoomOut = parser.isSet(zoomOutOpt);
    cmd.preset = parser.value(presetOpt);
    cmd.zoom = parser.value(zoomOpt);
    cmd.pan = parser.value(panOpt);
    cmd.virtualCamera = parser.value(vcamOpt);

    // Command-line control of a running instance needs no GUI (and no display).
    // Each process creates exactly one application object: Qt's D-Bus support
    // does not survive a QCoreApplication being replaced by a QApplication.
    if (cmd.remote() && !parser.isSet(guiStartOpt)) {
        QCoreApplication core(argc, argv);
        if (instanceRunning())
            return runRemote(cmd);
        if (cmd.status || cmd.quit) {
            err(QStringLiteral("camtune is not running."));
            return 1;
        }
        if (cmd.listPresets) {
            app::PresetStore store;
            store.load();
            for (const auto &p : store.presets())
                out(p.shortcut ? QStringLiteral("%1\tCtrl+%2").arg(p.name).arg(p.shortcut) : p.name);
            if (cmd.preset.isEmpty() && cmd.zoom.isEmpty() && cmd.pan.isEmpty() && cmd.virtualCamera.isEmpty() &&
                !cmd.zoomIn && !cmd.zoomOut && !cmd.resetFraming && !cmd.show)
                return 0;
        }
        // Not running: start the application in a fresh process image, which
        // then applies the requested commands.
        std::vector<char *> argvNew(argv, argv + argc);
        static char flag[] = "--gui-start";
        argvNew.insert(argvNew.begin() + 1, flag);
        argvNew.push_back(nullptr);
        execv("/proc/self/exe", argvNew.data());
        err(QStringLiteral("Could not start camtune: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return 1;
    }

    QApplication qapp(argc, argv);
    if (!parser.isSet(guiStartOpt) && instanceRunning()) {
        // Already running: bring the existing window forward instead.
        if (cmd.minimized)
            return 0;
        Commands c;
        c.show = true;
        return runRemote(c);
    }
    QApplication::setDesktopFileName(QStringLiteral("io.github.CamTune"));
    QApplication::setApplicationDisplayName(QStringLiteral("CamTune"));
    ui::theme::apply(qapp);
    QApplication::setQuitOnLastWindowClosed(false);

    app::migrateLegacyConfig();
    app::Application application;
    if (!application.registerDBus()) {
        // Lost a start-up race with another instance: hand over to it.
        Commands c = cmd;
        if (!c.remote())
            c.show = true;
        return runRemote(c);
    }
    application.start(cmd.minimized);

    // Commands given while starting the app are applied once it is up.
    QTimer::singleShot(0, &application, [&application, cmd] {
        if (!cmd.preset.isEmpty() && !application.applyPreset(cmd.preset))
            err(QStringLiteral("No preset named or numbered “%1”.").arg(cmd.preset));
        bool ok = false;
        double z = cmd.zoom.toDouble(&ok);
        if (ok)
            application.setZoom(z);
        double x, y;
        if (!cmd.pan.isEmpty() && parsePan(cmd.pan, x, y))
            application.setPan(x, y);
        if (cmd.resetFraming)
            application.resetFraming();
        if (cmd.zoomIn)
            application.adjustZoom(0.1);
        if (cmd.zoomOut)
            application.adjustZoom(-0.1);
        const QString v = cmd.virtualCamera.toLower();
        if (v == QStringLiteral("on") || v == QStringLiteral("off"))
            application.controller().setVirtualCameraEnabled(v == QStringLiteral("on"));
        else if (v == QStringLiteral("toggle"))
            application.controller().setVirtualCameraEnabled(!application.controller().output().enabled);
    });
    return qapp.exec();
}
