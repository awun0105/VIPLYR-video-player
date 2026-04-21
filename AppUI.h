#ifndef APPUI_H
#define APPUI_H

#include <QWidget>
#include <QSlider>
#include <QStringList>
#include "PlayerView.h"

class QMediaPlaylist;
class QListWidget;

// --- 1. THANH TUA VIDEO TÙY BIẾN (Fix lỗi click) ---
class SeekSlider : public QSlider {
    Q_OBJECT
public:
    explicit SeekSlider(QWidget *parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent *ev) override;
};

// --- 2. MÀN HÌNH KHỞI ĐỘNG (Kéo Thả & Nút Chọn) ---
class StartScreen : public QWidget {
    Q_OBJECT
public:
    explicit StartScreen(QWidget *parent = nullptr);
signals:
    void videosSelected(const QStringList &files);
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private slots:
    void openFileDialog();
};

// --- 3. MÀN HÌNH PHÁT VIDEO CHÍNH ---
class PlayerWindow : public QWidget {
    Q_OBJECT
public:
    explicit PlayerWindow(QWidget *parent = nullptr);
    void loadVideos(const QStringList &fileNames);
private:
    PlayerView *view;
    QMediaPlaylist *playlist;
    QListWidget *playlistUI;
    SeekSlider *seekSlider;
    void setupUI();
    void connectSignals();
};

#endif // APPUI_H