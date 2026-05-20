#include "MediaEngine.h"
#include <QFileInfo>
#include <algorithm>
#include "../Utils/Constants.h"

namespace {
void addFilesToPlaylist(QMediaPlaylist *playlist, const QStringList &files) {
    for (const QString &file : files) {
        playlist->addMedia(QUrl::fromLocalFile(file));
    }
}
}

MediaEngine::MediaEngine(QObject *parent) : QObject(parent), lastSubtitleIndex(-1) {
    player = new QMediaPlayer(this);
    playlist = new QMediaPlaylist(this);
    playlist->setPlaybackMode(QMediaPlaylist::Sequential);
    player->setPlaylist(playlist);
    
    thumbnailExtractor = new ThumbnailExtractor(this);

    // Forwarding internal signals
    connect(player, &QMediaPlayer::positionChanged, this, &MediaEngine::positionChanged);
    connect(player, &QMediaPlayer::durationChanged, this, &MediaEngine::durationChanged);
    connect(player, &QMediaPlayer::stateChanged, this, &MediaEngine::stateChanged);
    connect(playlist, &QMediaPlaylist::currentIndexChanged, this, &MediaEngine::playlistIndexChanged);
    connect(player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, &MediaEngine::handlePlayerError);

    // Subtitle synchronization
    connect(player, &QMediaPlayer::positionChanged, this, &MediaEngine::updateSubtitle);
    connect(playlist, &QMediaPlaylist::currentIndexChanged, this, &MediaEngine::loadSubtitlesForCurrent);
}

void MediaEngine::bindVideoOutput(QGraphicsVideoItem *item) {
    player->setVideoOutput(item);
}

void MediaEngine::loadFiles(const QStringList &files) {
    playlist->clear();
    addFilesToPlaylist(playlist, files);

    if (playlist->mediaCount() > 0) {
        playlist->setCurrentIndex(0);
        loadSubtitlesForCurrent(); // Manual trigger for first file
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
        loadSubtitlesForCurrent();
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
        currentSubtitles.clear();
        emit subtitleTextChanged("");
        return -1;
    }

    if (removingCurrent) {
        int nextIndex = index < playlist->mediaCount() ? index : playlist->mediaCount() - 1;
        playlist->setCurrentIndex(nextIndex);
        loadSubtitlesForCurrent();
        if (wasPlaying) player->play();
        return nextIndex;
    }

    if (index < currentIndex) currentIndex--;
    return currentIndex;
}

QStringList MediaEngine::mediaPaths() const {
    QStringList paths;
    for (int i = 0; i < playlist->mediaCount(); ++i) {
        paths.append(playlist->media(i).request().url().toLocalFile());
    }
    return paths;
}

void MediaEngine::togglePlayback() {
    if (player->state() == QMediaPlayer::PlayingState) player->pause();
    else player->play();
}

void MediaEngine::stop() { player->stop(); }
void MediaEngine::next() { playlist->next(); }
void MediaEngine::previous() { playlist->previous(); }
void MediaEngine::seekTo(qint64 position) { 
    player->setPosition(position); 
    lastSubtitleIndex = -1; // Force re-scan on seek
}
void MediaEngine::seekRelative(qint64 msOffset) {
    qint64 newPos = qBound(0LL, player->position() + msOffset, player->duration());
    player->setPosition(newPos);
    lastSubtitleIndex = -1;
}
void MediaEngine::setVolume(int volume) { player->setVolume(volume); }
void MediaEngine::setPlaybackRate(qreal rate) { player->setPlaybackRate(rate); }
void MediaEngine::setPlaylistIndex(int index) { playlist->setCurrentIndex(index); }

void MediaEngine::handlePlayerError(QMediaPlayer::Error error) {
    if (error == QMediaPlayer::NoError) return;
    emit errorOccurred(player->errorString());
}

void MediaEngine::updateSubtitle(qint64 position) {
    if (currentSubtitles.isEmpty()) return;

    // Fast check: is current subtitle still valid?
    if (lastSubtitleIndex >= 0 && lastSubtitleIndex < currentSubtitles.size()) {
        const auto &sub = currentSubtitles[lastSubtitleIndex];
        if (position >= sub.startTime && position <= sub.endTime) return;
    }

    // Binary search for the first subtitle with startTime > position
    auto it = std::upper_bound(currentSubtitles.begin(), currentSubtitles.end(), position, 
        [](qint64 pos, const SubtitleItem &item) {
            return pos < item.startTime;
        });

    int newIndex = -1;
    // The relevant subtitle could be the one right before 'it'
    if (it != currentSubtitles.begin()) {
        auto prev = std::prev(it);
        if (position >= prev->startTime && position <= prev->endTime) {
            newIndex = std::distance(currentSubtitles.begin(), prev);
        }
    }

    if (newIndex != lastSubtitleIndex) {
        lastSubtitleIndex = newIndex;
        emit subtitleTextChanged(newIndex >= 0 ? currentSubtitles[newIndex].text : "");
    }
}

void MediaEngine::loadSubtitlesForCurrent() {
    currentSubtitles.clear();
    lastSubtitleIndex = -1;
    emit subtitleTextChanged("");

    int idx = playlist->currentIndex();
    if (idx < 0) return;

    QString videoPath = playlist->media(idx).request().url().toLocalFile();
    thumbnailExtractor->setSource(videoPath);
    
    QFileInfo info(videoPath);
    QString srtPath = info.path() + "/" + info.completeBaseName() + ".srt";

    if (QFile::exists(srtPath)) {
        currentSubtitles = SubtitleParser::parse(srtPath);
    }
}
