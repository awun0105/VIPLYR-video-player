#include "OverlayTrack.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace {

void readCommon(const QJsonObject &object, OverlayShape *shape) {
    shape->label = object.value("label").toString();
    const QColor color(object.value("color").toString());
    if (color.isValid()) shape->color = color;
}

}  // namespace

void OverlayTrack::loadForVideo(const QString &videoPath) {
    entries_.clear();
    const QFileInfo video(videoPath);
    QFile file(video.path() + "/" + video.completeBaseName() + ".overlay.json");
    if (!file.open(QIODevice::ReadOnly)) return;

    const QJsonArray frames = QJsonDocument::fromJson(file.readAll()).object().value("frames").toArray();
    for (const QJsonValue &frameValue : frames) {
        const QJsonObject frame = frameValue.toObject();
        Entry entry;
        entry.time = frame.value("t").toDouble();

        for (const QJsonValue &value : frame.value("boxes").toArray()) {
            const QJsonObject object = value.toObject();
            OverlayShape shape;
            shape.type = OverlayShape::Type::Box;
            shape.box = QRectF(object.value("x").toDouble(), object.value("y").toDouble(),
                               object.value("w").toDouble(), object.value("h").toDouble());
            readCommon(object, &shape);
            entry.shapes.append(shape);
        }
        for (const QJsonValue &value : frame.value("points").toArray()) {
            const QJsonObject object = value.toObject();
            OverlayShape shape;
            shape.type = OverlayShape::Type::Point;
            shape.point = QPointF(object.value("x").toDouble(), object.value("y").toDouble());
            readCommon(object, &shape);
            entry.shapes.append(shape);
        }
        entries_.append(entry);
    }
    std::sort(entries_.begin(), entries_.end(),
              [](const Entry &a, const Entry &b) { return a.time < b.time; });
}

QVector<OverlayShape> OverlayTrack::shapesAt(double seconds) const {
    // The last entry at or before this time.
    auto after = std::upper_bound(entries_.begin(), entries_.end(), seconds,
                                  [](double time, const Entry &entry) { return time < entry.time; });
    if (after == entries_.begin()) return {};
    return std::prev(after)->shapes;
}
