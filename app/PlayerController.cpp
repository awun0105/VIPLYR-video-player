#include "PlayerController.h"

#include <QCryptographicHash>
#include <QFile>
#include <QSettings>
#include <QtConcurrent>
#include <algorithm>

namespace {

constexpr int kPositionIntervalMs = 40;
// "Previous" restarts the current video when it has played longer than this.
constexpr qint64 kRestartThresholdMs = 3000;
// A position is only worth remembering away from the very start and the very end.
constexpr qint64 kResumeMinimumMs = 5000;
constexpr qint64 kResumeEndMarginMs = 10000;

viplyr::OpenOptions openOptions(int audioStreamIndex, viplyr::HwAccel gpuFramePath) {
    viplyr::OpenOptions options;
    options.audioStreamIndex = audioStreamIndex;
    // VIPLYR_HWACCEL=none|vaapi|cuda overrides automatic selection, for troubleshooting;
    // those frames are copied to the CPU.
    const QByteArray choice = qgetenv("VIPLYR_HWACCEL").toLower();
    if (choice == "none") options.hwAccel = viplyr::HwAccel::None;
    else if (choice == "vaapi") options.hwAccel = viplyr::HwAccel::Vaapi;
    else if (choice == "cuda") options.hwAccel = viplyr::HwAccel::Cuda;
    else if (gpuFramePath != viplyr::HwAccel::None) {
        // Decoding on the GPU only pays off when frames stay there until shown.
        options.hwAccel = gpuFramePath;
        options.keepHwFrames = true;
    }
    return options;
}

std::vector<std::string> toStdStrings(const QStringList &list) {
    std::vector<std::string> result;
    result.reserve(static_cast<size_t>(list.size()));
    for (const QString &item : list) result.push_back(item.toStdString());
    return result;
}

QString resumeKey(const QString &path) {
    const QByteArray hash = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1);
    return "resume/" + QString::fromLatin1(hash.toHex());
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
    cancelSubtitleLoading();
    waitForOpenTask();
    rememberPosition();
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
    const bool wasPlaying = isPlaying() || isOpening_;
    const bool removedCurrent = playlist_.removeAt(index);
    if (!removedCurrent) return;

    if (playlist_.count() > 0) openCurrent(wasPlaying);
    else closeMedia();
}

void PlayerController::moveItem(int from, int to) {
    if (playlist_.move(from, to)) emit currentIndexChanged(playlist_.currentIndex());
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

// ------------------------------------------------------------------ opening

void PlayerController::openCurrent(bool startPlaying, qint64 startPositionMs, int audioStreamIndex) {
    rememberPosition();  // of the video that is about to be replaced
    cancelSubtitleLoading();
    if (currentPath() != openPath_) {  // reopening the same video keeps these
        clearLoop();
        emit pictureSubtitlesChanged(nullptr);
    }

    pendingRequest_ = {currentPath(), startPlaying, startPositionMs, audioStreamIndex};
    isOpening_ = true;
    openPath_.clear();
    setSubtitleText(QString());
    emit currentIndexChanged(playlist_.currentIndex());

    // The engine is used by one thread at a time: a request that arrives while
    // another is being opened waits, and replaces any request already waiting.
    if (openTaskRunning_) hasQueuedRequest_ = true;
    else startOpenTask();
}

void PlayerController::startOpenTask() {
    const OpenRequest request = pendingRequest_;
    openTaskRunning_ = true;
    auto result = std::make_shared<OpenResult>();

    const int generation = openGeneration_;
    const viplyr::OpenOptions options = openOptions(request.audioStreamIndex, gpuFramePath_);

    openTask_ = QtConcurrent::run([this, request, result, options] {
        result->ok = player_.open(request.path.toStdString(), options, &result->error);
    });
    // Runs on the GUI thread once the worker is done.
    openTask_.then(this, [this, request, result, generation] {
        if (generation != openGeneration_) return;  // closeMedia() already dealt with this task
        openTaskRunning_ = false;
        if (hasQueuedRequest_) {
            hasQueuedRequest_ = false;
            startOpenTask();
            return;
        }
        isOpening_ = false;
        finishOpen(request, *result);
    });
}

void PlayerController::finishOpen(const OpenRequest &request, const OpenResult &result) {
    if (!result.ok) {
        subtitles_.clear();
        emit durationChanged(0);
        emit mediaClosed();
        emit errorOccurred(QString::fromStdString(result.error));
        return;
    }

    openPath_ = request.path;  // from here on hasMedia() is true
    emit durationChanged(duration());
    player_.setRate(rate_);
    setSubtitleTrack(hasExternalSubtitles() ? kSubtitlesExternalFile : kSubtitlesOff);

    qint64 startMs = request.startPositionMs;
    const bool resuming = startMs < 0;
    if (resuming) startMs = rememberedPosition(request.path);
    if (startMs > 0) player_.seek(startMs / 1000.0);

    emit mediaOpened(request.path);
    if (resuming && startMs > 0) emit resumed(startMs);
    if (request.startPlaying) player_.play();
}

void PlayerController::waitForOpenTask() {
    hasQueuedRequest_ = false;
    if (openTaskRunning_) openTask_.waitForFinished();
    ++openGeneration_;  // its completion is still queued; make it do nothing
    openTaskRunning_ = false;
    isOpening_ = false;
}

void PlayerController::closeMedia() {
    clearLoop();
    cancelSubtitleLoading();
    waitForOpenTask();
    rememberPosition();
    player_.close();
    openPath_.clear();
    subtitles_.clear();
    setSubtitleText(QString());
    emit pictureSubtitlesChanged(nullptr);
    emit currentIndexChanged(playlist_.currentIndex());
    emit durationChanged(0);
    emit mediaClosed();
}

void PlayerController::handleEnded() {
    if (isOpening_ || player_.state() != viplyr::State::Ended) return;  // a newer video took over
    if (loopEndMs_ >= 0) {  // B at the very end of the video
        seekTo(loopStartMs_);
        play();
        return;
    }
    QSettings().remove(resumeKey(openPath_));  // watched to the end: start over next time
    if (playlist_.next()) openCurrent(true);
}

// ----------------------------------------------------------- resume position

void PlayerController::rememberPosition() {
    if (!hasMedia()) return;
    const qint64 current = position();
    const qint64 total = duration();
    QSettings settings;
    if (current > kResumeMinimumMs && current < total - kResumeEndMarginMs) {
        settings.setValue(resumeKey(openPath_), current);
    } else {
        settings.remove(resumeKey(openPath_));
    }
}

qint64 PlayerController::rememberedPosition(const QString &path) const {
    return QSettings().value(resumeKey(path), 0).toLongLong();
}

// ------------------------------------------------------------------- tracks

void PlayerController::setGpuFramePath(viplyr::HwAccel decoder) {
    if (decoder == gpuFramePath_) return;
    gpuFramePath_ = decoder;
    if (decoder != viplyr::HwAccel::None) return;
    qWarning("Viplyr: the screen could not take a GPU frame; decoding on the CPU");
    // A file already playing on the GPU path that the screen cannot take is reopened on the CPU.
    if (hasMedia() && !player_.info().hardwareDecoder.empty()) {
        openCurrent(isPlaying(), std::max<qint64>(position(), 1), player_.info().audioStreamIndex);
    }
}

void PlayerController::seekChapter(int direction) {
    if (!hasMedia()) return;
    const std::vector<viplyr::Chapter> &chapters = player_.info().chapters;
    if (chapters.empty()) return;
    const double now = position() / 1000.0;
    int current = 0;  // the chapter containing `now`
    for (size_t i = 0; i < chapters.size(); ++i) {
        if (chapters[i].start <= now + 0.01) current = static_cast<int>(i);
    }
    int target = current + direction;
    if (direction < 0 && now - chapters[current].start > kRestartThresholdMs / 1000.0) target = current;
    if (target < 0 || target >= static_cast<int>(chapters.size())) return;
    seekTo(static_cast<qint64>(chapters[target].start * 1000.0));
}

void PlayerController::cycleLoop() {
    if (!hasMedia()) return;
    const qint64 current = position();
    if (loopStartMs_ < 0) {
        loopStartMs_ = current;
    } else if (loopEndMs_ < 0 && current > loopStartMs_) {
        loopEndMs_ = current;
    } else {
        clearLoop();
        return;
    }
    emit loopChanged(loopStartMs_, loopEndMs_);
}

void PlayerController::clearLoop() {
    if (loopStartMs_ < 0) return;
    loopStartMs_ = loopEndMs_ = -1;
    emit loopChanged(-1, -1);
}

void PlayerController::setAudioTrack(int streamIndex) {
    if (!hasMedia() || streamIndex == player_.info().audioStreamIndex) return;
    // The engine chooses its audio stream when it opens, so reopen at the same place.
    const bool wasPlaying = isPlaying();
    const qint64 current = position();
    openCurrent(wasPlaying, std::max<qint64>(current, 1), streamIndex);
}

bool PlayerController::hasExternalSubtitles() const {
    return hasMedia() && QFile::exists(SubtitleTrack::externalFileFor(openPath_));
}

void PlayerController::setSubtitleTrack(int track) {
    if (!hasMedia()) return;
    cancelSubtitleLoading();
    subtitleTrack_ = track;
    subtitles_.clear();
    setSubtitleText(QString());
    emit pictureSubtitlesChanged(nullptr);

    if (track == kSubtitlesOff) return;
    if (track == kSubtitlesExternalFile) {
        subtitles_.loadForVideo(openPath_);
        return;
    }

    // An embedded track means scanning the whole file, so it runs on a worker thread.
    const QString path = openPath_;
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    subtitleLoadCancelled_ = cancelled;
    QtConcurrent::run([path, track, cancelled] {
        return std::make_shared<const viplyr::SubtitleTrackData>(
            viplyr::readSubtitleTrack(path.toStdString(), track, cancelled.get()));
    }).then(this, [this, cancelled](std::shared_ptr<const viplyr::SubtitleTrackData> data) {
        if (cancelled->load()) return;  // another track or video was chosen meanwhile
        // Styled and bitmap tracks are drawn over the picture; plain text goes to the label.
        if (data->isAss || data->isBitmap) emit pictureSubtitlesChanged(data);
        else subtitles_.setCues(data->cues);
    });
}

void PlayerController::cancelSubtitleLoading() {
    if (subtitleLoadCancelled_) subtitleLoadCancelled_->store(true);
    subtitleLoadCancelled_.reset();
}

void PlayerController::setSubtitleText(const QString &text) {
    if (text == subtitleText_) return;
    subtitleText_ = text;
    emit subtitleTextChanged(text);
}

// ----------------------------------------------------------------- playback

bool PlayerController::hasMedia() const { return !isOpening_ && !openPath_.isEmpty(); }
bool PlayerController::isPlaying() const { return hasMedia() && player_.state() == viplyr::State::Playing; }
viplyr::MediaInfo PlayerController::mediaInfo() const { return hasMedia() ? player_.info() : viplyr::MediaInfo(); }

void PlayerController::play() { if (hasMedia()) player_.play(); }
void PlayerController::pause() { if (hasMedia()) player_.pause(); }

void PlayerController::togglePlayback() {
    if (!hasMedia()) return;
    if (isPlaying()) player_.pause();
    else player_.play();
}

void PlayerController::stop() {
    if (!hasMedia()) return;
    player_.pause();
    player_.seek(0.0);
}

void PlayerController::seekTo(qint64 milliseconds) {
    if (!hasMedia()) return;
    player_.seek(milliseconds / 1000.0);
    refreshPosition();
}

void PlayerController::seekRelative(qint64 milliseconds) {
    seekTo(std::clamp(position() + milliseconds, qint64(0), duration()));
}

void PlayerController::stepFrame(int direction) { if (hasMedia()) player_.stepFrame(direction); }

void PlayerController::goToFrame(qint64 frameNumber) {
    if (!hasMedia() || player_.info().frameRate <= 0.0) return;
    // Aim at the middle of the frame so rounding cannot land on its neighbour.
    player_.seek((frameNumber + 0.5) / player_.info().frameRate);
    refreshPosition();
}

void PlayerController::setVolume(int percent) {
    volume_ = std::clamp(percent, 0, 100);
    audioOutput_.setVolume(volume_ / 100.0);
}

void PlayerController::setPlaybackRate(qreal rate) {
    rate_ = rate;
    if (hasMedia()) player_.setRate(rate);
}

qint64 PlayerController::position() const { return hasMedia() ? qRound64(player_.position() * 1000.0) : 0; }
qint64 PlayerController::duration() const { return hasMedia() ? qRound64(player_.duration() * 1000.0) : 0; }

void PlayerController::refreshPosition() {
    const qint64 current = position();
    if (loopEndMs_ >= 0 && current >= loopEndMs_ && isPlaying()) {
        seekTo(loopStartMs_);
        return;
    }
    if (current == lastPosition_) return;
    lastPosition_ = current;
    emit positionChanged(current);
    setSubtitleText(subtitles_.textAt(current));
}
