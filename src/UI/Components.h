#pragma once
#include <QWidget>
#include <QSlider>
#include <QListWidget>
#include <QStringList>
#include <QLabel>
// --- Slider Tùy biến ---
class SeekSlider : public QSlider
{
    Q_OBJECT
public:
    explicit SeekSlider(QWidget *parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent *ev) override;
};

// --- Thanh Điều khiển dưới cùng ---
class ControlBar : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);
    SeekSlider* getSeekSlider() const { return seekSlider; }
    QSlider* getVolSlider() const { return volSlider; }
    QLabel* getTimeLabel() const { return timeLabel; }

signals:
    void playClicked();
    void stopClicked();
    void nextClicked();
    void prevClicked();
    void rewindClicked();
    void forwardClicked();

private:
    SeekSlider *seekSlider;
    QSlider *volSlider;
    QLabel *timeLabel;
};

// --- Màn hình Khởi động Kéo/Thả ---
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