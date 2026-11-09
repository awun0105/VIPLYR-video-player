#pragma once
#include <QRect>
#include <QString>
#include <QStringList>

// What the user asked the export to do. Turned into an ffmpeg command line by
// buildFfmpegArguments(); nothing here touches the disk.
struct ExportOptions {
    enum class Audio { Keep, Remove, Replace };
    enum class Output { Video, ImageSequence };

    QString inputPath;
    QString outputPath;             // a file for Video, a folder for ImageSequence
    double startSeconds = 0.0;
    double endSeconds = 0.0;        // 0 = until the end
    bool frameAccurateCut = false;  // re-encode so the cut lands on the exact frame
    Audio audio = Audio::Keep;
    QString replacementAudioPath;   // for Audio::Replace
    QRect crop;                     // in displayed video pixels; null = no crop
    int rotateDegrees = 0;          // extra clockwise rotation: 0, 90, 180 or 270
    Output output = Output::Video;

    // False when the streams can be copied as they are, which takes seconds and loses
    // no quality. A copied video can only be cut at keyframes.
    bool needsReencode() const;
    // Length of the exported part, for progress reporting.
    double exportedSeconds(double mediaDurationSeconds) const;
};

// The arguments to run ffmpeg with (without the program name).
QStringList buildFfmpegArguments(const ExportOptions &options);

// The largest centred rectangle with the given aspect ratio inside a picture.
QRect centredCrop(int pictureWidth, int pictureHeight, double aspectRatio);
