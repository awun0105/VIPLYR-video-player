#pragma once
#include <QStringList>
#include <QWidget>

// The first screen: drop videos here or choose them with a dialog.
class StartPanel : public QWidget {
    Q_OBJECT
public:
    explicit StartPanel(QWidget *parent = nullptr);

signals:
    void pathsDropped(const QStringList &paths);  // files or folders, unfiltered
    void chooseRequested();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};
