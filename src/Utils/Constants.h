#pragma once
#include <QSet>
#include <QString>
#include <QStringList>
#include <QFileInfo>

namespace Constants {
    const QSet<QString> kVideoExtensions = {
        "mp4", "mkv", "avi", "mov", "wmv", "flv", "webm",
        "m4v", "mpeg", "mpg", "ts", "m2ts", "3gp", "ogg"
    };

    inline bool isSupportedVideoFile(const QString &path) {
        QFileInfo info(path);
        return info.exists() && info.isFile() && kVideoExtensions.contains(info.suffix().toLower());
    }

    inline QStringList filterVideoFiles(const QStringList &paths) {
        QStringList files;
        for (const QString &path : paths) {
            if (isSupportedVideoFile(path)) files.append(path);
        }
        return files;
    }

    const QString kVideoFilter = "Video Files (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.m4v *.mpeg *.mpg *.ts *.m2ts *.3gp *.ogg);;All Files (*)";

    inline QString formatTime(qint64 ms) {
        int seconds = (ms / 1000) % 60;
        int minutes = (ms / 60000) % 60;
        int hours = (ms / 3600000);
        if (hours > 0)
            return QString("%1:%2:%3").arg(hours, 2, 10, QChar('0')).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
        return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
    }
}
