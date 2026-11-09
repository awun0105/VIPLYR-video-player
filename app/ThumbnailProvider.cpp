#include "ThumbnailProvider.h"

#include <QTransform>

namespace {
constexpr int kThumbnailWidth = 156;
constexpr int kThumbnailHeight = 88;
constexpr qint64 kPositionStepMs = 1000;  // previews are shared within one second
constexpr int kCacheSize = 200;
}

// ------------------------------------------------------------------- worker

void ThumbnailWorker::setSource(const QString &path) {
    source_ = path;
    reader_.close();
    if (path.isEmpty()) return;

    viplyr::OpenOptions options;
    options.hwAccel = viplyr::HwAccel::None;  // one small frame at a time: the CPU is quicker to start
    reader_.open(path.toStdString(), options);
}

void ThumbnailWorker::extract() {
    const qint64 position = wantedPosition.exchange(-1);
    if (position < 0 || !reader_.isOpen()) return;  // already handled by an earlier call

    viplyr::VideoFrame frame;
    if (!reader_.seek(position / 1000.0, viplyr::SeekMode::Keyframe) || !reader_.read(frame)) return;

    const QSize size = QSize(frame.width(), frame.height())
                           .scaled(kThumbnailWidth, kThumbnailHeight, Qt::KeepAspectRatio);
    const viplyr::VideoFrame rgba = frame.convert(viplyr::PixelFormat::Rgba, size.width(), size.height());
    if (!rgba.isValid()) return;

    // copy(): the QImage must not point into the frame's buffer once it leaves this function.
    QImage image = QImage(rgba.plane(0), rgba.width(), rgba.height(), rgba.stride(0),
                          QImage::Format_RGBA8888).copy();
    const int rotation = reader_.info().rotationDegrees;
    if (rotation != 0) image = image.transformed(QTransform().rotate(rotation));
    emit extracted(source_, position, image);
}

// ----------------------------------------------------------------- provider

ThumbnailProvider::ThumbnailProvider(QObject *parent) : QObject(parent), cache_(kCacheSize) {
    worker_ = new ThumbnailWorker;
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &ThumbnailWorker::extracted, this, &ThumbnailProvider::onExtracted);
    thread_.start();
}

ThumbnailProvider::~ThumbnailProvider() {
    thread_.quit();
    thread_.wait();
}

void ThumbnailProvider::setSource(const QString &path) {
    if (path == source_) return;
    source_ = path;
    cache_.clear();
    QMetaObject::invokeMethod(worker_, "setSource", Qt::QueuedConnection, Q_ARG(QString, path));
}

void ThumbnailProvider::request(qint64 positionMs) {
    const qint64 position = positionMs / kPositionStepMs * kPositionStepMs;
    if (const QPixmap *cached = cache_.object(position)) {
        emit thumbnailReady(position, *cached);
        return;
    }
    worker_->wantedPosition = position;
    QMetaObject::invokeMethod(worker_, "extract", Qt::QueuedConnection);
}

void ThumbnailProvider::onExtracted(const QString &source, qint64 position, const QImage &image) {
    if (source != source_) return;  // the video changed while this one was being decoded
    const QPixmap pixmap = QPixmap::fromImage(image);
    cache_.insert(position, new QPixmap(pixmap));
    emit thumbnailReady(position, pixmap);
}
