#include "AudioTempoFilter.h"

#include <cmath>
#include <cstdio>
#include <string>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace viplyr {

namespace {

std::string describeLayout(const AVChannelLayout &layout) {
    AVChannelLayout usable = {};
    if (layout.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&usable, layout.nb_channels);
    else av_channel_layout_copy(&usable, &layout);

    char text[128] = {};
    av_channel_layout_describe(&usable, text, sizeof(text));
    av_channel_layout_uninit(&usable);
    return text;
}

std::string describeDefaultLayout(int channels) {
    AVChannelLayout layout = {};
    av_channel_layout_default(&layout, channels);
    const std::string text = describeLayout(layout);
    av_channel_layout_uninit(&layout);
    return text;
}

}  // namespace

bool AudioTempoFilter::isConfiguredFor(const AVFrame *input, double tempo) const {
    return graph_ && input->format == inputFormat_ && input->sample_rate == inputSampleRate_ &&
           input->ch_layout.nb_channels == inputChannels_ && std::fabs(tempo - tempo_) < 1e-3;
}

bool AudioTempoFilter::configure(const AVFrame *input, const AudioFormat &output, double tempo) {
    reset();
    graph_ = avfilter_graph_alloc();
    if (!graph_) return false;

    char sourceArgs[256];
    std::snprintf(sourceArgs, sizeof(sourceArgs),
                  "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                  input->sample_rate, input->sample_rate,
                  av_get_sample_fmt_name(static_cast<AVSampleFormat>(input->format)),
                  describeLayout(input->ch_layout).c_str());

    // The tempo stage is left out at normal speed, where it would only add latency.
    std::string chain;
    if (std::fabs(tempo - 1.0) >= 1e-3) chain = "atempo=" + std::to_string(tempo) + ",";
    chain += "aformat=sample_fmts=flt:sample_rates=" + std::to_string(output.sampleRate) +
             ":channel_layouts=" + describeDefaultLayout(output.channels);

    bool ok = avfilter_graph_create_filter(&source_, avfilter_get_by_name("abuffer"), "in",
                                           sourceArgs, nullptr, graph_) >= 0 &&
              avfilter_graph_create_filter(&sink_, avfilter_get_by_name("abuffersink"), "out",
                                           nullptr, nullptr, graph_) >= 0;
    if (ok) {
        // "outputs" of our source feed the chain; its result goes to the "inputs" of our sink.
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
        ok = avfilter_graph_parse_ptr(graph_, chain.c_str(), &inputs, &outputs, nullptr) >= 0 &&
             avfilter_graph_config(graph_, nullptr) >= 0;
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
    }
    if (!ok) {
        reset();
        return false;
    }

    inputFormat_ = input->format;
    inputSampleRate_ = input->sample_rate;
    inputChannels_ = input->ch_layout.nb_channels;
    tempo_ = tempo;
    samplesPushed_ = 0;
    return true;
}

void AudioTempoFilter::reset() {
    avfilter_graph_free(&graph_);  // also frees source_ and sink_
    source_ = nullptr;
    sink_ = nullptr;
    inputFormat_ = -1;
}

bool AudioTempoFilter::push(AVFrame *input) {
    if (!graph_) return false;
    input->pts = samplesPushed_;  // the graph's time base is 1/sample_rate
    samplesPushed_ += input->nb_samples;
    return av_buffersrc_add_frame(source_, input) >= 0;
}

bool AudioTempoFilter::pull(AVFrame *output) {
    return graph_ && av_buffersink_get_frame(sink_, output) >= 0;
}

}  // namespace viplyr
