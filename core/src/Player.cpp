#include "viplyr/Player.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "AudioTempoFilter.h"
#include "BoundedQueue.h"
#include "Clock.h"
#include "CodecUtil.h"
#include "Deinterlacer.h"

namespace viplyr {

namespace {

using std::chrono::milliseconds;

constexpr double kMinRate = 0.5;
constexpr double kMaxRate = 4.0;
// A frame this late is dropped when a newer one is already waiting.
constexpr double kLateFrameSeconds = 0.08;
// Frames ending within this margin of a seek target count as "before" it.
constexpr double kSeekToleranceSeconds = 0.0005;

constexpr size_t kVideoPacketCapacity = 128;
constexpr size_t kAudioPacketCapacity = 256;
constexpr size_t kVideoFrameCapacity = 4;

struct PacketItem {
    enum class Kind {
        Data,
        Flush,  // a seek happened: reset the decoder, then discard up to seekTarget
        Drain,  // end of file: emit the frames still inside the decoder
    };
    Kind kind = Kind::Data;
    PacketPtr packet;
    int serial = 0;
    double seekTarget = kNoDiscard;
};

struct FrameItem {
    VideoFrame frame;
    int serial = 0;
};

void sleepSeconds(double seconds) {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

}  // namespace

// Pipeline:
//   demux thread -> packet queues -> decode threads -> (video) frame queue -> present thread -> sinks
//                                                   -> (audio) tempo filter -> audio sink
//
// Every seek starts a new "serial". Packets and frames carry the serial they were
// produced under, so anything left over from before a seek is recognised and dropped.
class Player::Impl {
public:
    explicit Impl(Player *owner) : owner_(owner) {}
    ~Impl() { close(); }

    bool open(const std::string &url, const OpenOptions &options, std::string *error);
    void close();
    void play();
    void pause();
    void seek(double seconds);
    void stepFrame(int direction);
    void setRate(double rate);
    double position() const;

    Player *owner_;
    std::vector<IVideoSink *> videoSinks_;
    IAudioSink *audioSink_ = nullptr;
    MediaInfo info_;
    std::atomic<State> state_{State::Stopped};
    std::atomic<double> rate_{1.0};

private:
    void demuxLoop();
    void performSeek();
    void pushControl(PacketItem::Kind kind, int serial, double seekTarget);

    void videoDecodeLoop();
    void receiveVideoFrames(int serial, double discardBefore, bool endOfStream);
    void queueVideoFrame(AVFrame *frame, int serial, double discardBefore, double *nextPts);

    void audioDecodeLoop();
    void receiveAudioFrames(int serial, double discardBefore);
    void playAudioFrame(AVFrame *frame, int serial, double discardBefore);
    void writeSamples(const float *samples, size_t frameCount, int serial);

    void presentLoop();
    void finishIfEnded();

    void setState(State state);
    void reportError(const std::string &message);
    bool hasVideo() const { return videoStream_ != nullptr; }
    bool hasAudio() const { return audioStream_ != nullptr; }

    AVFormatContext *format_ = nullptr;
    AVCodecContext *videoDecoder_ = nullptr;
    AVCodecContext *audioDecoder_ = nullptr;
    const AVStream *videoStream_ = nullptr;
    const AVStream *audioStream_ = nullptr;
    bool keepHwFrames_ = false;
    bool deinterlace_ = false;
    double startOffset_ = 0.0;

    BoundedQueue<PacketItem> videoPackets_{kVideoPacketCapacity};
    BoundedQueue<PacketItem> audioPackets_{kAudioPacketCapacity};
    BoundedQueue<FrameItem> videoFrames_{kVideoFrameCapacity};

    Clock clock_;
    AudioTempoFilter tempoFilter_;
    Deinterlacer deinterlacer_;
    AudioFormat audioFormat_;

    std::thread demuxThread_;
    std::thread videoThread_;
    std::thread audioThread_;
    std::thread presentThread_;

    std::atomic<bool> abort_{false};
    std::atomic<bool> paused_{true};
    std::atomic<int> serial_{0};

    std::atomic<bool> seekPending_{false};
    mutable std::mutex seekMutex_;
    double seekTarget_ = 0.0;  // guarded by seekMutex_

    // A stream has finished when its decoder emptied itself under the current serial.
    std::atomic<int> videoFinishedSerial_{0};
    std::atomic<int> audioFinishedSerial_{0};
    std::atomic<int> endedSerial_{0};

    std::atomic<int> stepRequests_{0};
    std::atomic<bool> steppedSinceSeek_{false};
    std::atomic<double> lastVideoPts_{0.0};
    std::atomic<double> lastVideoDuration_{0.04};
};

// ---------------------------------------------------------------- lifecycle

bool Player::Impl::open(const std::string &url, const OpenOptions &options, std::string *error) {
    close();

    format_ = openInput(url, options, error);
    if (!format_) return false;

    const int videoIndex = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audioIndex = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, options.audioStreamIndex,
                                               videoIndex, nullptr, 0);

    std::string hardwareName;
    if (videoIndex >= 0) {
        videoStream_ = format_->streams[videoIndex];
        videoDecoder_ = openDecoder(videoStream_, effectiveHwAccel(options), &hardwareName, error);
        if (!videoDecoder_) {
            close();
            return false;
        }
        deinterlace_ = isInterlaced(videoStream_);
    }
    // Without an audio sink the audio stream is ignored and the clock free-runs.
    if (audioIndex >= 0 && audioSink_) {
        audioDecoder_ = openDecoder(format_->streams[audioIndex], HwAccel::None, nullptr, nullptr);
        if (audioDecoder_) {
            audioStream_ = format_->streams[audioIndex];
            audioFormat_ = audioSink_->format();
        }
    }
    if (!hasVideo() && !hasAudio()) {
        if (error) *error = "No playable stream in " + url;
        close();
        return false;
    }

    keepHwFrames_ = options.keepHwFrames;
    startOffset_ = startOffsetSeconds(format_, videoStream_);
    info_ = describeMedia(format_, videoStream_, audioStream_, hardwareName);

    abort_ = false;
    paused_ = true;
    seekPending_ = false;
    stepRequests_ = 0;
    steppedSinceSeek_ = false;
    lastVideoPts_ = 0.0;
    if (hasVideo()) lastVideoDuration_ = frameDurationSeconds(videoStream_);
    videoPackets_.reset();
    audioPackets_.reset();
    videoFrames_.reset();

    clock_.setPaused(true);
    clock_.setRate(rate_);
    clock_.set(0.0);
    if (hasAudio()) audioSink_->setPaused(true);

    const int serial = ++serial_;
    pushControl(PacketItem::Kind::Flush, serial, kNoDiscard);

    demuxThread_ = std::thread([this] { demuxLoop(); });
    if (hasVideo()) videoThread_ = std::thread([this] { videoDecodeLoop(); });
    if (hasAudio()) audioThread_ = std::thread([this] { audioDecodeLoop(); });
    presentThread_ = std::thread([this] { presentLoop(); });

    setState(State::Paused);
    return true;
}

void Player::Impl::close() {
    abort_ = true;
    videoPackets_.abort();
    audioPackets_.abort();
    videoFrames_.abort();
    for (std::thread *thread : {&demuxThread_, &videoThread_, &audioThread_, &presentThread_}) {
        if (thread->joinable()) thread->join();
    }

    if (hasAudio()) audioSink_->flush();
    tempoFilter_.reset();
    deinterlacer_.reset();
    avcodec_free_context(&videoDecoder_);
    avcodec_free_context(&audioDecoder_);
    avformat_close_input(&format_);
    videoStream_ = nullptr;
    audioStream_ = nullptr;
    info_ = MediaInfo();
    setState(State::Stopped);
}

// ----------------------------------------------------------------- controls

void Player::Impl::play() {
    const State current = state_;
    if (current == State::Stopped || current == State::Playing) return;

    if (current == State::Ended) {
        seek(0.0);
    } else if (steppedSinceSeek_) {
        // Frame stepping moved the picture without the audio; realign both.
        seek(lastVideoPts_);
    }
    paused_ = false;
    clock_.setPaused(false);
    if (hasAudio()) audioSink_->setPaused(false);
    setState(State::Playing);
}

void Player::Impl::pause() {
    if (state_ != State::Playing) return;
    paused_ = true;
    clock_.setPaused(true);
    if (hasAudio()) audioSink_->setPaused(true);
    setState(State::Paused);
}

void Player::Impl::seek(double seconds) {
    if (state_ == State::Stopped) return;
    const double upperBound = info_.duration > 0.0 ? info_.duration : seconds;
    {
        std::lock_guard<std::mutex> lock(seekMutex_);
        seekTarget_ = std::max(0.0, std::min(seconds, upperBound));
    }
    seekPending_ = true;
    if (state_ == State::Ended) setState(State::Paused);
}

void Player::Impl::stepFrame(int direction) {
    if (!hasVideo() || direction == 0) return;
    pause();
    if (state_ != State::Paused) return;

    if (direction > 0) {
        ++stepRequests_;
    } else {
        // Aim at the middle of the previous frame so rounding cannot miss it.
        seek(lastVideoPts_ - 0.5 * lastVideoDuration_);
    }
}

void Player::Impl::setRate(double rate) {
    rate_ = std::max(kMinRate, std::min(rate, kMaxRate));
    clock_.setRate(rate_);
}

double Player::Impl::position() const {
    if (state_ == State::Stopped) return 0.0;
    if (seekPending_) {
        std::lock_guard<std::mutex> lock(seekMutex_);
        return seekTarget_;
    }
    const double now = std::max(0.0, clock_.now());
    return info_.duration > 0.0 ? std::min(now, info_.duration) : now;
}

void Player::Impl::setState(State state) {
    if (state_.exchange(state) == state) return;
    if (owner_->onStateChanged) owner_->onStateChanged(state);
}

void Player::Impl::reportError(const std::string &message) {
    if (owner_->onError) owner_->onError(message);
}

// -------------------------------------------------------------------- demux

void Player::Impl::pushControl(PacketItem::Kind kind, int serial, double seekTarget) {
    for (BoundedQueue<PacketItem> *queue : {&videoPackets_, &audioPackets_}) {
        PacketItem item;
        item.kind = kind;
        item.serial = serial;
        item.seekTarget = seekTarget;
        queue->pushAlways(std::move(item));
    }
}

void Player::Impl::performSeek() {
    double target;
    {
        std::lock_guard<std::mutex> lock(seekMutex_);
        target = seekTarget_;
    }
    // Jump to the keyframe at or before the target; the decode threads then
    // discard everything up to the target, which makes the seek frame-accurate.
    const int64_t timestamp = static_cast<int64_t>((target + startOffset_) * AV_TIME_BASE);
    const int result = av_seek_frame(format_, -1, timestamp, AVSEEK_FLAG_BACKWARD);
    if (result < 0) reportError("Seek failed: " + errorText(result));

    videoPackets_.clear();
    audioPackets_.clear();
    videoFrames_.clear();
    const int serial = ++serial_;
    pushControl(PacketItem::Kind::Flush, serial, target);

    clock_.set(target);
    steppedSinceSeek_ = false;
}

void Player::Impl::demuxLoop() {
    PacketPtr packet(av_packet_alloc());
    bool endOfFile = false;

    while (!abort_) {
        if (seekPending_.exchange(false)) {
            performSeek();
            endOfFile = false;
            continue;
        }
        if (endOfFile) {
            sleepSeconds(0.01);
            continue;
        }

        const int result = av_read_frame(format_, packet.get());
        if (result < 0) {
            if (result != AVERROR_EOF) reportError("Read failed: " + errorText(result));
            pushControl(PacketItem::Kind::Drain, serial_, kNoDiscard);
            endOfFile = true;
            continue;
        }

        BoundedQueue<PacketItem> *queue = nullptr;
        if (hasVideo() && packet->stream_index == videoStream_->index) queue = &videoPackets_;
        else if (hasAudio() && packet->stream_index == audioStream_->index) queue = &audioPackets_;
        if (!queue) {
            av_packet_unref(packet.get());
            continue;
        }

        PacketItem item;
        item.packet.reset(av_packet_alloc());
        av_packet_move_ref(item.packet.get(), packet.get());
        item.serial = serial_;
        // Wait for space, but stay responsive to close() and to a new seek.
        while (!abort_ && !seekPending_ && !queue->push(item, milliseconds(10))) {}
    }
}

// ------------------------------------------------------------- video decode

void Player::Impl::videoDecodeLoop() {
    int serial = 0;
    double discardBefore = kNoDiscard;

    while (!abort_) {
        PacketItem item;
        if (!videoPackets_.pop(item, milliseconds(20))) continue;

        if (item.kind == PacketItem::Kind::Flush) {
            avcodec_flush_buffers(videoDecoder_);
            deinterlacer_.reset();
            serial = item.serial;
            discardBefore = item.seekTarget;
            continue;
        }
        if (item.serial != serial_) continue;  // left over from before a seek

        if (item.kind == PacketItem::Kind::Drain) {
            avcodec_send_packet(videoDecoder_, nullptr);
            receiveVideoFrames(serial, discardBefore, true);
            videoFinishedSerial_ = serial;
            continue;
        }
        if (avcodec_send_packet(videoDecoder_, item.packet.get()) < 0) continue;  // skip corrupt data
        receiveVideoFrames(serial, discardBefore, false);
    }
}

void Player::Impl::receiveVideoFrames(int serial, double discardBefore, bool endOfStream) {
    FramePtr decoded(av_frame_alloc());
    FramePtr deinterlaced(av_frame_alloc());
    double nextPts = lastVideoPts_;

    const auto queueDeinterlaced = [&] {
        while (!abort_ && deinterlacer_.pull(deinterlaced.get())) {
            queueVideoFrame(deinterlaced.get(), serial, discardBefore, &nextPts);
        }
    };

    while (!abort_ && avcodec_receive_frame(videoDecoder_, decoded.get()) == 0) {
        if (!deinterlace_) {
            queueVideoFrame(decoded.get(), serial, discardBefore, &nextPts);
            continue;
        }
        deinterlacer_.push(decoded.get(), videoStream_->time_base);
        av_frame_unref(decoded.get());
        queueDeinterlaced();
    }
    if (deinterlace_ && endOfStream) {
        deinterlacer_.finish();  // release the frame the filter was still holding
        queueDeinterlaced();
    }
}

void Player::Impl::queueVideoFrame(AVFrame *frame, int serial, double discardBefore, double *nextPts) {
    FrameItem item;
    item.frame = makeVideoFrame(frame, videoStream_, startOffset_, keepHwFrames_, nextPts);
    item.serial = serial;
    if (!item.frame.isValid()) return;
    if (item.frame.pts() + item.frame.duration() <= discardBefore + kSeekToleranceSeconds) return;

    // Blocks while the frame queue is full; gives up when a seek makes the frame obsolete.
    while (!abort_ && serial == serial_ && !videoFrames_.push(item, milliseconds(10))) {}
}

// ------------------------------------------------------------- audio decode

void Player::Impl::audioDecodeLoop() {
    int serial = 0;
    double discardBefore = kNoDiscard;

    while (!abort_) {
        PacketItem item;
        if (!audioPackets_.pop(item, milliseconds(20))) continue;

        if (item.kind == PacketItem::Kind::Flush) {
            avcodec_flush_buffers(audioDecoder_);
            tempoFilter_.reset();
            audioSink_->flush();
            serial = item.serial;
            discardBefore = item.seekTarget;
            continue;
        }
        if (item.serial != serial_) continue;

        if (item.kind == PacketItem::Kind::Drain) {
            avcodec_send_packet(audioDecoder_, nullptr);
            receiveAudioFrames(serial, discardBefore);
            audioFinishedSerial_ = serial;
            continue;
        }
        if (avcodec_send_packet(audioDecoder_, item.packet.get()) < 0) continue;
        receiveAudioFrames(serial, discardBefore);
    }
}

void Player::Impl::receiveAudioFrames(int serial, double discardBefore) {
    FramePtr decoded(av_frame_alloc());
    while (!abort_ && serial == serial_ && avcodec_receive_frame(audioDecoder_, decoded.get()) == 0) {
        playAudioFrame(decoded.get(), serial, discardBefore);
        av_frame_unref(decoded.get());
    }
}

void Player::Impl::playAudioFrame(AVFrame *frame, int serial, double discardBefore) {
    if (frame->pts == AV_NOPTS_VALUE || frame->sample_rate <= 0) return;
    const double startTime = toSeconds(frame->pts, audioStream_->time_base) - startOffset_;
    const double endTime = startTime + static_cast<double>(frame->nb_samples) / frame->sample_rate;
    if (endTime <= discardBefore) return;

    const double tempo = rate_;
    if (!tempoFilter_.isConfiguredFor(frame, tempo) &&
        !tempoFilter_.configure(frame, audioFormat_, tempo)) {
        return;
    }
    if (!tempoFilter_.push(frame)) return;

    FramePtr filtered(av_frame_alloc());
    while (tempoFilter_.pull(filtered.get())) {
        writeSamples(reinterpret_cast<const float *>(filtered->data[0]),
                     static_cast<size_t>(filtered->nb_samples), serial);
        av_frame_unref(filtered.get());
    }

    // What the listener hears right now is the end of what we wrote minus what is
    // still buffered. That position drives the clock, and so the video.
    if (!abort_ && serial == serial_) {
        clock_.set(endTime - audioSink_->bufferedSeconds() * tempo);
    }
}

void Player::Impl::writeSamples(const float *samples, size_t frameCount, int serial) {
    while (frameCount > 0 && !abort_ && serial == serial_) {
        if (paused_) {
            sleepSeconds(0.01);
            continue;
        }
        const size_t written = audioSink_->write(samples, frameCount);
        if (written == 0) {
            sleepSeconds(0.005);  // the sink is full; it drains in real time
            continue;
        }
        samples += written * static_cast<size_t>(audioFormat_.channels);
        frameCount -= written;
    }
}

// ------------------------------------------------------------------ present

void Player::Impl::presentLoop() {
    std::optional<FrameItem> pending;
    int firstFrameShownSerial = 0;

    while (!abort_) {
        if (!pending) {
            FrameItem item;
            if (!videoFrames_.pop(item, milliseconds(10))) {
                finishIfEnded();
                continue;
            }
            pending = std::move(item);
        }

        const int serial = serial_;
        if (pending->serial != serial) {
            pending.reset();
            continue;
        }

        const double pts = pending->frame.pts();
        // The first frame after open() or a seek is shown at once, even when paused.
        const bool isFirstFrame = firstFrameShownSerial != serial;
        bool isStep = false;

        if (!isFirstFrame) {
            if (paused_) {
                if (stepRequests_ <= 0) {
                    sleepSeconds(0.005);
                    continue;
                }
                isStep = true;
            } else {
                const double delay = (pts - clock_.now()) / rate_;
                if (delay > 0.001) {
                    sleepSeconds(std::min(delay, 0.01));
                    continue;
                }
                if (delay < -kLateFrameSeconds && videoFrames_.size() > 0) {
                    pending.reset();  // too late and a newer frame is ready: drop it
                    continue;
                }
            }
        }

        for (IVideoSink *sink : videoSinks_) sink->present(pending->frame);
        lastVideoPts_ = pts;
        lastVideoDuration_ = pending->frame.duration();

        if (isFirstFrame) {
            firstFrameShownSerial = serial;
            // With no audio to drive it, the clock restarts from the frame actually shown.
            if (!hasAudio()) clock_.set(pts);
        }
        if (isStep) {
            --stepRequests_;
            clock_.set(pts);
            steppedSinceSeek_ = true;
        }
        pending.reset();
    }
}

void Player::Impl::finishIfEnded() {
    const int serial = serial_;
    if (endedSerial_ == serial || seekPending_) return;
    if (hasVideo() && videoFinishedSerial_ != serial) return;
    if (hasAudio() && audioFinishedSerial_ != serial) return;
    if (videoFrames_.size() > 0) return;
    // Let the audio that is already queued in the sink play out.
    if (hasAudio() && !paused_ && audioSink_->bufferedSeconds() > 0.05) return;

    endedSerial_ = serial;
    paused_ = true;
    clock_.setPaused(true);
    if (hasAudio()) audioSink_->setPaused(true);
    setState(State::Ended);
    if (owner_->onEnded) owner_->onEnded();
}

// ------------------------------------------------------------ public facade

Player::Player() : impl_(new Impl(this)) {}
Player::~Player() = default;

void Player::addVideoSink(IVideoSink *sink) { impl_->videoSinks_.push_back(sink); }
void Player::setAudioSink(IAudioSink *sink) { impl_->audioSink_ = sink; }

bool Player::open(const std::string &url, const OpenOptions &options, std::string *error) {
    return impl_->open(url, options, error);
}
void Player::close() { impl_->close(); }
void Player::play() { impl_->play(); }
void Player::pause() { impl_->pause(); }
void Player::seek(double seconds) { impl_->seek(seconds); }
void Player::stepFrame(int direction) { impl_->stepFrame(direction); }
void Player::setRate(double rate) { impl_->setRate(rate); }

double Player::position() const { return impl_->position(); }
double Player::duration() const { return impl_->info_.duration; }
double Player::rate() const { return impl_->rate_; }
State Player::state() const { return impl_->state_; }
const MediaInfo &Player::info() const { return impl_->info_; }

}  // namespace viplyr
