#pragma once
#include <functional>
#include <memory>
#include <string>

#include "viplyr/MediaInfo.h"
#include "viplyr/Sinks.h"

namespace viplyr {

enum class State { Stopped, Paused, Playing, Ended };

// Plays one media URL: demuxes, decodes, keeps audio and video in sync and
// delivers frames to the registered sinks.
//
// The public methods must be called from one thread (the "control" thread).
// Callbacks run on internal threads; do not call close() or open() from inside one.
class Player {
public:
    Player();
    ~Player();
    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    // Sinks must be registered before open() and outlive the Player.
    void addVideoSink(IVideoSink *sink);
    void setAudioSink(IAudioSink *sink);

    // Opens the media paused, with its first frame presented.
    bool open(const std::string &url, const OpenOptions &options = {}, std::string *error = nullptr);
    void close();

    void play();
    void pause();
    void seek(double seconds);       // frame-accurate
    void stepFrame(int direction);   // pauses, then moves one frame forward (>0) or back (<0)
    void setRate(double rate);       // 0.5 .. 4.0, pitch is preserved

    double position() const;  // seconds
    double duration() const;  // seconds
    double rate() const;
    State state() const;
    const MediaInfo &info() const;

    std::function<void(State)> onStateChanged;
    std::function<void()> onEnded;
    std::function<void(const std::string &)> onError;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace viplyr
