#include "viplyr/VideoRecorder.h"

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "CodecUtil.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

namespace viplyr {

namespace {

constexpr AVRational kTimeBase = {1, 90000};

}  // namespace

class VideoRecorder::Impl {
public:
    ~Impl() { stop(nullptr); }

    bool start(const std::string &path, const RecorderOptions &options, std::string *error) {
        stop(nullptr);
        std::lock_guard<std::mutex> lock(mutex_);
        path_ = path;
        options_ = options;
        failure_.clear();
        framesWritten_ = 0;
        droppedFrames_ = 0;
        firstPts_ = NAN;
        lastPts_ = -1;
        finishing_ = false;
        queue_.clear();

        // Fail early on a name the muxer does not know.
        AVFormatContext *probe = nullptr;
        avformat_alloc_output_context2(&probe, nullptr, nullptr, path.c_str());
        if (!probe) {
            if (error) *error = "Unknown file type: " + path;
            return false;
        }
        avformat_free_context(probe);

        recording_ = true;
        thread_ = std::thread([this] { encodeLoop(); });
        return true;
    }

    bool stop(std::string *error) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!recording_) return true;
            finishing_ = true;
        }
        wake_.notify_one();
        thread_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        recording_ = false;
        if (!failure_.empty() && error) *error = failure_;
        return failure_.empty();
    }

    void present(const VideoFrame &frame) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!recording_ || finishing_ || !failure_.empty()) return;
            if (static_cast<int>(queue_.size()) >= options_.queueCapacity) {
                ++droppedFrames_;
                return;
            }
            queue_.push_back(frame);
        }
        wake_.notify_one();
    }

    bool isRecording() const {
        std::lock_guard<std::mutex> lock(mutex_);
        // Keep recording_ set until stop() joins the worker, but tell producers
        // to stop waiting for progress as soon as the encoder fails.
        return recording_ && failure_.empty();
    }

    std::atomic<int64_t> framesWritten_{0};
    std::atomic<int64_t> droppedFrames_{0};
    mutable std::mutex mutex_;
    std::string encoderName_;

private:
    void encodeLoop() {
        for (;;) {
            VideoFrame frame;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return finishing_ || !queue_.empty(); });
                if (queue_.empty()) break;  // finishing and nothing left
                frame = std::move(queue_.front());
                queue_.pop_front();
            }
            if (failed()) continue;  // keep draining so present() never blocks
            if (!encoder_ && !openOutput(frame)) continue;
            encode(frame);
        }
        if (encoder_ && !failed()) {
            sendToEncoder(nullptr);  // flush
            if (av_write_trailer(output_) < 0) fail("Cannot finish " + path_);
        }
        closeOutput();
    }

    bool openOutput(const VideoFrame &first) {
        width_ = options_.width > 0 ? options_.width : first.width();
        height_ = options_.height > 0 ? options_.height : first.height();
        width_ += width_ % 2;  // 4:2:0 needs even sizes
        height_ += height_ % 2;

        const char *candidates[] = {"h264_nvenc", "libx264", "libopenh264", "mpeg4"};
        bool opened = false;
        if (options_.encoder != "auto") {
            opened = openEncoder(options_.encoder);
        } else {
            for (const char *name : candidates) {
                if (openEncoder(name)) {
                    opened = true;
                    break;
                }
            }
        }
        if (!opened) {
            fail("No usable video encoder (" + options_.encoder + ")");
            return false;
        }

        avformat_alloc_output_context2(&output_, nullptr, nullptr, path_.c_str());
        stream_ = avformat_new_stream(output_, nullptr);
        avcodec_parameters_from_context(stream_->codecpar, encoder_);
        stream_->time_base = kTimeBase;
        int result = 0;
        if (!(output_->oformat->flags & AVFMT_NOFILE)) result = avio_open(&output_->pb, path_.c_str(), AVIO_FLAG_WRITE);
        if (result >= 0) result = avformat_write_header(output_, nullptr);
        if (result < 0) {
            fail("Cannot write " + path_ + ": " + errorText(result));
            return false;
        }
        return true;
    }

    bool openEncoder(const std::string &name) {
        const AVCodec *codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec || codec->type != AVMEDIA_TYPE_VIDEO) return false;

        AVCodecContext *context = avcodec_alloc_context3(codec);
        context->width = width_;
        context->height = height_;
        context->time_base = kTimeBase;
        context->framerate = av_d2q(options_.frameRate > 0.0 ? options_.frameRate : 30.0, 1000);
        context->gop_size = static_cast<int>(std::lround(2.0 * av_q2d(context->framerate)));
        context->max_b_frames = 0;  // frames go out in the order they come in
        context->color_range = AVCOL_RANGE_MPEG;
        // NV12 where the encoder takes it (NVENC reads it natively), else planar 4:2:0.
        context->pix_fmt = AV_PIX_FMT_YUV420P;
        for (const AVPixelFormat *format = codec->pix_fmts; format && *format != AV_PIX_FMT_NONE; ++format) {
            if (*format == AV_PIX_FMT_NV12) context->pix_fmt = AV_PIX_FMT_NV12;
        }
        pixelFormat_ = context->pix_fmt == AV_PIX_FMT_NV12 ? PixelFormat::Nv12 : PixelFormat::Yuv420p;

        const std::string quality = std::to_string(options_.quality);
        av_opt_set(context->priv_data, "crf", quality.c_str(), 0);     // x264, x265
        av_opt_set(context->priv_data, "cq", quality.c_str(), 0);      // NVENC
        av_opt_set(context->priv_data, "preset", name.find("nvenc") != std::string::npos ? "p4" : "veryfast", 0);
        if (name == "mpeg4") context->bit_rate = static_cast<int64_t>(width_) * height_ * 4;

        AVFormatContext *probe = nullptr;
        avformat_alloc_output_context2(&probe, nullptr, nullptr, path_.c_str());
        if (probe && (probe->oformat->flags & AVFMT_GLOBALHEADER)) context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        avformat_free_context(probe);

        if (avcodec_open2(context, codec, nullptr) < 0) {
            avcodec_free_context(&context);
            return false;
        }
        encoder_ = context;
        std::lock_guard<std::mutex> lock(mutex_);
        encoderName_ = name;
        return true;
    }

    void encode(const VideoFrame &frame) {
        const VideoFrame converted = frame.convert(pixelFormat_, width_, height_);
        if (!converted.isValid()) return;
        if (std::isnan(firstPts_)) firstPts_ = frame.pts();

        AVFrame *input = av_frame_clone(converted.native());
        int64_t pts = std::llround((frame.pts() - firstPts_) * kTimeBase.den);
        if (pts <= lastPts_) pts = lastPts_ + 1;  // after a seek back, keep time moving forward
        lastPts_ = pts;
        input->pts = pts;
        input->color_range = AVCOL_RANGE_MPEG;
        input->pict_type = AV_PICTURE_TYPE_NONE;
        sendToEncoder(input);
        av_frame_free(&input);
    }

    void sendToEncoder(AVFrame *frame) {
        if (avcodec_send_frame(encoder_, frame) < 0) {
            fail("The encoder refused a frame");
            return;
        }
        AVPacket *packet = av_packet_alloc();
        while (avcodec_receive_packet(encoder_, packet) >= 0) {
            av_packet_rescale_ts(packet, encoder_->time_base, stream_->time_base);
            packet->stream_index = stream_->index;
            if (av_interleaved_write_frame(output_, packet) < 0) fail("Cannot write " + path_);
            else ++framesWritten_;  // no B-frames: one packet per frame
        }
        av_packet_free(&packet);
    }

    void closeOutput() {
        if (output_) {
            if (!(output_->oformat->flags & AVFMT_NOFILE)) avio_closep(&output_->pb);
            avformat_free_context(output_);
        }
        avcodec_free_context(&encoder_);
        output_ = nullptr;
        stream_ = nullptr;
    }

    void fail(const std::string &message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (failure_.empty()) failure_ = message;
    }

    bool failed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !failure_.empty();
    }

    std::condition_variable wake_;
    std::deque<VideoFrame> queue_;
    std::thread thread_;
    bool recording_ = false;
    bool finishing_ = false;
    std::string path_;
    RecorderOptions options_;
    std::string failure_;

    // Encoder thread only.
    AVCodecContext *encoder_ = nullptr;
    AVFormatContext *output_ = nullptr;
    AVStream *stream_ = nullptr;
    PixelFormat pixelFormat_ = PixelFormat::Yuv420p;
    int width_ = 0;
    int height_ = 0;
    double firstPts_ = NAN;
    int64_t lastPts_ = -1;
};

VideoRecorder::VideoRecorder() : impl_(std::make_unique<Impl>()) {}
VideoRecorder::~VideoRecorder() = default;

bool VideoRecorder::start(const std::string &path, const RecorderOptions &options, std::string *error) {
    return impl_->start(path, options, error);
}
bool VideoRecorder::stop(std::string *error) { return impl_->stop(error); }
bool VideoRecorder::isRecording() const { return impl_->isRecording(); }
int64_t VideoRecorder::framesWritten() const { return impl_->framesWritten_; }
int64_t VideoRecorder::droppedFrames() const { return impl_->droppedFrames_; }
std::string VideoRecorder::encoderName() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->encoderName_;
}
void VideoRecorder::present(const VideoFrame &frame) { impl_->present(frame); }

}  // namespace viplyr
