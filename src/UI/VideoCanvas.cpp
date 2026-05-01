#include "VideoCanvas.h"
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>

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
    setFocusPolicy(Qt::StrongFocus);

    connect(videoItem, &QGraphicsVideoItem::nativeSizeChanged, this, [this](const QSizeF &size){
        videoItem->setSize(size);
        fitVideo();
    });
}

void VideoCanvas::fitVideo() {
    if (videoItem->boundingRect().isEmpty()) return;

    setSceneRect(videoItem->sceneBoundingRect());
    resetTransform();
    fitInView(videoItem, Qt::KeepAspectRatio);
}

void VideoCanvas::mousePressEvent(QMouseEvent *event) {
    setFocus();
    QGraphicsView::mousePressEvent(event);
}

void VideoCanvas::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) emit fullscreenToggleRequested();
    QGraphicsView::mouseDoubleClickEvent(event);
}

void VideoCanvas::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    fitVideo();
}

void VideoCanvas::wheelEvent(QWheelEvent *event) {
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    double scaleFactor = 1.15;
    if (event->angleDelta().y() > 0) scale(scaleFactor, scaleFactor);
    else scale(1.0 / scaleFactor, 1.0 / scaleFactor);
}

void VideoCanvas::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_R) {
        videoItem->setRotation(videoItem->rotation() + 90.0);
        fitVideo();
    }
    else if (event->key() == Qt::Key_Space) emit spacePressed();
    else if (event->key() == Qt::Key_Escape) emit exitFullscreenRequested();
    else QGraphicsView::keyPressEvent(event);
}
