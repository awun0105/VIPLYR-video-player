#pragma once
#include <QString>
#include <QStringList>

class QMimeData;

// Helpers for finding and naming video files.
namespace MediaFiles {

bool isVideoFile(const QString &path);

// Turns files and folders (searched recursively) into a sorted list of absolute
// video file paths. Touches the disk, so call it off the GUI thread for drops.
QStringList collectVideoFiles(const QStringList &paths);

// The local files and folders carried by a drag-and-drop.
QStringList localPaths(const QMimeData *mimeData);

QString fileDialogFilter();

// "mm:ss", or "hh:mm:ss" from one hour up.
QString formatTime(qint64 milliseconds);

// "hh:mm:ss.mmm", for frame-exact work.
QString formatTimePrecise(qint64 milliseconds);

}  // namespace MediaFiles
