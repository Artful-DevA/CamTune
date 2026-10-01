// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "PresetStore.h"
#include "pipeline/Engine.h"

#include <QFileSystemWatcher>
#include <QList>
#include <QMap>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVector>

#include <memory>

namespace app {

class AppSettings;

// The application model: owns the Engine, holds the current settings and
// translates worker-thread callbacks into Qt signals on the GUI thread.
class CameraController : public QObject {
    Q_OBJECT
public:
    static constexpr int kSliderTransitionMs = 90;

    explicit CameraController(AppSettings &settings, QObject *parent = nullptr);
    ~CameraController() override;

    void start();
    void shutdown();

    // Current settings.
    const cam::CameraSelection &camera() const { return m_camera; }
    const cam::CaptureRequest &captureRequest() const { return m_capture; }
    const cam::ColorParams &color() const { return m_color; }
    const cam::FramingParams &framing() const { return m_framing; }
    const cam::EffectParams &effects() const { return m_effects; }
    const cam::OutputConfig &output() const { return m_output; }
    const QMap<quint32, qint64> &hardwareValues() const { return m_hwValues; }

    // Live state.
    cam::CameraState cameraState() const { return m_cameraState; }
    QString cameraMessage() const { return m_cameraMessage; }
    cam::OutputState outputState() const { return m_outputState; }
    QString outputMessage() const { return m_outputMessage; }
    const std::vector<cam::v4l2::CameraMode> &modes() const { return m_modes; }
    const cam::ActiveMode &activeMode() const { return m_activeMode; }
    const std::vector<cam::v4l2::ControlInfo> &controls() const { return m_controls; }
    QList<cam::v4l2::DeviceInfo> cameras() const { return m_cameras; }
    QList<cam::v4l2::DeviceInfo> loopbackDevices() const { return m_loopbacks; }
    void renderSize(int &w, int &h) const;

    cam::FramePtr takePreviewFrame();
    cam::EngineStats stats();

    void setCamera(const cam::CameraSelection &sel);
    void setCaptureRequest(const cam::CaptureRequest &req);
    void setColor(const cam::ColorParams &c);
    void setFraming(const cam::FramingParams &f, int transitionMs = kSliderTransitionMs);
    void setEffects(const cam::EffectParams &e);
    void setOutput(const cam::OutputConfig &o);
    void setVirtualCameraEnabled(bool enabled);
    void setHardwareControl(quint32 id, qint64 value);
    void resetHardwareControls();
    void setPreviewWanted(bool wanted);
    void setSuspended(bool suspended);

    // Presets
    Preset capturePreset(const QString &name, bool framing, bool color, bool camera, bool output,
                         bool effects) const;
    void applyPreset(const Preset &p, int transitionMs);

    void refreshDevices();

Q_SIGNALS:
    void cameraStateChanged();
    void modesChanged();
    void controlsChanged();
    void controlError(const QString &message);
    void outputStateChanged();
    void previewFrameReady();
    void devicesChanged();

    void cameraChanged();
    void captureRequestChanged();
    void colorChanged();
    void framingChanged();
    void effectsChanged();
    void outputChanged();
    void settingsChanged(); // any persisted setting changed

private:
    void prepareEffectImages();
    void updatePlaceholder();
    void scheduleSave();
    void saveNow();

    AppSettings &m_settings;
    std::unique_ptr<cam::Engine> m_engine;

    cam::CameraSelection m_camera;
    cam::CaptureRequest m_capture;
    cam::ColorParams m_color;
    cam::FramingParams m_framing;
    cam::EffectParams m_effects;
    cam::OutputConfig m_output;
    QMap<quint32, qint64> m_hwValues;

    cam::CameraState m_cameraState = cam::CameraState::NoCamera;
    QString m_cameraMessage;
    cam::OutputState m_outputState = cam::OutputState::Disabled;
    QString m_outputMessage;
    int m_renderW = 1280, m_renderH = 720;
    std::vector<cam::v4l2::CameraMode> m_modes;
    cam::ActiveMode m_activeMode;
    std::vector<cam::v4l2::ControlInfo> m_controls;
    QList<cam::v4l2::DeviceInfo> m_cameras;
    QList<cam::v4l2::DeviceInfo> m_loopbacks;

    QFileSystemWatcher m_devWatcher;
    QTimer m_devRescan;
    QTimer m_saveTimer;
    QString m_preparedBackground, m_preparedMask;
    int m_preparedW = 0, m_preparedH = 0;
    std::shared_ptr<const cam::Frame> m_bgFrame;
    std::shared_ptr<const std::vector<uint8_t>> m_maskData;
    int m_maskW = 0, m_maskH = 0;
    cam::FramePtr m_lastPreview;
    QThreadPool m_enumPool;
    QString m_placeholderText;
    int m_placeholderW = 0, m_placeholderH = 0;
};

} // namespace app
