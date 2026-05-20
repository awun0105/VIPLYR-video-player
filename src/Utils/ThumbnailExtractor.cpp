#include "ThumbnailExtractor.h"
#include <QUrl>
#include <QTimer>

// --- ThumbnailSurface ---
ThumbnailSurface::ThumbnailSurface(QObject *parent) : QAbstractVideoSurface(parent) {}

QList<QVideoFrame::PixelFormat> ThumbnailSurface::supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType) const {
    if (handleType == QAbstractVideoBuffer::NoHandle) {
        return QList<QVideoFrame::PixelFormat>()
            << QVideoFrame::Format_RGB32
            << QVideoFrame::Format_ARGB32
            << QVideoFrame::Format_YV12
            << QVideoFrame::Format_YUYV;
    }
    return QList<QVideoFrame::PixelFormat>();
}

bool ThumbnailSurface::present(const QVideoFrame &frame) {
    if (frame.isValid()) {
        QVideoFrame cloneFrame(frame);
        if (cloneFrame.map(QAbstractVideoBuffer::ReadOnly)) {
            QImage image(cloneFrame.bits(), cloneFrame.width(), cloneFrame.height(), 
                         QVideoFrame::imageFormatFromPixelFormat(cloneFrame.pixelFormat()));
            
            if (!image.isNull()) {
                emit frameCaptured(image.copy());
            }
            cloneFrame.unmap();
            return true;
        }
    }
    return false;
}

// --- ThumbnailExtractor ---
ThumbnailExtractor::ThumbnailExtractor(QObject *parent) : QObject(parent), lastRequestedPos(-1) {
    extractorPlayer = new QMediaPlayer(this, QMediaPlayer::VideoSurface);
    extractorPlayer->setMuted(true);
    
    surface = new ThumbnailSurface(this);
    extractorPlayer->setVideoOutput(surface);
    
    cache.setMaxCost(100);
    
    connect(surface, &ThumbnailSurface::frameCaptured, this, &ThumbnailExtractor::onFrameCaptured);
}

void ThumbnailExtractor::setSource(const QString &filePath) {
    if (currentSource == filePath) return;
    currentSource = filePath;
    extractorPlayer->setMedia(QUrl::fromLocalFile(filePath));
    
    // Prime the player: play briefly then pause to ensure the surface is active and file is loaded
    extractorPlayer->play();
    QTimer::singleShot(1000, this, [this]() {
        extractorPlayer->pause();
    });
    
    cache.clear();
}

void ThumbnailExtractor::requestThumbnail(qint64 position) {
    // Round to nearest second for caching
    qint64 roundedPos = (position / 1000) * 1000;
    
    if (cache.contains(roundedPos)) {
        emit thumbnailReady(position, *cache.object(roundedPos));
        return;
    }
    
    // Ensure player is at least in Paused state to allow seeking
    if (extractorPlayer->state() == QMediaPlayer::StoppedState) {
        extractorPlayer->pause();
    }

    lastRequestedPos = roundedPos;
    extractorPlayer->setPosition(roundedPos);
}

void ThumbnailExtractor::onFrameCaptured(const QImage &image) {
    if (lastRequestedPos < 0) return;
    
    // Scale down for preview popup
    QPixmap pixmap = QPixmap::fromImage(image.scaled(160, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    
    // Cache the result
    cache.insert(lastRequestedPos, new QPixmap(pixmap));
    
    emit thumbnailReady(lastRequestedPos, pixmap);
    lastRequestedPos = -1; // Reset to wait for next request
}
