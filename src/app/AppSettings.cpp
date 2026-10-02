// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppSettings.h"

#include "Serialization.h"

#include <QCryptographicHash>
#include <QJsonDocument>

namespace app {

namespace {

QJsonObject readJson(QSettings &s, const QString &key)
{
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(s.value(key).toByteArray(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object();
}

void writeJson(QSettings &s, const QString &key, const QJsonObject &o)
{
    s.setValue(key, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

} // namespace

QString cameraKey(const cam::CameraSelection &s)
{
    // A stable, settings-key-safe identifier for a camera model/unit.
    const QString id = !s.byIdPath.empty() ? QString::fromStdString(s.byIdPath)
                                           : QString::fromStdString(s.card + "|" + s.busInfo);
    return QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

AppSettings::AppSettings() : m_settings(QStringLiteral("CamTune"), QStringLiteral("camtune")) {}

AppSettings::State AppSettings::loadState()
{
    State s;
    s.hasCamera = m_settings.contains(QStringLiteral("state/camera"));
    s.camera = cameraFromJson(readJson(m_settings, QStringLiteral("state/camera")));
    s.capture = captureFromJson(readJson(m_settings, QStringLiteral("state/capture")));
    s.color = colorFromJson(readJson(m_settings, QStringLiteral("state/color")));
    s.framing = framingFromJson(readJson(m_settings, QStringLiteral("state/framing")));
    s.effects = effectsFromJson(readJson(m_settings, QStringLiteral("state/effects")));
    s.output = outputFromJson(readJson(m_settings, QStringLiteral("state/output")));
    return s;
}

void AppSettings::saveState(const State &s)
{
    writeJson(m_settings, QStringLiteral("state/camera"), toJson(s.camera));
    writeJson(m_settings, QStringLiteral("state/capture"), toJson(s.capture));
    writeJson(m_settings, QStringLiteral("state/color"), toJson(s.color));
    writeJson(m_settings, QStringLiteral("state/framing"), toJson(s.framing));
    writeJson(m_settings, QStringLiteral("state/effects"), toJson(s.effects));
    writeJson(m_settings, QStringLiteral("state/output"), toJson(s.output));
    m_settings.setValue(QStringLiteral("prefs/initialized"), true);
}

QMap<quint32, qint64> AppSettings::cameraControls(const QString &key)
{
    return controlsFromJson(readJson(m_settings, QStringLiteral("controls/") + key));
}

void AppSettings::setCameraControls(const QString &key, const QMap<quint32, qint64> &values)
{
    writeJson(m_settings, QStringLiteral("controls/") + key, controlsToJson(values));
}

bool AppSettings::startMinimized() const { return m_settings.value(QStringLiteral("prefs/startMinimized"), false).toBool(); }
void AppSettings::setStartMinimized(bool v) { m_settings.setValue(QStringLiteral("prefs/startMinimized"), v); }
bool AppSettings::closeToTray() const { return m_settings.value(QStringLiteral("prefs/closeToTray"), true).toBool(); }
void AppSettings::setCloseToTray(bool v) { m_settings.setValue(QStringLiteral("prefs/closeToTray"), v); }
bool AppSettings::globalShortcuts() const { return m_settings.value(QStringLiteral("prefs/globalShortcuts"), false).toBool(); }
void AppSettings::setGlobalShortcuts(bool v) { m_settings.setValue(QStringLiteral("prefs/globalShortcuts"), v); }
int AppSettings::presetTransitionMs() const
{
    return qBound(0, m_settings.value(QStringLiteral("prefs/presetTransitionMs"), 450).toInt(), 3000);
}
void AppSettings::setPresetTransitionMs(int ms) { m_settings.setValue(QStringLiteral("prefs/presetTransitionMs"), ms); }
QString AppSettings::lastPreset() const { return m_settings.value(QStringLiteral("prefs/lastPreset")).toString(); }
void AppSettings::setLastPreset(const QString &name) { m_settings.setValue(QStringLiteral("prefs/lastPreset"), name); }
bool AppSettings::previewPaused() const { return m_settings.value(QStringLiteral("prefs/previewPaused"), false).toBool(); }
void AppSettings::setPreviewPaused(bool v) { m_settings.setValue(QStringLiteral("prefs/previewPaused"), v); }
bool AppSettings::hasCameraChoice() const { return m_settings.value(QStringLiteral("prefs/cameraChosen"), false).toBool(); }
void AppSettings::setHasCameraChoice(bool v) { m_settings.setValue(QStringLiteral("prefs/cameraChosen"), v); }
bool AppSettings::showPerformance() const { return m_settings.value(QStringLiteral("prefs/showPerformance"), false).toBool(); }
void AppSettings::setShowPerformance(bool v) { m_settings.setValue(QStringLiteral("prefs/showPerformance"), v); }
int AppSettings::lastTab() const { return m_settings.value(QStringLiteral("window/lastTab"), 0).toInt(); }
void AppSettings::setLastTab(int i) { m_settings.setValue(QStringLiteral("window/lastTab"), i); }
bool AppSettings::firstRun() const { return !m_settings.value(QStringLiteral("prefs/initialized"), false).toBool(); }

QByteArray AppSettings::windowGeometry() const { return m_settings.value(QStringLiteral("window/geometry")).toByteArray(); }
void AppSettings::setWindowGeometry(const QByteArray &g) { m_settings.setValue(QStringLiteral("window/geometry"), g); }
QByteArray AppSettings::splitterState() const { return m_settings.value(QStringLiteral("window/splitter")).toByteArray(); }
void AppSettings::setSplitterState(const QByteArray &s) { m_settings.setValue(QStringLiteral("window/splitter"), s); }

} // namespace app
