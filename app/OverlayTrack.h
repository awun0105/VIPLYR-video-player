#pragma once
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

// One thing drawn on top of the video, in video pixel coordinates.
struct OverlayShape {
    enum class Type { Box, Point };
    Type type = Type::Box;
    QRectF box;       // Type::Box
    QPointF point;    // Type::Point
    QString label;    // optional text drawn next to the shape
    QColor color = QColor(0, 230, 118);
};

// Time-stamped annotations for one video, for example detections from a
// computer-vision model. Loaded from "<video name>.overlay.json":
//
//   { "frames": [ { "t": 1.25,
//                   "boxes":  [ { "x": 10, "y": 20, "w": 100, "h": 50, "label": "car", "color": "#ff0000" } ],
//                   "points": [ { "x": 60, "y": 45, "label": "centre" } ] } ] }
//
// "t" is in seconds. An entry stays on screen until the next entry's time.
class OverlayTrack {
public:
    // Loads the overlay file next to the video; leaves the track empty when there is none.
    void loadForVideo(const QString &videoPath);
    void clear() { entries_.clear(); }
    bool isEmpty() const { return entries_.isEmpty(); }

    QVector<OverlayShape> shapesAt(double seconds) const;

private:
    struct Entry {
        double time;
        QVector<OverlayShape> shapes;
    };
    QVector<Entry> entries_;  // sorted by time
};
