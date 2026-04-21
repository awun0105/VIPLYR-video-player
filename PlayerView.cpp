#include "PlayerView.h"
#include <QGraphicsScene>
#include <QWheelEvent>
#include <QKeyEvent>

PlayerView::PlayerView(QWidget *parent) : QGraphicsView(parent) {
    QGraphicsScene *scene = new QGraphicsScene(this);
    setScene(scene);

    videoItem = new QGraphicsVideoItem;
    scene->addItem(videoItem);

    player = new QMediaPlayer(this);
    player->setVideoOutput(videoItem);

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

void PlayerView::wheelEvent(QWheelEvent *event) {
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    double scaleFactor = 1.15;
    if (event->angleDelta().y() > 0) scale(scaleFactor, scaleFactor);
    else scale(1.0 / scaleFactor, 1.0 / scaleFactor);
}

void PlayerView::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
        case Qt::Key_R:
            rotate(90);
            break;
        case Qt::Key_Space:
            if (player->state() == QMediaPlayer::PlayingState) player->pause();
            else player->play();
            break;
        default:
            QGraphicsView::keyPressEvent(event);
    }
}