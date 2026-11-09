#pragma once
#include <QObject>
#include <QStringList>
#include <QTimer>

#include "AudioOutput.h"
#include "SubtitleTrack.h"
#include "viplyr/Player.h"
#include "viplyr/Playlist.h"

// The application's playback model: one engine, one playlist and the subtitles
// of the current video. The UI talks only to this class, in milliseconds.
class PlayerController : public QObject {
    Q_OBJECT
public:
    explicit PlayerController(QObject *parent = nullptr);
    ~PlayerController() override;

    // Must be called before the first video is loaded.
    void addVideoSink(viplyr::IVideoSink *sink);

    void loadFiles(const QStringList &paths);    // replaces the playlist and plays
    void appendFiles(const QStringList &paths);  // skips paths already in the playlist
    void removeAt(int index);

    QStringList paths() const;
    int count() const { return playlist_.count(); }
    int currentIndex() const { return playlist_.currentIndex(); }
    QString currentPath() const;
    qint64 position() const;
    qint64 duration() const;
    bool isPlaying() const { return player_.state() == viplyr::State::Playing; }
    int volume() const { return volume_; }

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
    void setVolume(int percent);
    void setPlaybackRate(qreal rate);
    void setCurrentIndex(int index);

signals:
    void positionChanged(qint64 milliseconds);
    void durationChanged(qint64 milliseconds);
    void playingChanged(bool playing);
    void currentIndexChanged(int index);
    void mediaOpened(const QString &path, int rotationDegrees);
    void mediaClosed();
    void subtitleTextChanged(const QString &text);
    void errorOccurred(const QString &message);

private:
    void openCurrent(bool startPlaying);
    void closeMedia();
    void refreshPosition();
    void handleEnded();

    viplyr::Player player_;
    viplyr::Playlist playlist_;
    AudioOutput audioOutput_;
    SubtitleTrack subtitles_;
    QTimer positionTimer_;
    QString subtitleText_;
    qint64 lastPosition_ = -1;
    int volume_ = 100;
};
