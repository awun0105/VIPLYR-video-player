#include "ControlBar.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

#include "Icons.h"
#include "JumpSlider.h"
#include "MediaFiles.h"
#include "Theme.h"

namespace {
constexpr int kIconSize = 22;
constexpr int kPlayIconSize = 28;
}

ControlBar::ControlBar(QWidget *parent) : QWidget(parent) {
    seekSlider_ = new JumpSlider(Theme::SeekColor);
    timeLabel_ = new QLabel("00:00 / 00:00");
    timeLabel_->setStyleSheet("color: #e6e6ea; font-size: 12px; font-family: monospace; background: transparent;");

    QToolButton *previousButton = createButton(Icons::previous(), "Previous");
    QToolButton *rewindButton = createButton(Icons::rewind(), "Back 5 seconds (Left)");
    playButton_ = createButton(Icons::play(), "Play / Pause (Space)");
    playButton_->setIconSize(QSize(kPlayIconSize, kPlayIconSize));
    QToolButton *forwardButton = createButton(Icons::forward(), "Forward 5 seconds (Right)");
    QToolButton *nextButton = createButton(Icons::next(), "Next");
    QToolButton *stopButton = createButton(Icons::stop(), "Stop");

    muteButton_ = createButton(Icons::volumeHigh(), "Mute (M)");
    volumeSlider_ = new JumpSlider(Theme::VolColor);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(100);
    volumeSlider_->setFixedWidth(90);

    speedCombo_ = new QComboBox;
    for (qreal rate : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0}) {
        speedCombo_->addItem(QString::number(rate) + "x", rate);
    }
    speedCombo_->setCurrentIndex(speedCombo_->findData(1.0));
    speedCombo_->setToolTip("Playback speed");
    speedCombo_->setFocusPolicy(Qt::NoFocus);
    speedCombo_->setStyleSheet(
        "QComboBox { background-color: rgba(255, 255, 255, 30); color: white; border: none; "
        "padding: 4px 8px; border-radius: 6px; font-weight: bold; } "
        "QComboBox::drop-down { border: none; width: 0; } "
        "QComboBox QAbstractItemView { background-color: #24242a; color: white; "
        "selection-background-color: #3D9BFF; outline: none; }");

    tracksButton_ = createButton(Icons::tracks(), "Audio and subtitles");
    QToolButton *editButton = createButton(Icons::edit(), "Edit and export (E)");
    QToolButton *snapshotButton = createButton(Icons::snapshot(), "Save this frame as an image (S)");
    QToolButton *playlistButton = createButton(Icons::playlist(), "Show / hide playlist (P)");
    QToolButton *fullscreenButton = createButton(Icons::fullscreen(), "Fullscreen (F)");

    QHBoxLayout *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(4);
    for (QToolButton *button : {previousButton, rewindButton, playButton_, forwardButton, nextButton,
                                stopButton}) {
        buttonRow->addWidget(button);
    }
    buttonRow->addSpacing(8);
    buttonRow->addWidget(muteButton_);
    buttonRow->addWidget(volumeSlider_);
    buttonRow->addSpacing(12);
    buttonRow->addWidget(timeLabel_);
    buttonRow->addStretch(1);
    buttonRow->addWidget(speedCombo_);
    for (QToolButton *button : {tracksButton_, editButton, snapshotButton, playlistButton, fullscreenButton}) {
        buttonRow->addWidget(button);
    }

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 26, 16, 10);  // the top padding is the fade-in of the gradient
    layout->setSpacing(4);
    layout->addWidget(seekSlider_);
    layout->addLayout(buttonRow);

    connect(playButton_, &QToolButton::clicked, this, &ControlBar::playClicked);
    connect(stopButton, &QToolButton::clicked, this, &ControlBar::stopClicked);
    connect(previousButton, &QToolButton::clicked, this, &ControlBar::previousClicked);
    connect(nextButton, &QToolButton::clicked, this, &ControlBar::nextClicked);
    connect(rewindButton, &QToolButton::clicked, this, &ControlBar::rewindClicked);
    connect(forwardButton, &QToolButton::clicked, this, &ControlBar::forwardClicked);
    connect(muteButton_, &QToolButton::clicked, this, &ControlBar::toggleMute);
    connect(tracksButton_, &QToolButton::clicked, this, &ControlBar::tracksRequested);
    connect(editButton, &QToolButton::clicked, this, &ControlBar::editRequested);
    connect(snapshotButton, &QToolButton::clicked, this, &ControlBar::snapshotRequested);
    connect(playlistButton, &QToolButton::clicked, this, &ControlBar::playlistToggleRequested);
    connect(fullscreenButton, &QToolButton::clicked, this, &ControlBar::fullscreenRequested);

    connect(seekSlider_, &QSlider::sliderMoved, this,
            [this](int value) { emit seekRequested(value); });
    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int percent) {
        updateVolumeIcon(percent);
        emit volumeChanged(percent);
    });
    connect(speedCombo_, &QComboBox::currentIndexChanged, this,
            [this](int index) { emit playbackRateChanged(speedCombo_->itemData(index).toDouble()); });
}

QToolButton *ControlBar::createButton(const QIcon &icon, const QString &toolTip) {
    QToolButton *button = new QToolButton;
    button->setIcon(icon);
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setToolTip(toolTip);
    button->setStyleSheet(Theme::OverlayButton);
    button->setFocusPolicy(Qt::NoFocus);  // keep keyboard focus on the video
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

void ControlBar::paintEvent(QPaintEvent *) {
    // Fades from transparent to dark, so the controls stay readable over any picture.
    QPainter painter(this);
    QLinearGradient gradient(0, 0, 0, height());
    gradient.setColorAt(0.0, QColor(0, 0, 0, 0));
    gradient.setColorAt(0.45, QColor(0, 0, 0, 150));
    gradient.setColorAt(1.0, QColor(0, 0, 0, 215));
    painter.fillRect(rect(), gradient);
}

int ControlBar::volume() const { return volumeSlider_->value(); }
void ControlBar::setVolume(int percent) { volumeSlider_->setValue(percent); }
qreal ControlBar::playbackRate() const { return speedCombo_->currentData().toDouble(); }

void ControlBar::setPlaybackRate(qreal rate) {
    const int index = speedCombo_->findData(rate);
    if (index >= 0) speedCombo_->setCurrentIndex(index);
}

void ControlBar::toggleMute() {
    if (volumeSlider_->value() > 0) {
        volumeBeforeMute_ = volumeSlider_->value();
        volumeSlider_->setValue(0);
    } else {
        volumeSlider_->setValue(volumeBeforeMute_ > 0 ? volumeBeforeMute_ : 100);
    }
}

void ControlBar::updateVolumeIcon(int percent) {
    muteButton_->setIcon(percent == 0 ? Icons::volumeMuted()
                                      : percent < 50 ? Icons::volumeLow() : Icons::volumeHigh());
}

void ControlBar::setPlaying(bool playing) {
    playButton_->setIcon(playing ? Icons::pause() : Icons::play());
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
