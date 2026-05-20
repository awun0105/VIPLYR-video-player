#pragma once
#include <QWidget>
#include <QLabel>
#include <QPixmap>

class ThumbnailPopup : public QWidget {
    Q_OBJECT
public:
    explicit ThumbnailPopup(QWidget *parent = nullptr);
    void setPreview(qint64 pos, const QPixmap &pixmap);

private:
    QLabel *imageLabel;
    QLabel *timeLabel;
};
