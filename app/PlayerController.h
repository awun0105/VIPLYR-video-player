#pragma once
#include <QFuture>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <memory>

#include "AudioOutput.h"
#include "SubtitleTrack.h"
#include "viplyr/Player.h"
#include "viplyr/Playlist.h"

// The application's playback model: one engine, one playlist and the subtitles
// of the current video. The UI talks only to this class, in milliseconds.
//
// Videos are opened on a worker thread so the window never freezes. While one
// is opening, playback commands are ignored and position() reports 0.
class PlayerController : public QObject {
    Q_OBJECT
public:
    // Special values for setSubtitleTrack(); other values are stream indexes.
    static constexpr int kSubtitlesOff = -2;
    static constexpr int kSubtitlesExternalFile = -1;

    explicit PlayerController(QObject *parent = nullptr);
    ~PlayerController() override;

    // Must be called before the first video is loaded.
    void addVideoSink(viplyr::IVideoSink *sink);

    void loadFiles(const QStringList &paths);    // replaces the playlist and plays
    void appendFiles(const QStringList &paths);  // skips paths already in the playlist
    void removeAt(int index);
    void moveItem(int from, int to);

    QStringList paths() const;
    int count() const { return playlist_.count(); }
    int currentIndex() const { return playlist_.currentIndex(); }
    QString currentPath() const;

    bool hasMedia() const;
    viplyr::MediaInfo mediaInfo() const;  // of the open video; default-constructed otherwise
    qint64 position() const;
    qint64 duration() const;
    bool isPlaying() const;
    int volume() const { return volume_; }
    qreal playbackRate() const { return rate_; }
    int subtitleTrack() const { return subtitleTrack_; }
    bool hasExternalSubtitles() const;

public slots:
    void play();
    void pause();
    void togglePlayback();
    void stop();
    void next();
    void previous();
    void seekTo(qint64 milliseconds);
    void seekRelative(qint64 milliseconds);
    void stepFrame(int direction);
    void goToFrame(qint64 frameNumber);
    void setVolume(int percent);
    void setPlaybackRate(qreal rate);
    void setCurrentIndex(int index);
    void setAudioTrack(int streamIndex);
    // Next (+1) or previous (-1) chapter; "previous" restarts the current one first
    // when it has played for a few seconds.
    void seekChapter(int direction);
    // A-B loop, one key: the first call sets A at the current position, the second sets B
    // and starts looping, the third clears it.
    void cycleLoop();
    // The GPU decoder whose frames the screen shows directly (None: decode on the CPU).
    void setGpuFramePath(viplyr::HwAccel decoder);
    void setSubtitleTrack(int track);  // kSubtitlesOff, kSubtitlesExternalFile or a stream index

signals:
    void positionChanged(qint64 milliseconds);
    void durationChanged(qint64 milliseconds);
    void playingChanged(bool playing);
    void currentIndexChanged(int index);
    void mediaOpened(const QString &path);
    void mediaClosed();
    void resumed(qint64 milliseconds);  // playback continued from a remembered position
    void subtitleTextChanged(const QString &text);
    // Styled (ASS) or bitmap subtitles to draw over the picture; null when there are none.
    void pictureSubtitlesChanged(std::shared_ptr<const viplyr::SubtitleTrackData> track);
    void errorOccurred(const QString &message);
    void loopChanged(qint64 startMs, qint64 endMs);  // -1 for a point not set

private:
    struct OpenRequest {
        QString path;
        bool startPlaying = true;
        qint64 startPositionMs = -1;  // -1: resume from the remembered position, if any
        int audioStreamIndex = -1;
    };
    struct OpenResult {
        bool ok = false;
        std::string error;
    };

    void openCurrent(bool startPlaying, qint64 startPositionMs = -1, int audioStreamIndex = -1);
    void startOpenTask();
    void finishOpen(const OpenRequest &request, const OpenResult &result);
    void closeMedia();
    void waitForOpenTask();

    void refreshPosition();
    void handleEnded();
    void setSubtitleText(const QString &text);
    void cancelSubtitleLoading();

    void rememberPosition();
    void clearLoop();
    qint64 rememberedPosition(const QString &path) const;

    viplyr::Player player_;
    viplyr::Playlist playlist_;
    AudioOutput audioOutput_;
    SubtitleTrack subtitles_;
    QTimer positionTimer_;

    // Opening happens on a worker thread; only the newest request is carried out.
    QFuture<void> openTask_;
    OpenRequest pendingRequest_;
    bool isOpening_ = false;
    bool openTaskRunning_ = false;
    bool hasQueuedRequest_ = false;
    int openGeneration_ = 0;  // bumped to make an already queued completion do nothing

    QString openPath_;  // the video the engine has open; empty when none
    QString subtitleText_;
    int subtitleTrack_ = kSubtitlesOff;
    std::shared_ptr<std::atomic<bool>> subtitleLoadCancelled_;
    qint64 lastPosition_ = -1;
    int volume_ = 100;
    viplyr::HwAccel gpuFramePath_ = viplyr::HwAccel::None;
    qint64 loopStartMs_ = -1;
    qint64 loopEndMs_ = -1;
    qreal rate_ = 1.0;
};
