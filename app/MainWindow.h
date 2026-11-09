#pragma once
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "IdleInhibitor.h"

class ControlBar;
class PlayerController;
class PlaylistPanel;
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

    void addPaths(const QStringList &paths, bool replacePlaylist);
    void chooseFiles(bool replacePlaylist);
    void removeSelected();
    void refreshPlaylistPanel();

    void toggleFullscreen();
    void togglePlaylist();
    void toggleAlwaysOnTop();
    void changeVolume(int delta);

    void showThumbnail(int positionMs);
    void showError(const QString &message);
    void onPlayingChanged(bool playing);

    // Auto-hide of the controls ("chrome") while a video plays.
    void onUserActivity();
    void hideChromeIfIdle();
    void setChromeVisible(bool visible);

    PlayerController *controller_;
    ThumbnailProvider *thumbnails_;
    IdleInhibitor idleInhibitor_;

    QStackedWidget *stack_;
    StartPanel *startPanel_;
    QWidget *playerPage_;
    VideoWidget *videoWidget_;
    ControlBar *controlBar_;
    PlaylistPanel *playlistPanel_;
    ThumbnailPopup *thumbnailPopup_;

    QTimer hideTimer_;
    bool playlistVisible_ = true;
    bool chromeVisible_ = true;
    bool errorDialogOpen_ = false;
    qint64 wantedThumbnailMs_ = -1;
};
