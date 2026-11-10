#include "viplyr/Frame.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
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
        case PixelFormat::Yuv420p10: return AV_PIX_FMT_YUV420P10LE;
        case PixelFormat::P010:    return AV_PIX_FMT_P010LE;
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
        case AV_PIX_FMT_YUV420P10LE: return PixelFormat::Yuv420p10;
        case AV_PIX_FMT_P010LE:   return PixelFormat::P010;
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

bool VideoFrame::isBt2020() const {
    if (!frame_) return false;
    return frame_->colorspace == AVCOL_SPC_BT2020_NCL || frame_->colorspace == AVCOL_SPC_BT2020_CL;
}

TransferFunction VideoFrame::transfer() const {
    if (!frame_) return TransferFunction::Sdr;
    if (frame_->color_trc == AVCOL_TRC_SMPTE2084) return TransferFunction::Pq;
    if (frame_->color_trc == AVCOL_TRC_ARIB_STD_B67) return TransferFunction::Hlg;
    return TransferFunction::Sdr;
}

const uint8_t *VideoFrame::plane(int index) const { return frame_ ? frame_->data[index] : nullptr; }
int VideoFrame::stride(int index) const { return frame_ ? frame_->linesize[index] : 0; }

namespace {

constexpr uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(a) | static_cast<uint32_t>(b) << 8 | static_cast<uint32_t>(c) << 16 |
           static_cast<uint32_t>(d) << 24;
}

// Single-plane DRM formats, the way EGL imports each plane on its own.
constexpr uint32_t kDrmR8 = fourcc('R', '8', ' ', ' ');
constexpr uint32_t kDrmGr88 = fourcc('G', 'R', '8', '8');
constexpr uint32_t kDrmR16 = fourcc('R', '1', '6', ' ');
constexpr uint32_t kDrmGr1616 = fourcc('G', 'R', '3', '2');

}  // namespace

DmaBufImage VideoFrame::exportDmaBuf() const {
    if (!frame_ || pixelFormatOf(frame_.get()) != AV_PIX_FMT_VAAPI || !frame_->hw_frames_ctx) return {};

    const auto *framesContext = reinterpret_cast<const AVHWFramesContext *>(frame_->hw_frames_ctx->data);
    const AVPixelFormat storage = framesContext->sw_format;
    if (storage != AV_PIX_FMT_NV12 && storage != AV_PIX_FMT_P010LE) return {};
    const bool sixteenBit = storage == AV_PIX_FMT_P010LE;

    AVFrame *mapped = av_frame_alloc();
    mapped->format = AV_PIX_FMT_DRM_PRIME;
    if (av_hwframe_map(mapped, frame_.get(), AV_HWFRAME_MAP_READ) < 0) {
        av_frame_free(&mapped);
        return {};
    }
    std::shared_ptr<AVFrame> owner(mapped, [](AVFrame *owned) { av_frame_free(&owned); });
    const auto *descriptor = reinterpret_cast<const AVDRMFrameDescriptor *>(mapped->data[0]);

    DmaBufImage image;
    image.format = sixteenBit ? PixelFormat::P010 : PixelFormat::Nv12;
    // Drivers export either one layer holding both planes or one layer per plane;
    // either way, plane 0 is luma and plane 1 the interleaved chroma.
    for (int layer = 0; layer < descriptor->nb_layers; ++layer) {
        for (int i = 0; i < descriptor->layers[layer].nb_planes; ++i) {
            if (image.planeCount == 2) return {};
            const AVDRMPlaneDescriptor &source = descriptor->layers[layer].planes[i];
            const AVDRMObjectDescriptor &object = descriptor->objects[source.object_index];
            const bool luma = image.planeCount == 0;

            DmaBufPlane &plane = image.planes[image.planeCount++];
            plane.fd = object.fd;
            plane.modifier = object.format_modifier;
            plane.offset = static_cast<uint32_t>(source.offset);
            plane.pitch = static_cast<uint32_t>(source.pitch);
            plane.width = luma ? frame_->width : (frame_->width + 1) / 2;
            plane.height = luma ? frame_->height : (frame_->height + 1) / 2;
            if (sixteenBit) plane.drmFormat = luma ? kDrmR16 : kDrmGr1616;
            else plane.drmFormat = luma ? kDrmR8 : kDrmGr88;
        }
    }
    if (image.planeCount != 2) return {};
    image.keepAlive = std::move(owner);
    return image;
}

CudaImage VideoFrame::cudaPlanes() const {
    if (!frame_ || pixelFormatOf(frame_.get()) != AV_PIX_FMT_CUDA || !frame_->hw_frames_ctx) return {};
    const auto *framesContext = reinterpret_cast<const AVHWFramesContext *>(frame_->hw_frames_ctx->data);
    if (framesContext->sw_format != AV_PIX_FMT_NV12 && framesContext->sw_format != AV_PIX_FMT_P010LE) return {};

    CudaImage image;
    image.format = framesContext->sw_format == AV_PIX_FMT_P010LE ? PixelFormat::P010 : PixelFormat::Nv12;
    image.planeCount = 2;
    for (int i = 0; i < 2; ++i) {
        image.planes[i] = reinterpret_cast<uint64_t>(frame_->data[i]);
        image.pitch[i] = frame_->linesize[i];
        image.width[i] = i == 0 ? frame_->width : (frame_->width + 1) / 2;
        image.height[i] = i == 0 ? frame_->height : (frame_->height + 1) / 2;
    }
    return image;
}

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
    const int standard = isBt2020() ? SWS_CS_BT2020 : isBt709() ? SWS_CS_ITU709 : SWS_CS_ITU601;
    const int *matrix = sws_getCoefficients(standard);
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
