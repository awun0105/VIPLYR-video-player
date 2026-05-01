#pragma once
#include <QObject>
#include <QMediaPlayer>
#include <QMediaPlaylist>
#include <QGraphicsVideoItem>
#include <QStringList>

class MediaEngine : public QObject {
    Q_OBJECT
public:
    explicit MediaEngine(QObject *parent = nullptr);

    void bindVideoOutput(QGraphicsVideoItem *item);
    void loadFiles(const QStringList &files);
    void appendFiles(const QStringList &files);
    int removeAt(int index);

    // Các lệnh điều khiển (API)
    void togglePlayback();
    void stop();
    void next();
    void previous();
    void seekTo(qint64 position);
    void seekRelative(qint64 msOffset);
    void setVolume(int volume);

    QMediaPlayer* getPlayer() const { return player; }
    QMediaPlaylist* getPlaylist() const { return playlist; }

private:
    QMediaPlayer *player;
    QMediaPlaylist *playlist;
};
