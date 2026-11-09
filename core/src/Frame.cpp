#include "viplyr/Frame.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace viplyr {

namespace {

AVPixelFormat pixelFormatOf(const AVFrame *frame) {
    return static_cast<AVPixelFormat>(frame->format);
}

AVPixelFormat toAvFormat(PixelFormat format) {
    switch (format) {
        case PixelFormat::Yuv420p: return AV_PIX_FMT_YUV420P;
        case PixelFormat::Nv12:    return AV_PIX_FMT_NV12;
        case PixelFormat::Bgr24:   return AV_PIX_FMT_BGR24;
        case PixelFormat::Rgba:    return AV_PIX_FMT_RGBA;
        default:                   return AV_PIX_FMT_NONE;
    }
}

}  // namespace

VideoFrame VideoFrame::adopt(AVFrame *frame, double ptsSeconds, double durationSeconds) {
    VideoFrame result;
    result.frame_.reset(frame, [](AVFrame *owned) { av_frame_free(&owned); });
    result.pts_ = ptsSeconds;
    result.duration_ = durationSeconds;
    return result;
}

int VideoFrame::width() const { return frame_ ? frame_->width : 0; }
int VideoFrame::height() const { return frame_ ? frame_->height : 0; }

PixelFormat VideoFrame::format() const {
    if (!frame_) return PixelFormat::Unknown;
    if (frame_->hw_frames_ctx) return PixelFormat::Hardware;
    switch (pixelFormatOf(frame_.get())) {
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P: return PixelFormat::Yuv420p;
        case AV_PIX_FMT_NV12:     return PixelFormat::Nv12;
        case AV_PIX_FMT_BGR24:    return PixelFormat::Bgr24;
        case AV_PIX_FMT_RGBA:     return PixelFormat::Rgba;
        default:                  return PixelFormat::Unknown;
    }
}

bool VideoFrame::isFullRange() const {
    if (!frame_) return false;
    return frame_->color_range == AVCOL_RANGE_JPEG || pixelFormatOf(frame_.get()) == AV_PIX_FMT_YUVJ420P;
}

bool VideoFrame::isBt709() const {
    if (!frame_) return false;
    if (frame_->colorspace == AVCOL_SPC_BT709) return true;
    if (frame_->colorspace == AVCOL_SPC_UNSPECIFIED) return frame_->height >= 720;  // HD convention
    return false;
}

const uint8_t *VideoFrame::plane(int index) const { return frame_ ? frame_->data[index] : nullptr; }
int VideoFrame::stride(int index) const { return frame_ ? frame_->linesize[index] : 0; }

VideoFrame VideoFrame::download() const {
    if (!isHardware()) return *this;
    AVFrame *cpuFrame = av_frame_alloc();
    if (av_hwframe_transfer_data(cpuFrame, frame_.get(), 0) < 0) {
        av_frame_free(&cpuFrame);
        return {};
    }
    av_frame_copy_props(cpuFrame, frame_.get());
    return adopt(cpuFrame, pts_, duration_);
}

VideoFrame VideoFrame::convert(PixelFormat target, int width, int height) const {
    const AVPixelFormat targetFormat = toAvFormat(target);
    if (!frame_ || targetFormat == AV_PIX_FMT_NONE) return {};
    if (isHardware()) return download().convert(target, width, height);

    if (width <= 0) width = frame_->width;
    if (height <= 0) height = frame_->height;
    if (format() == target && width == frame_->width && height == frame_->height) return *this;

    SwsContext *scaler = sws_getContext(frame_->width, frame_->height, pixelFormatOf(frame_.get()),
                                        width, height, targetFormat, SWS_BILINEAR,
                                        nullptr, nullptr, nullptr);
    if (!scaler) return {};

    // Tell swscale which colour matrix and range the source uses, so RGB output is correct.
    const int *matrix = sws_getCoefficients(isBt709() ? SWS_CS_ITU709 : SWS_CS_ITU601);
    sws_setColorspaceDetails(scaler, matrix, isFullRange() ? 1 : 0, matrix, 1, 0, 1 << 16, 1 << 16);

    AVFrame *converted = av_frame_alloc();
    converted->format = targetFormat;
    converted->width = width;
    converted->height = height;
    if (av_frame_get_buffer(converted, 0) < 0) {
        av_frame_free(&converted);
        sws_freeContext(scaler);
        return {};
    }
    sws_scale(scaler, frame_->data, frame_->linesize, 0, frame_->height,
              converted->data, converted->linesize);
    sws_freeContext(scaler);

    av_frame_copy_props(converted, frame_.get());
    // The pixels are now full-range in the target format's own colour space.
    converted->color_range = AVCOL_RANGE_JPEG;
    return adopt(converted, pts_, duration_);
}

}  // namespace viplyr
