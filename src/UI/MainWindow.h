#pragma once
#include <QWidget>
#include <QStackedWidget>
#include <QListWidget>

class MediaEngine;
class VideoCanvas;
class ControlBar;
class StartPanel;

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void loadAndPlay(const QStringList &files);

private:
    QStackedWidget *stack;
    StartPanel *startPanel;
    QWidget *playerPanel;

    MediaEngine *engine;
    VideoCanvas *canvas;
    ControlBar *controls;
    QListWidget *playlistUI;

    void setupPlayerPanel();
    void wireConnections();
};