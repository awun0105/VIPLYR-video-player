#include "Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <functional>

namespace Icons {

namespace {

constexpr int kGrid = 24;   // icons are designed on a 24 x 24 grid
constexpr int kScale = 4;   // and rendered larger, for high-DPI screens

QIcon draw(const std::function<void(QPainter &)> &paint) {
    QPixmap pixmap(kGrid * kScale, kGrid * kScale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(kScale, kScale);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    paint(painter);
    return QIcon(pixmap);
}

QPolygonF triangleRight(qreal left, qreal right) {
    return QPolygonF({QPointF(left, 5), QPointF(right, 12), QPointF(left, 19)});
}

QPolygonF triangleLeft(qreal left, qreal right) {
    return QPolygonF({QPointF(right, 5), QPointF(left, 12), QPointF(right, 19)});
}

QPen strokePen(qreal width = 2.0) {
    return QPen(Qt::white, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

void drawSpeaker(QPainter &painter) {
    painter.drawPolygon(QPolygonF({QPointF(3, 9), QPointF(7, 9), QPointF(12, 5), QPointF(12, 19),
                                   QPointF(7, 15), QPointF(3, 15)}));
}

// A sound wave: part of a circle around the speaker.
void drawWave(QPainter &painter, qreal radius) {
    painter.setPen(strokePen());
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(12 - radius, 12 - radius, 2 * radius, 2 * radius), -50 * 16, 100 * 16);
}

}  // namespace

QIcon play() {
    return draw([](QPainter &p) { p.drawPolygon(QPolygonF({QPointF(7, 4), QPointF(20, 12), QPointF(7, 20)})); });
}

QIcon pause() {
    return draw([](QPainter &p) {
        p.drawRoundedRect(QRectF(6, 4, 4.5, 16), 1, 1);
        p.drawRoundedRect(QRectF(13.5, 4, 4.5, 16), 1, 1);
    });
}

QIcon stop() {
    return draw([](QPainter &p) { p.drawRoundedRect(QRectF(6, 6, 12, 12), 1.5, 1.5); });
}

QIcon previous() {
    return draw([](QPainter &p) {
        p.drawRect(QRectF(5, 5, 2.5, 14));
        p.drawPolygon(triangleLeft(9, 19));
    });
}

QIcon next() {
    return draw([](QPainter &p) {
        p.drawPolygon(triangleRight(5, 15));
        p.drawRect(QRectF(16.5, 5, 2.5, 14));
    });
}

QIcon rewind() {
    return draw([](QPainter &p) {
        p.drawPolygon(triangleLeft(2, 11.5));
        p.drawPolygon(triangleLeft(12, 21.5));
    });
}

QIcon forward() {
    return draw([](QPainter &p) {
        p.drawPolygon(triangleRight(2.5, 12));
        p.drawPolygon(triangleRight(12.5, 22));
    });
}

QIcon volumeHigh() {
    return draw([](QPainter &p) {
        drawSpeaker(p);
        drawWave(p, 4.5);
        drawWave(p, 8.5);
    });
}

QIcon volumeLow() {
    return draw([](QPainter &p) {
        drawSpeaker(p);
        drawWave(p, 4.5);
    });
}

QIcon volumeMuted() {
    return draw([](QPainter &p) {
        drawSpeaker(p);
        p.setPen(strokePen());
        p.drawLine(QPointF(15.5, 9), QPointF(21.5, 15));
        p.drawLine(QPointF(21.5, 9), QPointF(15.5, 15));
    });
}

QIcon playlist() {
    return draw([](QPainter &p) {
        p.setPen(strokePen());
        for (qreal y : {6.0, 12.0, 18.0}) p.drawLine(QPointF(4, y), QPointF(20, y));
    });
}

QIcon fullscreen() {
    return draw([](QPainter &p) {
        p.setPen(strokePen());
        p.setBrush(Qt::NoBrush);
        // Four corner brackets.
        p.drawPolyline(QPolygonF({QPointF(4, 9), QPointF(4, 4), QPointF(9, 4)}));
        p.drawPolyline(QPolygonF({QPointF(15, 4), QPointF(20, 4), QPointF(20, 9)}));
        p.drawPolyline(QPolygonF({QPointF(20, 15), QPointF(20, 20), QPointF(15, 20)}));
        p.drawPolyline(QPolygonF({QPointF(9, 20), QPointF(4, 20), QPointF(4, 15)}));
    });
}

QIcon tracks() {
    // A speech bubble: audio and subtitle tracks.
    return draw([](QPainter &p) {
        p.setPen(strokePen());
        p.setBrush(Qt::NoBrush);
        QPainterPath bubble;
        bubble.addRoundedRect(QRectF(3, 4, 18, 12), 3, 3);
        p.drawPath(bubble);
        p.drawPolyline(QPolygonF({QPointF(8, 16), QPointF(8, 20), QPointF(12.5, 16)}));
        p.drawLine(QPointF(7, 8.5), QPointF(17, 8.5));
        p.drawLine(QPointF(7, 12), QPointF(14, 12));
    });
}

QIcon edit() {
    // Scissors.
    return draw([](QPainter &p) {
        p.setPen(strokePen());
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(6.5, 6.5), 2.5, 2.5);
        p.drawEllipse(QPointF(6.5, 17.5), 2.5, 2.5);
        p.drawLine(QPointF(8.5, 8.2), QPointF(20, 18));
        p.drawLine(QPointF(8.5, 15.8), QPointF(20, 6));
    });
}

QIcon snapshot() {
    // A camera.
    return draw([](QPainter &p) {
        p.setPen(strokePen());
        p.setBrush(Qt::NoBrush);
        QPainterPath body;
        body.addRoundedRect(QRectF(3, 7, 18, 13), 2.5, 2.5);
        p.drawPath(body);
        p.drawPolyline(QPolygonF({QPointF(8, 7), QPointF(9.5, 4.5), QPointF(14.5, 4.5), QPointF(16, 7)}));
        p.drawEllipse(QPointF(12, 13.5), 3.5, 3.5);
    });
}

}  // namespace Icons
