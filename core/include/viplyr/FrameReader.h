#pragma once
#include <memory>
#include <string>

#include "viplyr/Frame.h"
#include "viplyr/MediaInfo.h"

namespace viplyr {

enum class SeekMode {
    Accurate,  // the next read() returns the frame displayed at the requested time
    Keyframe,  // the next read() returns the keyframe at or before it (fast)
};

// Reads video frames one by one on the calling thread: no clock, no audio, no
// extra threads. For batch processing, thumbnails and tests.
class FrameReader {
public:
    FrameReader();
    ~FrameReader();
    FrameReader(const FrameReader &) = delete;
    FrameReader &operator=(const FrameReader &) = delete;

    bool open(const std::string &url, const OpenOptions &options = {}, std::string *error = nullptr);
    void close();
    bool isOpen() const;
    const MediaInfo &info() const;

    bool seek(double seconds, SeekMode mode = SeekMode::Accurate);
    // Returns false at the end of the stream or on error.
    bool read(VideoFrame &frame);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace viplyr
