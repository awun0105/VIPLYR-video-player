#include "MainWindow.h"
#include "VideoCanvas.h"
#include "Components.h"
#include "../Core/MediaEngine.h"
#include "../Utils/Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("Viplyr - video player");
    resize(1100, 650);
    setStyleSheet(Theme::WindowBg);

    stack = new QStackedWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->addWidget(stack);

    startPanel = new StartPanel;
    playerPanel = new QWidget;
    setupPlayerPanel();

    stack->addWidget(startPanel);
    stack->addWidget(playerPanel);

    wireConnections();
}

void MainWindow::setupPlayerPanel() {
    engine = new MediaEngine(this);
    canvas = new VideoCanvas;
    controls = new ControlBar;

    engine->bindVideoOutput(canvas->getVideoItem());

    playlistUI = new QListWidget;
    playlistUI->setFixedWidth(250);
    playlistUI->setStyleSheet(Theme::PanelBg + Theme::PlaylistItem);

    QHBoxLayout *hLayout = new QHBoxLayout(playerPanel);
    hLayout->setContentsMargins(0,0,0,0); hLayout->setSpacing(0);

    QVBoxLayout *vLayout = new QVBoxLayout;
    vLayout->addWidget(canvas);
    vLayout->addWidget(controls);

    hLayout->addLayout(vLayout, 1);
    hLayout->addWidget(playlistUI);
}

void MainWindow::wireConnections() {
    // Application flow
    connect(startPanel, &StartPanel::filesReady, this, &MainWindow::loadAndPlay);

    // Controls -> Engine connections
    connect(controls, &ControlBar::playClicked, engine, &MediaEngine::togglePlayback);
    connect(controls, &ControlBar::stopClicked, engine, &MediaEngine::stop);
    connect(controls, &ControlBar::nextClicked, engine, &MediaEngine::next);
    connect(controls, &ControlBar::prevClicked, engine, &MediaEngine::previous);
    connect(controls, &ControlBar::rewindClicked, engine, [this](){ engine->seekRelative(-5000); });
    connect(controls, &ControlBar::forwardClicked, engine, [this](){ engine->seekRelative(5000); });
    connect(canvas, &VideoCanvas::spacePressed, engine, &MediaEngine::togglePlayback);

    // Engine -> Seek slider connections
    SeekSlider *seek = controls->getSeekSlider();
    connect(engine->getPlayer(), &QMediaPlayer::positionChanged, [seek](qint64 pos){
        if (!seek->isSliderDown()) seek->setValue(pos);
    });
    connect(engine->getPlayer(), &QMediaPlayer::durationChanged, seek, [seek](qint64 dur){ seek->setRange(0, dur); });
    connect(seek, &QSlider::sliderMoved, engine, &MediaEngine::seekTo);
    connect(controls->getVolSlider(), &QSlider::valueChanged, engine, &MediaEngine::setVolume);

    // Engine -> Time display connections
    QLabel *timeLabel = controls->getTimeLabel();

    // Create a lambda to format milliseconds to hh:mm:ss
    auto formatTime = [](qint64 ms) -> QString {
        int seconds = (ms / 1000) % 60;
        int minutes = (ms / 60000) % 60;
        int hours = (ms / 3600000);
        if (hours > 0)
            return QString("%1:%2:%3").arg(hours, 2, 10, QChar('0')).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
        return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
    };

    // Function to update the displayed time text
    auto updateTimeDisplay = [this, timeLabel, formatTime]() {
        qint64 pos = engine->getPlayer()->position();
        qint64 dur = engine->getPlayer()->duration();
        timeLabel->setText(formatTime(pos) + " / " + formatTime(dur));
    };

    // Event binding: update text whenever playback position or duration changes
    connect(engine->getPlayer(), &QMediaPlayer::positionChanged, this, updateTimeDisplay);
    connect(engine->getPlayer(), &QMediaPlayer::durationChanged, this, updateTimeDisplay);

    // Engine <-> Playlist connections
    connect(engine->getPlaylist(), &QMediaPlaylist::currentIndexChanged, playlistUI, [this](int index){
        playlistUI->setCurrentRow(index);
    });
    connect(playlistUI, &QListWidget::currentRowChanged, engine->getPlaylist(), &QMediaPlaylist::setCurrentIndex);
}

void MainWindow::loadAndPlay(const QStringList &files) {
    stack->setCurrentWidget(playerPanel); // Switch to player panel
    for(const QString &f : files) playlistUI->addItem(QFileInfo(f).fileName());
    engine->loadFiles(files);
}