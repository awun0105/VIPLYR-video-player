#pragma once
#include <map>
#include <string>
#include <vector>

namespace viplyr {

enum class HwAccel {
    // The fastest choice for how the frames are used: the CPU when frames are copied
    // to system memory (the default), the GPU (VAAPI, then CUDA) with keepHwFrames.
    // Copying 4K frames back from the GPU costs more than decoding them on the CPU.
    Auto,
    None,   // CPU only
    Vaapi,
    Cuda,
};

struct OpenOptions {
    HwAccel hwAccel = HwAccel::Auto;
    // Keep decoded frames on the GPU (VideoFrame::isHardware()) instead of copying
    // them to system memory. For sinks that consume GPU surfaces directly.
    bool keepHwFrames = false;
    // Stream index of the audio track to play (see MediaInfo::audioTracks); -1 picks the default.
    int audioStreamIndex = -1;
    // Passed to libavformat, e.g. {"rtsp_transport", "tcp"}, or to a device, e.g.
    // {"video_size", "1280x720"}, {"framerate", "30"}, {"input_format", "mjpeg"} for v4l2.
    std::map<std::string, std::string> formatOptions;
    // A libavdevice/libavformat input such as "v4l2" (cameras), "x11grab" or "lavfi".
    // Empty guesses from the url; "/dev/video*" is opened as "v4l2".
    std::string inputFormat;
    // For live sources (cameras, RTSP): no input buffering, no frame-threaded decoding
    // (it holds several frames back), and every frame is shown as soon as it is decoded,
    // skipping to the newest one when several are waiting.
    bool lowLatency = false;
};

// One audio or subtitle track inside the file.
struct TrackInfo {
    int streamIndex = -1;
    std::string language;  // e.g. "eng", may be empty
    std::string title;     // may be empty
    std::string codec;
    bool isText = false;   // subtitles only: text (readable with readSubtitleTrack) or bitmap
};

// A named part of the media (a film's scenes, a recording's sections).
struct Chapter {
    double start = 0.0;  // seconds, on the same time line as frame pts
    double end = 0.0;
    std::string title;   // may be empty
};

struct MediaInfo {
    std::string url;
    double duration = 0.0;  // seconds, 0 when unknown (live streams)
    bool hasVideo = false;
    bool hasAudio = false;
    int width = 0;
    int height = 0;
    double frameRate = 0.0;
    int rotationDegrees = 0;       // clockwise rotation requested by the file: 0/90/180/270
    double pixelAspectRatio = 1.0; // width / height of one pixel; 1 for square pixels
    bool isInterlaced = false;     // deinterlaced automatically while decoding
    std::string videoCodec;
    std::string audioCodec;
    std::string hardwareDecoder;   // "vaapi", "cuda" or empty for CPU decoding
    int audioStreamIndex = -1;     // the audio track being played, -1 when none
    std::vector<TrackInfo> audioTracks;
    std::vector<TrackInfo> subtitleTracks;
    std::vector<Chapter> chapters;  // in time order; empty when the file has none
};

}  // namespace viplyr
