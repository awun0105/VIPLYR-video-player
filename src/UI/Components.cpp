#include "Components.h"
#include "../Utils/Theme.h"
#include "../Utils/Constants.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMouseEvent>
#include <QStyleOptionSlider>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileDialog>
#include <QFileInfo>
#include <QSet>

namespace {
QStringList localVideoFilesFromUrls(const QList<QUrl> &urls) {
    QStringList paths;
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) paths.append(url.toLocalFile());
    }
    return Constants::filterVideoFiles(paths);
}

QStringList promptForVideoFiles(QWidget *parent, const QString &title) {
    return Constants::filterVideoFiles(QFileDialog::getOpenFileNames(
        parent,
        title,
        "",
        Constants::kVideoFilter
    ));
}
}

// --- JumpSlider ---
JumpSlider::JumpSlider(const QString &activeColor, QWidget *parent) : QSlider(Qt::Horizontal, parent) {
    setStyleSheet(QString("QSlider::groove:horizontal { %1 } QSlider::sub-page:horizontal { %2 } QSlider::handle:horizontal { %3 }")
                  .arg(Theme::SliderGroove, activeColor, Theme::SliderHandle));
}
void JumpSlider::mousePressEvent(QMouseEvent *ev) {
    if (ev->button() == Qt::LeftButton) {
        QStyleOptionSlider opt; initStyleOption(&opt);
        int sr = style()->pixelMetric(QStyle::PM_SliderLength, &opt, this);
        int w = width();
        int newValue = QStyle::sliderValueFromPosition(minimum(), maximum(), ev->pos().x() - sr / 2, w - sr, opt.upsideDown);
        setValue(newValue);
        emit sliderMoved(newValue);
    }
    QSlider::mousePressEvent(ev);
}

// --- ControlBar ---
ControlBar::ControlBar(QWidget *parent) : QWidget(parent) {
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(15, 10, 15, 10); layout->setSpacing(10);

    QPushButton *btnPrev = new QPushButton("⏮");
    QPushButton *btnRewind = new QPushButton("⏪ -5s");
    playButton = new QPushButton("▶ Play");
    QPushButton *btnStop = new QPushButton("⏹ Stop");
    QPushButton *btnForward = new QPushButton("+5s ⏩");
    QPushButton *btnNext = new QPushButton("⏭");
    playlistToggleButton = new QPushButton("Hide Playlist");

    for(auto btn : {btnPrev, btnRewind, playButton, btnStop, btnForward, btnNext, playlistToggleButton}) {
        btn->setStyleSheet(Theme::Button);
    }

    seekSlider = new JumpSlider(Theme::SeekColor);
    timeLabel = new QLabel("00:00 / 00:00");
    timeLabel->setStyleSheet("color: #ccc; font-size: 13px; font-weight: bold; margin: 0 10px; font-family: monospace;");
    
    muteButton = new QPushButton("🔊");
    muteButton->setStyleSheet("background: transparent; border: none; font-size: 18px; color: white; padding: 0;");
    
    volSlider = new JumpSlider(Theme::VolColor);
    volSlider->setRange(0, 100); volSlider->setValue(100); volSlider->setFixedWidth(100);
    lastVolume = 100;

    layout->addWidget(btnPrev); layout->addWidget(btnRewind); layout->addWidget(playButton);
    layout->addWidget(btnStop); layout->addWidget(btnForward); layout->addWidget(btnNext);
    layout->addWidget(playlistToggleButton);
    layout->addWidget(seekSlider, 1);
    layout->addWidget(timeLabel);
    layout->addWidget(muteButton); layout->addWidget(volSlider);

    connect(playButton, &QPushButton::clicked, this, &ControlBar::playClicked);
    connect(btnStop, &QPushButton::clicked, this, &ControlBar::stopClicked);
    connect(btnPrev, &QPushButton::clicked, this, &ControlBar::prevClicked);
    connect(btnNext, &QPushButton::clicked, this, &ControlBar::nextClicked);
    connect(btnRewind, &QPushButton::clicked, this, &ControlBar::rewindClicked);
    connect(btnForward, &QPushButton::clicked, this, &ControlBar::forwardClicked);
    connect(playlistToggleButton, &QPushButton::clicked, this, &ControlBar::playlistToggleRequested);
    connect(muteButton, &QPushButton::clicked, this, &ControlBar::toggleMute);
    connect(volSlider, &QSlider::valueChanged, this, &ControlBar::updateVolumeIcon);
}

void ControlBar::toggleMute() {
    if (volSlider->value() > 0) {
        lastVolume = volSlider->value();
        volSlider->setValue(0);
    } else {
        volSlider->setValue(lastVolume > 0 ? lastVolume : 100);
    }
}

void ControlBar::updateVolumeIcon(int value) {
    if (value == 0) {
        muteButton->setText("🔇");
    } else if (value < 50) {
        muteButton->setText("🔉");
    } else {
        muteButton->setText("🔊");
    }
}

void ControlBar::setPlayButtonPlaying(bool playing) {
    playButton->setText(playing ? "❚❚ Pause" : "▶ Play");
}

void ControlBar::setPlaylistVisible(bool visible) {
    playlistToggleButton->setText(visible ? "Hide Playlist" : "Show Playlist");
}

// --- PlaylistPanel ---
PlaylistPanel::PlaylistPanel(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);
    setStyleSheet(Theme::PanelBg);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    searchEdit = new QLineEdit;
    searchEdit->setPlaceholderText("🔍 Search video...");
    searchEdit->setStyleSheet("background-color: #222; color: #ccc; border: 1px solid #333; border-radius: 4px; padding: 5px;");

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    QPushButton *btnAdd = new QPushButton("Add");
    QPushButton *btnRemove = new QPushButton("Remove");
    btnAdd->setStyleSheet(Theme::Button);
    btnRemove->setStyleSheet(Theme::Button);

    playlistWidget = new QListWidget;
    playlistWidget->setStyleSheet(Theme::PanelBg + Theme::PlaylistItem);

    buttonLayout->addWidget(btnAdd);
    buttonLayout->addWidget(btnRemove);

    layout->addWidget(searchEdit);
    layout->addLayout(buttonLayout);
    layout->addWidget(playlistWidget, 1);

    connect(btnAdd, &QPushButton::clicked, this, [this](){
        QStringList files = promptForVideoFiles(this, "Add Videos");
        if (!files.isEmpty()) emit filesAppendRequested(files);
    });
    connect(btnRemove, &QPushButton::clicked, this, &PlaylistPanel::removeSelectedRequested);

    connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString &text){
        for(int i = 0; i < playlistWidget->count(); ++i) {
            QListWidgetItem *item = playlistWidget->item(i);
            bool match = item->text().contains(text, Qt::CaseInsensitive);
            item->setHidden(!match);
        }
    });
}

void PlaylistPanel::dragEnterEvent(QDragEnterEvent *event) {
    if (!localVideoFilesFromUrls(event->mimeData()->urls()).isEmpty()) event->acceptProposedAction();
}

void PlaylistPanel::dropEvent(QDropEvent *event) {
    QStringList files = localVideoFilesFromUrls(event->mimeData()->urls());
    if (!files.isEmpty()) {
        emit filesAppendRequested(files);
        event->acceptProposedAction();
    }
}

// --- StartPanel ---
StartPanel::StartPanel(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);

    QLabel *icon = new QLabel("🎬", this); icon->setStyleSheet("font-size: 70px;"); icon->setAlignment(Qt::AlignCenter);
    QLabel *title = new QLabel("DROP YOUR VIDEOS HERE", this); title->setStyleSheet("font-size: 24px; color: #888; font-weight: bold;"); title->setAlignment(Qt::AlignCenter);
    QPushButton *btnSelect = new QPushButton("Choose file", this);
    btnSelect->setFixedSize(250, 50); btnSelect->setStyleSheet("background-color: #1E90FF; border-radius: 8px; font-weight:bold; font-size: 16px; border:none;");

    layout->addWidget(icon); layout->addWidget(title); layout->addWidget(btnSelect, 0, Qt::AlignHCenter);

    connect(btnSelect, &QPushButton::clicked, this, [this](){
        QStringList files = promptForVideoFiles(this, "Choose Video");
        if(!files.isEmpty()) emit filesReady(files);
    });
}
void StartPanel::dragEnterEvent(QDragEnterEvent *event) {
    if (!localVideoFilesFromUrls(event->mimeData()->urls()).isEmpty()) event->acceptProposedAction();
}
void StartPanel::dropEvent(QDropEvent *event) {
    QStringList files = localVideoFilesFromUrls(event->mimeData()->urls());
    if (!files.isEmpty()) {
        emit filesReady(files);
        event->acceptProposedAction();
    }
}
