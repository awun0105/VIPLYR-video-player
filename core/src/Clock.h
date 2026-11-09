#pragma once
#include <chrono>
#include <mutex>

namespace viplyr {

// The playback clock: media time that advances with wall time at the playback
// rate. The audio thread keeps correcting it with set(); when there is no audio
// it simply free-runs.
class Clock {
public:
    void set(double mediaSeconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        base_ = mediaSeconds;
        wallBase_ = wallNow();
    }

    void setRate(double rate) {
        std::lock_guard<std::mutex> lock(mutex_);
        base_ = currentLocked();
        wallBase_ = wallNow();
        rate_ = rate;
    }

    void setPaused(bool paused) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (paused == paused_) return;
        base_ = currentLocked();
        wallBase_ = wallNow();
        paused_ = paused;
    }

    double now() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return currentLocked();
    }

private:
    static double wallNow() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    double currentLocked() const {
        return paused_ ? base_ : base_ + (wallNow() - wallBase_) * rate_;
    }

    mutable std::mutex mutex_;
    double base_ = 0.0;
    double wallBase_ = 0.0;
    double rate_ = 1.0;
    bool paused_ = true;
};

}  // namespace viplyr
