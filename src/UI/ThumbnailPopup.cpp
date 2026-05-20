#include "ThumbnailPopup.h"
#include "../Utils/Constants.h"
#include <QVBoxLayout>

ThumbnailPopup::ThumbnailPopup(QWidget *parent) : QWidget(parent) {
    setFixedSize(160, 120);
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet("background-color: #222; border: 1px solid #1E90FF; border-radius: 4px;");
    hide();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2); layout->setSpacing(2);

    imageLabel = new QLabel;
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setStyleSheet("background-color: black; color: #555;");
    imageLabel->setText("Loading...");

    timeLabel = new QLabel("00:00");
    timeLabel->setAlignment(Qt::AlignCenter);
    timeLabel->setStyleSheet("color: white; font-weight: bold; font-family: monospace; font-size: 11px;");

    layout->addWidget(imageLabel, 1);
    layout->addWidget(timeLabel);
}

void ThumbnailPopup::setPreview(qint64 pos, const QPixmap &pixmap) {
    timeLabel->setText(Constants::formatTime(pos));
    if (!pixmap.isNull()) {
        imageLabel->setPixmap(pixmap);
        imageLabel->setText("");
    } else {
        imageLabel->clear();
        imageLabel->setText("Loading...");
    }
}
