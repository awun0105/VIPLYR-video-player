#pragma once
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "ExportCommand.h"
#include "IdleInhibitor.h"
#include "OverlayTrack.h"

class ControlBar;
class ExportDialog;
class EditorDialog;
class ExportJob;
class QProgressBar;
class PlayerController;
class PlaylistPanel;
class QPropertyAnimation;
class QStackedWidget;
class StartPanel;
class ThumbnailPopup;
class ThumbnailProvider;
class VideoWidget;

// The application window. Builds the widgets and connects them to the
// PlayerController; holds no playback logic of its own.
class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Replaces the playlist with the videos found in these files and folders.
    void openPaths(const QStringList &paths);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildLayout();
    void wireConnections();
    void createShortcuts();
    void loadSettings();
    void saveSettings();

    // Opening media
    void addPaths(const QStringList &paths, bool replacePlaylist);
    void chooseFiles(bool replacePlaylist);
    void chooseFolder();
    void chooseUrl();
    void removeSelected();
    void refreshPlaylistPanel();
    void onMediaOpened(const QString &path);
    void onMediaClosed();

    // Window and view
    void toggleFullscreen();
    void togglePlaylist();
    void toggleAlwaysOnTop();
    void toggleInfo();
    void changeVolume(int delta);
    void seekBy(qint64 milliseconds);

    // Tools
    void showTracksMenu();
    void openExportDialog();
    void openVideoEditor();
    void startExport(const ExportOptions &options, double totalSeconds);
    void createExportProgress();
    void saveSnapshot();
    void askForFrame();
    void updateInfo(double frameSeconds);

    // Feedback
    void showThumbnail(int positionMs);
    void showError(const QString &message);
    void onPlayingChanged(bool playing);

    // The control bar floats over the video and fades away while a video plays.
    void layoutControlBar();
    void onUserActivity();
    void hideChromeIfIdle();
    void setChromeVisible(bool visible);

    PlayerController *controller_;
    ThumbnailProvider *thumbnails_;
    IdleInhibitor idleInhibitor_;
    OverlayTrack overlayTrack_;

    QStackedWidget *stack_;
    StartPanel *startPanel_;
    QWidget *playerPage_;
    VideoWidget *videoWidget_;
    ControlBar *controlBar_;
    PlaylistPanel *playlistPanel_;
    ThumbnailPopup *thumbnailPopup_;
    QPointer<ExportDialog> exportDialog_;
    QPointer<EditorDialog> editorDialog_;
    ExportJob *exportJob_ = nullptr;
    QWidget *exportProgress_ = nullptr;  // shown over the video while an export runs
    QProgressBar *exportProgressBar_ = nullptr;
    QPropertyAnimation *chromeFade_;

    QTimer hideTimer_;
    bool playlistVisible_ = true;
    bool chromeVisible_ = true;
    bool infoVisible_ = false;
    bool errorDialogOpen_ = false;
    qint64 wantedThumbnailMs_ = -1;
};
