// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Params.h"
#include "pipeline/Types.h"

#include <QJsonObject>
#include <QMap>

namespace app {

QJsonObject toJson(const cam::ColorParams &c);
QJsonObject toJson(const cam::FramingParams &f);
QJsonObject toJson(const cam::EffectParams &e);
QJsonObject toJson(const cam::OutputConfig &o);
QJsonObject toJson(const cam::CameraSelection &s);
QJsonObject toJson(const cam::CaptureRequest &r);
QJsonObject controlsToJson(const QMap<quint32, qint64> &controls);

// Parsers start from defaults and accept missing or out-of-range fields.
cam::ColorParams colorFromJson(const QJsonObject &o);
cam::FramingParams framingFromJson(const QJsonObject &o);
cam::EffectParams effectsFromJson(const QJsonObject &o);
cam::OutputConfig outputFromJson(const QJsonObject &o);
cam::CameraSelection cameraFromJson(const QJsonObject &o);
cam::CaptureRequest captureFromJson(const QJsonObject &o);
QMap<quint32, qint64> controlsFromJson(const QJsonObject &o);

} // namespace app
