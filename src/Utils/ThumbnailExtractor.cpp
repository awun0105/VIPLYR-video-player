#include "ThumbnailExtractor.h"
#include <QUrl>

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
        cloneFrame.map(QAbstractVideoBuffer::ReadOnly);
        
        QImage image(cloneFrame.bits(), cloneFrame.width(), cloneFrame.height(), 
                     QVideoFrame::imageFormatFromPixelFormat(cloneFrame.pixelFormat()));
        
        if (!image.isNull()) {
            emit frameCaptured(image.copy()); // Need copy because bits() is temporary
        }
        
        cloneFrame.unmap();
        return true;
    }
    return false;
}

// --- ThumbnailExtractor ---
ThumbnailExtractor::ThumbnailExtractor(QObject *parent) : QObject(parent), lastRequestedPos(-1) {
    extractorPlayer = new QMediaPlayer(this);
    surface = new ThumbnailSurface(this);
    extractorPlayer->setVideoOutput(surface);
    
    // Cache up to 100 thumbnails
    cache.setMaxCost(100);
    
    connect(surface, &ThumbnailSurface::frameCaptured, this, &ThumbnailExtractor::onFrameCaptured);
}

void ThumbnailExtractor::setSource(const QString &filePath) {
    if (currentSource == filePath) return;
    currentSource = filePath;
    extractorPlayer->setMedia(QUrl::fromLocalFile(filePath));
    cache.clear();
}

void ThumbnailExtractor::requestThumbnail(qint64 position) {
    // Round position to nearest 500ms for better caching
    qint64 roundedPos = (position / 500) * 500;
    
    if (cache.contains(roundedPos)) {
        emit thumbnailReady(position, *cache.object(roundedPos));
        return;
    }
    
    lastRequestedPos = roundedPos;
    extractorPlayer->setPosition(roundedPos);
}

void ThumbnailExtractor::onFrameCaptured(const QImage &image) {
    if (lastRequestedPos < 0) return;
    
    // Scale down for preview
    QPixmap pixmap = QPixmap::fromImage(image.scaled(160, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    cache.insert(lastRequestedPos, new QPixmap(pixmap));
    
    emit thumbnailReady(lastRequestedPos, pixmap);
    lastRequestedPos = -1; // Reset
}
