#pragma once
#include <QWidget>
#include <QSlider>
#include <QListWidget>
#include <QStringList>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>

class QPushButton;

// --- Custom Slider with "Jump to Click" behavior ---
class JumpSlider : public QSlider
{
    Q_OBJECT
public:
    explicit JumpSlider(const QString &activeColor, QWidget *parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent *ev) override;
};

// --- Bottom Control Bar ---
class ControlBar : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);
    JumpSlider* getSeekSlider() const { return seekSlider; }
    JumpSlider* getVolSlider() const { return volSlider; }
    QComboBox* getSpeedCombo() const { return speedCombo; }
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

public slots:
    void toggleMute();

private slots:
    void updateVolumeIcon(int value);

private:
    JumpSlider *seekSlider;
    JumpSlider *volSlider;
    QComboBox *speedCombo;
    QLabel *timeLabel;
    QPushButton *playButton;
    QPushButton *muteButton;
    QPushButton *playlistToggleButton;
    int lastVolume;
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
    QLineEdit *searchEdit;
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
