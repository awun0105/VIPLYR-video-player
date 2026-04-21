#include "AppUI.h"
#include <QMouseEvent>
#include <QStyleOptionSlider>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMediaPlaylist>
#include <QListWidget>
#include <QFileInfo>
#include <QUrl>


SeekSlider::SeekSlider(QWidget *parent) : QSlider(Qt::Horizontal, parent) {}

void SeekSlider::mousePressEvent(QMouseEvent *ev) {
    if (ev->button() == Qt::LeftButton) {

        QStyleOptionSlider opt;
        initStyleOption(&opt);
        int sr = style()->pixelMetric(QStyle::PM_SliderLength, &opt, this);
        int w = width();
        int newValue = QStyle::sliderValueFromPosition(minimum(), maximum(), ev->pos().x() - sr / 2, w - sr, opt.upsideDown);

        setValue(newValue);
        emit sliderMoved(newValue);
    }

    QSlider::mousePressEvent(ev);
}
// ==========================================
// MÀN HÌNH START (KÉO THẢ VIDEO)
// ==========================================
StartScreen::StartScreen(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true); // BẬT TÍNH NĂNG NHẬN FILE KÉO THẢ
    setWindowTitle("Welcome - C++ Video Player");
    resize(600, 400);
    setStyleSheet("background-color: #1a1a1a; color: white; font-family: sans-serif;");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);

    QLabel *icon = new QLabel("🎬", this);
    icon->setStyleSheet("font-size: 70px; margin-bottom: 10px;");
    icon->setAlignment(Qt::AlignCenter);

    QLabel *title = new QLabel("KÉO THẢ VIDEO VÀO ĐÂY", this);
    title->setStyleSheet("font-size: 24px; font-weight: bold; color: #888; margin-bottom: 20px;");
    title->setAlignment(Qt::AlignCenter);

    QPushButton *btnSelect = new QPushButton("Hoặc Click Chọn Video", this);
    btnSelect->setFixedSize(250, 50);
    btnSelect->setStyleSheet(
        "QPushButton { background-color: #1E90FF; border-radius: 8px; font-size: 16px; font-weight: bold; border: none; }"
        "QPushButton:hover { background-color: #4682B4; }"
    );

    layout->addWidget(icon);
    layout->addWidget(title);
    layout->addWidget(btnSelect, 0, Qt::AlignHCenter);

    connect(btnSelect, &QPushButton::clicked, this, &StartScreen::openFileDialog);
}

void StartScreen::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void StartScreen::dropEvent(QDropEvent *event) {
    QStringList files;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) files.append(url.toLocalFile());
    }
    if (!files.isEmpty()) emit videosSelected(files);
}

void StartScreen::openFileDialog() {
    QStringList files = QFileDialog::getOpenFileNames(this, "Chọn các Video", "", "Video Files (*.*)");
    if (!files.isEmpty()) emit videosSelected(files);
}

// ==========================================
// MÀN HÌNH PLAYER CHÍNH
// ==========================================
PlayerWindow::PlayerWindow(QWidget *parent) : QWidget(parent) {
    setupUI();
    connectSignals();
}

void PlayerWindow::setupUI() {
    setWindowTitle("C++ Video Player Pro");
    resize(1100, 650);
    setStyleSheet("background-color: #0f0f0f; color: white; font-family: sans-serif;");

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QVBoxLayout *leftLayout = new QVBoxLayout;
    view = new PlayerView;
    leftLayout->addWidget(view);

    playlist = new QMediaPlaylist;
    playlist->setPlaybackMode(QMediaPlaylist::Sequential);
    view->player->setPlaylist(playlist);

    playlistUI = new QListWidget;
    playlistUI->setFixedWidth(250);
    playlistUI->setStyleSheet(
        "QListWidget { background-color: #1a1a1a; color: #555; border-left: 1px solid #333; outline: none; }"
        "QListWidget::item { padding: 15px 10px; border-bottom: 1px solid #222; }"
        "QListWidget::item:hover { background-color: #2a2a2a; color: #888; }"
        "QListWidget::item:selected { background-color: #1E90FF; color: white; font-weight: bold; }"
    );

    QHBoxLayout *controlLayout = new QHBoxLayout;
    controlLayout->setContentsMargins(15, 10, 15, 10);
    controlLayout->setSpacing(10);

    QString btnStyle = "QPushButton { background-color: #333; border-radius: 4px; padding: 6px 10px; font-weight: bold; border: none; color: white; }"
                       "QPushButton:hover { background-color: #555; }";

    QPushButton *btnPrev = new QPushButton("⏮");
    QPushButton *btnRewind = new QPushButton("⏪ -5s");
    QPushButton *btnPlay = new QPushButton("⏯ Play/Pause");
    QPushButton *btnStop = new QPushButton("⏹ Stop");
    QPushButton *btnForward = new QPushButton("+5s ⏩");
    QPushButton *btnNext = new QPushButton("⏭");

    btnPrev->setObjectName("btnPrev"); btnRewind->setObjectName("btnRewind");
    btnPlay->setObjectName("btnPlay"); btnStop->setObjectName("btnStop");
    btnForward->setObjectName("btnForward"); btnNext->setObjectName("btnNext");

    QList<QPushButton*> btns = {btnPrev, btnRewind, btnPlay, btnStop, btnForward, btnNext};
    for(auto btn : btns) btn->setStyleSheet(btnStyle);

    seekSlider = new SeekSlider; // SỬ DỤNG THANH TUA TÙY BIẾN
    seekSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #333; height: 6px; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: #1E90FF; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: white; width: 12px; margin-top: -3px; margin-bottom: -3px; border-radius: 6px; }"
    );

    QLabel *volLabel = new QLabel("🔊");
    QSlider *volSlider = new QSlider(Qt::Horizontal);
    volSlider->setObjectName("volSlider");
    volSlider->setRange(0, 100);
    volSlider->setValue(100);
    volSlider->setFixedWidth(100);
    volSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #333; height: 6px; border-radius: 3px; }"
        "QSlider::sub-page:horizontal { background: #4CAF50; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: white; width: 12px; margin-top: -3px; margin-bottom: -3px; border-radius: 6px; }"
    );

    controlLayout->addWidget(btnPrev); controlLayout->addWidget(btnRewind);
    controlLayout->addWidget(btnPlay); controlLayout->addWidget(btnStop);
    controlLayout->addWidget(btnForward); controlLayout->addWidget(btnNext);
    controlLayout->addWidget(seekSlider, 1);
    controlLayout->addWidget(volLabel); controlLayout->addWidget(volSlider);

    leftLayout->addLayout(controlLayout);
    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addWidget(playlistUI);
}

void PlayerWindow::connectSignals() {
    connect(playlist, &QMediaPlaylist::currentIndexChanged, playlistUI, [this](int index){
        playlistUI->setCurrentRow(index);
    });
    connect(playlistUI, &QListWidget::currentRowChanged, playlist, &QMediaPlaylist::setCurrentIndex);

    QPushButton *btnPlay = findChild<QPushButton*>("btnPlay");
    QPushButton *btnStop = findChild<QPushButton*>("btnStop");
    QPushButton *btnPrev = findChild<QPushButton*>("btnPrev");
    QPushButton *btnNext = findChild<QPushButton*>("btnNext");
    QPushButton *btnRewind = findChild<QPushButton*>("btnRewind");
    QPushButton *btnForward = findChild<QPushButton*>("btnForward");
    QSlider *volSlider = findChild<QSlider*>("volSlider");

    connect(btnPlay, &QPushButton::clicked, [this](){
        if(view->player->state() == QMediaPlayer::PlayingState) view->player->pause();
        else view->player->play();
    });
    connect(btnStop, &QPushButton::clicked, view->player, &QMediaPlayer::stop);
    connect(btnPrev, &QPushButton::clicked, playlist, &QMediaPlaylist::previous);
    connect(btnNext, &QPushButton::clicked, playlist, &QMediaPlaylist::next);

    connect(btnRewind, &QPushButton::clicked, [this](){
        view->player->setPosition(qMax(0LL, view->player->position() - 5000));
    });
    connect(btnForward, &QPushButton::clicked, [this](){
        view->player->setPosition(qMin(view->player->duration(), view->player->position() + 5000));
    });

    // CHỈNH LẠI LOGIC THANH TUA ĐỂ KHÔNG BỊ GIẬT KHI KÉO
    connect(view->player, &QMediaPlayer::positionChanged, [this](qint64 pos){
        if (!seekSlider->isSliderDown()) { // Chỉ tự chạy khi user KHÔNG nắm kéo thanh trượt
            seekSlider->setValue(pos);
        }
    });
    connect(view->player, &QMediaPlayer::durationChanged, seekSlider, [this](qint64 duration){
        seekSlider->setRange(0, duration);
    });
    connect(seekSlider, &QSlider::sliderMoved, view->player, &QMediaPlayer::setPosition);
    connect(volSlider, &QSlider::valueChanged, view->player, &QMediaPlayer::setVolume);
}

void PlayerWindow::loadVideos(const QStringList &fileNames) {
    for(const QString &file : fileNames) {
        playlist->addMedia(QUrl::fromLocalFile(file));
        playlistUI->addItem(QFileInfo(file).fileName());
    }
    view->player->play();
}