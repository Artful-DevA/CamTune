// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusObjectPath>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace app {

// System-wide shortcuts through the XDG desktop portal (works on Wayland, e.g.
// KDE Plasma and GNOME 48+, and on X11 desktops running the portal). The
// desktop asks the user to confirm or change the key bindings.
//
// Where the portal is unavailable, the same actions can be bound to commands
// such as `camadjust --preset 2` in the desktop's keyboard settings.
class GlobalShortcuts : public QObject {
    Q_OBJECT
public:
    explicit GlobalShortcuts(QObject *parent = nullptr);

    static bool portalAvailable();
    void enable();
    void disable();
    bool isActive() const { return !m_session.isEmpty(); }
    QString statusText() const { return m_status; }

Q_SIGNALS:
    void activated(const QString &id);
    void statusChanged();

private Q_SLOTS:
    void onCreateSessionResponse(uint response, const QVariantMap &results);
    void onBindResponse(uint response, const QVariantMap &results);
    void onActivated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp,
                     const QVariantMap &options);

private:
    QString requestPath(const QString &token) const;
    void bind();
    void setStatus(const QString &s);

    QString m_session;
    QString m_status;
    bool m_wanted = false;
};

} // namespace app
