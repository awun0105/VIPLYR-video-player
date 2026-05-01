#include "MediaEngine.h"

namespace {
void addFilesToPlaylist(QMediaPlaylist *playlist, const QStringList &files) {
    for (const QString &file : files) {
        playlist->addMedia(QUrl::fromLocalFile(file));
    }
}
}

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
    addFilesToPlaylist(playlist, files);

    if (playlist->mediaCount() > 0) {
        playlist->setCurrentIndex(0);
        player->play();
    } else {
        player->stop();
    }
}

void MediaEngine::appendFiles(const QStringList &files) {
    int previousCount = playlist->mediaCount();
    addFilesToPlaylist(playlist, files);

    if (previousCount == 0 && playlist->mediaCount() > 0) {
        playlist->setCurrentIndex(0);
        player->play();
    }
}

int MediaEngine::removeAt(int index) {
    int mediaCount = playlist->mediaCount();
    if (index < 0 || index >= mediaCount) return playlist->currentIndex();

    int currentIndex = playlist->currentIndex();
    bool removingCurrent = index == currentIndex;
    bool wasPlaying = player->state() == QMediaPlayer::PlayingState;

    playlist->removeMedia(index);

    if (playlist->mediaCount() == 0) {
        player->stop();
        return -1;
    }

    if (removingCurrent) {
        int nextIndex = index < playlist->mediaCount() ? index : playlist->mediaCount() - 1;
        playlist->setCurrentIndex(nextIndex);
        if (wasPlaying) player->play();
        return nextIndex;
    }

    if (index < currentIndex) currentIndex--;
    return currentIndex;
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
