// Runs the ffmpeg commands built by ExportCommand and checks what comes out.
// Usage: export_tests <test_clip.mp4> <test_tracks.mkv>
// test_clip.mp4 is 320x240, 3 s, 30 fps, one keyframe per second, with sound.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "ExportCommand.h"
#include "ExportJob.h"
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
    QCoreApplication app(argc, argv);  // ExportJob needs an event loop
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
        options.segments = {{1.0, 2.0}};
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
        options.segments = {{0.5, 1.5}};
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
        options.segments = {{0.0, 0.5}};
        options.output = ExportOptions::Output::ImageSequence;
        CHECK(runFfmpeg(options));
        CHECK(QDir(options.outputPath).entryList({"frame_*.png"}).size() == 15);  // 0.5 s at 30 fps
    }

    std::printf("joining parts\n");
    {
        ExportOptions options;
        options.inputPath = clip;
        options.outputPath = folder.filePath("joined.mp4");
        options.segments = {{0.0, 0.5}, {2.0, 3.0}, {1.0, 1.5}};  // any order
        CHECK(options.joinsSegments() && options.needsReencode());
        CHECK(near(options.exportedSeconds(3.0), 2.0, 1e-6));
        CHECK(runFfmpeg(options));
        const viplyr::MediaInfo info = inspect(options.outputPath);
        CHECK(info.audioTracks.size() == 1);
        CHECK(near(info.duration, 2.0, 0.1));
        // 15 + 30 + 15 frames, and the third part starts with the clip's frame 30.
        viplyr::FrameReader reader;
        reader.open(options.outputPath.toStdString());
        int frames = 0;
        viplyr::VideoFrame frame;
        while (reader.read(frame)) ++frames;
        CHECK(frames == 60);

        options.audio = ExportOptions::Audio::Remove;  // the picture alone, cropped
        options.crop = QRect(0, 0, 160, 120);
        options.outputPath = folder.filePath("joined_silent.mp4");
        CHECK(runFfmpeg(options));
        const viplyr::MediaInfo silent = inspect(options.outputPath);
        CHECK(silent.audioTracks.empty() && silent.width == 160 && near(silent.duration, 2.0, 0.1));

        options.audio = ExportOptions::Audio::Keep;
        options.inputHasAudio = false;  // a video without sound joins without audio inputs
        options.crop = QRect();
        options.outputPath = folder.filePath("joined_no_audio_input.mp4");
        CHECK(runFfmpeg(options));
        CHECK(inspect(options.outputPath).audioTracks.empty());
    }

    for (const auto &[encoder, name] : {std::pair{ExportOptions::Encoder::Nvenc, "nvenc"},
                                        std::pair{ExportOptions::Encoder::Vaapi, "vaapi"}}) {
        QProcess probe;
        probe.start("ffmpeg", encoderProbeArguments(encoder));
        if (!probe.waitForFinished(20000) || probe.exitCode() != 0) {
            std::printf("gpu encoder (%s): not available, skipped\n", name);
            continue;
        }
        std::printf("gpu encoder (%s)\n", name);
        ExportOptions options;
        options.inputPath = clip;
        options.outputPath = folder.filePath(QString("gpu_%1.mp4").arg(name));
        options.segments = {{0.0, 1.0}, {2.0, 3.0}};
        options.crop = QRect(0, 0, 160, 120);
        options.encoder = encoder;
        CHECK(runFfmpeg(options));
        const viplyr::MediaInfo info = inspect(options.outputPath);
        CHECK(info.videoCodec == "h264" && info.width == 160 && info.height == 120);
        CHECK(near(info.duration, 2.0, 0.1));
    }

    std::printf("export job (progress, finish, cancel)\n");
    {
        ExportJob job;
        double lastProgress = -1.0;
        int progressUpdates = 0;
        bool done = false, succeeded = false;
        QString reportedPath, reportedError;
        QObject::connect(&job, &ExportJob::progressChanged, [&](double fraction) {
            lastProgress = fraction;
            ++progressUpdates;
        });
        QObject::connect(&job, &ExportJob::finished, [&](bool ok, const QString &path, const QString &error) {
            done = true;
            succeeded = ok;
            reportedPath = path;
            reportedError = error;
        });
        const auto waitUntilDone = [&] {
            QElapsedTimer timer;
            timer.start();
            while (!done && timer.elapsed() < 60000) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        };

        ExportOptions options;
        options.inputPath = clip;
        options.outputPath = folder.filePath("job.mp4");
        options.segments = {{0.0, 1.0}, {2.0, 3.0}};
        job.start(options, options.exportedSeconds(3.0));
        CHECK(job.isRunning());
        waitUntilDone();
        CHECK(done && succeeded && reportedError.isEmpty() && reportedPath == options.outputPath);
        CHECK(lastProgress == 1.0 && progressUpdates >= 2);
        CHECK(near(inspect(options.outputPath).duration, 2.0, 0.1));

        done = false;  // a failing export reports ffmpeg's message
        options.outputPath = folder.filePath("no_such_folder/job.mp4");
        job.start(options, 2.0);
        waitUntilDone();
        CHECK(done && !succeeded && !reportedError.isEmpty());

        done = false;  // cancelling reports neither success nor an error
        options.outputPath = folder.filePath("cancelled.mp4");
        options.segments = {};
        options.frameAccurateCut = true;
        job.start(options, 3.0);
        job.cancel();
        waitUntilDone();
        CHECK(done && !succeeded && reportedError.isEmpty() && !job.isRunning());
    }

    std::printf("centred crop\n");
    CHECK(centredCrop(1920, 1080, 1.0) == QRect(420, 0, 1080, 1080));
    CHECK(centredCrop(1920, 1080, 9.0 / 16.0) == QRect(656, 0, 606, 1080));
    CHECK(centredCrop(1080, 1920, 16.0 / 9.0) == QRect(0, 656, 1080, 606));

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
