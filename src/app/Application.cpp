// SPDX-License-Identifier: GPL-3.0-or-later
#include "Application.h"

#include "DBusService.h"
#include "GlobalShortcuts.h"
#include "SystemIntegration.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QDBusConnection>
#include <QTimer>

namespace app {

Application::Application(QObject *parent) : QObject(parent)
{
    m_presets.load();
    m_controller = new CameraController(m_settings, this);
    // Only highlight the remembered preset if the picture still matches it.
    m_currentPreset = m_settings.lastPreset();
    if (int i = m_presets.indexOf(m_currentPreset); i < 0) {
        m_currentPreset.clear();
    } else {
        const Preset &p = m_presets.presets().at(i);
        if ((p.hasFraming && p.framing != m_controller->framing()) || (p.hasColor && p.color != m_controller->color()))
            m_currentPreset.clear();
    }

    // The camera is released before suspend and reopened after resume; USB
    // cameras re-enumerate on resume, so give them a moment.
    m_sleep = new SleepMonitor(this);
    connect(m_sleep, &SleepMonitor::aboutToSleep, this, [this] { m_controller->setSuspended(true); });
    connect(m_sleep, &SleepMonitor::resumed, this, [this] {
        QTimer::singleShot(1500, this, [this] { m_controller->setSuspended(false); });
    });

    // A manual change means the picture no longer matches the last preset.
    auto clearPreset = [this] {
        if (m_applyingPreset || m_currentPreset.isEmpty())
            return;
        m_currentPreset.clear();
        m_settings.setLastPreset(QString());
        Q_EMIT currentPresetChanged();
    };
    connect(m_controller, &CameraController::framingChanged, this, clearPreset);
    connect(m_controller, &CameraController::colorChanged, this, clearPreset);
    connect(m_controller, &CameraController::effectsChanged, this, clearPreset);

    m_shortcuts = new GlobalShortcuts(this);
    connect(m_shortcuts, &GlobalShortcuts::activated, this, &Application::onGlobalShortcut);
}

Application::~Application()
{
    delete m_window;
    m_controller->shutdown();
}

bool Application::registerDBus()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return true; // no session bus: run without automation support
    if (!bus.registerService(QString::fromLatin1(kDBusService)))
        return false; // another instance owns the name
    new DBusAdaptor(this);
    bus.registerObject(QString::fromLatin1(kDBusPath), this);
    return true;
}

void Application::start(bool minimized)
{
    m_controller->start();
    m_window = new ui::MainWindow(*this);
    if (!(minimized || m_settings.startMinimized()) || !m_window->hasTray())
        m_window->show();
    else
        m_window->hideToTray();
    if (m_settings.globalShortcuts())
        QTimer::singleShot(1000, this, [this] { m_shortcuts->enable(); });
}

bool Application::applyPreset(const QString &nameOrNumber)
{
    int i = m_presets.find(nameOrNumber.trimmed());
    if (i < 0)
        return false;
    applyPresetIndex(i);
    return true;
}

void Application::applyPresetIndex(int index)
{
    if (index < 0 || index >= m_presets.presets().size())
        return;
    const Preset p = m_presets.presets().at(index);
    m_applyingPreset = true;
    m_controller->applyPreset(p, m_settings.presetTransitionMs());
    m_applyingPreset = false;
    m_currentPreset = p.name;
    m_settings.setLastPreset(p.name);
    Q_EMIT presetApplied(p.name);
    Q_EMIT currentPresetChanged();
}

void Application::setZoom(double zoom)
{
    cam::FramingParams f = m_controller->framing();
    f.zoom = qBound(1.0, zoom, 8.0);
    m_controller->setFraming(f, 250);
}

void Application::adjustZoom(double delta)
{
    setZoom(m_controller->framing().zoom + delta);
}

void Application::setPan(double x, double y)
{
    cam::FramingParams f = m_controller->framing();
    f.panX = qBound(-1.0, x, 1.0);
    f.panY = qBound(-1.0, y, 1.0);
    m_controller->setFraming(f, 250);
}

void Application::resetFraming()
{
    cam::FramingParams f = m_controller->framing();
    cam::FramingParams def;
    // Keep orientation settings, reset the view.
    def.mirror = f.mirror;
    def.flip = f.flip;
    def.rotation = f.rotation;
    def.aspect = f.aspect;
    m_controller->setFraming(def, m_settings.presetTransitionMs());
}

void Application::showWindow()
{
    if (m_window)
        m_window->showAndRaise();
}

void Application::quit()
{
    if (m_window)
        m_window->prepareQuit();
    m_controller->shutdown();
    QApplication::quit();
}

QString Application::statusText() const
{
    const auto &c = *m_controller;
    QString cameraState;
    switch (c.cameraState()) {
    case cam::CameraState::Streaming: cameraState = QStringLiteral("streaming"); break;
    case cam::CameraState::Opening: cameraState = QStringLiteral("opening"); break;
    case cam::CameraState::Waiting: cameraState = QStringLiteral("waiting for camera"); break;
    case cam::CameraState::Busy: cameraState = QStringLiteral("camera busy"); break;
    case cam::CameraState::Error: cameraState = QStringLiteral("error"); break;
    case cam::CameraState::Suspended: cameraState = QStringLiteral("suspended"); break;
    case cam::CameraState::Idle: cameraState = QStringLiteral("idle (camera released)"); break;
    case cam::CameraState::NoCamera: cameraState = QStringLiteral("no camera"); break;
    }
    QString out;
    switch (c.outputState()) {
    case cam::OutputState::Active: out = QStringLiteral("on"); break;
    case cam::OutputState::Disabled: out = QStringLiteral("off"); break;
    case cam::OutputState::NoDevice: out = QStringLiteral("no v4l2loopback device"); break;
    case cam::OutputState::Error: out = QStringLiteral("error"); break;
    }
    int w, h;
    c.renderSize(w, h);
    return QStringLiteral("camera: %1 (%2)\nvirtual camera: %3, %4x%5@%6\npreset: %7\nzoom: %8")
        .arg(QString::fromStdString(c.camera().displayName()), cameraState, out)
        .arg(w)
        .arg(h)
        .arg(c.output().fps)
        .arg(m_currentPreset.isEmpty() ? QStringLiteral("-") : m_currentPreset)
        .arg(c.framing().zoom, 0, 'f', 2);
}

void Application::setGlobalShortcutsEnabled(bool enabled)
{
    m_settings.setGlobalShortcuts(enabled);
    if (enabled)
        m_shortcuts->enable();
    else
        m_shortcuts->disable();
}

void Application::onGlobalShortcut(const QString &id)
{
    if (id.startsWith(QStringLiteral("preset-"))) {
        int n = id.mid(7).toInt();
        int i = m_presets.indexForShortcut(n);
        if (i < 0 && n >= 1 && n <= m_presets.presets().size())
            i = n - 1;
        applyPresetIndex(i);
    } else if (id == QStringLiteral("zoom-in")) {
        adjustZoom(0.1);
    } else if (id == QStringLiteral("zoom-out")) {
        adjustZoom(-0.1);
    } else if (id == QStringLiteral("reset-framing")) {
        resetFraming();
    } else if (id == QStringLiteral("toggle-virtual-camera")) {
        m_controller->setVirtualCameraEnabled(!m_controller->output().enabled);
    }
}

} // namespace app
