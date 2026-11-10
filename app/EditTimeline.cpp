#include "EditTimeline.h"
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include "MediaFiles.h"

namespace {
constexpr int kMargin = 24;
constexpr int kTrackTop = 58;
constexpr int kTrackHeight = 100;
constexpr int kHeight = kTrackTop + kTrackHeight + 14;
constexpr const char *kClipMime = "application/x-viplyr-edit-clip";
}

EditTimeline::EditTimeline(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);
    setMouseTracking(true);
    setFixedHeight(kHeight);
    setToolTip("Drag the yellow edges to trim. Drag a clip to reorder. Click the ruler to scrub. Ctrl+wheel to zoom.");
}

void EditTimeline::setProject(const EditProject &project, int selected) {
    project_ = project;
    selected_ = selected;
    setMinimumWidth(sizeHint().width());
    updateGeometry();
    update();
}

void EditTimeline::setPosition(qint64 positionMs) {
    if (positionMs_ == positionMs) return;
    // Called 50 times a second during playback: repaint only where the playhead was and is.
    const double before = xAt(positionMs_);
    positionMs_ = positionMs;
    for (double x : {before, xAt(positionMs_)}) update(QRect(qFloor(x) - 8, 0, 17, height()));
}

void EditTimeline::setZoom(double pixelsPerSecond) {
    zoom_ = std::clamp(pixelsPerSecond, 0.02, 400.0);
    setMinimumWidth(sizeHint().width());
    updateGeometry();
    update();
}

QSize EditTimeline::sizeHint() const {
    return QSize(std::max(600, qCeil(project_.duration() / 1000.0 * zoom_) + 2 * kMargin), kHeight);
}

double EditTimeline::xAt(qint64 ms) const { return kMargin + ms / 1000.0 * zoom_; }

QRectF EditTimeline::clipRect(int index) const {
    return QRectF(xAt(project_.startOf(index)), kTrackTop,
                  project_.clips[index].duration() / 1000.0 * zoom_, kTrackHeight);
}

qint64 EditTimeline::positionAt(double x) const {
    qint64 ms = std::clamp(qRound64((x - kMargin) / zoom_ * 1000), qint64(0), project_.duration());
    for (int i = 0; i <= project_.clips.size(); ++i) {
        const qint64 boundary = project_.startOf(i);
        if (std::fabs(xAt(boundary) - x) < 6) return boundary;
    }
    return ms;
}

int EditTimeline::insertionAt(double x) const {
    for (int i = 0; i < project_.clips.size(); ++i)
        if (x < clipRect(i).center().x()) return i;
    return project_.clips.size();
}

void EditTimeline::paintEvent(QPaintEvent *event) {
    QPainter p(this);
    p.fillRect(event->rect(), QColor("#121419"));
    p.setRenderHint(QPainter::Antialiasing);
    const QRect viewport = visibleRegion().boundingRect();  // what the scroll area shows
    const QRect visible = viewport.intersected(event->rect());  // what needs drawing now
    const double candidates[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 3600};
    double tick = 3600;
    for (double value : candidates) if (value * zoom_ >= 75) { tick = value; break; }
    const double start = std::max(0.0, std::floor((visible.left() - kMargin) / zoom_ / tick) * tick);
    p.setPen(QColor("#929ba9"));
    for (double t = start; xAt(qRound64(t * 1000)) < visible.right(); t += tick) {
        const int x = qRound(xAt(qRound64(t * 1000)));
        p.drawLine(x, 32, x, 45);
        p.drawText(QRect(x + 4, 9, 90, 20), MediaFiles::formatTimePrecise(qRound64(t * 1000)).mid(3));
    }
    if (project_.clips.isEmpty()) {
        p.setPen(QColor("#aab4c2"));
        p.drawText(QRect(30, 60, viewport.width() - 60, 90), Qt::AlignCenter,
                   "Drop videos here to start your edit");
    }
    for (int i = 0; i < project_.clips.size(); ++i) {
        const QRectF r = clipRect(i);
        if (!r.intersects(visible)) continue;
        const auto &clip = project_.clips[i];
        p.save();
        p.setClipRect(r.adjusted(2, 2, -2, -2));
        p.fillRect(r, QColor(i == selected_ ? "#4c4220" : "#253a51"));
        if (!clip.thumbnails.isEmpty()) {
            const int tileWidth = 92;
            const double first = std::max(r.left(), static_cast<double>(visible.left()));
            const int firstTile = std::max(0, static_cast<int>((first - r.left()) / tileWidth));
            for (int n = firstTile; r.left() + n * tileWidth < std::min(r.right(), double(visible.right())); ++n) {
                const double fraction = (n * tileWidth + tileWidth / 2.0) / std::max(1.0, r.width());
                const double source = clip.inMs + std::clamp(fraction, 0.0, 1.0) * clip.duration();
                const int imageIndex = std::clamp(int(source / clip.sourceDurationMs * clip.thumbnails.size()),
                                                 0, int(clip.thumbnails.size()) - 1);
                p.drawImage(QRectF(r.left() + n * tileWidth, r.top() + 27, tileWidth, 58), clip.thumbnails[imageIndex]);
            }
        }
        p.setPen(Qt::white);
        p.drawText(r.adjusted(10, 3, -10, -72), Qt::AlignLeft | Qt::AlignVCenter,
                   p.fontMetrics().elidedText(QFileInfo(clip.path).fileName(), Qt::ElideMiddle, std::max(0, int(r.width() - 20))));
        p.setPen(QColor("#d6dce6"));
        p.drawText(r.adjusted(10, 84, -10, -1), Qt::AlignLeft | Qt::AlignVCenter,
                   QString("%1  ·  %2").arg(MediaFiles::formatTimePrecise(clip.duration()).mid(3),
                                              clip.hasAudio ? "Audio" : "Silent"));
        p.restore();
        p.setPen(QPen(QColor(i == selected_ ? "#f8cf55" : "#385778"), i == selected_ ? 3 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 5, 5);
        if (i == selected_) {
            for (double x : {r.left(), r.right() - 10}) {
                p.fillRect(QRectF(x, r.top() + 3, 10, r.height() - 6), QColor("#f8cf55"));
                p.setPen(QPen(QColor("#4a3b0c"), 2));
                p.drawLine(QPointF(x + 5, r.center().y() - 11), QPointF(x + 5, r.center().y() + 11));
            }
        }
    }
    const double head = xAt(positionMs_);
    p.setPen(QPen(QColor("#f5f7fa"), 2));
    p.drawLine(QPointF(head, 38), QPointF(head, kTrackTop + kTrackHeight + 8));
    p.setBrush(Qt::white);
    p.drawPolygon(QPolygonF{QPointF(head - 6, 31), QPointF(head + 6, 31), QPointF(head, 40)});
    if (dropBefore_ >= 0) {
        p.setPen(QPen(QColor("#65d6b3"), 4));
        const double x = xAt(project_.startOf(dropBefore_));
        p.drawLine(QPointF(x, 51), QPointF(x, 168));
    }
}

void EditTimeline::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    pressPosition_ = event->pos();
    if (selected_ >= 0 && selected_ < project_.clips.size() &&
        event->position().y() >= kTrackTop && event->position().y() <= kTrackTop + kTrackHeight) {
        const auto r = clipRect(selected_);
        const bool left = std::fabs(event->position().x() - r.left()) < 12;
        const bool right = std::fabs(event->position().x() - r.right()) < 12;
        if (left || right) {
            original_ = project_.clips[selected_];
            drag_ = left ? Drag::Left : Drag::Right;
            emit trimStarted();
            return;
        }
    }
    if (std::fabs(event->position().x() - xAt(positionMs_)) <= 7) {
        drag_ = Drag::Scrub;
        emit seekRequested(positionAt(event->position().x()));
        return;
    }
    for (int i = 0; i < project_.clips.size(); ++i) {
        if (clipRect(i).contains(event->position())) {
            drag_ = Drag::Clip;
            selected_ = i;
            emit clipSelected(i);
            emit seekRequested(positionAt(event->position().x()));
            return;
        }
    }
    drag_ = Drag::Scrub;
    emit seekRequested(positionAt(event->position().x()));
}

void EditTimeline::mouseMoveEvent(QMouseEvent *event) {
    if (drag_ == Drag::Left || drag_ == Drag::Right) {
        const qint64 delta = qRound64((event->position().x() - pressPosition_.x()) / zoom_ * 1000);
        qint64 in = original_.inMs, out = original_.outMs;
        const qint64 frame = std::min(original_.sourceDurationMs, std::max<qint64>(1, qRound64(1000.0 / original_.frameRate)));
        if (drag_ == Drag::Left) in = std::clamp(original_.inMs + delta, qint64(0), original_.outMs - frame);
        else out = std::clamp(original_.outMs + delta, original_.inMs + frame, original_.sourceDurationMs);
        emit trimRequested(selected_, in, out, drag_ == Drag::Right);
        return;
    }
    if (drag_ == Drag::Scrub) emit seekRequested(positionAt(event->position().x()));
    if (drag_ == Drag::Clip && (event->pos() - pressPosition_).manhattanLength() >= QApplication::startDragDistance()) {
        QDrag drag(this);
        auto *mime = new QMimeData;
        mime->setData(kClipMime, project_.clips[selected_].id.toUtf8());
        drag.setMimeData(mime);
        if (!project_.clips[selected_].thumbnails.isEmpty())
            drag.setPixmap(QPixmap::fromImage(project_.clips[selected_].thumbnails.first()).scaledToWidth(130));
        drag_ = Drag::None;
        drag.exec(Qt::MoveAction);
    }
    if (drag_ == Drag::None) {
        bool edge = false;
        if (selected_ >= 0 && selected_ < project_.clips.size()) {
            const auto r = clipRect(selected_);
            edge = event->position().y() >= r.top() && event->position().y() <= r.bottom() &&
                   (std::fabs(event->position().x() - r.left()) < 12 || std::fabs(event->position().x() - r.right()) < 12);
        }
        setCursor(edge ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }
}

void EditTimeline::mouseReleaseEvent(QMouseEvent *) {
    if (drag_ == Drag::Left || drag_ == Drag::Right) emit trimFinished();
    drag_ = Drag::None;
}

void EditTimeline::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat(kClipMime) || event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void EditTimeline::dragMoveEvent(QDragMoveEvent *event) {
    dropBefore_ = insertionAt(event->position().x());
    event->acceptProposedAction();
    update();
}

void EditTimeline::dragLeaveEvent(QDragLeaveEvent *) { dropBefore_ = -1; update(); }

void EditTimeline::dropEvent(QDropEvent *event) {
    const int before = insertionAt(event->position().x());
    if (event->mimeData()->hasFormat(kClipMime)) {
        const QString id = QString::fromUtf8(event->mimeData()->data(kClipMime));
        for (int i = 0; i < project_.clips.size(); ++i)
            if (project_.clips[i].id == id) { emit moveRequested(i, before); break; }
    } else emit filesDropped(MediaFiles::localPaths(event->mimeData()), before);
    dropBefore_ = -1;
    event->acceptProposedAction();
    update();
}

void EditTimeline::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        emit zoomRequested(zoom_ * std::pow(1.2, event->angleDelta().y() / 120.0));
        event->accept();
    } else QWidget::wheelEvent(event);
}
