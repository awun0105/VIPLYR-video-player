#pragma once
#include <QLabel>
#include <QPixmap>
#include <QWidget>

// The small preview shown above the seek bar: a picture and a time.
class ThumbnailPopup : public QWidget {
    Q_OBJECT
public:
    explicit ThumbnailPopup(QWidget *parent = nullptr);
    void setTime(qint64 positionMs, const QString &chapter = QString());
    void setPicture(const QPixmap &pixmap);  // a null pixmap shows "Loading..."

private:
    QLabel *imageLabel_;
    QLabel *timeLabel_;
};
