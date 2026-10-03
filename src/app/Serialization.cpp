// SPDX-License-Identifier: GPL-3.0-or-later
#include "Serialization.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace app {

namespace {

double num(const QJsonObject &o, const char *key, double def, double lo, double hi)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (!v.isDouble())
        return def;
    double d = v.toDouble();
    if (!std::isfinite(d))
        return def;
    return std::clamp(d, lo, hi);
}

bool flag(const QJsonObject &o, const char *key, bool def)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isBool() ? v.toBool() : def;
}

QString str(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toString();
}

QString colorString(uint32_t argb)
{
    return QStringLiteral("#%1").arg(argb, 8, 16, QLatin1Char('0'));
}

uint32_t parseColor(const QJsonObject &o, const char *key, uint32_t def)
{
    QString s = str(o, key);
    if (!s.startsWith(QLatin1Char('#')))
        return def;
    bool ok = false;
    uint32_t v = s.mid(1).toUInt(&ok, 16);
    if (!ok)
        return def;
    if (s.size() == 7)
        v |= 0xff000000u;
    return v;
}

} // namespace

QJsonObject toJson(const cam::ColorParams &c)
{
    return QJsonObject{{"brightness", c.brightness}, {"contrast", c.contrast}, {"saturation", c.saturation},
                       {"gamma", c.gamma},           {"sharpness", c.sharpness}, {"warmth", c.warmth},
                       {"tint", c.tint},
                       {"exposure", c.exposure},     {"blackPoint", c.blackPoint},
                       {"whitePoint", c.whitePoint}, {"highlights", c.highlights},
                       {"shadows", c.shadows},       {"vibrance", c.vibrance},
                       {"hue", c.hue}};
}

cam::ColorParams colorFromJson(const QJsonObject &o)
{
    cam::ColorParams c;
    c.brightness = num(o, "brightness", c.brightness, -1, 1);
    c.contrast = num(o, "contrast", c.contrast, 0, 3);
    c.saturation = num(o, "saturation", c.saturation, 0, 3);
    c.gamma = num(o, "gamma", c.gamma, 0.2, 5);
    c.sharpness = num(o, "sharpness", c.sharpness, 0, 2);
    c.warmth = num(o, "warmth", c.warmth, -1, 1);
    c.tint = num(o, "tint", c.tint, -1, 1);
    c.exposure = num(o, "exposure", c.exposure, -2, 2);
    c.blackPoint = num(o, "blackPoint", c.blackPoint, 0, 0.5);
    c.whitePoint = num(o, "whitePoint", c.whitePoint, 0.5, 1);
    c.highlights = num(o, "highlights", c.highlights, -1, 1);
    c.shadows = num(o, "shadows", c.shadows, -1, 1);
    c.vibrance = num(o, "vibrance", c.vibrance, -1, 1);
    c.hue = num(o, "hue", c.hue, -180, 180);
    return c;
}

QJsonObject toJson(const cam::FramingParams &f)
{
    return QJsonObject{{"zoom", f.zoom},
                       {"panX", f.panX},
                       {"panY", f.panY},
                       {"rotation", f.rotation},
                       {"mirror", f.mirror},
                       {"flip", f.flip},
                       {"aspect", int(f.aspect)},
                       {"cropLeft", f.cropLeft},
                       {"cropTop", f.cropTop},
                       {"cropRight", f.cropRight},
                       {"cropBottom", f.cropBottom}};
}

cam::FramingParams framingFromJson(const QJsonObject &o)
{
    cam::FramingParams f;
    f.zoom = num(o, "zoom", 1, 1, 8);
    f.panX = num(o, "panX", 0, -1, 1);
    f.panY = num(o, "panY", 0, -1, 1);
    f.rotation = num(o, "rotation", 0, -360, 360);
    f.mirror = flag(o, "mirror", false);
    f.flip = flag(o, "flip", false);
    f.aspect = cam::AspectMode(int(num(o, "aspect", 0, 0, 2)));
    f.cropLeft = num(o, "cropLeft", 0, 0, 0.45);
    f.cropTop = num(o, "cropTop", 0, 0, 0.45);
    f.cropRight = num(o, "cropRight", 0, 0, 0.45);
    f.cropBottom = num(o, "cropBottom", 0, 0, 0.45);
    return f;
}

static QJsonObject rectJson(const cam::RectF &r)
{
    return QJsonObject{{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}};
}

static cam::RectF rectFromJson(const QJsonObject &o, cam::RectF def)
{
    cam::RectF r;
    r.x = num(o, "x", def.x, 0, 1);
    r.y = num(o, "y", def.y, 0, 1);
    r.w = num(o, "w", def.w, 0, 1);
    r.h = num(o, "h", def.h, 0, 1);
    return r;
}

QJsonObject toJson(const cam::EffectParams &e)
{
    QJsonArray regions;
    for (const auto &r : e.blurRegions)
        regions.append(rectJson(r));
    return QJsonObject{{"mode", int(e.mode)},
                       {"fill", int(e.fill)},
                       {"blurStrength", e.blurStrength},
                       {"fillColor", colorString(e.fillColor)},
                       {"blurRegions", regions},
                       {"foreground", rectJson(e.foreground)},
                       {"foregroundEllipse", e.foregroundEllipse},
                       {"feather", e.feather},
                       {"keyColor", colorString(e.keyColor)},
                       {"keySimilarity", e.keySimilarity},
                       {"keySmoothness", e.keySmoothness},
                       {"backgroundImage", QString::fromStdString(e.backgroundImagePath)},
                       {"maskImage", QString::fromStdString(e.maskImagePath)}};
}

cam::EffectParams effectsFromJson(const QJsonObject &o)
{
    cam::EffectParams e;
    e.mode = cam::EffectMode(int(num(o, "mode", 0, 0, 6)));
    e.fill = cam::BackgroundFill(int(num(o, "fill", 0, 0, 2)));
    e.blurStrength = num(o, "blurStrength", e.blurStrength, 0, 1);
    e.fillColor = parseColor(o, "fillColor", e.fillColor);
    for (const QJsonValue &v : o.value(QLatin1String("blurRegions")).toArray())
        if (v.isObject() && e.blurRegions.size() < 32)
            e.blurRegions.push_back(rectFromJson(v.toObject(), cam::RectF{}));
    e.foreground = rectFromJson(o.value(QLatin1String("foreground")).toObject(), e.foreground);
    e.foregroundEllipse = flag(o, "foregroundEllipse", e.foregroundEllipse);
    e.feather = num(o, "feather", e.feather, 0, 0.5);
    e.keyColor = parseColor(o, "keyColor", e.keyColor);
    e.keySimilarity = num(o, "keySimilarity", e.keySimilarity, 0, 1);
    e.keySmoothness = num(o, "keySmoothness", e.keySmoothness, 0, 1);
    e.backgroundImagePath = str(o, "backgroundImage").toStdString();
    e.maskImagePath = str(o, "maskImage").toStdString();
    return e;
}

QJsonObject toJson(const cam::OutputConfig &o)
{
    return QJsonObject{{"enabled", o.enabled},
                       {"device", QString::fromStdString(o.devicePath)},
                       {"width", o.width},
                       {"height", o.height},
                       {"fps", o.fps},
                       {"pixelFormat", int(o.pixelFormat)}};
}

cam::OutputConfig outputFromJson(const QJsonObject &o)
{
    cam::OutputConfig c;
    c.enabled = flag(o, "enabled", c.enabled);
    c.devicePath = str(o, "device").toStdString();
    c.width = int(num(o, "width", c.width, 160, 3840)) & ~1;
    c.height = int(num(o, "height", c.height, 120, 2160)) & ~1;
    c.fps = int(num(o, "fps", c.fps, 5, 60));
    c.pixelFormat = cam::OutputPixelFormat(int(num(o, "pixelFormat", 0, 0, 1)));
    return c;
}

QJsonObject toJson(const cam::CameraSelection &s)
{
    return QJsonObject{{"byId", QString::fromStdString(s.byIdPath)},
                       {"card", QString::fromStdString(s.card)},
                       {"bus", QString::fromStdString(s.busInfo)},
                       {"path", QString::fromStdString(s.path)},
                       {"testPattern", s.testPattern}};
}

cam::CameraSelection cameraFromJson(const QJsonObject &o)
{
    cam::CameraSelection s;
    s.byIdPath = str(o, "byId").toStdString();
    s.card = str(o, "card").toStdString();
    s.busInfo = str(o, "bus").toStdString();
    s.path = str(o, "path").toStdString();
    s.testPattern = flag(o, "testPattern", false);
    return s;
}

QJsonObject toJson(const cam::CaptureRequest &r)
{
    return QJsonObject{{"auto", r.automatic},
                       {"fourcc", qint64(r.fourcc)},
                       {"width", r.width},
                       {"height", r.height},
                       {"rateNum", qint64(r.rate.num)},
                       {"rateDen", qint64(r.rate.den)}};
}

cam::CaptureRequest captureFromJson(const QJsonObject &o)
{
    cam::CaptureRequest r;
    r.automatic = flag(o, "auto", true);
    r.fourcc = uint32_t(num(o, "fourcc", 0, 0, 4294967295.0));
    r.width = int(num(o, "width", 0, 0, 16384));
    r.height = int(num(o, "height", 0, 0, 16384));
    r.rate.num = uint32_t(num(o, "rateNum", 30, 1, 1000000));
    r.rate.den = uint32_t(num(o, "rateDen", 1, 1, 1000000));
    if (!r.automatic && (r.fourcc == 0 || r.width == 0 || r.height == 0))
        r.automatic = true;
    return r;
}

QJsonObject controlsToJson(const QMap<quint32, qint64> &controls)
{
    QJsonObject o;
    for (auto it = controls.begin(); it != controls.end(); ++it)
        o.insert(QString::number(it.key()), double(it.value()));
    return o;
}

QMap<quint32, qint64> controlsFromJson(const QJsonObject &o)
{
    QMap<quint32, qint64> m;
    for (auto it = o.begin(); it != o.end(); ++it) {
        bool ok = false;
        quint32 id = it.key().toUInt(&ok);
        if (ok && it.value().isDouble())
            m.insert(id, qint64(it.value().toDouble()));
    }
    return m;
}

} // namespace app
