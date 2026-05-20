#pragma once
#include <QObject>
#include <QMediaPlayer>
#include <QAbstractVideoSurface>
#include <QVideoSurfaceFormat>
#include <QVideoFrame>
#include <QPixmap>
#include <QCache>

// A surface to grab frames into QImage
class ThumbnailSurface : public QAbstractVideoSurface {
    Q_OBJECT
public:
    explicit ThumbnailSurface(QObject *parent = nullptr);
    QList<QVideoFrame::PixelFormat> supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType = QAbstractVideoBuffer::NoHandle) const override;
    bool present(const QVideoFrame &frame) override;
    
    void setCaptureRequested(bool req) { captureRequested = req; }

signals:
    void frameCaptured(const QImage &image);

private:
    bool captureRequested;
};

// Manager to handle background extraction and caching
class ThumbnailExtractor : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailExtractor(QObject *parent = nullptr);
    void setSource(const QString &filePath);
    void requestThumbnail(qint64 position);

signals:
    void thumbnailReady(qint64 position, const QPixmap &pixmap);

private slots:
    void onFrameCaptured(const QImage &image);

private:
    QMediaPlayer *extractorPlayer;
    ThumbnailSurface *surface;
    QString currentSource;
    qint64 lastRequestedPos;
    QCache<qint64, QPixmap> cache;
};
