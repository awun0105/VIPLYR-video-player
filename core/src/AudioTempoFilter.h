#pragma once
#include "viplyr/Sinks.h"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavutil/frame.h>
}

namespace viplyr {

// Converts decoded audio to the sink's format (interleaved float) and changes its
// tempo without changing the pitch. A thin wrapper around a libavfilter graph.
class AudioTempoFilter {
public:
    AudioTempoFilter() = default;
    ~AudioTempoFilter() { reset(); }
    AudioTempoFilter(const AudioTempoFilter &) = delete;
    AudioTempoFilter &operator=(const AudioTempoFilter &) = delete;

    // True when the graph was built for this input layout and tempo.
    bool isConfiguredFor(const AVFrame *input, double tempo) const;
    bool configure(const AVFrame *input, const AudioFormat &output, double tempo);
    void reset();

    bool push(AVFrame *input);    // consumes the frame's data
    bool pull(AVFrame *output);   // false when no more output is ready

private:
    AVFilterGraph *graph_ = nullptr;
    AVFilterContext *source_ = nullptr;
    AVFilterContext *sink_ = nullptr;
    int inputFormat_ = -1;
    int inputSampleRate_ = 0;
    int inputChannels_ = 0;
    double tempo_ = 1.0;
    int64_t samplesPushed_ = 0;
};

}  // namespace viplyr
