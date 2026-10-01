// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Params.h"
#include "pipeline/Types.h"

#include <QByteArray>
#include <QMap>
#include <QSettings>
#include <QString>

namespace app {

// Persistent application state (~/.config/LinuxCameraAdjust/camadjust.conf).
class AppSettings {
public:
    AppSettings();

    struct State {
        cam::CameraSelection camera;
        cam::CaptureRequest capture;
        cam::ColorParams color;
        cam::FramingParams framing;
        cam::EffectParams effects;
        cam::OutputConfig output;
        bool hasCamera = false;
    };
    State loadState();
    void saveState(const State &s);

    QMap<quint32, qint64> cameraControls(const QString &cameraKey);
    void setCameraControls(const QString &cameraKey, const QMap<quint32, qint64> &values);

    bool startMinimized() const;
    void setStartMinimized(bool v);
    bool closeToTray() const;
    void setCloseToTray(bool v);
    bool globalShortcuts() const;
    void setGlobalShortcuts(bool v);
    int presetTransitionMs() const;
    void setPresetTransitionMs(int ms);
    QString lastPreset() const;
    void setLastPreset(const QString &name);
    bool previewPaused() const;
    void setPreviewPaused(bool v);
    bool firstRun() const;
    // Whether the user (or first-run auto-selection) has chosen a camera before.
    bool hasCameraChoice() const;
    void setHasCameraChoice(bool v);

    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &g);
    QByteArray splitterState() const;
    void setSplitterState(const QByteArray &s);

    void sync() { m_settings.sync(); }

private:
    mutable QSettings m_settings;
};

QString cameraKey(const cam::CameraSelection &s);

} // namespace app
