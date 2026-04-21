#pragma once
#include <QGraphicsView>
#include <QGraphicsVideoItem>

class VideoCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit VideoCanvas(QWidget *parent = nullptr);
    QGraphicsVideoItem* getVideoItem() const { return videoItem; }

signals:
    void spacePressed();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QGraphicsVideoItem *videoItem;
};