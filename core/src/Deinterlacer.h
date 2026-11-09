#pragma once
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavutil/frame.h>
#include <libavutil/rational.h>
}

namespace viplyr {

// Turns interlaced frames into progressive ones (one output frame per input
// frame). A thin wrapper around libavfilter's bwdif filter.
class Deinterlacer {
public:
    Deinterlacer() = default;
    ~Deinterlacer() { reset(); }
    Deinterlacer(const Deinterlacer &) = delete;
    Deinterlacer &operator=(const Deinterlacer &) = delete;

    // Consumes the frame's data. `timeBase` is the time base of its timestamps.
    bool push(AVFrame *frame, AVRational timeBase);
    // Tells the filter no more frames are coming, so it releases the ones it holds.
    void finish();
    bool pull(AVFrame *output);  // false when no output is ready
    void reset();                // forget everything, e.g. after a seek

private:
    bool configure(const AVFrame *frame, AVRational timeBase);

    AVFilterGraph *graph_ = nullptr;
    AVFilterContext *source_ = nullptr;
    AVFilterContext *sink_ = nullptr;
    AVRational inputTimeBase_ = {0, 1};
};

}  // namespace viplyr
