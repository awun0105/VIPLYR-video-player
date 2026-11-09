#include "PlayerController.h"

#include <algorithm>

namespace {

constexpr int kPositionIntervalMs = 40;
// "Previous" restarts the current video when it has played longer than this.
constexpr qint64 kRestartThresholdMs = 3000;

viplyr::OpenOptions openOptions() {
    viplyr::OpenOptions options;
    // VIPLYR_HWACCEL=none|vaapi|cuda overrides automatic selection, for troubleshooting.
    const QByteArray choice = qgetenv("VIPLYR_HWACCEL").toLower();
    if (choice == "none") options.hwAccel = viplyr::HwAccel::None;
    else if (choice == "vaapi") options.hwAccel = viplyr::HwAccel::Vaapi;
    else if (choice == "cuda") options.hwAccel = viplyr::HwAccel::Cuda;
    return options;
}

std::vector<std::string> toStdStrings(const QStringList &list) {
    std::vector<std::string> result;
    result.reserve(static_cast<size_t>(list.size()));
    for (const QString &item : list) result.push_back(item.toStdString());
    return result;
}

}  // namespace

PlayerController::PlayerController(QObject *parent) : QObject(parent) {
    if (audioOutput_.isAvailable()) player_.setAudioSink(&audioOutput_);

    // Engine callbacks arrive on engine threads; forward them to the GUI thread.
    player_.onStateChanged = [this](viplyr::State state) {
        const bool playing = state == viplyr::State::Playing;
        QMetaObject::invokeMethod(this, [this, playing] { emit playingChanged(playing); },
                                  Qt::QueuedConnection);
    };
    player_.onEnded = [this] {
        QMetaObject::invokeMethod(this, [this] { handleEnded(); }, Qt::QueuedConnection);
    };
    player_.onError = [this](const std::string &message) {
        const QString text = QString::fromStdString(message);
        QMetaObject::invokeMethod(this, [this, text] { emit errorOccurred(text); },
                                  Qt::QueuedConnection);
    };

    positionTimer_.setInterval(kPositionIntervalMs);
    connect(&positionTimer_, &QTimer::timeout, this, &PlayerController::refreshPosition);
    positionTimer_.start();
}

PlayerController::~PlayerController() {
    player_.close();
}

void PlayerController::addVideoSink(viplyr::IVideoSink *sink) {
    player_.addVideoSink(sink);
}

// ----------------------------------------------------------------- playlist

void PlayerController::loadFiles(const QStringList &paths) {
    playlist_.assign(toStdStrings(paths));
    if (playlist_.count() > 0) openCurrent(true);
    else closeMedia();
}

void PlayerController::appendFiles(const QStringList &paths) {
    QStringList fresh;
    for (const QString &path : paths) {
        if (!playlist_.contains(path.toStdString()) && !fresh.contains(path)) fresh.append(path);
    }
    if (fresh.isEmpty()) return;

    const bool wasEmpty = playlist_.count() == 0;
    playlist_.append(toStdStrings(fresh));
    if (wasEmpty) openCurrent(true);
}

void PlayerController::removeAt(int index) {
    const bool wasPlaying = isPlaying();
    const bool removedCurrent = playlist_.removeAt(index);
    if (!removedCurrent) return;

    if (playlist_.count() > 0) openCurrent(wasPlaying);
    else closeMedia();
}

void PlayerController::setCurrentIndex(int index) {
    if (playlist_.setCurrentIndex(index)) openCurrent(true);
}

void PlayerController::next() {
    if (playlist_.next()) openCurrent(true);
}

void PlayerController::previous() {
    if (position() > kRestartThresholdMs || !playlist_.previous()) {
        seekTo(0);
        return;
    }
    openCurrent(true);
}

QStringList PlayerController::paths() const {
    QStringList result;
    for (const std::string &path : playlist_.paths()) result.append(QString::fromStdString(path));
    return result;
}

QString PlayerController::currentPath() const {
    const int index = playlist_.currentIndex();
    return index < 0 ? QString() : QString::fromStdString(playlist_.at(index));
}

void PlayerController::openCurrent(bool startPlaying) {
    const QString path = currentPath();
    std::string error;
    const bool opened = player_.open(path.toStdString(), openOptions(), &error);

    subtitleText_.clear();
    emit subtitleTextChanged(subtitleText_);
    emit currentIndexChanged(playlist_.currentIndex());
    emit durationChanged(duration());

    if (!opened) {
        subtitles_.clear();
        emit mediaClosed();
        emit errorOccurred(QString::fromStdString(error));
        return;
    }
    subtitles_.loadForVideo(path);
    emit mediaOpened(path, player_.info().rotationDegrees);
    if (startPlaying) player_.play();
}

void PlayerController::closeMedia() {
    player_.close();
    subtitles_.clear();
    subtitleText_.clear();
    emit subtitleTextChanged(subtitleText_);
    emit currentIndexChanged(playlist_.currentIndex());
    emit durationChanged(0);
    emit mediaClosed();
}

void PlayerController::handleEnded() {
    // A newer video may have been opened since this was queued.
    if (player_.state() != viplyr::State::Ended) return;
    if (playlist_.next()) openCurrent(true);
}

// ----------------------------------------------------------------- playback

void PlayerController::play() { player_.play(); }
void PlayerController::pause() { player_.pause(); }

void PlayerController::togglePlayback() {
    if (isPlaying()) player_.pause();
    else player_.play();
}

void PlayerController::stop() {
    player_.pause();
    player_.seek(0.0);
}

void PlayerController::seekTo(qint64 milliseconds) {
    player_.seek(milliseconds / 1000.0);
    refreshPosition();
}

void PlayerController::seekRelative(qint64 milliseconds) {
    seekTo(std::clamp(position() + milliseconds, qint64(0), duration()));
}

void PlayerController::stepFrame(int direction) { player_.stepFrame(direction); }

void PlayerController::setVolume(int percent) {
    volume_ = std::clamp(percent, 0, 100);
    audioOutput_.setVolume(volume_ / 100.0);
}

void PlayerController::setPlaybackRate(qreal rate) { player_.setRate(rate); }

qint64 PlayerController::position() const { return qRound64(player_.position() * 1000.0); }
qint64 PlayerController::duration() const { return qRound64(player_.duration() * 1000.0); }

void PlayerController::refreshPosition() {
    const qint64 current = position();
    if (current == lastPosition_) return;
    lastPosition_ = current;
    emit positionChanged(current);

    const QString text = subtitles_.textAt(current);
    if (text != subtitleText_) {
        subtitleText_ = text;
        emit subtitleTextChanged(text);
    }
}
