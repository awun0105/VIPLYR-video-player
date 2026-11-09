#pragma once
#include <cstddef>

#include "viplyr/Frame.h"

namespace viplyr {

// Receives frames at their presentation time. present() is called from a Player
// thread, so an implementation must hand the frame over to its own thread itself.
class IVideoSink {
public:
    virtual ~IVideoSink() = default;
    virtual void present(const VideoFrame &frame) = 0;
};

// Audio is always delivered as interleaved 32-bit float samples.
struct AudioFormat {
    int sampleRate = 48000;
    int channels = 2;
};

// Plays audio. All methods are called from a Player thread.
class IAudioSink {
public:
    virtual ~IAudioSink() = default;
    virtual AudioFormat format() const = 0;
    // Queues up to `frameCount` frames without blocking; returns how many were taken.
    virtual size_t write(const float *samples, size_t frameCount) = 0;
    // Audio that has been written but not yet heard, in seconds.
    virtual double bufferedSeconds() const = 0;
    virtual void flush() = 0;
    virtual void setPaused(bool paused) = 0;
};

}  // namespace viplyr
