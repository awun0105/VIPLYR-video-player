#pragma once
#include <QWidget>

class JumpSlider;
class QComboBox;
class QLabel;
class QPushButton;

// The bar under the video: transport buttons, seek bar, time, speed and volume.
class ControlBar : public QWidget {
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);

    JumpSlider *seekSlider() const { return seekSlider_; }
    int volume() const;

public slots:
    void setPlaying(bool playing);
    void setPlaylistVisible(bool visible);
    void setPosition(qint64 milliseconds);
    void setDuration(qint64 milliseconds);
    void setVolume(int percent);
    void toggleMute();

signals:
    void playClicked();
    void stopClicked();
    void nextClicked();
    void previousClicked();
    void rewindClicked();
    void forwardClicked();
    void playlistToggleRequested();
    void seekRequested(qint64 milliseconds);
    void volumeChanged(int percent);
    void playbackRateChanged(qreal rate);

private:
    void updateTimeLabel();
    void updateVolumeIcon(int percent);

    JumpSlider *seekSlider_;
    JumpSlider *volumeSlider_;
    QComboBox *speedCombo_;
    QLabel *timeLabel_;
    QPushButton *playButton_;
    QPushButton *muteButton_;
    QPushButton *playlistButton_;
    qint64 position_ = 0;
    qint64 duration_ = 0;
    int volumeBeforeMute_ = 100;
};
