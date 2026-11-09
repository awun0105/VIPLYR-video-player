#pragma once
#include <QCache>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QThread>
#include <atomic>

#include "viplyr/FrameReader.h"

// Decodes preview pictures on its own thread. Lives on that thread.
class ThumbnailWorker : public QObject {
    Q_OBJECT
public:
    // The position to decode next; -1 when there is nothing to do. Written by the
    // GUI thread, so a newer request simply replaces an older one.
    std::atomic<qint64> wantedPosition{-1};

public slots:
    void setSource(const QString &path);
    void extract();

signals:
    void extracted(const QString &source, qint64 position, const QImage &image);

private:
    viplyr::FrameReader reader_;
    QString source_;
};

// Preview pictures for the seek bar. Each result is tagged with the position it
// was requested for, and only the most recent request is decoded.
class ThumbnailProvider : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailProvider(QObject *parent = nullptr);
    ~ThumbnailProvider() override;

    void setSource(const QString &path);
    void request(qint64 positionMs);

signals:
    void thumbnailReady(qint64 positionMs, const QPixmap &pixmap);

private:
    void onExtracted(const QString &source, qint64 position, const QImage &image);

    QThread thread_;
    ThumbnailWorker *worker_;
    QString source_;
    QCache<qint64, QPixmap> cache_;  // keyed by position
};
