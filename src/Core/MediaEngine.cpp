#include "MediaEngine.h"

MediaEngine::MediaEngine(QObject *parent) : QObject(parent) {
    player = new QMediaPlayer(this);
    playlist = new QMediaPlaylist(this);
    playlist->setPlaybackMode(QMediaPlaylist::Sequential);
    player->setPlaylist(playlist);
}

void MediaEngine::bindVideoOutput(QGraphicsVideoItem *item) {
    player->setVideoOutput(item);
}

void MediaEngine::loadFiles(const QStringList &files) {
    playlist->clear();
    for(const QString &file : files) {
        playlist->addMedia(QUrl::fromLocalFile(file));
    }
    player->play();
}

void MediaEngine::togglePlayback() {
    if (player->state() == QMediaPlayer::PlayingState) player->pause();
    else player->play();
}

void MediaEngine::stop() { player->stop(); }
void MediaEngine::next() { playlist->next(); }
void MediaEngine::previous() { playlist->previous(); }
void MediaEngine::seekTo(qint64 position) { player->setPosition(position); }
void MediaEngine::seekRelative(qint64 msOffset) {
    qint64 newPos = qBound(0LL, player->position() + msOffset, player->duration());
    player->setPosition(newPos);
}
void MediaEngine::setVolume(int volume) { player->setVolume(volume); }