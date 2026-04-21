#ifndef PLAYERVIEW_H
#define PLAYERVIEW_H

#include <QGraphicsView>
#include <QMediaPlayer>
#include <QGraphicsVideoItem>

class QWheelEvent;
class QKeyEvent;

class PlayerView : public QGraphicsView {
    Q_OBJECT

public:
    explicit PlayerView(QWidget *parent = nullptr);
    QMediaPlayer *player;

protected:
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QGraphicsVideoItem *videoItem;
};

#endif // PLAYERVIEW_H