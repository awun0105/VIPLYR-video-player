#include "PlaylistPanel.h"

#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "MediaFiles.h"
#include "Theme.h"

PlaylistPanel::PlaylistPanel(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);
    setStyleSheet(Theme::PanelBg);

    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText("🔍 Search video...");
    searchEdit_->setStyleSheet("background-color: #222; color: #ccc; border: 1px solid #333; "
                               "border-radius: 4px; padding: 5px;");

    QPushButton *addButton = new QPushButton("Add");
    QPushButton *removeButton = new QPushButton("Remove");
    addButton->setStyleSheet(Theme::Button);
    removeButton->setStyleSheet(Theme::Button);

    list_ = new QListWidget;
    list_->setStyleSheet(Theme::PanelBg + Theme::PlaylistItem);

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    layout->addWidget(searchEdit_);
    layout->addLayout(buttonLayout);
    layout->addWidget(list_, 1);

    connect(addButton, &QPushButton::clicked, this, &PlaylistPanel::addRequested);
    connect(removeButton, &QPushButton::clicked, this, &PlaylistPanel::removeRequested);
    connect(searchEdit_, &QLineEdit::textChanged, this, &PlaylistPanel::applyFilter);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) emit itemActivated(row);
    });
}

void PlaylistPanel::setPaths(const QStringList &paths) {
    const QSignalBlocker blocker(list_);
    const int current = list_->currentRow();
    list_->clear();
    for (const QString &path : paths) {
        QListWidgetItem *item = new QListWidgetItem(QFileInfo(path).fileName());
        item->setToolTip(path);
        list_->addItem(item);
    }
    list_->setCurrentRow(current < list_->count() ? current : -1);
    applyFilter();
}

void PlaylistPanel::setCurrentIndex(int index) {
    const QSignalBlocker blocker(list_);
    list_->setCurrentRow(index);
}

int PlaylistPanel::selectedIndex() const { return list_->currentRow(); }
bool PlaylistPanel::isSearching() const { return searchEdit_->hasFocus(); }

void PlaylistPanel::applyFilter() {
    const QString text = searchEdit_->text();
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem *item = list_->item(i);
        item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
    }
}

void PlaylistPanel::dragEnterEvent(QDragEnterEvent *event) {
    if (!MediaFiles::localPaths(event->mimeData()).isEmpty()) event->acceptProposedAction();
}

void PlaylistPanel::dropEvent(QDropEvent *event) {
    const QStringList paths = MediaFiles::localPaths(event->mimeData());
    if (paths.isEmpty()) return;
    emit pathsDropped(paths);
    event->acceptProposedAction();
}
