// SPDX-License-Identifier: GPL-3.0-or-later
#include "GlobalShortcuts.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QRandomGenerator>

namespace {

const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kShortcutsIface = QStringLiteral("org.freedesktop.portal.GlobalShortcuts");
const QString kRequestIface = QStringLiteral("org.freedesktop.portal.Request");

struct PortalShortcut {
    QString id;
    QVariantMap properties;
};
using PortalShortcutList = QList<PortalShortcut>;

QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &s)
{
    arg.beginStructure();
    arg << s.id << s.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &s)
{
    arg.beginStructure();
    arg >> s.id >> s.properties;
    arg.endStructure();
    return arg;
}

QString token()
{
    return QStringLiteral("camadjust%1").arg(QRandomGenerator::global()->generate());
}

} // namespace

Q_DECLARE_METATYPE(PortalShortcut)
Q_DECLARE_METATYPE(PortalShortcutList)

namespace app {

GlobalShortcuts::GlobalShortcuts(QObject *parent) : QObject(parent)
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcutList>();
}

bool GlobalShortcuts::portalAvailable()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()->isServiceRegistered(kPortalService))
        return false;
    QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, kPortalPath,
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    msg << kShortcutsIface << QStringLiteral("version");
    QDBusMessage reply = bus.call(msg, QDBus::Block, 1500);
    return reply.type() == QDBusMessage::ReplyMessage;
}

QString GlobalShortcuts::requestPath(const QString &tok) const
{
    QString sender = QDBusConnection::sessionBus().baseService().mid(1).replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, tok);
}

void GlobalShortcuts::setStatus(const QString &s)
{
    m_status = s;
    Q_EMIT statusChanged();
}

void GlobalShortcuts::enable()
{
    m_wanted = true;
    if (!m_session.isEmpty())
        return;
    if (!portalAvailable()) {
        setStatus(tr("The desktop portal does not offer global shortcuts here. Bind keys to commands such "
                     "as “camadjust --preset 1” in your desktop's keyboard settings instead."));
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    const QString handleToken = token();
    // Subscribe to the response before making the call to avoid a race.
    bus.connect(kPortalService, requestPath(handleToken), kRequestIface, QStringLiteral("Response"), this,
                SLOT(onCreateSessionResponse(uint, QVariantMap)));
    QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kShortcutsIface,
                                                      QStringLiteral("CreateSession"));
    msg << QVariantMap{{QStringLiteral("handle_token"), handleToken},
                       {QStringLiteral("session_handle_token"), token()}};
    bus.asyncCall(msg);
    setStatus(tr("Requesting global shortcuts from the desktop…"));
}

void GlobalShortcuts::disable()
{
    m_wanted = false;
    if (m_session.isEmpty())
        return;
    auto bus = QDBusConnection::sessionBus();
    bus.disconnect(kPortalService, kPortalPath, kShortcutsIface, QStringLiteral("Activated"), this,
                   SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
    QDBusMessage close = QDBusMessage::createMethodCall(kPortalService, m_session,
                                                        QStringLiteral("org.freedesktop.portal.Session"),
                                                        QStringLiteral("Close"));
    bus.asyncCall(close);
    m_session.clear();
    setStatus(tr("Global shortcuts are off."));
}

void GlobalShortcuts::onCreateSessionResponse(uint response, const QVariantMap &results)
{
    if (response != 0) {
        setStatus(tr("The desktop declined the global shortcut session."));
        return;
    }
    QVariant handle = results.value(QStringLiteral("session_handle"));
    m_session = handle.canConvert<QDBusObjectPath>() ? handle.value<QDBusObjectPath>().path() : handle.toString();
    if (m_session.isEmpty()) {
        setStatus(tr("The desktop portal returned no session."));
        return;
    }
    if (!m_wanted) {
        disable();
        return;
    }
    QDBusConnection::sessionBus().connect(kPortalService, kPortalPath, kShortcutsIface, QStringLiteral("Activated"),
                                          this, SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
    bind();
}

void GlobalShortcuts::bind()
{
    PortalShortcutList list;
    auto add = [&](const QString &id, const QString &description, const QString &trigger) {
        list.append({id, QVariantMap{{QStringLiteral("description"), description},
                                     {QStringLiteral("preferred_trigger"), trigger}}});
    };
    for (int i = 1; i <= 9; ++i)
        add(QStringLiteral("preset-%1").arg(i), tr("Apply camera preset %1").arg(i),
            QStringLiteral("CTRL+ALT+%1").arg(i));
    add(QStringLiteral("zoom-in"), tr("Camera zoom in"), QStringLiteral("CTRL+ALT+Up"));
    add(QStringLiteral("zoom-out"), tr("Camera zoom out"), QStringLiteral("CTRL+ALT+Down"));
    add(QStringLiteral("reset-framing"), tr("Reset camera framing"), QStringLiteral("CTRL+ALT+0"));
    add(QStringLiteral("toggle-virtual-camera"), tr("Turn the virtual camera on/off"), QStringLiteral("CTRL+ALT+V"));

    auto bus = QDBusConnection::sessionBus();
    const QString handleToken = token();
    bus.connect(kPortalService, requestPath(handleToken), kRequestIface, QStringLiteral("Response"), this,
                SLOT(onBindResponse(uint, QVariantMap)));
    QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kShortcutsIface,
                                                      QStringLiteral("BindShortcuts"));
    msg << QVariant::fromValue(QDBusObjectPath(m_session)) << QVariant::fromValue(list) << QString()
        << QVariantMap{{QStringLiteral("handle_token"), handleToken}};
    bus.asyncCall(msg);
}

void GlobalShortcuts::onBindResponse(uint response, const QVariantMap &)
{
    if (response == 0)
        setStatus(tr("Global shortcuts are active. Change the keys in your desktop's settings."));
    else
        setStatus(tr("Global shortcuts were not confirmed."));
}

void GlobalShortcuts::onActivated(const QDBusObjectPath &session, const QString &id, qulonglong, const QVariantMap &)
{
    if (session.path() == m_session)
        Q_EMIT activated(id);
}

} // namespace app
