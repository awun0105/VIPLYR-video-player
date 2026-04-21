#include <QApplication>
#include "UI/MainWindow.h"

int main(int argc, char *argv[])
 {
    QApplication app(argc, argv);
    MainWindow window;

    //Open With
    QStringList args = app.arguments();
    args.removeFirst();
    if (!args.isEmpty()) window.loadAndPlay(args);

    window.show();
    return app.exec();
}