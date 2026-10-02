// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Params.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

namespace app {

struct Preset {
    QString name;
    int shortcut = 0; // 1..9 => Ctrl+N, 0 = none

    bool hasFraming = true;
    bool hasColor = false;
    bool hasCamera = false;  // hardware controls
    bool hasOutput = false;  // output resolution / frame rate
    bool hasEffects = false;

    cam::FramingParams framing;
    cam::ColorParams color;
    QMap<quint32, qint64> controls;
    int outputWidth = 1280, outputHeight = 720, outputFps = 30;
    cam::EffectParams effects;
};

// Presets live in ~/.config/CamTune/presets.json and are written
// atomically, so a crash can never leave a half-written file behind.
class PresetStore : public QObject {
    Q_OBJECT
public:
    explicit PresetStore(QObject *parent = nullptr);

    static QString filePath();
    bool load();
    bool save();

    const QList<Preset> &presets() const { return m_presets; }
    int indexOf(const QString &name) const;
    int indexForShortcut(int shortcut) const;
    // Accepts a name, or a 1-based index / shortcut number.
    int find(const QString &nameOrNumber) const;

    void add(const Preset &p);
    void replace(int index, const Preset &p);
    bool rename(int index, const QString &name);
    void remove(int index);
    void setShortcut(int index, int shortcut);
    void move(int from, int to);

    QString uniqueName(const QString &base) const;
    static QList<Preset> defaults();

Q_SIGNALS:
    void changed();

private:
    QList<Preset> m_presets;
};

} // namespace app
