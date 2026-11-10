#pragma once
#include <QWidget>

class JumpSlider;
class QComboBox;
class QLabel;
class QToolButton;

// The controls drawn over the bottom of the video: seek bar, transport buttons,
// time, speed, volume and the tool buttons.
class ControlBar : public QWidget {
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);

    JumpSlider *seekSlider() const { return seekSlider_; }
    QToolButton *tracksButton() const { return tracksButton_; }  // anchor for the tracks menu
    int volume() const;
    // The level to bring back next time: the slider's, or the one before muting.
    int volumeToRemember() const;
    qreal playbackRate() const;

public slots:
    void setPlaying(bool playing);
    void setPosition(qint64 milliseconds);
    void setDuration(qint64 milliseconds);
    void setVolume(int percent);
    void setPlaybackRate(qreal rate);
    void toggleMute();

signals:
    void playClicked();
    void stopClicked();
    void nextClicked();
    void previousClicked();
    void rewindClicked();
    void forwardClicked();
    void seekRequested(qint64 milliseconds);
    void volumeChanged(int percent);
    void playbackRateChanged(qreal rate);
    void tracksRequested();
    void editRequested();
    void snapshotRequested();
    void playlistToggleRequested();
    void fullscreenRequested();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QToolButton *createButton(const QIcon &icon, const QString &toolTip);
    void updateTimeLabel();
    void updateVolumeIcon(int percent);

    JumpSlider *seekSlider_;
    JumpSlider *volumeSlider_;
    QComboBox *speedCombo_;
    QLabel *timeLabel_;
    QToolButton *playButton_;
    QToolButton *muteButton_;
    QToolButton *tracksButton_;
    qint64 position_ = 0;
    qint64 duration_ = 0;
    int volumeBeforeMute_ = 100;
};
