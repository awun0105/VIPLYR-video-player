#include "Components.h"
#include "../Utils/Theme.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMouseEvent>
#include <QStyleOptionSlider>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QFileDialog>

// --- SeekSlider ---
SeekSlider::SeekSlider(QWidget *parent) : QSlider(Qt::Horizontal, parent) {
    setStyleSheet(QString("QSlider::groove:horizontal { %1 } QSlider::sub-page:horizontal { %2 } QSlider::handle:horizontal { %3 }")
                  .arg(Theme::SliderGroove, Theme::SeekColor, Theme::SliderHandle));
}
void SeekSlider::mousePressEvent(QMouseEvent *ev) {
    if (ev->button() == Qt::LeftButton) {
        QStyleOptionSlider opt; initStyleOption(&opt);
        int sr = style()->pixelMetric(QStyle::PM_SliderLength, &opt, this);
        int w = width();
        int newValue = QStyle::sliderValueFromPosition(minimum(), maximum(), ev->pos().x() - sr / 2, w - sr, opt.upsideDown);
        setValue(newValue);
        emit sliderMoved(newValue);
    }
    QSlider::mousePressEvent(ev);
}

// --- ControlBar ---
ControlBar::ControlBar(QWidget *parent) : QWidget(parent) {
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(15, 10, 15, 10); layout->setSpacing(10);

    QPushButton *btnPrev = new QPushButton("⏮");
    QPushButton *btnRewind = new QPushButton("⏪ -5s");
    QPushButton *btnPlay = new QPushButton("⏯ Play/Pause");
    QPushButton *btnStop = new QPushButton("⏹ Stop");
    QPushButton *btnForward = new QPushButton("+5s ⏩");
    QPushButton *btnNext = new QPushButton("⏭");

    for(auto btn : {btnPrev, btnRewind, btnPlay, btnStop, btnForward, btnNext}) {
        btn->setStyleSheet(Theme::Button);
    }

    seekSlider = new SeekSlider;
    timeLabel = new QLabel("00:00 / 00:00");
    timeLabel->setStyleSheet("color: #ccc; font-size: 13px; font-weight: bold; margin: 0 10px; font-family: monospace;");
    QLabel *volLabel = new QLabel("🔊");
    volSlider = new QSlider(Qt::Horizontal);
    volSlider->setRange(0, 100); volSlider->setValue(100); volSlider->setFixedWidth(100);
    volSlider->setStyleSheet(QString("QSlider::groove:horizontal { %1 } QSlider::sub-page:horizontal { %2 } QSlider::handle:horizontal { %3 }")
                  .arg(Theme::SliderGroove, Theme::VolColor, Theme::SliderHandle));

    layout->addWidget(btnPrev); layout->addWidget(btnRewind); layout->addWidget(btnPlay);
    layout->addWidget(btnStop); layout->addWidget(btnForward); layout->addWidget(btnNext);
    layout->addWidget(seekSlider, 1);
    layout->addWidget(timeLabel);
    layout->addWidget(volLabel); layout->addWidget(volSlider);

    connect(btnPlay, &QPushButton::clicked, this, &ControlBar::playClicked);
    connect(btnStop, &QPushButton::clicked, this, &ControlBar::stopClicked);
    connect(btnPrev, &QPushButton::clicked, this, &ControlBar::prevClicked);
    connect(btnNext, &QPushButton::clicked, this, &ControlBar::nextClicked);
    connect(btnRewind, &QPushButton::clicked, this, &ControlBar::rewindClicked);
    connect(btnForward, &QPushButton::clicked, this, &ControlBar::forwardClicked);
}

// --- StartPanel ---
StartPanel::StartPanel(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);

    QLabel *icon = new QLabel("🎬", this); icon->setStyleSheet("font-size: 70px;"); icon->setAlignment(Qt::AlignCenter);
    QLabel *title = new QLabel("DROP YOUR VIDEOS HERE", this); title->setStyleSheet("font-size: 24px; color: #888; font-weight: bold;"); title->setAlignment(Qt::AlignCenter);
    QPushButton *btnSelect = new QPushButton("Choose file", this);
    btnSelect->setFixedSize(250, 50); btnSelect->setStyleSheet("background-color: #1E90FF; border-radius: 8px; font-weight:bold; font-size: 16px; border:none;");

    layout->addWidget(icon); layout->addWidget(title); layout->addWidget(btnSelect, 0, Qt::AlignHCenter);

    connect(btnSelect, &QPushButton::clicked, this, [this](){
        QStringList files = QFileDialog::getOpenFileNames(this, "Choose Video", "", "Video (*.*)");
        if(!files.isEmpty()) emit filesReady(files);
    });
}
void StartPanel::dragEnterEvent(QDragEnterEvent *event) { if (event->mimeData()->hasUrls()) event->acceptProposedAction(); }
void StartPanel::dropEvent(QDropEvent *event) {
    QStringList files;
    for (const QUrl &url : event->mimeData()->urls()) if (url.isLocalFile()) files.append(url.toLocalFile());
    if (!files.isEmpty()) emit filesReady(files);
}