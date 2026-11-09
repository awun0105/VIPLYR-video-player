#pragma once
#include <QImage>
#include <QString>

// A quick look at a video file: its length and a small preview picture.
struct MediaProbe {
    qint64 durationMs = 0;
    QImage thumbnail;  // null when no frame could be read

    // Opens the file and decodes one frame, so call it from a worker thread.
    static MediaProbe probe(const QString &path, int thumbnailWidth, int thumbnailHeight);
};
