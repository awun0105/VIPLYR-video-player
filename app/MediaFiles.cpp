#include "MediaFiles.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QMimeData>
#include <QSet>
#include <QUrl>

namespace MediaFiles {

namespace {

const QStringList &videoExtensions() {
    static const QStringList extensions = {"mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v",
                                           "mpeg", "mpg", "ts", "m2ts", "3gp", "ogv", "ogg"};
    return extensions;
}

QStringList videoFilesInFolder(const QString &folder) {
    QStringList files;
    QDirIterator iterator(folder, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (isVideoFile(path)) files.append(path);
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

}  // namespace

bool isVideoFile(const QString &path) {
    return videoExtensions().contains(QFileInfo(path).suffix().toLower());
}

QStringList collectVideoFiles(const QStringList &paths) {
    QStringList files;
    QSet<QString> seen;
    const auto add = [&files, &seen](const QString &path) {
        if (seen.contains(path)) return;
        seen.insert(path);
        files.append(path);
    };

    for (const QString &path : paths) {
        const QFileInfo info(path);
        const QString absolutePath = info.absoluteFilePath();
        if (info.isDir()) {
            for (const QString &file : videoFilesInFolder(absolutePath)) add(file);
        } else if (info.isFile() && isVideoFile(absolutePath)) {
            add(absolutePath);
        }
    }
    return files;
}

QStringList localPaths(const QMimeData *mimeData) {
    QStringList paths;
    for (const QUrl &url : mimeData->urls()) {
        if (url.isLocalFile()) paths.append(url.toLocalFile());
    }
    return paths;
}

QString fileDialogFilter() {
    QStringList patterns;
    for (const QString &extension : videoExtensions()) patterns.append("*." + extension);
    return QString("Video Files (%1);;All Files (*)").arg(patterns.join(' '));
}

QString formatTime(qint64 milliseconds) {
    const qint64 totalSeconds = milliseconds / 1000;
    const int seconds = static_cast<int>(totalSeconds % 60);
    const int minutes = static_cast<int>((totalSeconds / 60) % 60);
    const int hours = static_cast<int>(totalSeconds / 3600);
    const QChar zero('0');
    if (hours > 0) {
        return QString("%1:%2:%3").arg(hours, 2, 10, zero).arg(minutes, 2, 10, zero).arg(seconds, 2, 10, zero);
    }
    return QString("%1:%2").arg(minutes, 2, 10, zero).arg(seconds, 2, 10, zero);
}

QString formatTimePrecise(qint64 milliseconds) {
    const QChar zero('0');
    return QString("%1:%2:%3.%4")
        .arg(milliseconds / 3600000, 2, 10, zero)
        .arg((milliseconds / 60000) % 60, 2, 10, zero)
        .arg((milliseconds / 1000) % 60, 2, 10, zero)
        .arg(milliseconds % 1000, 3, 10, zero);
}

}  // namespace MediaFiles
