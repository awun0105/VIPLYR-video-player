#include "MainWindow.h"


#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QSettings>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "ControlBar.h"
#include "ExportDialog.h"
#include "EditorDialog.h"
#include "ExportJob.h"
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
constexpr int kFadeDurationMs = 180;
constexpr qint64 kSeekStepMs = 5000;
constexpr int kVolumeStep = 5;
constexpr int kAdjustmentStep = 5;
constexpr int kPlaylistWidth = 300;
constexpr qint64 kThumbnailStepMs = 1000;

namespace Setting {
const char *kVolume = "volume";
const char *kRate = "playbackRate";
const char *kGeometry = "window/geometry";
const char *kPlaylistVisible = "window/playlistVisible";
}

QString describeTrack(const viplyr::TrackInfo &track, int number) {
    QString text = QString("Track %1").arg(number);
    if (!track.title.empty()) text = QString::fromStdString(track.title);
    if (!track.language.empty()) text += QString(" [%1]").arg(QString::fromStdString(track.language));
    return text + QString(" (%1)").arg(QString::fromStdString(track.codec));
}

}  // namespace

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("Viplyr");
    resize(1100, 650);
    setStyleSheet(Theme::WindowBg);

    controller_ = new PlayerController(this);
    thumbnails_ = new ThumbnailProvider(this);
    exportJob_ = new ExportJob(this);

    buildLayout();
    controller_->addVideoSink(videoWidget_);
    videoWidget_->setOverlayTrack(&overlayTrack_);
    wireConnections();
    createShortcuts();

    hideTimer_.setSingleShot(true);
    hideTimer_.setInterval(kHideDelayMs);
    connect(&hideTimer_, &QTimer::timeout, this, &MainWindow::hideChromeIfIdle);
    qApp->installEventFilter(this);  // any mouse or key activity shows the controls again

    loadSettings();
}

MainWindow::~MainWindow() {
    videoWidget_->setOverlayTrack(nullptr);
}

void MainWindow::buildLayout() {
    startPanel_ = new StartPanel;
    videoWidget_ = new VideoWidget;
    playlistPanel_ = new PlaylistPanel;
    playlistPanel_->setFixedWidth(kPlaylistWidth);

    // The control bar is a child of the video area, positioned by layoutControlBar().
    controlBar_ = new ControlBar(videoWidget_);
    QGraphicsOpacityEffect *opacity = new QGraphicsOpacityEffect(controlBar_);
    opacity->setOpacity(1.0);
    controlBar_->setGraphicsEffect(opacity);
    chromeFade_ = new QPropertyAnimation(opacity, "opacity", this);
    chromeFade_->setDuration(kFadeDurationMs);
    connect(chromeFade_, &QPropertyAnimation::finished, this, [this] {
        if (!chromeVisible_) controlBar_->hide();
    });
    videoWidget_->installEventFilter(this);

    playerPage_ = new QWidget;
    QHBoxLayout *playerLayout = new QHBoxLayout(playerPage_);
    playerLayout->setContentsMargins(0, 0, 0, 0);
    playerLayout->setSpacing(0);
    playerLayout->addWidget(videoWidget_, 1);
    playerLayout->addWidget(playlistPanel_);

    stack_ = new QStackedWidget;
    stack_->addWidget(startPanel_);
    stack_->addWidget(playerPage_);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    thumbnailPopup_ = new ThumbnailPopup(this);
    createExportProgress();
}

void MainWindow::createExportProgress() {
    // A small panel over the video: the export runs on while the player is used.
    exportProgress_ = new QWidget(videoWidget_);
    exportProgress_->setAttribute(Qt::WA_StyledBackground);
    exportProgress_->setStyleSheet("background-color: rgba(0, 0, 0, 170); border-radius: 6px;");
    QLabel *label = new QLabel("Exporting");
    label->setStyleSheet("color: white; font-weight: bold; background: transparent;");
    exportProgressBar_ = new QProgressBar;
    exportProgressBar_->setRange(0, 1000);
    exportProgressBar_->setFixedWidth(140);
    exportProgressBar_->setStyleSheet(
        "QProgressBar { background-color: #2a2a30; border: none; border-radius: 4px; height: 8px; color: white; "
        "text-align: center; font-size: 10px; } QProgressBar::chunk { background-color: #3D9BFF; border-radius: 4px; }");
    QToolButton *cancel = new QToolButton;
    cancel->setText("Cancel");
    cancel->setStyleSheet(Theme::OverlayButton);
    QHBoxLayout *row = new QHBoxLayout(exportProgress_);
    row->setContentsMargins(10, 6, 6, 6);
    row->addWidget(label);
    row->addWidget(exportProgressBar_);
    row->addWidget(cancel);
    exportProgress_->hide();

    connect(cancel, &QToolButton::clicked, exportJob_, &ExportJob::cancel);
    connect(exportJob_, &ExportJob::progressChanged, this,
            [this](double fraction) { exportProgressBar_->setValue(qRound(fraction * 1000)); });
    connect(exportJob_, &ExportJob::finished, this,
            [this](bool succeeded, const QString &outputPath, const QString &error) {
                exportProgress_->hide();
                if (exportDialog_) exportDialog_->setExportRunning(false);
                if (editorDialog_) {
                    editorDialog_->setExportRunning(false);
                    editorDialog_->setMessage(succeeded ? "Exported " + outputPath :
                                              error.isEmpty() ? "Export cancelled." : "Export failed: " + error);
                }
                if (succeeded) videoWidget_->showMessage("Exported " + QFileInfo(outputPath).fileName());
                else if (error.isEmpty()) videoWidget_->showMessage("Export cancelled");
                else QMessageBox::critical(this, "Export failed", error);
            });
}

void MainWindow::startExport(const ExportOptions &options, double totalSeconds) {
    if (exportJob_->isRunning()) return;
    exportJob_->start(options, totalSeconds);
    if (exportDialog_) exportDialog_->setExportRunning(true);
    if (editorDialog_) editorDialog_->setExportRunning(true);
    exportProgressBar_->setValue(0);
    exportProgress_->adjustSize();
    exportProgress_->show();
    exportProgress_->raise();
    layoutControlBar();
}

void MainWindow::wireConnections() {
    // Opening files
    connect(startPanel_, &StartPanel::pathsDropped, this,
            [this](const QStringList &paths) { addPaths(paths, true); });
    connect(startPanel_, &StartPanel::chooseRequested, this, [this] { chooseFiles(true); });
    connect(playlistPanel_, &PlaylistPanel::pathsDropped, this,
            [this](const QStringList &paths) { addPaths(paths, false); });
    connect(playlistPanel_, &PlaylistPanel::addFilesRequested, this, [this] { chooseFiles(false); });
    connect(playlistPanel_, &PlaylistPanel::addFolderRequested, this, &MainWindow::chooseFolder);
    connect(playlistPanel_, &PlaylistPanel::addUrlRequested, this, &MainWindow::chooseUrl);
    connect(playlistPanel_, &PlaylistPanel::removeRequested, this, &MainWindow::removeSelected);
    connect(playlistPanel_, &PlaylistPanel::itemActivated, controller_, &PlayerController::setCurrentIndex);
    connect(playlistPanel_, &PlaylistPanel::itemMoved, controller_, &PlayerController::moveItem);

    // Control bar -> controller
    connect(controlBar_, &ControlBar::playClicked, controller_, &PlayerController::togglePlayback);
    connect(controlBar_, &ControlBar::stopClicked, controller_, &PlayerController::stop);
    connect(controlBar_, &ControlBar::nextClicked, controller_, &PlayerController::next);
    connect(controlBar_, &ControlBar::previousClicked, controller_, &PlayerController::previous);
    connect(controlBar_, &ControlBar::rewindClicked, this, [this] { seekBy(-kSeekStepMs); });
    connect(controlBar_, &ControlBar::forwardClicked, this, [this] { seekBy(kSeekStepMs); });
    connect(controlBar_, &ControlBar::seekRequested, controller_, &PlayerController::seekTo);
    connect(controlBar_, &ControlBar::volumeChanged, controller_, &PlayerController::setVolume);
    connect(controlBar_, &ControlBar::playbackRateChanged, this, [this](qreal rate) {
        controller_->setPlaybackRate(rate);
        videoWidget_->showMessage(QString("Speed %1x").arg(rate));
    });

    // Control bar -> window tools
    connect(controlBar_, &ControlBar::tracksRequested, this, &MainWindow::showTracksMenu);
    connect(controlBar_, &ControlBar::editRequested, this, &MainWindow::openVideoEditor);
    connect(controlBar_, &ControlBar::snapshotRequested, this, &MainWindow::saveSnapshot);
    connect(controlBar_, &ControlBar::playlistToggleRequested, this, &MainWindow::togglePlaylist);
    connect(controlBar_, &ControlBar::fullscreenRequested, this, &MainWindow::toggleFullscreen);

    // Controller -> widgets
    connect(controller_, &PlayerController::positionChanged, controlBar_, &ControlBar::setPosition);
    connect(controller_, &PlayerController::durationChanged, controlBar_, &ControlBar::setDuration);
    connect(controller_, &PlayerController::playingChanged, this, &MainWindow::onPlayingChanged);
    connect(controller_, &PlayerController::currentIndexChanged, playlistPanel_, &PlaylistPanel::setCurrentIndex);
    connect(controller_, &PlayerController::subtitleTextChanged, videoWidget_, &VideoWidget::setSubtitleText);
    connect(controller_, &PlayerController::errorOccurred, this, &MainWindow::showError);
    connect(controller_, &PlayerController::mediaOpened, this, &MainWindow::onMediaOpened);
    connect(controller_, &PlayerController::pictureSubtitlesChanged, videoWidget_, &VideoWidget::setPictureSubtitles);
    connect(controller_, &PlayerController::loopChanged, this, [this](qint64 startMs, qint64 endMs) {
        controlBar_->seekSlider()->setHighlight(static_cast<int>(startMs), static_cast<int>(endMs));
        if (startMs < 0) videoWidget_->showMessage("Loop off");
        else if (endMs < 0) videoWidget_->showMessage("Loop start set: press L again at the end");
        else videoWidget_->showMessage(QString("Looping %1 - %2").arg(MediaFiles::formatTime(startMs), MediaFiles::formatTime(endMs)));
    });
    connect(controller_, &PlayerController::mediaClosed, this, &MainWindow::onMediaClosed);
    connect(controller_, &PlayerController::resumed, this, [this](qint64 positionMs) {
        videoWidget_->showMessage("Resumed from " + MediaFiles::formatTime(positionMs));
    });

    // Video area
    connect(videoWidget_, &VideoWidget::fullscreenToggleRequested, this, &MainWindow::toggleFullscreen);
    connect(videoWidget_, &VideoWidget::frameShown, this, &MainWindow::updateInfo);
    connect(videoWidget_, &VideoWidget::gpuFramePathChanged, controller_, &PlayerController::setGpuFramePath);
    const viplyr::HwAccel gpuPath = VideoWidget::probeGpuFramePath();
    qInfo("Viplyr: GPU frames shown without a CPU copy: %s",
          gpuPath == viplyr::HwAccel::Vaapi ? "VAAPI (DMA-BUF)"
          : gpuPath == viplyr::HwAccel::Cuda ? "NVDEC (CUDA/OpenGL)"
                                             : "no, decoding on the CPU");
    controller_->setGpuFramePath(gpuPath);

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
    bind(Qt::Key_Left, [this] { seekBy(-kSeekStepMs); });
    bind(Qt::Key_Right, [this] { seekBy(kSeekStepMs); });
    bind(Qt::Key_Comma, [this] { controller_->stepFrame(-1); });
    bind(Qt::Key_Period, [this] { controller_->stepFrame(1); });
    bind(Qt::Key_Up, [this] { changeVolume(kVolumeStep); });
    bind(Qt::Key_Down, [this] { changeVolume(-kVolumeStep); });
    bind(Qt::Key_M, [this] { controlBar_->toggleMute(); });
    bind(Qt::Key_R, [this] { videoWidget_->rotateClockwise(); });
    bind(Qt::Key_F, [this] { toggleFullscreen(); });
    bind(Qt::Key_T, [this] { toggleAlwaysOnTop(); });
    bind(Qt::Key_P, [this] { togglePlaylist(); });
    bind(Qt::Key_I, [this] { toggleInfo(); });
    bind(Qt::Key_S, [this] { saveSnapshot(); });
    bind(Qt::Key_E, [this] { openVideoEditor(); });
    bind(Qt::CTRL | Qt::Key_E, [this] { openVideoEditor(); });
    bind(Qt::SHIFT | Qt::Key_E, [this] { openExportDialog(); });
    bind(Qt::Key_L, [this] { controller_->cycleLoop(); });
    bind(Qt::Key_PageDown, [this] { controller_->seekChapter(1); });
    bind(Qt::Key_PageUp, [this] { controller_->seekChapter(-1); });
    // Picture adjustments, in pairs like mpv: 1/2 contrast, 3/4 brightness, 5/6 gamma,
    // 7/8 saturation; 0 resets them.
    const auto adjust = [this](int PictureAdjustments::*field, const char *name, int delta) {
        PictureAdjustments adjustments = videoWidget_->pictureAdjustments();
        adjustments.*field = qBound(-100, adjustments.*field + delta, 100);
        videoWidget_->setPictureAdjustments(adjustments);
        videoWidget_->showMessage(QString("%1 %2%3").arg(name).arg(adjustments.*field > 0 ? "+" : "").arg(adjustments.*field));
    };
    bind(Qt::Key_1, [adjust] { adjust(&PictureAdjustments::contrast, "Contrast", -kAdjustmentStep); });
    bind(Qt::Key_2, [adjust] { adjust(&PictureAdjustments::contrast, "Contrast", kAdjustmentStep); });
    bind(Qt::Key_3, [adjust] { adjust(&PictureAdjustments::brightness, "Brightness", -kAdjustmentStep); });
    bind(Qt::Key_4, [adjust] { adjust(&PictureAdjustments::brightness, "Brightness", kAdjustmentStep); });
    bind(Qt::Key_5, [adjust] { adjust(&PictureAdjustments::gamma, "Gamma", -kAdjustmentStep); });
    bind(Qt::Key_6, [adjust] { adjust(&PictureAdjustments::gamma, "Gamma", kAdjustmentStep); });
    bind(Qt::Key_7, [adjust] { adjust(&PictureAdjustments::saturation, "Saturation", -kAdjustmentStep); });
    bind(Qt::Key_8, [adjust] { adjust(&PictureAdjustments::saturation, "Saturation", kAdjustmentStep); });
    bind(Qt::Key_0, [this] {
        videoWidget_->setPictureAdjustments(PictureAdjustments());
        videoWidget_->showMessage("Picture reset");
    });
    bind(QKeySequence(Qt::CTRL | Qt::Key_G), [this] { askForFrame(); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_O), [this] { chooseFiles(false); });
    bind(QKeySequence(Qt::CTRL | Qt::Key_U), [this] { chooseUrl(); });
    bind(Qt::Key_Escape, [this] { if (isFullScreen()) toggleFullscreen(); });
}

void MainWindow::loadSettings() {
    const QSettings settings;
    restoreGeometry(settings.value(Setting::kGeometry).toByteArray());
    playlistVisible_ = settings.value(Setting::kPlaylistVisible, true).toBool();
    playlistPanel_->setVisible(playlistVisible_);

    // Never start muted: a muted last session is remembered at its level before muting.
    const int volume = settings.value(Setting::kVolume, 100).toInt();
    controlBar_->setVolume(volume > 0 && volume <= 100 ? volume : 100);
    controller_->setVolume(controlBar_->volume());
    controlBar_->setPlaybackRate(settings.value(Setting::kRate, 1.0).toDouble());
    controller_->setPlaybackRate(controlBar_->playbackRate());
}

void MainWindow::saveSettings() {
    QSettings settings;
    settings.setValue(Setting::kVolume, controlBar_->volumeToRemember());
    settings.setValue(Setting::kRate, controlBar_->playbackRate());
    settings.setValue(Setting::kPlaylistVisible, playlistVisible_);
    if (!isFullScreen()) settings.setValue(Setting::kGeometry, saveGeometry());
}

// ------------------------------------------------------------ opening media

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

void MainWindow::chooseFolder() {
    const QString folder = QFileDialog::getExistingDirectory(this, "Add Folder");
    if (!folder.isEmpty()) addPaths({folder}, false);
}

void MainWindow::chooseUrl() {
    const QString url = QInputDialog::getText(this, "Open Network URL",
                                              "Stream address (rtsp://, http://, https://, ...):").trimmed();
    if (!url.contains("://")) return;
    stack_->setCurrentWidget(playerPage_);
    controller_->appendFiles({url});
    refreshPlaylistPanel();
    controller_->setCurrentIndex(controller_->paths().indexOf(url));
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

void MainWindow::onMediaOpened(const QString &path) {
    const viplyr::MediaInfo info = controller_->mediaInfo();
    QVector<int> chapterStarts;
    for (const viplyr::Chapter &chapter : info.chapters) chapterStarts.append(qRound(chapter.start * 1000.0));
    controlBar_->seekSlider()->setMarks(chapterStarts);
    videoWidget_->setMediaGeometry(info.rotationDegrees, info.pixelAspectRatio);
    overlayTrack_.loadForVideo(path);
    thumbnails_->setSource(path.contains("://") ? QString() : path);
    setWindowTitle(QFileInfo(path).fileName() + " - Viplyr");
    if (exportDialog_) exportDialog_->close();  // it belongs to the previous video
}

void MainWindow::onMediaClosed() {
    videoWidget_->clear();
    controlBar_->seekSlider()->setMarks({});
    overlayTrack_.clear();
    thumbnails_->setSource(QString());
    setWindowTitle("Viplyr");
    if (exportDialog_) exportDialog_->close();
    if (controller_->count() == 0) stack_->setCurrentWidget(startPanel_);
}

// ---------------------------------------------------------- window and view

void MainWindow::toggleFullscreen() {
    if (isFullScreen()) showNormal();
    else showFullScreen();
    videoWidget_->resetView();
    videoWidget_->setFocus();
}

void MainWindow::togglePlaylist() {
    playlistVisible_ = !playlistVisible_;
    playlistPanel_->setVisible(playlistVisible_);
    videoWidget_->setFocus();
}

void MainWindow::toggleAlwaysOnTop() {
    const bool onTop = !(windowFlags() & Qt::WindowStaysOnTopHint);
    setWindowFlag(Qt::WindowStaysOnTopHint, onTop);
    show();  // changing window flags hides the window
    videoWidget_->showMessage(onTop ? "Always on top: on" : "Always on top: off");
}

void MainWindow::toggleInfo() {
    infoVisible_ = !infoVisible_;
    if (infoVisible_) updateInfo(videoWidget_->currentFrameTime());
    else videoWidget_->setInfoText(QString());
}

void MainWindow::changeVolume(int delta) {
    controlBar_->setVolume(qBound(0, controlBar_->volume() + delta, 100));
    videoWidget_->showMessage(QString("Volume %1%").arg(controlBar_->volume()));
}

void MainWindow::seekBy(qint64 milliseconds) {
    controller_->seekRelative(milliseconds);
    videoWidget_->showMessage(QString("%1%2 s   %3")
                                  .arg(milliseconds > 0 ? "+" : "")
                                  .arg(milliseconds / 1000)
                                  .arg(MediaFiles::formatTime(controller_->position())));
}

// -------------------------------------------------------------------- tools

void MainWindow::showTracksMenu() {
    if (!controller_->hasMedia()) return;
    const viplyr::MediaInfo info = controller_->mediaInfo();

    QMenu menu(this);
    menu.setStyleSheet(Theme::Menu);
    const auto addHeading = [&menu](const QString &text) { menu.addAction(text)->setEnabled(false); };
    const auto addChoice = [&menu](QActionGroup *group, const QString &text, bool checked) {
        QAction *action = menu.addAction(text);
        action->setCheckable(true);
        action->setChecked(checked);
        group->addAction(action);
        return action;
    };

    addHeading("Audio");
    QActionGroup *audioGroup = new QActionGroup(&menu);
    for (size_t i = 0; i < info.audioTracks.size(); ++i) {
        const int streamIndex = info.audioTracks[i].streamIndex;
        QAction *action = addChoice(audioGroup, describeTrack(info.audioTracks[i], static_cast<int>(i) + 1),
                                    streamIndex == info.audioStreamIndex);
        connect(action, &QAction::triggered, controller_,
                [this, streamIndex] { controller_->setAudioTrack(streamIndex); });
    }
    if (info.audioTracks.empty()) menu.addAction("No audio")->setEnabled(false);

    menu.addSeparator();
    addHeading("Subtitles");
    QActionGroup *subtitleGroup = new QActionGroup(&menu);
    const int current = controller_->subtitleTrack();
    const auto addSubtitleChoice = [&](const QString &text, int track, bool enabled = true) {
        QAction *action = addChoice(subtitleGroup, text, track == current);
        action->setEnabled(enabled);
        connect(action, &QAction::triggered, controller_, [this, track] { controller_->setSubtitleTrack(track); });
    };
    addSubtitleChoice("Off", PlayerController::kSubtitlesOff);
    if (controller_->hasExternalSubtitles()) {
        addSubtitleChoice("File next to the video (.srt)", PlayerController::kSubtitlesExternalFile);
    }
    for (size_t i = 0; i < info.subtitleTracks.size(); ++i) {
        const viplyr::TrackInfo &track = info.subtitleTracks[i];
        addSubtitleChoice(describeTrack(track, static_cast<int>(i) + 1), track.streamIndex, true);
    }

    const QToolButton *anchor = controlBar_->tracksButton();
    const QPoint above = anchor->mapToGlobal(QPoint(0, 0)) - QPoint(0, menu.sizeHint().height());
    menu.exec(above);
}

void MainWindow::openVideoEditor() {
    if (editorDialog_) {
        editorDialog_->show();
        editorDialog_->raise();
        editorDialog_->activateWindow();
        return;
    }
    controller_->pause();
    auto *editor = new EditorDialog(this);
    editorDialog_ = editor;
    auto *preview = new PlayerController(editor, false);
    preview->setVolume(controller_->volume());
    preview->addVideoSink(editor->videoWidget());
    preview->setGpuFramePath(VideoWidget::probeGpuFramePath());
    connect(editor->videoWidget(), &VideoWidget::gpuFramePathChanged, preview, &PlayerController::setGpuFramePath);
    connect(preview, &PlayerController::timelinePositionChanged, editor, &EditorDialog::setPosition);
    connect(preview, &PlayerController::playingChanged, editor, &EditorDialog::setPlaying);
    connect(preview, &PlayerController::errorOccurred, editor, &EditorDialog::setMessage);
    connect(preview, &PlayerController::mediaOpened, editor, [editor, preview] {
        const auto info = preview->mediaInfo();
        editor->videoWidget()->setMediaGeometry(info.rotationDegrees, info.pixelAspectRatio);
    });
    connect(editor, &EditorDialog::timelineChanged, preview, [editor, preview] {
        preview->setTimeline(editor->playbackClips(), editor->position());
        if (editor->playbackClips().isEmpty()) editor->videoWidget()->clear();
    });
    connect(editor, &EditorDialog::seekRequested, preview, [preview](qint64 ms) { preview->seekTimeline(ms); });
    connect(editor, &EditorDialog::playRequested, preview, &PlayerController::toggleTimeline);
    // Stop the engine while the video widget is still alive, including parent-window teardown.
    connect(editor, &EditorDialog::closing, preview, [preview] { preview->setTimeline({}); });
    connect(editor, &EditorDialog::exportRequested, this, &MainWindow::startExport);
    connect(editor, &EditorDialog::cancelExportRequested, exportJob_, &ExportJob::cancel);
    connect(editor, &EditorDialog::quickExportRequested, this, &MainWindow::openExportDialog);
    connect(exportJob_, &ExportJob::progressChanged, editor, &EditorDialog::setProgress);
    editor->setExportRunning(exportJob_->isRunning());
    editor->show();
    const QString path = controller_->currentPath();
    if (!path.isEmpty() && !path.contains("://")) editor->importPaths({path});
}

void MainWindow::openExportDialog() {
    if (!controller_->hasMedia()) return;
    if (exportDialog_) {
        exportDialog_->raise();
        exportDialog_->activateWindow();
        return;
    }
    const QString path = controller_->currentPath();
    if (path.contains("://")) {
        videoWidget_->showMessage("Network streams cannot be edited");
        return;
    }

    const viplyr::MediaInfo info = controller_->mediaInfo();
    const bool sideways = info.rotationDegrees % 180 != 0;
    const int pictureWidth = sideways ? info.height : info.width;
    const int pictureHeight = sideways ? info.width : info.height;

    exportDialog_ = new ExportDialog(path, info.duration, pictureWidth, pictureHeight, info.hasAudio, this);
    exportDialog_->setExportRunning(exportJob_->isRunning());
    exportDialog_->setPositionProvider([this] { return videoWidget_->currentFrameTime(); });
    // The crop outline is drawn in the file's own pixel grid, which only matches
    // what the dialog edits when the file carries no rotation.
    if (info.rotationDegrees == 0) {
        connect(exportDialog_, &ExportDialog::cropChanged, videoWidget_, &VideoWidget::setCropPreview);
    }
    connect(exportDialog_, &ExportDialog::exportRequested, this, &MainWindow::startExport);
    const double duration = info.duration;
    // Segments in milliseconds, with "until the end" made explicit.
    const auto toMilliseconds = [duration](const QVector<ExportOptions::Segment> &segments) {
        QVector<QPair<qint64, qint64>> result;
        for (const ExportOptions::Segment &segment : segments) {
            const double end = segment.endSeconds > 0.0 ? segment.endSeconds : duration;
            result.append({qRound64(segment.startSeconds * 1000.0), qRound64(end * 1000.0)});
        }
        return result;
    };
    connect(exportDialog_, &ExportDialog::segmentsChanged, this,
            [this, toMilliseconds](const QVector<ExportOptions::Segment> &segments) {
                QVector<QPair<int, int>> ranges;
                for (const auto &range : toMilliseconds(segments)) ranges.append({int(range.first), int(range.second)});
                // The whole video is no selection worth showing.
                if (ranges.size() == 1 && ranges[0].first == 0 && ranges[0].second >= controlBar_->seekSlider()->maximum() - 1) {
                    ranges.clear();
                }
                controlBar_->seekSlider()->setRanges(ranges);
            });
    connect(exportDialog_, &ExportDialog::previewRequested, this,
            [this, toMilliseconds](const QVector<ExportOptions::Segment> &segments) {
                controller_->previewSegments(toMilliseconds(segments));
            });
    exportDialog_->show();
}

void MainWindow::saveSnapshot() {
    const QImage image = videoWidget_->currentImage();
    if (image.isNull()) return;

    const QString folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Viplyr";
    QDir().mkpath(folder);
    const qint64 positionMs = qRound64(videoWidget_->currentFrameTime() * 1000.0);
    const QString name = QString("%1_%2.png")
                             .arg(QFileInfo(controller_->currentPath()).completeBaseName(),
                                  MediaFiles::formatTimePrecise(positionMs).replace(':', '-'));
    videoWidget_->showMessage(image.save(folder + "/" + name) ? "Saved to Pictures/Viplyr: " + name
                                                              : "Could not save the image");
}

void MainWindow::askForFrame() {
    const viplyr::MediaInfo info = controller_->mediaInfo();
    if (info.frameRate <= 0.0) return;
    const int lastFrame = qMax(0, static_cast<int>(info.duration * info.frameRate) - 1);
    const int currentFrame = qRound(videoWidget_->currentFrameTime() * info.frameRate);

    bool accepted = false;
    const int frame = QInputDialog::getInt(this, "Go to frame", QString("Frame number (0 - %1):").arg(lastFrame),
                                           currentFrame, 0, lastFrame, 1, &accepted);
    if (accepted) controller_->goToFrame(frame);
}

void MainWindow::updateInfo(double frameSeconds) {
    if (!infoVisible_) return;
    const viplyr::MediaInfo info = controller_->mediaInfo();
    if (!info.hasVideo) return;
    QString decoder = info.hardwareDecoder.empty() ? "cpu" : QString::fromStdString(info.hardwareDecoder);
    if (videoWidget_->isShowingGpuFrame()) decoder += ", zero-copy";
    videoWidget_->setInfoText(QString("frame %1   %2   %3x%4   %5 fps   %6 (%7)")
                                  .arg(qRound64(frameSeconds * info.frameRate))
                                  .arg(MediaFiles::formatTimePrecise(qRound64(frameSeconds * 1000.0)))
                                  .arg(info.width)
                                  .arg(info.height)
                                  .arg(info.frameRate, 0, 'f', 2)
                                  .arg(QString::fromStdString(info.videoCodec), decoder));
}

// ----------------------------------------------------------------- feedback

void MainWindow::showThumbnail(int positionMs) {
    if (!controller_->hasMedia()) return;

    const qint64 wanted = positionMs / kThumbnailStepMs * kThumbnailStepMs;
    if (thumbnailPopup_->isHidden()) thumbnailPopup_->setPicture(QPixmap());
    // The previous picture stays until the new one arrives, so sweeping does not flicker.
    wantedThumbnailMs_ = wanted;
    QString chapterTitle;
    for (const viplyr::Chapter &chapter : controller_->mediaInfo().chapters) {
        if (chapter.start * 1000.0 <= positionMs) chapterTitle = QString::fromStdString(chapter.title);
    }
    thumbnailPopup_->setTime(positionMs, chapterTitle);
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
        case QEvent::Resize:
            if (watched == videoWidget_) layoutControlBar();
            break;
        default:
            break;
    }
    return QWidget::eventFilter(watched, event);
}

void MainWindow::layoutControlBar() {
    const int barHeight = controlBar_->sizeHint().height();
    controlBar_->setGeometry(0, videoWidget_->height() - barHeight, videoWidget_->width(), barHeight);
    if (exportProgress_) {
        exportProgress_->move(videoWidget_->width() - exportProgress_->width() - 14,
                              videoWidget_->height() - barHeight - exportProgress_->height() - 10);
    }
    videoWidget_->setBottomInset(chromeVisible_ ? barHeight : 0);
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

    if (visible) controlBar_->show();  // hidden again when the fade-out finishes
    chromeFade_->stop();
    chromeFade_->setEndValue(visible ? 1.0 : 0.0);
    chromeFade_->start();
    videoWidget_->setBottomInset(visible ? controlBar_->height() : 0);

    // The playlist only gets out of the way in fullscreen.
    if (visible) playlistPanel_->setVisible(playlistVisible_);
    else if (isFullScreen()) playlistPanel_->hide();

    if (visible) videoWidget_->unsetCursor();
    else videoWidget_->setCursor(Qt::BlankCursor);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (editorDialog_ && !editorDialog_->close()) { event->ignore(); return; }
    saveSettings();
    QWidget::closeEvent(event);
}
