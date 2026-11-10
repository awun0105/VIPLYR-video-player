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

constexpr const char *kVaapiDevice = "/dev/dri/renderD128";

// The encoder's options. VAAPI encodes from GPU memory, so frames are uploaded last.
QStringList videoEncoderArguments(ExportOptions::Encoder encoder) {
    switch (encoder) {
        case ExportOptions::Encoder::Nvenc:
            return {"-c:v", "h264_nvenc", "-preset", "p5", "-rc", "vbr", "-cq", "21", "-b:v", "0", "-pix_fmt", "yuv420p"};
        case ExportOptions::Encoder::Vaapi:
            return {"-c:v", "h264_vaapi", "-qp", "21"};
        case ExportOptions::Encoder::Cpu:
            break;
    }
    return {"-c:v", "libx264", "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p"};
}

QString withUpload(QString filters, ExportOptions::Encoder encoder) {
    if (encoder != ExportOptions::Encoder::Vaapi) return filters;
    if (!filters.isEmpty()) filters += ',';
    return filters + "format=nv12,hwupload";
}

}  // namespace

bool ExportOptions::needsReencode() const {
    return frameAccurateCut || joinsSegments() || outputSize.isValid() || !crop.isNull() || rotateDegrees != 0 ||
           output == Output::ImageSequence;
}

double ExportOptions::exportedSeconds(double mediaDurationSeconds) const {
    if (segments.isEmpty()) return mediaDurationSeconds;
    double total = 0.0;
    for (const Segment &segment : segments) {
        const double sourceDuration = segment.sourceDurationSeconds > 0 ? segment.sourceDurationSeconds : mediaDurationSeconds;
        const double end = segment.endSeconds > 0.0 ? std::min(segment.endSeconds, sourceDuration) : sourceDuration;
        total += std::max(0.0, end - segment.startSeconds);
    }
    return total;
}

QStringList buildFfmpegArguments(const ExportOptions &options) {
    QStringList arguments = {"-y", "-hide_banner", "-nostdin", "-nostats", "-progress", "pipe:1"};
    const bool imageSequence = options.output == ExportOptions::Output::ImageSequence;
    const ExportOptions::Encoder encoder = imageSequence ? ExportOptions::Encoder::Cpu : options.encoder;
    if (encoder == ExportOptions::Encoder::Vaapi) arguments << "-vaapi_device" << kVaapiDevice;

    // One input per part. Placed before the input, -ss/-to limit what ffmpeg reads at
    // all, so each part is decoded on its own and nothing waits in memory.
    const QVector<ExportOptions::Segment> parts =
        options.segments.isEmpty() ? QVector<ExportOptions::Segment>{{}} : options.segments;
    for (const ExportOptions::Segment &part : parts) {
        if (part.startSeconds > 0.0) arguments << "-ss" << seconds(part.startSeconds);
        if (part.endSeconds > 0.0) arguments << "-to" << seconds(part.endSeconds);
        arguments << "-i" << (part.inputPath.isEmpty() ? options.inputPath : part.inputPath);
    }
    const int partCount = parts.size();
    const QString filters = videoFilters(options);
    const bool keepAudio = options.audio == ExportOptions::Audio::Keep && options.inputHasAudio && !imageSequence;

    // Parts are joined by the concat filter; the picture filters then apply once.
    QString videoLabel = "0:v:0";
    QString audioLabel = "0:a?";
    if (partCount > 1 || options.outputSize.isValid()) {
        QString graph;
        for (int i = 0; i < partCount; ++i) {
            const auto &part = parts[i];
            const double duration = part.endSeconds - part.startSeconds;
            graph += QString("[%1:v:0]setpts=PTS-STARTPTS").arg(i);
            if (options.outputSize.isValid()) {
                const int w = roundDownToEven(options.outputSize.width());
                const int h = roundDownToEven(options.outputSize.height());
                graph += QString(",scale=w=trunc(iw*sar/2)*2:h=ih,setsar=1,scale=%1:%2:force_original_aspect_ratio=decrease:force_divisible_by=2,"
                                 "pad=%1:%2:(ow-iw)/2:(oh-ih)/2,setsar=1,fps=%3,format=yuv420p")
                             .arg(w).arg(h).arg(options.outputFrameRate);
                if (duration > 0) graph += ",trim=duration=" + seconds(duration);
            }
            graph += QString("[v%1];").arg(i);
            if (keepAudio) {
                const bool hasAudio = part.inputPath.isEmpty() ? options.inputHasAudio : part.inputHasAudio;
                if (hasAudio) {
                    graph += QString("[%1:a:0]").arg(i);
                    if (options.outputSize.isValid()) graph += "aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo,";
                    graph += "asetpts=PTS-STARTPTS";
                    if (duration > 0) graph += ",apad,atrim=duration=" + seconds(duration);
                } else {
                    graph += "anullsrc=r=48000:cl=stereo,atrim=duration=" + seconds(std::max(0.001, duration));
                }
                graph += QString("[a%1];").arg(i);
            }
        }
        for (int i = 0; i < partCount; ++i) graph += QString("[v%1]").arg(i) + (keepAudio ? QString("[a%1]").arg(i) : "");
        graph += QString("concat=n=%1:v=1:a=%2[joined]").arg(partCount).arg(keepAudio ? 1 : 0) + (keepAudio ? "[a]" : "");
        const QString pictureFilters = withUpload(filters, encoder);
        graph += pictureFilters.isEmpty() ? "" : ";[joined]" + pictureFilters + "[v]";
        arguments << "-filter_complex" << graph << "-map_chapters" << "-1";  // the original chapter times no longer fit
        videoLabel = pictureFilters.isEmpty() ? "[joined]" : "[v]";
        audioLabel = "[a]";
    }

    if (imageSequence) {
        arguments << "-map" << videoLabel;
        if (partCount == 1 && !options.outputSize.isValid() && !filters.isEmpty()) arguments << "-vf" << filters;
        arguments << "-an" << "-start_number" << "0" << options.outputPath + "/frame_%06d.png";
        return arguments;
    }

    const bool replaceAudio = options.audio == ExportOptions::Audio::Replace;
    if (replaceAudio) arguments << "-i" << options.replacementAudioPath;

    arguments << "-map" << videoLabel;
    if (keepAudio) arguments << "-map" << audioLabel;  // "0:a?": fine if there is none
    else if (replaceAudio) arguments << "-map" << QString("%1:a:0").arg(partCount) << "-shortest";
    else arguments << "-an";

    if (options.needsReencode()) {
        if (partCount == 1 && !options.outputSize.isValid()) {
            const QString pictureFilters = withUpload(filters, encoder);
            if (!pictureFilters.isEmpty()) arguments << "-vf" << pictureFilters;
        }
        arguments << videoEncoderArguments(encoder);
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

QStringList encoderProbeArguments(ExportOptions::Encoder encoder) {
    QStringList arguments = {"-hide_banner", "-nostdin", "-loglevel", "error"};
    if (encoder == ExportOptions::Encoder::Vaapi) arguments << "-vaapi_device" << kVaapiDevice;
    arguments << "-f" << "lavfi" << "-i" << "testsrc2=size=320x240:rate=30:duration=1";
    const QString upload = withUpload(QString(), encoder);
    if (!upload.isEmpty()) arguments << "-vf" << upload;
    arguments << videoEncoderArguments(encoder) << "-f" << "null" << "-";
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
