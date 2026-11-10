#pragma once
#include <QWidget>
#include "EditProject.h"

// One ripple-editing track: clips stay adjacent, and drops choose an insertion boundary.
class EditTimeline : public QWidget {
    Q_OBJECT
public:
    explicit EditTimeline(QWidget *parent = nullptr);
    void setProject(const EditProject &project, int selected);
    void setPosition(qint64 positionMs);
    void setZoom(double pixelsPerSecond);
    double zoom() const { return zoom_; }
    QSize sizeHint() const override;

signals:
    void clipSelected(int index);
    void seekRequested(qint64 positionMs);
    void trimStarted();
    void trimRequested(int index, qint64 inMs, qint64 outMs, bool rightEdge);
    void trimFinished();
    void moveRequested(int from, int before);
    void filesDropped(const QStringList &paths, int before);
    void zoomRequested(double pixelsPerSecond);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    QRectF clipRect(int index) const;
    qint64 positionAt(double x) const;
    int insertionAt(double x) const;
    double xAt(qint64 milliseconds) const;

    EditProject project_;
    int selected_ = -1;
    qint64 positionMs_ = 0;
    double zoom_ = 40;
    QPoint pressPosition_;
    EditClip original_;
    enum class Drag { None, Scrub, Left, Right, Clip };
    Drag drag_ = Drag::None;
    int dropBefore_ = -1;
};
