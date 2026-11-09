#include "viplyr/FrameReader.h"

#include "CodecUtil.h"

namespace viplyr {

class FrameReader::Impl {
public:
    ~Impl() { close(); }

    bool open(const std::string &url, const OpenOptions &options, std::string *error);
    void close();
    bool seek(double seconds, SeekMode mode);
    bool read(VideoFrame &frame);

    AVFormatContext *format_ = nullptr;
    AVCodecContext *decoder_ = nullptr;
    const AVStream *stream_ = nullptr;
    PacketPtr packet_{av_packet_alloc()};
    FramePtr decoded_{av_frame_alloc()};
    MediaInfo info_;
    bool keepHwFrames_ = false;
    bool draining_ = false;       // the demuxer hit the end; the decoder is being emptied
    double startOffset_ = 0.0;
    double nextPts_ = 0.0;
    double discardBefore_ = kNoDiscard;
};

bool FrameReader::Impl::open(const std::string &url, const OpenOptions &options, std::string *error) {
    close();
    format_ = openInput(url, options, error);
    if (!format_) return false;

    const int index = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0) {
        if (error) *error = "No video stream in " + url;
        close();
        return false;
    }
    stream_ = format_->streams[index];

    std::string hardwareName;
    decoder_ = openDecoder(stream_, options.hwAccel, &hardwareName, error);
    if (!decoder_) {
        close();
        return false;
    }
    keepHwFrames_ = options.keepHwFrames;
    startOffset_ = startOffsetSeconds(format_);
    info_ = describeMedia(format_, stream_, nullptr, hardwareName);
    return true;
}

void FrameReader::Impl::close() {
    avcodec_free_context(&decoder_);
    avformat_close_input(&format_);
    stream_ = nullptr;
    draining_ = false;
    nextPts_ = 0.0;
    discardBefore_ = kNoDiscard;
    info_ = MediaInfo();
}

bool FrameReader::Impl::seek(double seconds, SeekMode mode) {
    if (!format_) return false;
    const int64_t timestamp = static_cast<int64_t>((seconds + startOffset_) * AV_TIME_BASE);
    if (av_seek_frame(format_, -1, timestamp, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(decoder_);
    draining_ = false;
    nextPts_ = seconds;
    discardBefore_ = mode == SeekMode::Accurate ? seconds : kNoDiscard;
    return true;
}

bool FrameReader::Impl::read(VideoFrame &frame) {
    if (!format_) return false;
    while (true) {
        const int received = avcodec_receive_frame(decoder_, decoded_.get());
        if (received == 0) {
            VideoFrame candidate =
                makeVideoFrame(decoded_.get(), stream_, startOffset_, keepHwFrames_, &nextPts_);
            if (!candidate.isValid()) return false;
            // After an accurate seek, skip frames that end before the target.
            if (candidate.pts() + candidate.duration() <= discardBefore_ + 0.0005) continue;
            discardBefore_ = kNoDiscard;
            frame = std::move(candidate);
            return true;
        }
        if (received != AVERROR(EAGAIN)) return false;  // end of stream or decoder error

        // The decoder needs more input.
        if (draining_) return false;
        const int readResult = av_read_frame(format_, packet_.get());
        if (readResult < 0) {
            draining_ = true;
            avcodec_send_packet(decoder_, nullptr);
            continue;
        }
        if (packet_->stream_index == stream_->index) avcodec_send_packet(decoder_, packet_.get());
        av_packet_unref(packet_.get());
    }
}

FrameReader::FrameReader() : impl_(new Impl) {}
FrameReader::~FrameReader() = default;

bool FrameReader::open(const std::string &url, const OpenOptions &options, std::string *error) {
    return impl_->open(url, options, error);
}
void FrameReader::close() { impl_->close(); }
bool FrameReader::isOpen() const { return impl_->format_ != nullptr; }
const MediaInfo &FrameReader::info() const { return impl_->info_; }
bool FrameReader::seek(double seconds, SeekMode mode) { return impl_->seek(seconds, mode); }
bool FrameReader::read(VideoFrame &frame) { return impl_->read(frame); }

}  // namespace viplyr
