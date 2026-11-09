#include "PlaylistPanel.h"

#include <QAbstractItemModel>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPushButton>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "MediaFiles.h"
#include "Theme.h"

namespace {

constexpr int kThumbnailWidth = 96;
constexpr int kThumbnailHeight = 54;
constexpr int kPathRole = Qt::UserRole;

bool isLocalFile(const QString &path) { return !path.contains("://"); }

QString displayName(const QString &path) {
    return isLocalFile(path) ? QFileInfo(path).fileName() : path;
}

}  // namespace

PlaylistPanel::PlaylistPanel(QWidget *parent) : QWidget(parent) {
    setAcceptDrops(true);
    setStyleSheet(Theme::PanelBg);
    probePool_.setMaxThreadCount(2);

    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText("Search videos...");
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setStyleSheet(Theme::SearchBox);

    QPushButton *addButton = new QPushButton("Add");
    QPushButton *removeButton = new QPushButton("Remove");
    addButton->setStyleSheet(Theme::Button);
    removeButton->setStyleSheet(Theme::Button);

    QMenu *addMenu = new QMenu(addButton);
    addMenu->setStyleSheet(Theme::Menu);
    connect(addMenu->addAction("Files..."), &QAction::triggered, this, &PlaylistPanel::addFilesRequested);
    connect(addMenu->addAction("Folder..."), &QAction::triggered, this, &PlaylistPanel::addFolderRequested);
    connect(addMenu->addAction("Network URL..."), &QAction::triggered, this, &PlaylistPanel::addUrlRequested);
    addButton->setMenu(addMenu);

    list_ = new QListWidget;
    list_->setStyleSheet(Theme::PlaylistList);
    list_->setIconSize(QSize(kThumbnailWidth, kThumbnailHeight));
    list_->setDragDropMode(QAbstractItemView::InternalMove);  // drag an item to reorder
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setTextElideMode(Qt::ElideMiddle);

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);
    layout->addWidget(searchEdit_);
    layout->addLayout(buttonLayout);
    layout->addWidget(list_, 1);

    connect(removeButton, &QPushButton::clicked, this, &PlaylistPanel::removeRequested);
    connect(searchEdit_, &QLineEdit::textChanged, this, &PlaylistPanel::applyFilter);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) emit itemActivated(row);
    });
    connect(list_->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int from, int, const QModelIndex &, int destination) {
                // Qt reports the row the item was dropped in front of, counted before removal.
                emit itemMoved(from, destination > from ? destination - 1 : destination);
            });
}

PlaylistPanel::~PlaylistPanel() {
    probePool_.clear();
    probePool_.waitForDone();
}

void PlaylistPanel::setPaths(const QStringList &paths) {
    const QSignalBlocker blocker(list_);
    const int current = list_->currentRow();
    list_->clear();
    for (const QString &path : paths) {
        QListWidgetItem *item = new QListWidgetItem(displayName(path));
        item->setData(kPathRole, path);
        item->setToolTip(path);
        list_->addItem(item);

        if (probes_.contains(path)) showProbe(item, probes_.value(path));
        else if (isLocalFile(path)) requestProbe(path);
    }
    list_->setCurrentRow(current < list_->count() ? current : -1);
    applyFilter();
}

void PlaylistPanel::requestProbe(const QString &path) {
    QtConcurrent::run(&probePool_, &MediaProbe::probe, path, kThumbnailWidth, kThumbnailHeight)
        .then(this, [this, path](const MediaProbe &probe) {
            probes_.insert(path, probe);
            // The list may have changed while this ran, so look the item up again.
            for (int i = 0; i < list_->count(); ++i) {
                if (list_->item(i)->data(kPathRole).toString() == path) showProbe(list_->item(i), probe);
            }
        });
}

void PlaylistPanel::showProbe(QListWidgetItem *item, const MediaProbe &probe) {
    if (!probe.thumbnail.isNull()) {
        // The same picture for the selected state, or Qt would tint it with the highlight colour.
        const QPixmap pixmap = QPixmap::fromImage(probe.thumbnail);
        QIcon icon;
        for (QIcon::Mode mode : {QIcon::Normal, QIcon::Active, QIcon::Selected}) {
            icon.addPixmap(pixmap, mode, QIcon::Off);
            icon.addPixmap(pixmap, mode, QIcon::On);
        }
        item->setIcon(icon);
    }
    if (probe.durationMs > 0) {
        const QString path = item->data(kPathRole).toString();
        item->setText(displayName(path) + "\n" + MediaFiles::formatTime(probe.durationMs));
    }
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
