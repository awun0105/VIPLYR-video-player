#pragma once
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>

// Non-destructive cuts: all times refer to the original source file.
struct EditClip {
    QString id;
    QString path;
    qint64 sourceDurationMs = 0;
    qint64 inMs = 0;
    qint64 outMs = 0;
    QSize pictureSize;
    double frameRate = 30.0;
    bool hasAudio = false;
    QVector<QImage> thumbnails;

    qint64 duration() const { return outMs - inMs; }
};

class EditProject {
public:
    QVector<EditClip> clips;
    qint64 duration() const;
    qint64 startOf(int index) const;
    int clipAt(qint64 positionMs) const;
    bool trim(int index, qint64 inMs, qint64 outMs);
    bool split(qint64 positionMs);
    bool move(int from, int before);  // insertion boundary in the original list, including its end
};

// Reads metadata and a small filmstrip. Call on a worker, never the GUI thread.
QVector<EditClip> probeEditClips(const QStringList &paths, const std::atomic<bool> *cancelled = nullptr);
