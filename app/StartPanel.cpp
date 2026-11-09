#include "StartPanel.h"

#include <QDragEnterEvent>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QVBoxLayout>

#include "MediaFiles.h"

StartPanel::StartPanel(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);

    QLabel *icon = new QLabel("🎬");
    icon->setStyleSheet("font-size: 70px;");
    icon->setAlignment(Qt::AlignCenter);

    QLabel *title = new QLabel("DROP YOUR VIDEOS OR FOLDERS HERE");
    title->setStyleSheet("font-size: 24px; color: #888; font-weight: bold;");
    title->setAlignment(Qt::AlignCenter);

    QPushButton *chooseButton = new QPushButton("Choose file");
    chooseButton->setFixedSize(250, 50);
    chooseButton->setStyleSheet("background-color: #1E90FF; border-radius: 8px; font-weight: bold; "
                                "font-size: 16px; border: none;");

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);
    layout->addWidget(icon);
    layout->addWidget(title);
    layout->addWidget(chooseButton, 0, Qt::AlignHCenter);

    connect(chooseButton, &QPushButton::clicked, this, &StartPanel::chooseRequested);
}

void StartPanel::dragEnterEvent(QDragEnterEvent *event) {
    if (!MediaFiles::localPaths(event->mimeData()).isEmpty()) event->acceptProposedAction();
}

void StartPanel::dropEvent(QDropEvent *event) {
    const QStringList paths = MediaFiles::localPaths(event->mimeData());
    if (paths.isEmpty()) return;
    emit pathsDropped(paths);
    event->acceptProposedAction();
}
