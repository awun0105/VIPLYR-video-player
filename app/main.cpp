#include <QApplication>

#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("Viplyr");
    QApplication::setApplicationName("Viplyr");

    MainWindow window;
    window.show();

    // "Open with..." and command-line use: files or folders to play.
    const QStringList paths = app.arguments().mid(1);
    if (!paths.isEmpty()) window.openPaths(paths);

    return app.exec();
}
