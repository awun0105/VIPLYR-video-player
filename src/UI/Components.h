#pragma once
#include <QWidget>
#include <QSlider>
#include <QListWidget>
#include <QStringList>
#include <QLabel>

class QPushButton;

// --- Custom Slider ---
class SeekSlider : public QSlider
{
    Q_OBJECT
public:
    explicit SeekSlider(QWidget *parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent *ev) override;
};

// --- Bottom Control Bar ---
class ControlBar : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);
    SeekSlider* getSeekSlider() const { return seekSlider; }
    QSlider* getVolSlider() const { return volSlider; }
    QLabel* getTimeLabel() const { return timeLabel; }
    void setPlayButtonPlaying(bool playing);
    void setPlaylistVisible(bool visible);

signals:
    void playClicked();
    void stopClicked();
    void nextClicked();
    void prevClicked();
    void rewindClicked();
    void forwardClicked();
    void playlistToggleRequested();

private:
    SeekSlider *seekSlider;
    QSlider *volSlider;
    QLabel *timeLabel;
    QPushButton *playButton;
    QPushButton *playlistToggleButton;
};

// --- Playlist Panel ---
class PlaylistPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPanel(QWidget *parent = nullptr);
    QListWidget* getListWidget() const { return playlistWidget; }

signals:
    void filesAppendRequested(const QStringList &files);
    void removeSelectedRequested();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QListWidget *playlistWidget;
};

// --- Drag & Drop Start Panel ---
class StartPanel : public QWidget {
    Q_OBJECT
public:
    explicit StartPanel(QWidget *parent = nullptr);
signals:
    void filesReady(const QStringList &files);
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};
