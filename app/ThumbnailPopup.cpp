#include "ThumbnailPopup.h"

#include <QVBoxLayout>

#include "MediaFiles.h"

ThumbnailPopup::ThumbnailPopup(QWidget *parent) : QWidget(parent) {
    setFixedSize(160, 120);
    setAttribute(Qt::WA_StyledBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setStyleSheet("background-color: #222; border: 1px solid #1E90FF; border-radius: 4px;");
    hide();

    imageLabel_ = new QLabel;
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setStyleSheet("background-color: black; color: #555; border: none;");

    timeLabel_ = new QLabel("00:00");
    timeLabel_->setAlignment(Qt::AlignCenter);
    timeLabel_->setStyleSheet("color: white; font-weight: bold; font-family: monospace; "
                              "font-size: 11px; border: none;");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);
    layout->addWidget(imageLabel_, 1);
    layout->addWidget(timeLabel_);
}

void ThumbnailPopup::setTime(qint64 positionMs) {
    timeLabel_->setText(MediaFiles::formatTime(positionMs));
}

void ThumbnailPopup::setPicture(const QPixmap &pixmap) {
    if (pixmap.isNull()) imageLabel_->setText("Loading...");
    else imageLabel_->setPixmap(pixmap);
}
