#pragma once
#include <QGraphicsView>
#include <QGraphicsVideoItem>
#include <QLabel>

class VideoCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit VideoCanvas(QWidget *parent = nullptr);
    QGraphicsVideoItem* getVideoItem() const { return videoItem; }
    void fitVideo();

public slots:
    void rotate();
    void setSubtitleText(const QString &text);

signals:
    void spacePressed();
    void fullscreenToggleRequested();
    void exitFullscreenRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QGraphicsVideoItem *videoItem;
    QLabel *subtitleLabel;
};
