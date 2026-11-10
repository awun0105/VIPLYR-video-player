#include "EditProject.h"

#include <QHash>
#include <QTransform>
#include <QUuid>
#include <algorithm>
#include "viplyr/FrameReader.h"

qint64 EditProject::duration() const {
    qint64 result = 0;
    for (const auto &clip : clips) result += clip.duration();
    return result;
}

qint64 EditProject::startOf(int index) const {
    qint64 result = 0;
    for (int i = 0; i < std::min(index, static_cast<int>(clips.size())); ++i) result += clips[i].duration();
    return result;
}

int EditProject::clipAt(qint64 positionMs) const {
    if (clips.isEmpty()) return -1;
    qint64 start = 0;
    for (int i = 0; i < clips.size(); ++i) {
        start += clips[i].duration();
        if (positionMs < start) return i;
    }
    return clips.size() - 1;
}

bool EditProject::trim(int index, qint64 inMs, qint64 outMs) {
    if (index < 0 || index >= clips.size()) return false;
    auto &clip = clips[index];
    const double frameMs = 1000.0 / std::max(1.0, clip.frameRate);
    if (clip.sourceDurationMs < 1) return false;
    const qint64 minimum = std::min(clip.sourceDurationMs, std::max<qint64>(1, qRound64(frameMs)));
    inMs = std::clamp<qint64>(qRound64(qRound64(inMs / frameMs) * frameMs), 0,
                             std::max<qint64>(0, clip.sourceDurationMs - minimum));
    outMs = std::clamp<qint64>(qRound64(qRound64(outMs / frameMs) * frameMs), inMs + minimum,
                              clip.sourceDurationMs);
    if (clip.inMs == inMs && clip.outMs == outMs) return false;
    clip.inMs = inMs;
    clip.outMs = outMs;
    return true;
}

bool EditProject::split(qint64 positionMs) {
    const int index = clipAt(positionMs);
    if (index < 0) return false;
    const auto clip = clips[index];
    const double frameMs = 1000.0 / std::max(1.0, clip.frameRate);
    const qint64 cut = qRound64(qRound64((clip.inMs + positionMs - startOf(index)) / frameMs) * frameMs);
    const qint64 minimum = std::min(clip.sourceDurationMs, std::max<qint64>(1, qRound64(frameMs)));
    if (cut - clip.inMs < minimum || clip.outMs - cut < minimum) return false;
    clips[index].outMs = cut;
    EditClip right = clip;
    right.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    right.inMs = cut;
    clips.insert(index + 1, right);
    return true;
}

bool EditProject::move(int from, int before) {
    if (from < 0 || from >= clips.size() || before < 0 || before > clips.size()) return false;
    const int to = before > from ? before - 1 : before;
    if (to == from) return false;
    clips.move(from, to);
    return true;
}

QVector<EditClip> probeEditClips(const QStringList &paths, const std::atomic<bool> *cancelled) {
    QVector<EditClip> result;
    QHash<QString, EditClip> cache;
    for (const QString &path : paths) {
        if (cancelled && *cancelled) break;
        if (cache.contains(path)) {
            auto clip = cache.value(path);
            clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            result.append(clip);
            continue;
        }
        viplyr::FrameReader reader;
        viplyr::OpenOptions options;
        options.hwAccel = viplyr::HwAccel::Vaapi;
        if (!reader.open(path.toStdString(), options)) continue;
        const auto info = reader.info();
        if (info.duration <= 0 || info.width <= 0 || info.height <= 0) continue;
        EditClip clip;
        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        clip.path = path;
        clip.sourceDurationMs = qRound64(info.duration * 1000);
        clip.outMs = clip.sourceDurationMs;
        clip.frameRate = info.frameRate > 0 ? info.frameRate : 30;
        // FrameReader only opens the video decoder; its track list still describes audio.
        clip.hasAudio = !info.audioTracks.empty();
        const bool sideways = info.rotationDegrees % 180 != 0;
        clip.pictureSize = QSize(sideways ? info.height : qRound(info.width * info.pixelAspectRatio),
                                 sideways ? qRound(info.width * info.pixelAspectRatio) : info.height);
        for (int i = 0; i < 8; ++i) {
            if (cancelled && *cancelled) break;
            viplyr::VideoFrame frame;
            reader.seek(info.duration * (i + 0.1) / 8, viplyr::SeekMode::Keyframe);
            if (!reader.read(frame)) continue;
            const QSize size = QSize(frame.width(), frame.height()).scaled(144, 80, Qt::KeepAspectRatio);
            const auto rgba = frame.convert(viplyr::PixelFormat::Rgba, size.width(), size.height());
            if (!rgba.isValid()) continue;
            QImage image(rgba.plane(0), rgba.width(), rgba.height(), rgba.stride(0), QImage::Format_RGBA8888);
            image = image.copy();
            if (info.rotationDegrees) image = image.transformed(QTransform().rotate(info.rotationDegrees));
            clip.thumbnails.append(image);
        }
        cache.insert(path, clip);
        result.append(clip);
    }
    return result;
}
