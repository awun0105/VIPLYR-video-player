#include "IdleInhibitor.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>

namespace {
const QString kApplicationName = QStringLiteral("Viplyr");
const QString kReason = QStringLiteral("Video playback is active");
constexpr uint kGnomeInhibitIdle = 8;
}

void IdleInhibitor::setInhibited(bool inhibited) {
    if (inhibited == active_) return;

    if (!inhibited) {
        QDBusInterface interface(service_, path_, service_, QDBusConnection::sessionBus());
        if (interface.isValid()) interface.call(releaseMethod_, cookie_);
        active_ = false;
        return;
    }

    // Most desktops offer the freedesktop service; GNOME has its own.
    active_ = inhibit("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "UnInhibit",
                      {kApplicationName, kReason}) ||
              inhibit("org.gnome.SessionManager", "/org/gnome/SessionManager", "Uninhibit",
                      {kApplicationName, uint(0), kReason, kGnomeInhibitIdle});
}

bool IdleInhibitor::inhibit(const QString &service, const QString &path,
                            const QString &releaseMethod, const QVariantList &arguments) {
    QDBusInterface interface(service, path, service, QDBusConnection::sessionBus());
    if (!interface.isValid()) return false;

    const QDBusReply<uint> reply = interface.callWithArgumentList(QDBus::Block, "Inhibit", arguments);
    if (!reply.isValid()) return false;

    cookie_ = reply.value();
    service_ = service;
    path_ = path;
    releaseMethod_ = releaseMethod;
    return true;
}
