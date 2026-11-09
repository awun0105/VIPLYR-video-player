#pragma once
#include <QString>
#include <QVariantList>

// Keeps the screen from blanking or locking while a video plays, through the
// desktop's D-Bus screen-saver service.
class IdleInhibitor {
public:
    ~IdleInhibitor() { setInhibited(false); }
    void setInhibited(bool inhibited);

private:
    bool inhibit(const QString &service, const QString &path, const QString &releaseMethod,
                 const QVariantList &arguments);

    bool active_ = false;
    uint cookie_ = 0;
    QString service_;
    QString path_;
    QString releaseMethod_;
};
