// SPDX-License-Identifier: GPL-3.0-or-later
#include "CameraController.h"

#include "AppSettings.h"
#include "ImageUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QThreadPool>

namespace app {

namespace {

std::map<uint32_t, int64_t> toStdMap(const QMap<quint32, qint64> &m)
{
    std::map<uint32_t, int64_t> out;
    for (auto it = m.begin(); it != m.end(); ++it)
        out[it.key()] = it.value();
    return out;
}

QImage loadImage(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    // Refuse absurd sizes rather than allocating gigabytes.
    if (reader.size().isValid() && qint64(reader.size().width()) * reader.size().height() > 64LL * 1024 * 1024)
        return {};
    return reader.read();
}

} // namespace

CameraController::CameraController(AppSettings &settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    AppSettings::State st = m_settings.loadState();
    m_camera = st.camera;
    m_capture = st.capture;
    m_color = st.color;
    m_framing = st.framing;
    m_effects = st.effects;
    m_output = st.output;
    if (!m_camera.empty())
        m_hwValues = m_settings.cameraControls(cameraKey(m_camera));

    // Worker-thread callbacks are marshalled onto the GUI thread. If the
    // controller is gone by then, Qt drops the queued call.
    cam::EngineCallbacks cb;
    cb.cameraState = [this](cam::CameraState s, const std::string &m) {
        const QString msg = QString::fromStdString(m);
        QMetaObject::invokeMethod(this, [this, s, msg] {
            m_cameraState = s;
            m_cameraMessage = msg;
            updatePlaceholder();
            Q_EMIT cameraStateChanged();
        }, Qt::QueuedConnection);
    };
    cb.cameraModes = [this](const std::vector<cam::v4l2::CameraMode> &modes, const cam::ActiveMode &active) {
        QMetaObject::invokeMethod(this, [this, modes, active] {
            m_modes = modes;
            m_activeMode = active;
            Q_EMIT modesChanged();
        }, Qt::QueuedConnection);
    };
    cb.controls = [this](const std::vector<cam::v4l2::ControlInfo> &controls) {
        QMetaObject::invokeMethod(this, [this, controls] {
            m_controls = controls;
            Q_EMIT controlsChanged();
        }, Qt::QueuedConnection);
    };
    cb.controlError = [this](const std::string &m) {
        const QString msg = QString::fromStdString(m);
        QMetaObject::invokeMethod(this, [this, msg] { Q_EMIT controlError(msg); }, Qt::QueuedConnection);
    };
    cb.outputState = [this](cam::OutputState s, const std::string &m, int w, int h) {
        const QString msg = QString::fromStdString(m);
        QMetaObject::invokeMethod(this, [this, s, msg, w, h] {
            m_outputState = s;
            m_outputMessage = msg;
            m_renderW = w;
            m_renderH = h;
            prepareEffectImages();
            updatePlaceholder();
            Q_EMIT outputStateChanged();
        }, Qt::QueuedConnection);
    };
    cb.previewReady = [this] {
        QMetaObject::invokeMethod(this, [this] {
            m_lastPreview = m_engine ? m_engine->takePreviewFrame() : nullptr;
            if (m_lastPreview)
                Q_EMIT previewFrameReady();
        }, Qt::QueuedConnection);
    };
    m_engine = std::make_unique<cam::Engine>(std::move(cb));

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);
    connect(&m_saveTimer, &QTimer::timeout, this, &CameraController::saveNow);

    // Hot-plug detection without libudev: device nodes appear/disappear in /dev.
    m_devRescan.setSingleShot(true);
    m_devRescan.setInterval(400);
    connect(&m_devRescan, &QTimer::timeout, this, &CameraController::refreshDevices);
    m_devWatcher.addPath(QStringLiteral("/dev"));
    if (QFileInfo::exists(QStringLiteral("/dev/v4l/by-id")))
        m_devWatcher.addPath(QStringLiteral("/dev/v4l/by-id"));
    connect(&m_devWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        if (QFileInfo::exists(QStringLiteral("/dev/v4l/by-id")) &&
            !m_devWatcher.directories().contains(QStringLiteral("/dev/v4l/by-id")))
            m_devWatcher.addPath(QStringLiteral("/dev/v4l/by-id"));
        m_devRescan.start();
    });
}

CameraController::~CameraController() { shutdown(); }

void CameraController::start()
{
    m_engine->selectCamera(m_camera);
    m_engine->setDesiredControls(toStdMap(m_hwValues));
    m_engine->setCaptureRequest(m_capture);
    m_engine->setColor(m_color);
    m_engine->setFraming(m_framing, 0);
    prepareEffectImages();
    m_engine->setEffects(m_effects);
    m_engine->setOutput(m_output);
    m_engine->start();
    refreshDevices();
}

void CameraController::shutdown()
{
    if (!m_engine)
        return;
    m_saveTimer.stop();
    saveNow();
    m_enumPool.waitForDone();
    m_engine->stop();
}

void CameraController::refreshDevices()
{
    // Opening device nodes can take a while for sleeping USB cameras; keep it
    // off the GUI thread.
    m_enumPool.start([this] {
        auto devices = cam::v4l2::enumerateDevices();
        QMetaObject::invokeMethod(this, [this, devices] {
            QList<cam::v4l2::DeviceInfo> cams, loops;
            for (const auto &d : devices) {
                if (d.isCapture)
                    cams.append(d);
                else if (d.isLoopback)
                    loops.append(d);
            }
            auto key = [](const QList<cam::v4l2::DeviceInfo> &l) {
                QStringList s;
                for (const auto &d : l)
                    s << QString::fromStdString(d.path + "|" + d.card + "|" + d.busInfo);
                return s.join(QLatin1Char(';'));
            };
            const bool changed = key(cams) != key(m_cameras) || key(loops) != key(m_loopbacks);
            m_cameras = cams;
            m_loopbacks = loops;
            // First start: pick the first camera automatically.
            if (m_camera.empty() && !m_settings.hasCameraChoice() && !cams.isEmpty()) {
                cam::CameraSelection sel;
                sel.byIdPath = cams.first().byIdPath;
                sel.card = cams.first().card;
                sel.busInfo = cams.first().busInfo;
                sel.path = cams.first().path;
                setCamera(sel);
            }
            if (changed)
                Q_EMIT devicesChanged();
        }, Qt::QueuedConnection);
    });
}

void CameraController::renderSize(int &w, int &h) const
{
    if (m_outputState == cam::OutputState::Active) {
        w = m_renderW;
        h = m_renderH;
    } else {
        w = m_output.width;
        h = m_output.height;
    }
}

cam::FramePtr CameraController::takePreviewFrame() { return m_lastPreview; }

cam::EngineStats CameraController::stats() { return m_engine->stats(); }

void CameraController::setCamera(const cam::CameraSelection &sel)
{
    if (sel == m_camera && !m_camera.empty())
        return;
    // Remember the outgoing camera's hardware settings.
    if (!m_camera.empty())
        m_settings.setCameraControls(cameraKey(m_camera), m_hwValues);
    m_camera = sel;
    m_settings.setHasCameraChoice(true);
    m_hwValues = sel.empty() ? QMap<quint32, qint64>() : m_settings.cameraControls(cameraKey(sel));
    m_controls.clear();
    m_modes.clear();
    m_engine->selectCamera(sel);
    m_engine->setDesiredControls(toStdMap(m_hwValues));
    Q_EMIT controlsChanged();
    Q_EMIT modesChanged();
    Q_EMIT cameraChanged();
    scheduleSave();
}

void CameraController::setCaptureRequest(const cam::CaptureRequest &req)
{
    if (req == m_capture)
        return;
    m_capture = req;
    m_engine->setCaptureRequest(req);
    Q_EMIT captureRequestChanged();
    scheduleSave();
}

void CameraController::setColor(const cam::ColorParams &c)
{
    if (c == m_color)
        return;
    m_color = c;
    m_engine->setColor(c);
    Q_EMIT colorChanged();
    scheduleSave();
}

void CameraController::setFraming(const cam::FramingParams &f, int transitionMs)
{
    if (f == m_framing)
        return;
    m_framing = f;
    m_engine->setFraming(f, transitionMs);
    Q_EMIT framingChanged();
    scheduleSave();
}

void CameraController::setEffects(const cam::EffectParams &e)
{
    m_effects = e;
    prepareEffectImages();
    m_engine->setEffects(m_effects);
    Q_EMIT effectsChanged();
    scheduleSave();
}

void CameraController::setOutput(const cam::OutputConfig &o)
{
    cam::OutputConfig c = o;
    c.width &= ~1;
    c.height &= ~1;
    m_output = c;
    m_engine->setOutput(c);
    prepareEffectImages();
    Q_EMIT outputChanged();
    scheduleSave();
}

void CameraController::setVirtualCameraEnabled(bool enabled)
{
    if (m_output.enabled == enabled)
        return;
    cam::OutputConfig c = m_output;
    c.enabled = enabled;
    setOutput(c);
}

void CameraController::setHardwareControl(quint32 id, qint64 value)
{
    m_hwValues[id] = value;
    m_engine->setControl(id, value);
    scheduleSave();
}

void CameraController::resetHardwareControls()
{
    m_hwValues.clear();
    m_engine->resetControls();
    scheduleSave();
}

void CameraController::setPreviewWanted(bool wanted) { m_engine->setPreviewWanted(wanted); }

void CameraController::setSuspended(bool suspended) { m_engine->setSuspended(suspended); }

Preset CameraController::capturePreset(const QString &name, bool framing, bool color, bool camera, bool output,
                                       bool effects) const
{
    Preset p;
    p.name = name;
    p.hasFraming = framing;
    p.framing = m_framing;
    p.hasColor = color;
    p.color = m_color;
    p.hasCamera = camera;
    // Store complete hardware state so the preset is reproducible.
    for (const auto &c : m_controls)
        if (!c.readOnly && c.type != cam::v4l2::ControlInfo::Type::Button)
            p.controls.insert(c.id, c.value);
    for (auto it = m_hwValues.begin(); it != m_hwValues.end(); ++it)
        p.controls.insert(it.key(), it.value());
    p.hasOutput = output;
    p.outputWidth = m_output.width;
    p.outputHeight = m_output.height;
    p.outputFps = m_output.fps;
    p.hasEffects = effects;
    p.effects = m_effects;
    p.effects.backgroundImage.reset();
    p.effects.maskImage.reset();
    return p;
}

void CameraController::applyPreset(const Preset &p, int transitionMs)
{
    if (p.hasFraming)
        setFraming(p.framing, transitionMs);
    if (p.hasColor)
        setColor(p.color);
    if (p.hasEffects)
        setEffects(p.effects);
    if (p.hasCamera) {
        for (auto it = p.controls.begin(); it != p.controls.end(); ++it)
            m_hwValues[it.key()] = it.value();
        m_engine->setDesiredControls(toStdMap(m_hwValues));
        scheduleSave();
    }
    if (p.hasOutput &&
        (p.outputWidth != m_output.width || p.outputHeight != m_output.height || p.outputFps != m_output.fps)) {
        cam::OutputConfig c = m_output;
        c.width = p.outputWidth;
        c.height = p.outputHeight;
        c.fps = p.outputFps;
        setOutput(c);
    }
}

void CameraController::prepareEffectImages()
{
    int w, h;
    renderSize(w, h);
    const QString bg = QString::fromStdString(m_effects.backgroundImagePath);
    const QString mask = QString::fromStdString(m_effects.maskImagePath);
    bool changed = false;

    const bool needBg = m_effects.fill == cam::BackgroundFill::Image && !bg.isEmpty();
    if (!needBg) {
        if (m_bgFrame) {
            m_bgFrame.reset();
            changed = true;
        }
        m_preparedBackground.clear();
    } else if (bg != m_preparedBackground || w != m_preparedW || h != m_preparedH || !m_bgFrame) {
        m_bgFrame = imageToI420(loadImage(bg), w, h);
        m_preparedBackground = bg;
        changed = true;
        if (!m_bgFrame)
            Q_EMIT controlError(tr("Could not load background image “%1”.").arg(bg));
    }

    const bool needMask = m_effects.mode == cam::EffectMode::MaskImage && !mask.isEmpty();
    if (!needMask) {
        if (m_maskData) {
            m_maskData.reset();
            changed = true;
        }
        m_preparedMask.clear();
    } else if (mask != m_preparedMask || !m_maskData) {
        int mw = 0, mh = 0;
        m_maskData = imageToMask(loadImage(mask), mw, mh);
        m_maskW = mw;
        m_maskH = mh;
        m_preparedMask = mask;
        changed = true;
        if (!m_maskData)
            Q_EMIT controlError(tr("Could not load mask image “%1”.").arg(mask));
    }
    m_preparedW = w;
    m_preparedH = h;

    m_effects.backgroundImage = m_bgFrame;
    m_effects.maskImage = m_maskData;
    m_effects.maskWidth = m_maskData ? m_maskW : 0;
    m_effects.maskHeight = m_maskData ? m_maskH : 0;
    if (changed)
        m_engine->setEffects(m_effects);
}

void CameraController::updatePlaceholder()
{
    QString text;
    switch (m_cameraState) {
    case cam::CameraState::NoCamera:
        text = tr("No camera selected");
        break;
    case cam::CameraState::Waiting:
        text = tr("Camera disconnected — reconnecting automatically");
        break;
    case cam::CameraState::Busy:
        text = tr("The camera is in use by another application");
        break;
    case cam::CameraState::Suspended:
        text = tr("Camera paused");
        break;
    case cam::CameraState::Error:
        text = tr("Camera unavailable — retrying");
        break;
    default:
        text = tr("Starting camera…");
        break;
    }
    int w, h;
    renderSize(w, h);
    if (text == m_placeholderText && w == m_placeholderW && h == m_placeholderH)
        return;
    m_placeholderText = text;
    m_placeholderW = w;
    m_placeholderH = h;
    m_engine->setPlaceholder(renderPlaceholder(w, h, text));
}

void CameraController::scheduleSave()
{
    m_saveTimer.start();
    Q_EMIT settingsChanged();
}

void CameraController::saveNow()
{
    AppSettings::State st;
    st.camera = m_camera;
    st.capture = m_capture;
    st.color = m_color;
    st.framing = m_framing;
    st.effects = m_effects;
    st.output = m_output;
    m_settings.saveState(st);
    if (!m_camera.empty())
        m_settings.setCameraControls(cameraKey(m_camera), m_hwValues);
    m_settings.sync();
}

} // namespace app
