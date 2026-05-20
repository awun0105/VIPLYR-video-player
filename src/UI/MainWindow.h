#pragma once
#include <QWidget>
#include <QStackedWidget>
#include <QListWidget>
#include <QFutureWatcher>
#include <QStringList>
#include <QTimer>
#include "ThumbnailPopup.h"

class MediaEngine;
class VideoCanvas;
class ControlBar;
class StartPanel;
class PlaylistPanel;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void loadAndPlay(const QStringList &files);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onAsyncFilesProcessed();
    void handleInactivity();
    void resetInactivity();

private:
    QStackedWidget *stack;
    StartPanel *startPanel;
    QWidget *playerPanel;

    MediaEngine *engine;
    VideoCanvas *canvas;
    ControlBar *controls;
    PlaylistPanel *playlistPanel;
    QListWidget *playlistUI;
    bool playlistVisible;

    QFutureWatcher<QStringList> *watcher;
    QTimer *hideTimer;
    
    ThumbnailPopup *thumbPopup;

    void setupPlayerPanel();
    void wireConnections();
    void appendFiles(const QStringList &files);
    void removeSelectedPlaylistItem();
    void toggleFullscreen();
    void togglePlaylistVisibility();
};
