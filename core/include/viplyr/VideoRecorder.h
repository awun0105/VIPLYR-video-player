#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "viplyr/Sinks.h"

namespace viplyr {

struct RecorderOptions {
    // "auto" tries h264_nvenc, then libx264. Any FFmpeg video encoder name works,
    // e.g. "libx265", "hevc_nvenc", "libx264".
    std::string encoder = "auto";
    int quality = 20;       // constant quality (CRF / CQ): lower is better and bigger
    int width = 0;          // 0 keeps the size of the first frame
    int height = 0;
    double frameRate = 30.0; // a hint for rate control; timestamps still follow the frames
    int queueCapacity = 32; // frames waiting for the encoder; more are dropped (see droppedFrames)
};

// A frame sink that encodes what it receives into a video file (format from the
// file name: .mp4, .mkv, .mov...). Encoding runs on its own thread, so present()
// never holds up playback. Timestamps follow the frames' pts, starting at 0.
//
// Attach it to a Player (or feed it frames yourself), then start() and stop().
// While stopped, present() does nothing.
class VideoRecorder : public IVideoSink {
public:
    VideoRecorder();
    ~VideoRecorder() override;  // stops and finishes the file
    VideoRecorder(const VideoRecorder &) = delete;
    VideoRecorder &operator=(const VideoRecorder &) = delete;

    // The encoder opens with the first frame, so most errors appear in stop()'s result.
    bool start(const std::string &path, const RecorderOptions &options = {}, std::string *error = nullptr);
    // Encodes what is queued and finishes the file. False when recording failed.
    bool stop(std::string *error = nullptr);
    bool isRecording() const;

    int64_t framesWritten() const;
    int64_t droppedFrames() const;  // arrived while the queue was full
    std::string encoderName() const; // the encoder in use, once the first frame is encoded

    void present(const VideoFrame &frame) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace viplyr
