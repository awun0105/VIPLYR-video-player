#include "JumpSlider.h"

#include <QMouseEvent>
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
