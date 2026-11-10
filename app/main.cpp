#include <QApplication>

#include "MainWindow.h"

int main(int argc, char *argv[]) {
    // On X11 Qt draws with GLX unless told otherwise; EGL is what can show
    // VAAPI frames without a copy. Qt falls back to GLX when EGL does not work.
    if (qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION")) qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
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
