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

    // Wrapper getters
    qint64 position() const { return player->position(); }
    qint64 duration() const { return player->duration(); }
    int playlistCount() const { return playlist->mediaCount(); }
    int currentIndex() const { return playlist->currentIndex(); }
    QMediaPlayer::State state() const { return player->state(); }
    int volume() const { return player->volume(); }
    QStringList mediaPaths() const;

public slots:
    void play() { player->play(); }
    void pause() { player->pause(); }
    void togglePlayback();
    void stop();
    void next();
    void previous();
    void seekTo(qint64 position);
    void seekRelative(qint64 msOffset);
    void setVolume(int volume);
    void setPlaylistIndex(int index);

signals:
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void stateChanged(QMediaPlayer::State state);
    void playlistIndexChanged(int index);
    void errorOccurred(const QString &errorString);

private slots:
    void handlePlayerError(QMediaPlayer::Error error);

private:
    QMediaPlayer *player;
    QMediaPlaylist *playlist;
};
