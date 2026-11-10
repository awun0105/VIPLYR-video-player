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
#include <cstring>

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
    const bool ok = process.waitForFinished(60000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!ok) std::fprintf(stderr, "%s\n", process.readAllStandardError().constData());
    return ok;
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

    std::printf("timeline: multiple sources, mixed sizes/FPS and missing audio\n");
    {
        const QString red = folder.filePath("red.mp4"), blue = folder.filePath("blue.mp4");
        QProcess generate;
        generate.start("ffmpeg", {"-y", "-v", "error", "-f", "lavfi", "-i", "color=red:s=320x180:r=30:d=1.5",
                                  "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=1.5",
                                  "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", red});
        CHECK(generate.waitForFinished(20000) && generate.exitCode() == 0);
        generate.start("ffmpeg", {"-y", "-v", "error", "-f", "lavfi", "-i", "color=blue:s=192x320:r=25:d=2",
                                  "-c:v", "libx264", "-pix_fmt", "yuv420p", blue});
        CHECK(generate.waitForFinished(20000) && generate.exitCode() == 0);
        ExportOptions options;
        options.inputPath = red;
        options.outputPath = folder.filePath("mixed.mp4");
        options.outputSize = QSize(320, 240);
        options.outputFrameRate = 30;
        options.segments = {{1.3, 1.8, blue, false, 2.0}, {0.1, 0.6, red, true, 1.5}};
        CHECK(near(options.exportedSeconds(1.5), 1.0, 1e-6));
        CHECK(runFfmpeg(options));
        const auto info = inspect(options.outputPath);
        CHECK(info.width == 320 && info.height == 240 && info.audioTracks.size() == 1);
        CHECK(near(info.duration, 1.0, 0.07) && near(info.frameRate, 30, 0.01));
        viplyr::FrameReader reader;
        CHECK(reader.open(options.outputPath.toStdString()));
        viplyr::VideoFrame frame;
        int count = 0;
        while (reader.read(frame)) {
            const auto rgba = frame.convert(viplyr::PixelFormat::Rgba);
            const auto *pixel = rgba.plane(0) + rgba.stride(0) * 120 + 160 * 4;
            if (count == 4) CHECK(pixel[2] > 200 && pixel[0] < 40);  // blue first, despite red being inputPath
            if (count == 22) CHECK(pixel[0] > 200 && pixel[2] < 40);
            ++count;
        }
        CHECK(count == 30);
        for (double start : {0.1, 0.7}) {
            QProcess decode;
            decode.start("ffmpeg", {"-v", "error", "-ss", QString::number(start), "-i", options.outputPath,
                                    "-t", "0.15", "-vn", "-ac", "2", "-ar", "48000", "-f", "f32le", "pipe:1"});
            CHECK(decode.waitForFinished(10000) && decode.exitCode() == 0);
            const QByteArray samples = decode.readAllStandardOutput();
            double energy = 0;
            for (qsizetype i = 0; i + 4 <= samples.size(); i += 4) {
                float value;
                std::memcpy(&value, samples.constData() + i, sizeof(value));
                energy += value * value;
            }
            const double rms = samples.isEmpty() ? -1 : std::sqrt(energy / (samples.size() / 4));
            CHECK(start < 0.5 ? rms >= 0 && rms < 0.001 : rms > 0.01);
        }
        options.segments.removeFirst();  // one normalized clip must work too
        options.outputPath = folder.filePath("one_normalized.mp4");
        CHECK(runFfmpeg(options));
        CHECK(inspect(options.outputPath).height == 240);
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
