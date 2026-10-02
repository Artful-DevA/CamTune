// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresetStore.h"

#include "Serialization.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace app {

PresetStore::PresetStore(QObject *parent) : QObject(parent) {}

QString PresetStore::filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/CamTune/presets.json");
}

QList<Preset> PresetStore::defaults()
{
    QList<Preset> list;
    auto make = [&](const QString &name, int key, double zoom, double panX, double panY) {
        Preset p;
        p.name = name;
        p.shortcut = key;
        p.framing.zoom = zoom;
        p.framing.panX = panX;
        p.framing.panY = panY;
        list.append(p);
    };
    make(QStringLiteral("Normal"), 1, 1.0, 0, 0);
    make(QStringLiteral("Zoom 1.25×"), 2, 1.25, 0, 0);
    make(QStringLiteral("Close-up"), 3, 1.7, 0, -0.35);
    make(QStringLiteral("Desk"), 4, 1.3, 0, 1.0);
    return list;
}

static QJsonObject presetToJson(const Preset &p)
{
    QJsonObject o{{"name", p.name}, {"shortcut", p.shortcut}};
    if (p.hasFraming)
        o.insert("framing", toJson(p.framing));
    if (p.hasColor)
        o.insert("color", toJson(p.color));
    if (p.hasCamera)
        o.insert("cameraControls", controlsToJson(p.controls));
    if (p.hasOutput)
        o.insert("output", QJsonObject{{"width", p.outputWidth}, {"height", p.outputHeight}, {"fps", p.outputFps}});
    if (p.hasEffects)
        o.insert("effects", toJson(p.effects));
    return o;
}

static bool presetFromJson(const QJsonObject &o, Preset &p)
{
    p.name = o.value("name").toString().trimmed();
    if (p.name.isEmpty())
        return false;
    p.shortcut = qBound(0, o.value("shortcut").toInt(), 9);
    p.hasFraming = o.contains("framing");
    p.framing = framingFromJson(o.value("framing").toObject());
    p.hasColor = o.contains("color");
    p.color = colorFromJson(o.value("color").toObject());
    p.hasCamera = o.contains("cameraControls");
    p.controls = controlsFromJson(o.value("cameraControls").toObject());
    p.hasOutput = o.contains("output");
    const QJsonObject out = o.value("output").toObject();
    p.outputWidth = qBound(160, out.value("width").toInt(1280), 3840) & ~1;
    p.outputHeight = qBound(120, out.value("height").toInt(720), 2160) & ~1;
    p.outputFps = qBound(5, out.value("fps").toInt(30), 60);
    p.hasEffects = o.contains("effects");
    p.effects = effectsFromJson(o.value("effects").toObject());
    return true;
}

bool PresetStore::load()
{
    QFile f(filePath());
    if (!f.exists()) {
        m_presets = defaults();
        save();
        Q_EMIT changed();
        return true;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        m_presets = defaults();
        Q_EMIT changed();
        return false;
    }
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    QList<Preset> list;
    if (err.error == QJsonParseError::NoError) {
        for (const QJsonValue &v : doc.object().value("presets").toArray()) {
            Preset p;
            if (v.isObject() && presetFromJson(v.toObject(), p)) {
                bool dup = false;
                for (const auto &q : list)
                    dup |= q.name == p.name;
                if (!dup)
                    list.append(p);
            }
        }
    } else {
        // Keep the unreadable file for the user and start from defaults.
        QFile::rename(filePath(), filePath() + QStringLiteral(".broken"));
        list = defaults();
    }
    m_presets = list;
    Q_EMIT changed();
    return err.error == QJsonParseError::NoError;
}

bool PresetStore::save()
{
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    QJsonArray arr;
    for (const auto &p : m_presets)
        arr.append(presetToJson(p));
    QSaveFile f(filePath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"presets", arr}}).toJson());
    return f.commit();
}

int PresetStore::indexOf(const QString &name) const
{
    for (int i = 0; i < m_presets.size(); ++i)
        if (m_presets[i].name.compare(name, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

int PresetStore::indexForShortcut(int shortcut) const
{
    if (shortcut <= 0)
        return -1;
    for (int i = 0; i < m_presets.size(); ++i)
        if (m_presets[i].shortcut == shortcut)
            return i;
    return -1;
}

int PresetStore::find(const QString &nameOrNumber) const
{
    int i = indexOf(nameOrNumber);
    if (i >= 0)
        return i;
    bool ok = false;
    int n = nameOrNumber.toInt(&ok);
    if (!ok)
        return -1;
    i = indexForShortcut(n);
    if (i >= 0)
        return i;
    return (n >= 1 && n <= m_presets.size()) ? n - 1 : -1;
}

QString PresetStore::uniqueName(const QString &base) const
{
    QString name = base.trimmed().isEmpty() ? QStringLiteral("Preset") : base.trimmed();
    if (indexOf(name) < 0)
        return name;
    for (int i = 2;; ++i) {
        QString n = QStringLiteral("%1 %2").arg(name).arg(i);
        if (indexOf(n) < 0)
            return n;
    }
}

void PresetStore::add(const Preset &p)
{
    Preset copy = p;
    copy.name = uniqueName(p.name);
    if (copy.shortcut && indexForShortcut(copy.shortcut) >= 0)
        copy.shortcut = 0;
    m_presets.append(copy);
    save();
    Q_EMIT changed();
}

void PresetStore::replace(int index, const Preset &p)
{
    if (index < 0 || index >= m_presets.size())
        return;
    m_presets[index] = p;
    save();
    Q_EMIT changed();
}

bool PresetStore::rename(int index, const QString &name)
{
    const QString n = name.trimmed();
    if (index < 0 || index >= m_presets.size() || n.isEmpty())
        return false;
    int other = indexOf(n);
    if (other >= 0 && other != index)
        return false;
    m_presets[index].name = n;
    save();
    Q_EMIT changed();
    return true;
}

void PresetStore::remove(int index)
{
    if (index < 0 || index >= m_presets.size())
        return;
    m_presets.removeAt(index);
    save();
    Q_EMIT changed();
}

void PresetStore::setShortcut(int index, int shortcut)
{
    if (index < 0 || index >= m_presets.size())
        return;
    shortcut = qBound(0, shortcut, 9);
    // A shortcut belongs to one preset at a time.
    if (shortcut)
        for (auto &p : m_presets)
            if (p.shortcut == shortcut)
                p.shortcut = 0;
    m_presets[index].shortcut = shortcut;
    save();
    Q_EMIT changed();
}

void PresetStore::move(int from, int to)
{
    if (from < 0 || from >= m_presets.size() || to < 0 || to >= m_presets.size() || from == to)
        return;
    m_presets.move(from, to);
    save();
    Q_EMIT changed();
}

} // namespace app
