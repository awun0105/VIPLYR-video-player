#pragma once
#include <QStringList>
#include <QWidget>

class QLineEdit;
class QListWidget;

// The side panel listing the playlist, with search, add and remove.
class PlaylistPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaylistPanel(QWidget *parent = nullptr);

    void setPaths(const QStringList &paths);
    void setCurrentIndex(int index);  // does not emit itemActivated
    int selectedIndex() const;
    bool isSearching() const;         // the search box has keyboard focus

signals:
    void itemActivated(int index);
    void pathsDropped(const QStringList &paths);  // files or folders, unfiltered
    void addRequested();
    void removeRequested();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void applyFilter();

    QListWidget *list_;
    QLineEdit *searchEdit_;
};
