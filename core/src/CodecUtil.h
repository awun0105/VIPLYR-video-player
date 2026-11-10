#pragma once
#include <memory>
#include <string>

#include "viplyr/Frame.h"
#include "viplyr/MediaInfo.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

// Small FFmpeg helpers shared by Player and FrameReader.
namespace viplyr {

struct PacketDeleter {
    void operator()(AVPacket *packet) const { av_packet_free(&packet); }
};
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

struct FrameDeleter {
    void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

// "Discard nothing": a seek target lower than any timestamp.
constexpr double kNoDiscard = -1e18;

std::string errorText(int averror);

inline double toSeconds(int64_t timestamp, AVRational timeBase) {
    return static_cast<double>(timestamp) * av_q2d(timeBase);
}

// Opens `url` and reads its stream information. Returns nullptr on failure.
AVFormatContext *openInput(const std::string &url, const OpenOptions &options, std::string *error);

// Opens a decoder for `stream`, using hardware decoding when `hwAccel` allows it
// and it is available. `hardwareName` receives "vaapi"/"cuda" or stays empty.
// `lowDelay` trades decoding throughput for latency (see OpenOptions::lowLatency).
AVCodecContext *openDecoder(const AVStream *stream, HwAccel hwAccel, std::string *hardwareName,
                            std::string *error, bool lowDelay = false);

// Resolves HwAccel::Auto for a video stream (see HwAccel::Auto).
HwAccel effectiveHwAccel(const OpenOptions &options);

// Seconds to subtract from stream timestamps so that the first video frame is at 0.
// Files often start a little before or after zero (audio priming, broadcast streams).
// `video` may be null for audio-only media.
double startOffsetSeconds(const AVFormatContext *format, const AVStream *video);

MediaInfo describeMedia(const AVFormatContext *format, const AVStream *video, const AVStream *audio,
                        const std::string &hardwareName);

// True when the stream is declared as interlaced.
bool isInterlaced(const AVStream *video);

// Estimated duration of one video frame, in seconds.
double frameDurationSeconds(const AVStream *video);

// Wraps a decoded frame into a VideoFrame with media-relative timing and, unless
// `keepHwFrames`, copies a GPU frame to system memory. Leaves `decoded` empty.
// `nextPts` supplies the timestamp when the frame has none and is advanced.
VideoFrame makeVideoFrame(AVFrame *decoded, const AVStream *stream, double startOffset,
                          bool keepHwFrames, double *nextPts);

}  // namespace viplyr
