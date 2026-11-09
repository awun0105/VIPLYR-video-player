#include "Deinterlacer.h"

#include <cstdio>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/mathematics.h>
}

namespace viplyr {

bool Deinterlacer::configure(const AVFrame *frame, AVRational timeBase) {
    reset();
    graph_ = avfilter_graph_alloc();
    if (!graph_) return false;

    char sourceArgs[256];
    std::snprintf(sourceArgs, sizeof(sourceArgs),
                  "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1",
                  frame->width, frame->height, frame->format, timeBase.num, timeBase.den);

    bool ok = avfilter_graph_create_filter(&source_, avfilter_get_by_name("buffer"), "in",
                                           sourceArgs, nullptr, graph_) >= 0 &&
              avfilter_graph_create_filter(&sink_, avfilter_get_by_name("buffersink"), "out",
                                           nullptr, nullptr, graph_) >= 0;
    if (ok) {
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs = avfilter_inout_alloc();
        outputs->name = av_strdup("in");
        outputs->filter_ctx = source_;
        outputs->pad_idx = 0;
        outputs->next = nullptr;
        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink_;
        inputs->pad_idx = 0;
        inputs->next = nullptr;
        // send_frame: one frame out per frame in, so timestamps and counts are unchanged.
        ok = avfilter_graph_parse_ptr(graph_, "bwdif=mode=send_frame:deint=all", &inputs, &outputs,
                                      nullptr) >= 0 &&
             avfilter_graph_config(graph_, nullptr) >= 0;
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
    }
    if (!ok) reset();
    inputTimeBase_ = timeBase;
    return ok;
}

bool Deinterlacer::push(AVFrame *frame, AVRational timeBase) {
    if (!graph_ && !configure(frame, timeBase)) return false;
    if (frame->pts == AV_NOPTS_VALUE) frame->pts = frame->best_effort_timestamp;
    return av_buffersrc_add_frame(source_, frame) >= 0;
}

void Deinterlacer::finish() {
    // A null frame marks the end of the stream. A failure only means nothing more comes out.
    if (graph_ && av_buffersrc_add_frame(source_, nullptr) < 0) reset();
}

bool Deinterlacer::pull(AVFrame *output) {
    if (!graph_ || av_buffersink_get_frame(sink_, output) < 0) return false;
    // The filter counts time in its own (finer) unit; convert back to the caller's.
    const AVRational filterTimeBase = av_buffersink_get_time_base(sink_);
    output->pts = av_rescale_q(output->pts, filterTimeBase, inputTimeBase_);
    output->duration = av_rescale_q(output->duration, filterTimeBase, inputTimeBase_);
    output->best_effort_timestamp = output->pts;
    return true;
}

void Deinterlacer::reset() {
    avfilter_graph_free(&graph_);
    source_ = nullptr;
    sink_ = nullptr;
}

}  // namespace viplyr
