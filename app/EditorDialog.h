#pragma once
#include <QDialog>
#include "EditProject.h"
#include "ExportCommand.h"
#include "PlayerController.h"

class EditTimeline;
class QLabel;
class QPushButton;
class QComboBox;
class QProgressBar;
class QScrollArea;
class VideoWidget;

// Editing UI and undo history. Playback and exporting are wired by MainWindow.
class EditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit EditorDialog(QWidget *parent = nullptr);
    ~EditorDialog() override;
    VideoWidget *videoWidget() const { return video_; }
    QVector<PlayerController::TimelineClip> playbackClips() const;
    qint64 position() const { return positionMs_; }
    void importPaths(const QStringList &paths, int before = -1);
    void setPosition(qint64 milliseconds);
    void setPlaying(bool playing);
    void setExportRunning(bool running);
    void setProgress(double fraction);
    void setMessage(const QString &message);

signals:
    void timelineChanged();
    void seekRequested(qint64 positionMs);
    void playRequested();
    void closing();
    void exportRequested(const ExportOptions &options, double durationSeconds);
    void cancelExportRequested();
    void quickExportRequested();

protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    struct State { EditProject project; int selected; qint64 position; };
    State state() const;
    void restore(const State &state);
    void commit(const State &before);
    void refresh(bool preview = true);
    void undo();
    void redo();
    void split();
    void remove();
    void duplicate();
    void move(int from, int before);
    void fitTimeline();
    void exportVideo();
    QByteArray projectJson() const;
    bool saveProject();
    void loadProject();

    EditProject project_;
    int selected_ = -1;
    qint64 positionMs_ = 0;
    QVector<State> undo_, redo_;
    State gestureBefore_;
    bool gestureChanged_ = false;
    bool importing_ = false;
    bool loadingProject_ = false;
    std::shared_ptr<std::atomic<bool>> importCancelled_ = std::make_shared<std::atomic<bool>>(false);
    bool exportRunning_ = false;
    QByteArray savedJson_;
    QString projectPath_;
    VideoWidget *video_;
    EditTimeline *timeline_;
    QScrollArea *scroll_;
    QLabel *timeLabel_, *clipLabel_, *statusLabel_;
    QPushButton *play_, *undoButton_, *redoButton_, *splitButton_, *deleteButton_, *duplicateButton_, *exportButton_;
    QComboBox *canvas_, *encoder_;
    QProgressBar *progress_;
    QPushButton *cancelButton_;
};
