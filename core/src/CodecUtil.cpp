#include "CodecUtil.h"

#include <cmath>

extern "C" {
#include <libavutil/display.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

namespace viplyr {

namespace {

// Stored in AVCodecContext::opaque so the get_format callback knows which
// hardware pixel format the decoder was set up for.
AVPixelFormat chosenHwFormat(const AVCodecContext *context) {
    return static_cast<AVPixelFormat>(reinterpret_cast<intptr_t>(context->opaque));
}

AVPixelFormat selectPixelFormat(AVCodecContext *context, const AVPixelFormat *formats) {
    const AVPixelFormat wanted = chosenHwFormat(context);
    for (const AVPixelFormat *format = formats; *format != AV_PIX_FMT_NONE; ++format) {
        if (*format == wanted) return *format;
    }
    // The hardware cannot decode this stream: fall back to software decoding.
    return avcodec_default_get_format(context, formats);
}

// Returns the pixel format `codec` produces on `type` devices, or NONE if unsupported.
AVPixelFormat hwFormatFor(const AVCodec *codec, AVHWDeviceType type) {
    for (int i = 0;; ++i) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config) return AV_PIX_FMT_NONE;
        const bool usesDeviceContext = config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX;
        if (usesDeviceContext && config->device_type == type) return config->pix_fmt;
    }
}

bool attachHardware(AVCodecContext *context, const AVCodec *codec, AVHWDeviceType type) {
    const AVPixelFormat hwFormat = hwFormatFor(codec, type);
    if (hwFormat == AV_PIX_FMT_NONE) return false;

    AVBufferRef *device = nullptr;
    if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) < 0) return false;

    context->hw_device_ctx = device;  // the context now owns the reference
    context->opaque = reinterpret_cast<void *>(static_cast<intptr_t>(hwFormat));
    context->get_format = selectPixelFormat;
    context->extra_hw_frames = 8;  // frames we keep queued outside the decoder
    return true;
}

std::string tryHardware(AVCodecContext *context, const AVCodec *codec, HwAccel hwAccel) {
    const bool wantVaapi = hwAccel == HwAccel::Auto || hwAccel == HwAccel::Vaapi;
    const bool wantCuda = hwAccel == HwAccel::Auto || hwAccel == HwAccel::Cuda;
    if (wantVaapi && attachHardware(context, codec, AV_HWDEVICE_TYPE_VAAPI)) return "vaapi";
    if (wantCuda && attachHardware(context, codec, AV_HWDEVICE_TYPE_CUDA)) return "cuda";
    return "";
}

int rotationDegrees(const AVStream *video) {
    const AVPacketSideData *sideData = av_packet_side_data_get(
        video->codecpar->coded_side_data, video->codecpar->nb_coded_side_data,
        AV_PKT_DATA_DISPLAYMATRIX);
    if (!sideData) return 0;
    // The matrix stores a counter-clockwise angle; we report clockwise.
    const double counterClockwise =
        av_display_rotation_get(reinterpret_cast<const int32_t *>(sideData->data));
    if (std::isnan(counterClockwise)) return 0;
    const int clockwise = static_cast<int>(std::lround(-counterClockwise / 90.0)) * 90;
    return ((clockwise % 360) + 360) % 360;
}

std::string metadataValue(const AVStream *stream, const char *key) {
    const AVDictionaryEntry *entry = av_dict_get(stream->metadata, key, nullptr, 0);
    return entry ? entry->value : "";
}

TrackInfo describeTrack(const AVStream *stream) {
    TrackInfo track;
    track.streamIndex = stream->index;
    track.language = metadataValue(stream, "language");
    track.title = metadataValue(stream, "title");
    track.codec = avcodec_get_name(stream->codecpar->codec_id);
    const AVCodecDescriptor *descriptor = avcodec_descriptor_get(stream->codecpar->codec_id);
    track.isText = descriptor && (descriptor->props & AV_CODEC_PROP_TEXT_SUB);
    return track;
}

}  // namespace

std::string errorText(int averror) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(averror, buffer, sizeof(buffer));
    return buffer;
}

AVFormatContext *openInput(const std::string &url, const OpenOptions &options, std::string *error) {
    AVDictionary *dictionary = nullptr;
    for (const auto &option : options.formatOptions) {
        av_dict_set(&dictionary, option.first.c_str(), option.second.c_str(), 0);
    }

    AVFormatContext *format = nullptr;
    int result = avformat_open_input(&format, url.c_str(), nullptr, &dictionary);
    av_dict_free(&dictionary);
    if (result >= 0) result = avformat_find_stream_info(format, nullptr);
    if (result < 0) {
        if (error) *error = "Cannot open " + url + ": " + errorText(result);
        avformat_close_input(&format);
        return nullptr;
    }
    return format;
}

AVCodecContext *openDecoder(const AVStream *stream, HwAccel hwAccel, std::string *hardwareName,
                            std::string *error) {
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        if (error) *error = std::string("No decoder for ") + avcodec_get_name(stream->codecpar->codec_id);
        return nullptr;
    }

    AVCodecContext *context = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(context, stream->codecpar);
    context->pkt_timebase = stream->time_base;

    std::string hardware;
    const bool isVideo = stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
    // Interlaced video is deinterlaced by a CPU filter, so it is decoded on the CPU too.
    const bool allowHardware = isVideo && hwAccel != HwAccel::None && !isInterlaced(stream);
    if (allowHardware) hardware = tryHardware(context, codec, hwAccel);
    if (hardware.empty()) {
        context->thread_count = 0;  // let FFmpeg use every CPU core
        context->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }

    const int result = avcodec_open2(context, codec, nullptr);
    if (result < 0) {
        if (error) *error = std::string("Cannot open decoder ") + codec->name + ": " + errorText(result);
        avcodec_free_context(&context);
        return nullptr;
    }
    if (hardwareName) *hardwareName = hardware;
    return context;
}

HwAccel effectiveHwAccel(const OpenOptions &options) {
    if (options.hwAccel != HwAccel::Auto) return options.hwAccel;
    return options.keepHwFrames ? HwAccel::Auto : HwAccel::None;
}

bool isInterlaced(const AVStream *video) {
    switch (video->codecpar->field_order) {
        case AV_FIELD_TT:
        case AV_FIELD_BB:
        case AV_FIELD_TB:
        case AV_FIELD_BT: return true;
        default:          return false;
    }
}

double startOffsetSeconds(const AVFormatContext *format, const AVStream *video) {
    if (video && video->start_time != AV_NOPTS_VALUE) return toSeconds(video->start_time, video->time_base);
    if (format->start_time == AV_NOPTS_VALUE) return 0.0;
    return static_cast<double>(format->start_time) / AV_TIME_BASE;
}

double frameDurationSeconds(const AVStream *video) {
    AVRational rate = video->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0) rate = video->r_frame_rate;
    if (rate.num <= 0 || rate.den <= 0) return 1.0 / 25.0;
    return av_q2d(av_inv_q(rate));
}

MediaInfo describeMedia(const AVFormatContext *format, const AVStream *video, const AVStream *audio,
                        const std::string &hardwareName) {
    MediaInfo info;
    info.url = format->url ? format->url : "";
    if (format->duration != AV_NOPTS_VALUE && format->duration > 0) {
        info.duration = static_cast<double>(format->duration) / AV_TIME_BASE;
    }
    if (video) {
        info.hasVideo = true;
        info.width = video->codecpar->width;
        info.height = video->codecpar->height;
        info.frameRate = 1.0 / frameDurationSeconds(video);
        info.rotationDegrees = rotationDegrees(video);
        info.videoCodec = avcodec_get_name(video->codecpar->codec_id);
        info.isInterlaced = isInterlaced(video);
        const AVRational pixelAspect = av_guess_sample_aspect_ratio(
            const_cast<AVFormatContext *>(format), const_cast<AVStream *>(video), nullptr);
        if (pixelAspect.num > 0 && pixelAspect.den > 0) info.pixelAspectRatio = av_q2d(pixelAspect);
        info.hardwareDecoder = hardwareName;
    }
    if (audio) {
        info.hasAudio = true;
        info.audioCodec = avcodec_get_name(audio->codecpar->codec_id);
        info.audioStreamIndex = audio->index;
    }
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const AVStream *stream = format->streams[i];
        const AVMediaType type = stream->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_AUDIO) info.audioTracks.push_back(describeTrack(stream));
        else if (type == AVMEDIA_TYPE_SUBTITLE) info.subtitleTracks.push_back(describeTrack(stream));
    }
    return info;
}

VideoFrame makeVideoFrame(AVFrame *decoded, const AVStream *stream, double startOffset,
                          bool keepHwFrames, double *nextPts) {
    double pts = *nextPts;
    if (decoded->best_effort_timestamp != AV_NOPTS_VALUE) {
        pts = toSeconds(decoded->best_effort_timestamp, stream->time_base) - startOffset;
    }
    double duration = decoded->duration > 0 ? toSeconds(decoded->duration, stream->time_base)
                                            : frameDurationSeconds(stream);
    *nextPts = pts + duration;

    AVFrame *owned = av_frame_alloc();
    const bool isHardware = decoded->hw_frames_ctx != nullptr;
    if (isHardware && !keepHwFrames) {
        if (av_hwframe_transfer_data(owned, decoded, 0) < 0) {
            av_frame_free(&owned);
            av_frame_unref(decoded);
            return {};
        }
        av_frame_copy_props(owned, decoded);
        av_frame_unref(decoded);
    } else {
        av_frame_move_ref(owned, decoded);
    }
    return VideoFrame::adopt(owned, pts, duration);
}

}  // namespace viplyr
