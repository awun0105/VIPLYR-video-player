#pragma once
#include <map>
#include <string>

namespace viplyr {

enum class HwAccel {
    Auto,   // try VAAPI, then CUDA, then fall back to the CPU
    None,   // CPU only
    Vaapi,
    Cuda,
};

struct OpenOptions {
    HwAccel hwAccel = HwAccel::Auto;
    // Keep decoded frames on the GPU (VideoFrame::isHardware()) instead of copying
    // them to system memory. For sinks that consume GPU surfaces directly.
    bool keepHwFrames = false;
    // Passed to libavformat, e.g. {"rtsp_transport", "tcp"} or {"fflags", "nobuffer"}.
    std::map<std::string, std::string> formatOptions;
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
    std::string videoCodec;
    std::string audioCodec;
    std::string hardwareDecoder;   // "vaapi", "cuda" or empty for CPU decoding
};

}  // namespace viplyr
