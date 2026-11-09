// Runs the ffmpeg commands built by ExportCommand and checks what comes out.
// Usage: export_tests <test_clip.mp4> <test_tracks.mkv>
// test_clip.mp4 is 320x240, 3 s, 30 fps, one keyframe per second, with sound.
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "ExportCommand.h"
#include "viplyr/FrameReader.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            ++failures;                                                         \
            std::printf("  FAILED %s:%d  %s\n", __FILE__, __LINE__, #condition); \
        }                                                                       \
    } while (0)

bool runFfmpeg(const ExportOptions &options) {
    QProcess process;
    process.start("ffmpeg", buildFfmpegArguments(options));
    return process.waitForFinished(60000) && process.exitStatus() == QProcess::NormalExit &&
           process.exitCode() == 0;
}

viplyr::MediaInfo inspect(const QString &path) {
    viplyr::FrameReader reader;
    reader.open(path.toStdString());
    return reader.info();
}

bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <test_clip.mp4> <test_tracks.mkv>\n", argv[0]);
        return 2;
    }
    const QString clip = argv[1];
    const QString otherClip = argv[2];
    QTemporaryDir folder;

    std::printf("fast trim (stream copy)\n");
    {
        ExportOptions options;
        options.inputPath = clip;
        options.outputPath = folder.filePath("copy.mp4");
        options.startSeconds = 1.0;
        options.endSeconds = 2.0;
        CHECK(!options.needsReencode());
        CHECK(runFfmpeg(options));
        const viplyr::MediaInfo info = inspect(options.outputPath);
        CHECK(info.width == 320 && info.height == 240);
        CHECK(near(info.duration, 1.0, 0.15));
        CHECK(info.audioTracks.size() == 1);
    }

    std::printf("frame-accurate trim with crop and rotation\n");
    {
        ExportOptions options;
        options.inputPath = clip;
        options.outputPath = folder.filePath("edit.mp4");
        options.startSeconds = 0.5;
        options.endSeconds = 1.5;
        options.frameAccurateCut = true;
        options.crop = QRect(40, 20, 160, 120);
        options.rotateDegrees = 90;
        CHECK(options.needsReencode());
        CHECK(runFfmpeg(options));
        const viplyr::MediaInfo info = inspect(options.outputPath);
        CHECK(info.width == 120 && info.height == 160);  // cropped, then turned on its side
        CHECK(near(info.duration, 1.0, 0.06));
    }

    std::printf("remove sound\n");
    {
        ExportOptions options;
        options.inputPath = clip;
        options.outputPath = folder.filePath("silent.mp4");
        options.audio = ExportOptions::Audio::Remove;
        CHECK(runFfmpeg(options));
        const viplyr::MediaInfo info = inspect(options.outputPath);
        CHECK(info.audioTracks.empty());
        CHECK(near(info.duration, 3.0, 0.1));
    }

    std::printf("replace sound\n");
    {
        ExportOptions options;
        options.inputPath = folder.filePath("silent.mp4");
        options.outputPath = folder.filePath("revoiced.mp4");
        options.audio = ExportOptions::Audio::Replace;
        options.replacementAudioPath = otherClip;  // 2 s long, so the result is cut to 2 s
        CHECK(!options.needsReencode());
        CHECK(runFfmpeg(options));
        const viplyr::MediaInfo info = inspect(options.outputPath);
        CHECK(info.audioTracks.size() == 1);
        CHECK(near(info.duration, 2.0, 0.15));
    }

    std::printf("image sequence\n");
    {
        ExportOptions options;
        options.inputPath = clip;
        options.outputPath = folder.filePath("frames");
        QDir().mkpath(options.outputPath);
        options.endSeconds = 0.5;
        options.output = ExportOptions::Output::ImageSequence;
        CHECK(runFfmpeg(options));
        CHECK(QDir(options.outputPath).entryList({"frame_*.png"}).size() == 15);  // 0.5 s at 30 fps
    }

    std::printf("centred crop\n");
    CHECK(centredCrop(1920, 1080, 1.0) == QRect(420, 0, 1080, 1080));
    CHECK(centredCrop(1920, 1080, 9.0 / 16.0) == QRect(656, 0, 606, 1080));
    CHECK(centredCrop(1080, 1920, 16.0 / 9.0) == QRect(0, 656, 1080, 606));

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
