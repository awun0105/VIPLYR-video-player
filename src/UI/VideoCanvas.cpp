#include "VideoCanvas.h"
#include <QGraphicsScene>
#include <QWheelEvent>
#include <QKeyEvent>

VideoCanvas::VideoCanvas(QWidget *parent) : QGraphicsView(parent) {
    QGraphicsScene *scene = new QGraphicsScene(this);
    setScene(scene);

    videoItem = new QGraphicsVideoItem;
    scene->addItem(videoItem);

    setDragMode(QGraphicsView::ScrollHandDrag);
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(Qt::black);

    connect(videoItem, &QGraphicsVideoItem::nativeSizeChanged, this, [this](const QSizeF &size){
        videoItem->setSize(size);
        setSceneRect(QRectF(QPointF(0,0), size));
        fitInView(videoItem, Qt::KeepAspectRatio);
    });
}

void VideoCanvas::wheelEvent(QWheelEvent *event) {
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    double scaleFactor = 1.15;
    if (event->angleDelta().y() > 0) scale(scaleFactor, scaleFactor);
    else scale(1.0 / scaleFactor, 1.0 / scaleFactor);
}

void VideoCanvas::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_R) rotate(90);
    else if (event->key() == Qt::Key_Space) emit spacePressed();
    else QGraphicsView::keyPressEvent(event);
}