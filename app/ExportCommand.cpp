#include "ExportCommand.h"

#include <algorithm>

namespace {

// ffmpeg's 4:2:0 encoders need even sizes and offsets.
int roundDownToEven(int value) { return value & ~1; }

QString videoFilters(const ExportOptions &options) {
    QStringList filters;
    if (!options.crop.isNull()) {
        filters << QString("crop=%1:%2:%3:%4")
                       .arg(roundDownToEven(options.crop.width()))
                       .arg(roundDownToEven(options.crop.height()))
                       .arg(roundDownToEven(options.crop.x()))
                       .arg(roundDownToEven(options.crop.y()));
    }
    switch (options.rotateDegrees) {
        case 90:  filters << "transpose=clock"; break;
        case 180: filters << "hflip" << "vflip"; break;
        case 270: filters << "transpose=cclock"; break;
        default:  break;
    }
    return filters.join(',');
}

QString seconds(double value) { return QString::number(value, 'f', 3); }

}  // namespace

bool ExportOptions::needsReencode() const {
    return frameAccurateCut || !crop.isNull() || rotateDegrees != 0 || output == Output::ImageSequence;
}

double ExportOptions::exportedSeconds(double mediaDurationSeconds) const {
    const double end = endSeconds > 0.0 ? std::min(endSeconds, mediaDurationSeconds) : mediaDurationSeconds;
    return std::max(0.0, end - startSeconds);
}

QStringList buildFfmpegArguments(const ExportOptions &options) {
    QStringList arguments = {"-y", "-hide_banner", "-nostdin", "-nostats", "-progress", "pipe:1"};

    // Placed before the input, these limit what ffmpeg reads at all.
    if (options.startSeconds > 0.0) arguments << "-ss" << seconds(options.startSeconds);
    if (options.endSeconds > 0.0) arguments << "-to" << seconds(options.endSeconds);
    arguments << "-i" << options.inputPath;

    const QString filters = videoFilters(options);

    if (options.output == ExportOptions::Output::ImageSequence) {
        if (!filters.isEmpty()) arguments << "-vf" << filters;
        arguments << "-an" << "-start_number" << "0" << options.outputPath + "/frame_%06d.png";
        return arguments;
    }

    const bool replaceAudio = options.audio == ExportOptions::Audio::Replace;
    if (replaceAudio) arguments << "-i" << options.replacementAudioPath;

    arguments << "-map" << "0:v:0";
    switch (options.audio) {
        case ExportOptions::Audio::Keep:    arguments << "-map" << "0:a?"; break;  // "?": fine if there is none
        case ExportOptions::Audio::Replace: arguments << "-map" << "1:a:0" << "-shortest"; break;
        case ExportOptions::Audio::Remove:  arguments << "-an"; break;
    }

    if (options.needsReencode()) {
        if (!filters.isEmpty()) arguments << "-vf" << filters;
        arguments << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "18" << "-pix_fmt" << "yuv420p";
    } else {
        arguments << "-c:v" << "copy" << "-avoid_negative_ts" << "make_zero";
    }
    if (options.audio != ExportOptions::Audio::Remove) {
        // New audio, or audio cut together with a re-encoded picture, is encoded again.
        const bool encodeAudio = replaceAudio || options.needsReencode();
        if (encodeAudio) arguments << "-c:a" << "aac" << "-b:a" << "192k";
        else arguments << "-c:a" << "copy";
    }

    arguments << "-movflags" << "+faststart" << options.outputPath;
    return arguments;
}

QRect centredCrop(int pictureWidth, int pictureHeight, double aspectRatio) {
    int width = pictureWidth;
    int height = pictureHeight;
    if (static_cast<double>(pictureWidth) / pictureHeight > aspectRatio) {
        width = static_cast<int>(pictureHeight * aspectRatio);
    } else {
        height = static_cast<int>(pictureWidth / aspectRatio);
    }
    width = roundDownToEven(width);
    height = roundDownToEven(height);
    return QRect(roundDownToEven((pictureWidth - width) / 2), roundDownToEven((pictureHeight - height) / 2),
                 width, height);
}
