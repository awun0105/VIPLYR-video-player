#include <QApplication>
#include "AppUI.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    StartScreen startScreen;
    PlayerWindow playerWindow;

    // Chuyển đổi trạng thái khi user kéo thả hoặc chọn file xong
    QObject::connect(&startScreen, &StartScreen::videosSelected, [&](const QStringList &files) {
        startScreen.hide();             // Ẩn UI Khởi động
        playerWindow.loadVideos(files); // Load danh sách
        playerWindow.show();            // Hiện App chính
    });

    // Hỗ trợ tính năng "Open With..." từ hệ điều hành
    QStringList args = app.arguments();
    args.removeFirst();
    if (!args.isEmpty()) {
        playerWindow.loadVideos(args);
        playerWindow.show();
    } else {
        // Mở bình thường thì hiện màn hình Start
        startScreen.show();
    }

    return app.exec();
}