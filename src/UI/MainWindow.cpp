#include "MainWindow.h"
#include "VideoCanvas.h"
#include "Components.h"
#include "../Core/MediaEngine.h"
#include "../Utils/Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QSet>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent), playlistVisible(true) {
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
    playlistPanel = new PlaylistPanel;

    engine->bindVideoOutput(canvas->getVideoItem());

    playlistUI = playlistPanel->getListWidget();
    playlistPanel->setFixedWidth(280);
    controls->setPlaylistVisible(playlistVisible);

    QHBoxLayout *hLayout = new QHBoxLayout(playerPanel);
    hLayout->setContentsMargins(0,0,0,0); hLayout->setSpacing(0);

    QVBoxLayout *vLayout = new QVBoxLayout;
    vLayout->addWidget(canvas);
    vLayout->addWidget(controls);

    hLayout->addLayout(vLayout, 1);
    hLayout->addWidget(playlistPanel);
}

void MainWindow::wireConnections() {
    // Application flow
    connect(startPanel, &StartPanel::filesReady, this, &MainWindow::loadAndPlay);
    connect(playlistPanel, &PlaylistPanel::filesAppendRequested, this, &MainWindow::appendFiles);
    connect(playlistPanel, &PlaylistPanel::removeSelectedRequested, this, &MainWindow::removeSelectedPlaylistItem);

    // Controls -> Engine connections
    connect(controls, &ControlBar::playClicked, engine, &MediaEngine::togglePlayback);
    connect(controls, &ControlBar::stopClicked, engine, &MediaEngine::stop);
    connect(controls, &ControlBar::nextClicked, engine, &MediaEngine::next);
    connect(controls, &ControlBar::prevClicked, engine, &MediaEngine::previous);
    connect(controls, &ControlBar::rewindClicked, engine, [this](){ engine->seekRelative(-5000); });
    connect(controls, &ControlBar::forwardClicked, engine, [this](){ engine->seekRelative(5000); });
    connect(controls, &ControlBar::playlistToggleRequested, this, &MainWindow::togglePlaylistVisibility);
    connect(canvas, &VideoCanvas::spacePressed, engine, &MediaEngine::togglePlayback);
    connect(canvas, &VideoCanvas::fullscreenToggleRequested, this, &MainWindow::toggleFullscreen);
    connect(canvas, &VideoCanvas::exitFullscreenRequested, this, [this]() {
        if (isFullScreen()) toggleFullscreen();
    });

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
    connect(engine->getPlayer(), &QMediaPlayer::stateChanged, controls, [this](QMediaPlayer::State state){
        controls->setPlayButtonPlaying(state == QMediaPlayer::PlayingState);
    });
    controls->setPlayButtonPlaying(engine->getPlayer()->state() == QMediaPlayer::PlayingState);

    // Engine <-> Playlist connections
    connect(engine->getPlaylist(), &QMediaPlaylist::currentIndexChanged, playlistUI, [this](int index){
        QSignalBlocker blocker(playlistUI);
        if (index >= 0) playlistUI->setCurrentRow(index);
        else playlistUI->clearSelection();
    });
    connect(playlistUI, &QListWidget::currentRowChanged, this, [this](int row){
        if (row < 0) return;
        engine->getPlaylist()->setCurrentIndex(row);
        engine->getPlayer()->play();
    });
}

void MainWindow::loadAndPlay(const QStringList &files) {
    stack->setCurrentWidget(playerPanel); // Switch to player panel
    playlistUI->clear();
    for(const QString &f : files) playlistUI->addItem(QFileInfo(f).fileName());
    engine->loadFiles(files);
    canvas->setFocus();
}

void MainWindow::appendFiles(const QStringList &files) {
    if (files.isEmpty()) return;

    QSet<QString> existingFiles;
    QMediaPlaylist *playlist = engine->getPlaylist();
    for (int i = 0; i < playlist->mediaCount(); ++i) {
        existingFiles.insert(playlist->media(i).request().url().toLocalFile());
    }

    QStringList uniqueFiles;
    for (const QString &file : files) {
        if (existingFiles.contains(file)) continue;
        existingFiles.insert(file);
        uniqueFiles.append(file);
    }

    if (uniqueFiles.isEmpty()) return;

    stack->setCurrentWidget(playerPanel);
    for (const QString &f : uniqueFiles) playlistUI->addItem(QFileInfo(f).fileName());
    engine->appendFiles(uniqueFiles);
    canvas->setFocus();
}

void MainWindow::removeSelectedPlaylistItem() {
    int row = playlistUI->currentRow();
    if (row < 0) return;

    QSignalBlocker blocker(playlistUI);
    delete playlistUI->takeItem(row);

    int nextIndex = engine->removeAt(row);
    if (nextIndex >= 0 && nextIndex < playlistUI->count()) playlistUI->setCurrentRow(nextIndex);
    else playlistUI->clearSelection();
}

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) showNormal();
    else showFullScreen();

    canvas->fitVideo();
    canvas->setFocus();
}

void MainWindow::togglePlaylistVisibility() {
    playlistVisible = !playlistVisible;
    playlistPanel->setVisible(playlistVisible);
    controls->setPlaylistVisible(playlistVisible);
    canvas->fitVideo();
    canvas->setFocus();
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        toggleFullscreen();
        return;
    }

    QWidget::keyPressEvent(event);
}
