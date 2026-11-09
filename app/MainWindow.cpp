#include "MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "ControlBar.h"
#include "JumpSlider.h"
#include "MediaFiles.h"
#include "PlayerController.h"
#include "PlaylistPanel.h"
#include "StartPanel.h"
#include "Theme.h"
#include "ThumbnailPopup.h"
#include "ThumbnailProvider.h"
#include "VideoWidget.h"

namespace {
constexpr int kHideDelayMs = 3000;
constexpr qint64 kSeekStepMs = 5000;
constexpr int kVolumeStep = 5;
constexpr int kPlaylistWidth = 280;
constexpr qint64 kThumbnailStepMs = 1000;
const char *kVolumeSetting = "volume";
}

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("Viplyr");
    resize(1100, 650);
    setStyleSheet(Theme::WindowBg);

    controller_ = new PlayerController(this);
    thumbnails_ = new ThumbnailProvider(this);

    buildLayout();
    controller_->addVideoSink(videoWidget_);
    wireConnections();
    createShortcuts();

    hideTimer_.setSingleShot(true);
    hideTimer_.setInterval(kHideDelayMs);
    connect(&hideTimer_, &QTimer::timeout, this, &MainWindow::hideChromeIfIdle);
    qApp->installEventFilter(this);  // any mouse or key activity shows the controls again

    controlBar_->setVolume(QSettings().value(kVolumeSetting, 100).toInt());
    controller_->setVolume(controlBar_->volume());
}

MainWindow::~MainWindow() = default;

void MainWindow::buildLayout() {
    startPanel_ = new StartPanel;
    videoWidget_ = new VideoWidget;
    controlBar_ = new ControlBar;
    playlistPanel_ = new PlaylistPanel;
    playlistPanel_->setFixedWidth(kPlaylistWidth);

    QVBoxLayout *videoColumn = new QVBoxLayout;
    videoColumn->setSpacing(0);
    videoColumn->addWidget(videoWidget_, 1);
    videoColumn->addWidget(controlBar_);

    playerPage_ = new QWidget;
    QHBoxLayout *playerLayout = new QHBoxLayout(playerPage_);
    playerLayout->setContentsMargins(0, 0, 0, 0);
    playerLayout->setSpacing(0);
    playerLayout->addLayout(videoColumn, 1);
    playerLayout->addWidget(playlistPanel_);

    stack_ = new QStackedWidget;
    stack_->addWidget(startPanel_);
    stack_->addWidget(playerPage_);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    thumbnailPopup_ = new ThumbnailPopup(this);
}

void MainWindow::wireConnections() {
    // Opening files
    connect(startPanel_, &StartPanel::pathsDropped, this,
            [this](const QStringList &paths) { addPaths(paths, true); });
    connect(startPanel_, &StartPanel::chooseRequested, this, [this] { chooseFiles(true); });
    connect(playlistPanel_, &PlaylistPanel::pathsDropped, this,
            [this](const QStringList &paths) { addPaths(paths, false); });
    connect(playlistPanel_, &PlaylistPanel::addRequested, this, [this] { chooseFiles(false); });
    connect(playlistPanel_, &PlaylistPanel::removeRequested, this, &MainWindow::removeSelected);
    connect(playlistPanel_, &PlaylistPanel::itemActivated, controller_, &PlayerController::setCurrentIndex);

    // Control bar -> controller
    connect(controlBar_, &ControlBar::playClicked, controller_, &PlayerController::togglePlayback);
    connect(controlBar_, &ControlBar::stopClicked, controller_, &PlayerController::stop);
    connect(controlBar_, &ControlBar::nextClicked, controller_, &PlayerController::next);
    connect(controlBar_, &ControlBar::previousClicked, controller_, &PlayerController::previous);
    connect(controlBar_, &ControlBar::rewindClicked, controller_,
            [this] { controller_->seekRelative(-kSeekStepMs); });
    connect(controlBar_, &ControlBar::forwardClicked, controller_,
            [this] { controller_->seekRelative(kSeekStepMs); });
    connect(controlBar_, &ControlBar::seekRequested, controller_, &PlayerController::seekTo);
    connect(controlBar_, &ControlBar::volumeChanged, controller_, &PlayerController::setVolume);
    connect(controlBar_, &ControlBar::playbackRateChanged, controller_, &PlayerController::setPlaybackRate);
    connect(controlBar_, &ControlBar::playlistToggleRequested, this, &MainWindow::togglePlaylist);

    // Controller -> widgets
    connect(controller_, &PlayerController::positionChanged, controlBar_, &ControlBar::setPosition);
    connect(controller_, &PlayerController::durationChanged, controlBar_, &ControlBar::setDuration);
    connect(controller_, &PlayerController::playingChanged, this, &MainWindow::onPlayingChanged);
    connect(controller_, &PlayerController::currentIndexChanged, playlistPanel_, &PlaylistPanel::setCurrentIndex);
    connect(controller_, &PlayerController::subtitleTextChanged, videoWidget_, &VideoWidget::setSubtitleText);
    connect(controller_, &PlayerController::errorOccurred, this, &MainWindow::showError);
    connect(controller_, &PlayerController::mediaOpened, this, [this](const QString &path, int rotation) {
        videoWidget_->setMediaRotation(rotation);
        thumbnails_->setSource(path);
        setWindowTitle(QFileInfo(path).fileName() + " - Viplyr");
    });
    connect(controller_, &PlayerController::mediaClosed, this, [this] {
        videoWidget_->clear();
        thumbnails_->setSource(QString());
        setWindowTitle("Viplyr");
        if (controller_->count() == 0) stack_->setCurrentWidget(startPanel_);
    });

    // Video area
    connect(videoWidget_, &VideoWidget::fullscreenToggleRequested, this, &MainWindow::toggleFullscreen);

    // Seek-bar preview
    JumpSlider *seekSlider = controlBar_->seekSlider();
    connect(seekSlider, &JumpSlider::hoverValueChanged, this, &MainWindow::showThumbnail);
    connect(seekSlider, &JumpSlider::hoverLeft, this, [this] {
        thumbnailPopup_->hide();
        wantedThumbnailMs_ = -1;
    });
    connect(thumbnails_, &ThumbnailProvider::thumbnailReady, this,
            [this](qint64 positionMs, const QPixmap &pixmap) {
                if (positionMs == wantedThumbnailMs_) thumbnailPopup_->setPicture(pixmap);
            });
}

void MainWindow::createShortcuts() {
    const auto bind = [this](const QKeySequence &keys, std::function<void()> action) {
        connect(new QShortcut(keys, this), &QShortcut::activated, this, std::move(action));
    };
    bind(Qt::Key_Space, [this] { controller_->togglePlayback(); });
    bind(Qt::Key_Left, [this] { controller_->seekRelative(-kSeekStepMs); });
    bind(Qt::Key_Right, [this] { controller_->seekRelative(kSeekStepMs); });
    bind(Qt::Key_Comma, [this] { controller_->stepFrame(-1); });
    bind(Qt::Key_Period, [this] { controller_->stepFrame(1); });
    bind(Qt::Key_Up, [this] { changeVolume(kVolumeStep); });
    bind(Qt::Key_Down, [this] { changeVolume(-kVolumeStep); });
    bind(Qt::Key_M, [this] { controlBar_->toggleMute(); });
    bind(Qt::Key_R, [this] { videoWidget_->rotateClockwise(); });
    bind(Qt::Key_F, [this] { toggleFullscreen(); });
    bind(Qt::Key_T, [this] { toggleAlwaysOnTop(); });
    bind(Qt::Key_Escape, [this] { if (isFullScreen()) toggleFullscreen(); });
}

// ------------------------------------------------------------ opening files

void MainWindow::openPaths(const QStringList &paths) {
    addPaths(paths, true);
}

void MainWindow::addPaths(const QStringList &paths, bool replacePlaylist) {
    if (paths.isEmpty()) return;
    // Folders can hold thousands of files, so the search runs on a worker thread.
    // Every call has its own result, so quick successive drops are all kept.
    QtConcurrent::run(MediaFiles::collectVideoFiles, paths)
        .then(this, [this, replacePlaylist](const QStringList &files) {
            if (files.isEmpty()) return;
            stack_->setCurrentWidget(playerPage_);
            if (replacePlaylist) controller_->loadFiles(files);
            else controller_->appendFiles(files);
            refreshPlaylistPanel();
            videoWidget_->setFocus();
        });
}

void MainWindow::chooseFiles(bool replacePlaylist) {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, replacePlaylist ? "Choose Video" : "Add Videos", QString(), MediaFiles::fileDialogFilter());
    addPaths(files, replacePlaylist);
}

void MainWindow::removeSelected() {
    const int index = playlistPanel_->selectedIndex();
    if (index < 0) return;
    controller_->removeAt(index);
    refreshPlaylistPanel();
}

void MainWindow::refreshPlaylistPanel() {
    playlistPanel_->setPaths(controller_->paths());
    playlistPanel_->setCurrentIndex(controller_->currentIndex());
}

// ------------------------------------------------------------ window modes

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) showNormal();
    else showFullScreen();
    videoWidget_->resetView();
    videoWidget_->setFocus();
}

void MainWindow::togglePlaylist() {
    playlistVisible_ = !playlistVisible_;
    playlistPanel_->setVisible(playlistVisible_);
    controlBar_->setPlaylistVisible(playlistVisible_);
    videoWidget_->setFocus();
}

void MainWindow::toggleAlwaysOnTop() {
    setWindowFlag(Qt::WindowStaysOnTopHint, !(windowFlags() & Qt::WindowStaysOnTopHint));
    show();  // changing window flags hides the window
}

void MainWindow::changeVolume(int delta) {
    controlBar_->setVolume(qBound(0, controlBar_->volume() + delta, 100));
}

// ----------------------------------------------------------------- feedback

void MainWindow::showThumbnail(int positionMs) {
    if (controller_->count() == 0) return;

    const qint64 wanted = positionMs / kThumbnailStepMs * kThumbnailStepMs;
    if (thumbnailPopup_->isHidden()) thumbnailPopup_->setPicture(QPixmap());
    // The previous picture stays until the new one arrives, so sweeping does not flicker.
    wantedThumbnailMs_ = wanted;
    thumbnailPopup_->setTime(positionMs);
    thumbnails_->request(wanted);

    const JumpSlider *seekSlider = controlBar_->seekSlider();
    const int cursorX = mapFromGlobal(QCursor::pos()).x();
    const int x = qBound(0, cursorX - thumbnailPopup_->width() / 2, width() - thumbnailPopup_->width());
    const int y = seekSlider->mapTo(this, QPoint(0, 0)).y() - thumbnailPopup_->height() - 10;
    thumbnailPopup_->move(x, y);
    thumbnailPopup_->show();
    thumbnailPopup_->raise();
}

void MainWindow::showError(const QString &message) {
    if (errorDialogOpen_) return;  // one dialog at a time, however many errors arrive
    errorDialogOpen_ = true;
    QMessageBox::critical(this, "Playback Error", message);
    errorDialogOpen_ = false;
}

void MainWindow::onPlayingChanged(bool playing) {
    controlBar_->setPlaying(playing);
    idleInhibitor_.setInhibited(playing);
    if (playing) {
        hideTimer_.start();
    } else {
        hideTimer_.stop();
        setChromeVisible(true);
    }
}

// ---------------------------------------------------------------- auto-hide

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
        case QEvent::KeyPress:
            onUserActivity();
            break;
        default:
            break;
    }
    return QWidget::eventFilter(watched, event);
}

void MainWindow::onUserActivity() {
    setChromeVisible(true);
    if (controller_->isPlaying()) hideTimer_.start();
}

void MainWindow::hideChromeIfIdle() {
    if (!controller_->isPlaying() || stack_->currentWidget() != playerPage_) return;

    // Never hide what the user is pointing at or typing into.
    const bool inUse = controlBar_->underMouse() || playlistPanel_->underMouse() ||
                       playlistPanel_->isSearching() || QApplication::activePopupWidget() ||
                       QApplication::activeModalWidget();
    if (inUse) {
        hideTimer_.start();
        return;
    }
    setChromeVisible(false);
}

void MainWindow::setChromeVisible(bool visible) {
    if (visible == chromeVisible_) return;
    chromeVisible_ = visible;

    controlBar_->setVisible(visible);
    // The playlist only gets out of the way in fullscreen.
    if (visible) playlistPanel_->setVisible(playlistVisible_);
    else if (isFullScreen()) playlistPanel_->hide();

    if (visible || !isFullScreen()) videoWidget_->unsetCursor();
    else videoWidget_->setCursor(Qt::BlankCursor);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    QSettings().setValue(kVolumeSetting, controlBar_->volume());
    QWidget::closeEvent(event);
}
