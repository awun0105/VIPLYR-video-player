// Plays a video in a real window and drives it the way a user would: seeking,
// frame stepping, snapshots, pausing, audio track changes and a long playback,
// while checking that frames keep arriving and memory stays flat.
//
// Needs a display (and a GPU to test the zero-copy path), so it is not part of
// ctest. Usage: display_tests <video> [seconds of continuous playback]
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QScreen>
#include <map>
#include <QTimer>
#include <cstdio>
#include <functional>

#include "PlayerController.h"
#include "VideoWidget.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("  FAILED: %s (line %d)\n", #condition, __LINE__);   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

void wait(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

// Waits until `done` is true or the time runs out; returns `done`.
bool waitFor(const std::function<bool()> &done, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs) wait(10);
    return done();
}

long residentMegabytes() {
    QFile status("/proc/self/status");
    if (!status.open(QIODevice::ReadOnly)) return 0;
    for (const QByteArray &line : status.readAll().split('\n')) {
        if (line.startsWith("VmRSS:")) return line.mid(6).trimmed().split(' ').first().toLong() / 1024;
    }
    return 0;
}

}  // namespace

int main(int argc, char *argv[]) {
    if (qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION")) qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
    QApplication app(argc, argv);
    QApplication::setApplicationName("ViplyrDisplayTests");  // keeps the player's settings untouched
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <video> [seconds]\n", argv[0]);
        return 2;
    }
    const QString video = QString::fromLocal8Bit(argv[1]);
    const int soakSeconds = argc > 2 ? std::atoi(argv[2]) : 20;

    VideoWidget widget;
    widget.resize(960, 540);
    widget.show();
    PlayerController controller;
    controller.addVideoSink(&widget);
    QObject::connect(&widget, &VideoWidget::gpuFramePathChanged, &controller, &PlayerController::setGpuFramePath);
    const viplyr::HwAccel path = VideoWidget::probeGpuFramePath();
    controller.setGpuFramePath(path);
    controller.setVolume(0);

    int framesShown = 0;
    double lastFrameTime = -1.0;
    QElapsedTimer clock;
    clock.start();
    std::vector<qint64> shownAtNs;  // when each new frame reached the screen
    bool recordCadence = false;
    bool newFrameDrawn = false;
    QObject::connect(&widget, &VideoWidget::frameShown, [&](double seconds) {
        ++framesShown;
        lastFrameTime = seconds;
        newFrameDrawn = true;
    });
    QObject::connect(&widget, &QOpenGLWidget::frameSwapped, [&] {
        if (recordCadence && newFrameDrawn) shownAtNs.push_back(clock.nsecsElapsed());
        newFrameDrawn = false;
    });
    const auto framesSince = [&](int before) { return framesShown - before; };

    std::printf("gpu frame path: %s\n", path == viplyr::HwAccel::Vaapi  ? "vaapi"
                                        : path == viplyr::HwAccel::Cuda ? "cuda"
                                                                        : "none");
    controller.loadFiles({video});
    CHECK(waitFor([&] { return controller.hasMedia() && framesShown > 10; }, 5000));
    const viplyr::MediaInfo info = controller.mediaInfo();
    const double frameDuration = info.frameRate > 0 ? 1.0 / info.frameRate : 1.0 / 30.0;
    std::printf("decoder: %s, zero-copy: %s\n", info.hardwareDecoder.empty() ? "cpu" : info.hardwareDecoder.c_str(),
                widget.isShowingGpuFrame() ? "yes" : "no");
    const bool expectZeroCopy = path != viplyr::HwAccel::None && !info.hardwareDecoder.empty();
    if (expectZeroCopy) CHECK(widget.isShowingGpuFrame());

    std::printf("seeking\n");
    for (int i = 0; i < 20; ++i) {
        const qint64 target = (i * 7919) % std::max<qint64>(1, static_cast<qint64>(info.duration * 900));
        controller.seekTo(target);
        wait(60);
    }
    controller.seekTo(static_cast<qint64>(info.duration * 500));  // the middle
    CHECK(waitFor([&] { return std::abs(lastFrameTime - info.duration / 2) < 0.5; }, 3000));

    std::printf("pause and frame stepping\n");
    controller.pause();
    wait(300);
    const double pausedAt = lastFrameTime;
    for (int i = 0; i < 5; ++i) {
        controller.stepFrame(1);
        wait(80);
    }
    CHECK(std::abs(lastFrameTime - (pausedAt + 5 * frameDuration)) < frameDuration / 2);
    for (int i = 0; i < 3; ++i) controller.stepFrame(-1);  // quick presses add up
    CHECK(waitFor([&] { return std::abs(lastFrameTime - (pausedAt + 2 * frameDuration)) < frameDuration / 2; },
                  3000));

    std::printf("snapshot\n");
    const QImage snapshot = widget.currentImage();
    CHECK(!snapshot.isNull() && snapshot.width() == info.width && snapshot.height() == info.height);

    if (info.audioTracks.size() > 1) {
        std::printf("audio track change\n");
        const int other = info.audioTracks[1].streamIndex;
        controller.play();
        controller.setAudioTrack(other);
        CHECK(waitFor([&] { return controller.mediaInfo().audioStreamIndex == other; }, 5000));
        const int before = framesShown;
        CHECK(waitFor([&] { return framesSince(before) > 10; }, 3000));
    }

    std::printf("continuous playback for %d s (looping)\n", soakSeconds);
    controller.play();
    wait(1000);
    recordCadence = true;
    const long startMemory = residentMegabytes();
    long peakMemory = startMemory;
    QElapsedTimer soak;
    soak.start();
    int soakFrames = 0;
    while (soak.elapsed() < soakSeconds * 1000) {
        const int before = framesShown;
        wait(1000);
        soakFrames += framesSince(before);
        peakMemory = std::max(peakMemory, residentMegabytes());
        if (!controller.isPlaying()) controller.seekTo(0), controller.play();  // loop at the end
    }
    recordCadence = false;
    const double fps = soakFrames * 1000.0 / soak.elapsed();

    // How many screen refreshes each frame stayed up. 24 fps on 60 Hz is ideally 2,3,2,3...;
    // 1s and 4s mean a frame was shown late or skipped.
    const double refresh = widget.screen()->refreshRate();
    std::map<int, int> refreshesPerFrame;
    for (size_t i = 1; i < shownAtNs.size(); ++i) {
        const double ms = (shownAtNs[i] - shownAtNs[i - 1]) / 1e6;
        ++refreshesPerFrame[static_cast<int>(std::lround(ms * refresh / 1000.0))];
    }
    std::printf("  screen %.0f Hz, video %.3f fps; refreshes per frame:", refresh, info.frameRate);
    for (const auto &[refreshes, count] : refreshesPerFrame) std::printf("  %dx%d", refreshes, count);
    std::printf("\n");
    if (qEnvironmentVariableIsSet("CADENCE_RAW")) {
        std::map<int, int> histogram;  // 2 ms buckets
        for (size_t i = 1; i < shownAtNs.size(); ++i) ++histogram[static_cast<int>((shownAtNs[i] - shownAtNs[i - 1]) / 2e6) * 2];
        std::printf("  intervals (ms):");
        for (const auto &[ms, count] : histogram) std::printf("  %d:%d", ms, count);
        std::printf("\n");
    }
    std::printf("  %.1f frames/s shown, memory %ld MB -> peak %ld MB\n", fps, startMemory, peakMemory);
    CHECK(fps > info.frameRate * 0.85);
    CHECK(peakMemory < startMemory + 150);
    if (expectZeroCopy) CHECK(widget.isShowingGpuFrame());

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
