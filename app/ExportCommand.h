#pragma once
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QSize>

// What the user asked the export to do. Turned into an ffmpeg command line by
// buildFfmpegArguments(); nothing here touches the disk.
struct ExportOptions {
    enum class Audio { Keep, Remove, Replace };
    enum class Output { Video, ImageSequence };
    enum class Encoder { Cpu, Nvenc, Vaapi };  // for re-encoded video

    struct Segment {
        double startSeconds = 0.0;
        double endSeconds = 0.0;    // 0 = until the end
        QString inputPath;          // empty uses the project's inputPath (quick export)
        bool inputHasAudio = true;
        double sourceDurationSeconds = 0.0;
    };

    QString inputPath;
    QString outputPath;             // a file for Video, a folder for ImageSequence
    QVector<Segment> segments;      // the parts to keep, joined in this order; empty = all
    bool inputHasAudio = true;
    bool frameAccurateCut = false;  // re-encode so the cut lands on the exact frame
    Encoder encoder = Encoder::Cpu;
    Audio audio = Audio::Keep;
    QString replacementAudioPath;   // for Audio::Replace
    QRect crop;                     // in displayed video pixels; null = no crop
    int rotateDegrees = 0;          // extra clockwise rotation: 0, 90, 180 or 270
    QSize outputSize;               // timeline export: fit/pad all sources to this canvas
    double outputFrameRate = 30.0;
    Output output = Output::Video;

    // False when the streams can be copied as they are, which takes seconds and loses
    // no quality. A copied video can only be cut at keyframes, and joining parts
    // always re-encodes.
    bool needsReencode() const;
    bool joinsSegments() const { return segments.size() > 1; }
    // Length of the exported part, for progress reporting.
    double exportedSeconds(double mediaDurationSeconds) const;
};

// The arguments to run ffmpeg with (without the program name).
QStringList buildFfmpegArguments(const ExportOptions &options);

// A one-second test encode that succeeds only when `encoder` works on this machine
// (the encoder is built into ffmpeg and the GPU and driver support it).
QStringList encoderProbeArguments(ExportOptions::Encoder encoder);

// The largest centred rectangle with the given aspect ratio inside a picture.
QRect centredCrop(int pictureWidth, int pictureHeight, double aspectRatio);
