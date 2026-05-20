#include "ThumbnailExtractor.h"
#include <QUrl>
#include <QTimer>

// --- ThumbnailSurface ---
ThumbnailSurface::ThumbnailSurface(QObject *parent) 
    : QAbstractVideoSurface(parent), captureRequested(false) {}

QList<QVideoFrame::PixelFormat> ThumbnailSurface::supportedPixelFormats(QAbstractVideoBuffer::HandleType handleType) const {
    if (handleType == QAbstractVideoBuffer::NoHandle) {
        return QList<QVideoFrame::PixelFormat>()
            << QVideoFrame::Format_RGB32 << QVideoFrame::Format_ARGB32
            << QVideoFrame::Format_RGB24 << QVideoFrame::Format_BGRA32
            << QVideoFrame::Format_BGR32 << QVideoFrame::Format_BGR24;
    }
    return QList<QVideoFrame::PixelFormat>();
}

bool ThumbnailSurface::present(const QVideoFrame &frame) {
    if (captureRequested && frame.isValid()) {
        QVideoFrame cloneFrame(frame);
        if (cloneFrame.map(QAbstractVideoBuffer::ReadOnly)) {
            QImage image(cloneFrame.bits(), cloneFrame.width(), cloneFrame.height(), 
                         cloneFrame.bytesPerLine(),
                         QVideoFrame::imageFormatFromPixelFormat(cloneFrame.pixelFormat()));
            
            if (!image.isNull()) {
                captureRequested = false; // Got it!
                emit frameCaptured(image.copy());
            }
            cloneFrame.unmap();
            return true;
        }
    }
    return true; // Always return true to keep stream alive, but only process when requested
}

// --- ThumbnailExtractor ---
ThumbnailExtractor::ThumbnailExtractor(QObject *parent) : QObject(parent), lastRequestedPos(-1) {
    extractorPlayer = new QMediaPlayer(this, QMediaPlayer::VideoSurface);
    extractorPlayer->setMuted(true);
    
    surface = new ThumbnailSurface(this);
    extractorPlayer->setVideoOutput(surface);
    
    cache.setMaxCost(150);
    
    connect(surface, &ThumbnailSurface::frameCaptured, this, &ThumbnailExtractor::onFrameCaptured);
}

void ThumbnailExtractor::setSource(const QString &filePath) {
    if (currentSource == filePath) return;
    currentSource = filePath;
    extractorPlayer->setMedia(QUrl::fromLocalFile(filePath));
    
    extractorPlayer->play();
    QTimer::singleShot(200, this, [this]() {
        extractorPlayer->pause();
    });
    
    cache.clear();
}

void ThumbnailExtractor::requestThumbnail(qint64 position) {
    qint64 roundedPos = (position / 1000) * 1000;
    
    if (cache.contains(roundedPos)) {
        emit thumbnailReady(position, *cache.object(roundedPos));
        return;
    }
    
    if (extractorPlayer->state() == QMediaPlayer::StoppedState) {
        extractorPlayer->pause();
    }

    lastRequestedPos = roundedPos;
    surface->setCaptureRequested(true);
    extractorPlayer->setPosition(roundedPos);
}

void ThumbnailExtractor::onFrameCaptured(const QImage &image) {
    if (lastRequestedPos < 0) return;
    
    QPixmap pixmap = QPixmap::fromImage(image.scaled(156, 88, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    if (pixmap.isNull()) return;

    cache.insert(lastRequestedPos, new QPixmap(pixmap));
    emit thumbnailReady(lastRequestedPos, pixmap);
    lastRequestedPos = -1; 
}
