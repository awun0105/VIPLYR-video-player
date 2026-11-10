#include "JumpSlider.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionSlider>

#include "Theme.h"

JumpSlider::JumpSlider(const QString &filledStyle, QWidget *parent) : QSlider(Qt::Horizontal, parent) {
    setStyleSheet(QString("QSlider::groove:horizontal { %1 } QSlider::sub-page:horizontal { %2 } "
                          "QSlider::handle:horizontal { %3 }")
                      .arg(Theme::SliderGroove, filledStyle, Theme::SliderHandle));
    setMouseTracking(true);
}

int JumpSlider::valueAt(int x) const {
    QStyleOptionSlider option;
    initStyleOption(&option);
    const int handleLength = style()->pixelMetric(QStyle::PM_SliderLength, &option, this);
    return QStyle::sliderValueFromPosition(minimum(), maximum(), x - handleLength / 2,
                                           width() - handleLength, option.upsideDown);
}

int JumpSlider::xFor(int value) const {
    QStyleOptionSlider option;
    initStyleOption(&option);
    const int handleLength = style()->pixelMetric(QStyle::PM_SliderLength, &option, this);
    return QStyle::sliderPositionFromValue(minimum(), maximum(), value, width() - handleLength, option.upsideDown) +
           handleLength / 2;
}

void JumpSlider::setMarks(const QVector<int> &values) {
    marks_ = values;
    update();
}

void JumpSlider::setHighlight(int from, int to) {
    highlightFrom_ = from;
    highlightTo_ = to;
    update();
}

void JumpSlider::paintEvent(QPaintEvent *event) {
    QSlider::paintEvent(event);
    if (marks_.isEmpty() && highlightFrom_ < 0) return;

    QPainter painter(this);
    const int middle = height() / 2;
    if (highlightFrom_ >= 0) {
        const QColor loopColor(255, 196, 0);
        const int start = xFor(highlightFrom_);
        if (highlightTo_ >= 0) painter.fillRect(QRect(QPoint(start, middle - 3), QPoint(xFor(highlightTo_), middle + 2)),
                                                QColor(255, 196, 0, 110));
        painter.fillRect(start - 1, middle - 7, 2, 14, loopColor);
        if (highlightTo_ >= 0) painter.fillRect(xFor(highlightTo_) - 1, middle - 7, 2, 14, loopColor);
    }
    for (int value : marks_) {
        if (value <= minimum()) continue;  // a chapter at the very start needs no mark
        painter.fillRect(xFor(value) - 1, middle - 4, 2, 8, QColor(255, 255, 255, 200));
    }
}

void JumpSlider::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        const int value = valueAt(event->position().toPoint().x());
        setValue(value);
        emit sliderMoved(value);
    }
    QSlider::mousePressEvent(event);
}

void JumpSlider::mouseMoveEvent(QMouseEvent *event) {
    emit hoverValueChanged(valueAt(event->position().toPoint().x()));
    QSlider::mouseMoveEvent(event);
}

void JumpSlider::leaveEvent(QEvent *event) {
    emit hoverLeft();
    QSlider::leaveEvent(event);
}
