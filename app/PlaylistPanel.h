#pragma once
#include <QHash>
#include <QStringList>
#include <QThreadPool>
#include <QWidget>

#include "MediaProbe.h"

class QLineEdit;
class QListWidget;
class QListWidgetItem;

// The side panel listing the playlist with a preview picture and the length of
// each video. Supports search, drag-to-reorder, add and remove.
class PlaylistPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPanel(QWidget *parent = nullptr);
    ~PlaylistPanel() override;

    void setPaths(const QStringList &paths);
    void setCurrentIndex(int index);  // does not emit itemActivated
    int selectedIndex() const;
    bool isSearching() const;         // the search box has keyboard focus

signals:
    void itemActivated(int index);
    void itemMoved(int from, int to);
    void pathsDropped(const QStringList &paths);  // files or folders, unfiltered
    void addFilesRequested();
    void addFolderRequested();
    void addUrlRequested();
    void removeRequested();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void applyFilter();
    void requestProbe(const QString &path);
    void showProbe(QListWidgetItem *item, const MediaProbe &probe);

    QListWidget *list_;
    QLineEdit *searchEdit_;
    QThreadPool probePool_;               // a couple of threads reading previews
    QHash<QString, MediaProbe> probes_;   // finished previews, by path
};
