#include "VideoCanvas.h"
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QResizeEvent>

VideoCanvas::VideoCanvas(QWidget *parent) : QGraphicsView(parent), manualTransformActive(false) {
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
    setMouseTracking(true);

    subtitleLabel = new QLabel(this);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setStyleSheet("color: white; font-size: 22px; font-weight: bold; "
                                 "background-color: rgba(0, 0, 0, 150); border-radius: 5px; padding: 5px 15px;");
    subtitleLabel->setVisible(false);
    subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents); // Don't block clicks

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
    manualTransformActive = false;
}

void VideoCanvas::setSubtitleText(const QString &text) {
    if (text.isEmpty()) {
        subtitleLabel->hide();
    } else {
        subtitleLabel->setText(text);
        subtitleLabel->adjustSize();

        // Center at bottom
        int x = (width() - subtitleLabel->width()) / 2;
        int y = height() - subtitleLabel->height() - 40; // 40px from bottom
        subtitleLabel->move(x, y);
        subtitleLabel->show();
    }
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
    if (!manualTransformActive) fitVideo();

    // Reposition subtitle on resize if visible
    if (subtitleLabel->isVisible()) {
        int x = (width() - subtitleLabel->width()) / 2;
        int y = height() - subtitleLabel->height() - 40;
        subtitleLabel->move(x, y);
    }
}

void VideoCanvas::wheelEvent(QWheelEvent *event) {
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    double scaleFactor = 1.15;
    if (event->angleDelta().y() > 0) scale(scaleFactor, scaleFactor);
    else scale(1.0 / scaleFactor, 1.0 / scaleFactor);
    manualTransformActive = true;
}

void VideoCanvas::rotate() {
    videoItem->setRotation(videoItem->rotation() + 90.0);
    fitVideo();
}
