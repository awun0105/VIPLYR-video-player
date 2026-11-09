#include "MediaProbe.h"

#include <QSize>
#include <QTransform>

#include "viplyr/FrameReader.h"

MediaProbe MediaProbe::probe(const QString &path, int thumbnailWidth, int thumbnailHeight) {
    MediaProbe result;
    viplyr::OpenOptions options;
    options.hwAccel = viplyr::HwAccel::None;
    viplyr::FrameReader reader;
    if (!reader.open(path.toStdString(), options)) return result;

    const viplyr::MediaInfo &info = reader.info();
    result.durationMs = qRound64(info.duration * 1000.0);

    // A frame a little way in is more telling than the (often black) first one.
    viplyr::VideoFrame frame;
    reader.seek(info.duration * 0.1, viplyr::SeekMode::Keyframe);
    if (!reader.read(frame)) return result;

    const QSize size = QSize(qRound(frame.width() * info.pixelAspectRatio), frame.height())
                           .scaled(thumbnailWidth, thumbnailHeight, Qt::KeepAspectRatio);
    const viplyr::VideoFrame rgba = frame.convert(viplyr::PixelFormat::Rgba, size.width(), size.height());
    if (!rgba.isValid()) return result;

    // copy(): the QImage must own its pixels once the frame is released.
    result.thumbnail = QImage(rgba.plane(0), rgba.width(), rgba.height(), rgba.stride(0),
                              QImage::Format_RGBA8888).copy();
    if (info.rotationDegrees != 0) {
        result.thumbnail = result.thumbnail.transformed(QTransform().rotate(info.rotationDegrees));
    }
    return result;
}
