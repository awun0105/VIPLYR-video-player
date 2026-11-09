#include "ControlBar.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "JumpSlider.h"
#include "MediaFiles.h"
#include "Theme.h"

ControlBar::ControlBar(QWidget *parent) : QWidget(parent) {
    QPushButton *previousButton = new QPushButton("⏮");
    QPushButton *rewindButton = new QPushButton("⏪ -5s");
    playButton_ = new QPushButton("▶ Play");
    QPushButton *stopButton = new QPushButton("⏹ Stop");
    QPushButton *forwardButton = new QPushButton("+5s ⏩");
    QPushButton *nextButton = new QPushButton("⏭");
    playlistButton_ = new QPushButton("Hide Playlist");

    seekSlider_ = new JumpSlider(Theme::SeekColor);
    timeLabel_ = new QLabel("00:00 / 00:00");
    timeLabel_->setStyleSheet("color: #ccc; font-size: 13px; font-weight: bold; margin: 0 10px; "
                              "font-family: monospace;");

    speedCombo_ = new QComboBox;
    for (qreal rate : {2.0, 1.5, 1.25, 1.0, 0.5}) {
        speedCombo_->addItem(QString::number(rate) + "x", rate);
    }
    speedCombo_->setCurrentIndex(speedCombo_->findData(1.0));
    speedCombo_->setStyleSheet(
        "QComboBox { background-color: #333; color: white; border: none; padding: 2px 5px; "
        "border-radius: 3px; font-weight: bold; } QComboBox::drop-down { border: none; } "
        "QComboBox QAbstractItemView { background-color: #333; color: white; "
        "selection-background-color: #1E90FF; }");

    muteButton_ = new QPushButton("🔊");
    muteButton_->setStyleSheet("background: transparent; border: none; font-size: 18px; "
                               "color: white; padding: 0;");
    volumeSlider_ = new JumpSlider(Theme::VolColor);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(100);
    volumeSlider_->setFixedWidth(100);

    // Top row: the seek bar gets the full width. Bottom row: buttons left, speed and volume right.
    QHBoxLayout *seekRow = new QHBoxLayout;
    seekRow->addWidget(seekSlider_, 1);
    seekRow->addWidget(timeLabel_);

    QHBoxLayout *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(10);
    for (QPushButton *button : {previousButton, rewindButton, playButton_, stopButton, forwardButton,
                                nextButton, playlistButton_}) {
        button->setStyleSheet(Theme::Button);
        button->setFocusPolicy(Qt::NoFocus);  // keep keyboard focus on the video
        buttonRow->addWidget(button);
    }
    buttonRow->addStretch(1);
    buttonRow->addWidget(speedCombo_);
    buttonRow->addWidget(muteButton_);
    buttonRow->addWidget(volumeSlider_);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(15, 8, 15, 10);
    layout->setSpacing(6);
    layout->addLayout(seekRow);
    layout->addLayout(buttonRow);

    connect(playButton_, &QPushButton::clicked, this, &ControlBar::playClicked);
    connect(stopButton, &QPushButton::clicked, this, &ControlBar::stopClicked);
    connect(previousButton, &QPushButton::clicked, this, &ControlBar::previousClicked);
    connect(nextButton, &QPushButton::clicked, this, &ControlBar::nextClicked);
    connect(rewindButton, &QPushButton::clicked, this, &ControlBar::rewindClicked);
    connect(forwardButton, &QPushButton::clicked, this, &ControlBar::forwardClicked);
    connect(playlistButton_, &QPushButton::clicked, this, &ControlBar::playlistToggleRequested);
    connect(muteButton_, &QPushButton::clicked, this, &ControlBar::toggleMute);

    connect(seekSlider_, &QSlider::sliderMoved, this,
            [this](int value) { emit seekRequested(value); });
    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int percent) {
        updateVolumeIcon(percent);
        emit volumeChanged(percent);
    });
    connect(speedCombo_, &QComboBox::currentIndexChanged, this,
            [this](int index) { emit playbackRateChanged(speedCombo_->itemData(index).toDouble()); });
}

int ControlBar::volume() const { return volumeSlider_->value(); }
void ControlBar::setVolume(int percent) { volumeSlider_->setValue(percent); }

void ControlBar::toggleMute() {
    if (volumeSlider_->value() > 0) {
        volumeBeforeMute_ = volumeSlider_->value();
        volumeSlider_->setValue(0);
    } else {
        volumeSlider_->setValue(volumeBeforeMute_ > 0 ? volumeBeforeMute_ : 100);
    }
}

void ControlBar::updateVolumeIcon(int percent) {
    muteButton_->setText(percent == 0 ? "🔇" : percent < 50 ? "🔉" : "🔊");
}

void ControlBar::setPlaying(bool playing) {
    playButton_->setText(playing ? "❚❚ Pause" : "▶ Play");
}

void ControlBar::setPlaylistVisible(bool visible) {
    playlistButton_->setText(visible ? "Hide Playlist" : "Show Playlist");
}

void ControlBar::setPosition(qint64 milliseconds) {
    position_ = milliseconds;
    // While the user drags the handle, the handle follows the mouse, not playback.
    if (!seekSlider_->isSliderDown()) seekSlider_->setValue(static_cast<int>(milliseconds));
    updateTimeLabel();
}

void ControlBar::setDuration(qint64 milliseconds) {
    duration_ = milliseconds;
    seekSlider_->setRange(0, static_cast<int>(milliseconds));
    updateTimeLabel();
}

void ControlBar::updateTimeLabel() {
    timeLabel_->setText(MediaFiles::formatTime(position_) + " / " + MediaFiles::formatTime(duration_));
}
