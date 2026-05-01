#pragma once
#include <QWidget>
#include <QStackedWidget>
#include <QListWidget>

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

    void setupPlayerPanel();
    void wireConnections();
    void appendFiles(const QStringList &files);
    void removeSelectedPlaylistItem();
    void toggleFullscreen();
    void togglePlaylistVisibility();
};
