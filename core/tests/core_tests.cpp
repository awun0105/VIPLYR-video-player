// Headless tests for viplyr_core. Usage: core_tests <test_clip.mp4>
// The clip is 3 s at 30 fps (90 frames) with one keyframe per second and a sine tone.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "viplyr/FrameReader.h"
#include "viplyr/Player.h"
#include "viplyr/Playlist.h"
#include "viplyr/SubtitleReader.h"
#include "viplyr/VideoRecorder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

using namespace viplyr;

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            ++failures;                                                         \
            std::printf("  FAILED %s:%d  %s\n", __FILE__, __LINE__, #condition); \
        }                                                                       \
    } while (0)

const double kFrame = 1.0 / 30.0;

double wallNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void sleepSeconds(double seconds) {
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

struct RecordingVideoSink : IVideoSink {
    struct Entry {
        double pts;
        double wallTime;
    };

    void present(const VideoFrame &frame) override {
        std::lock_guard<std::mutex> lock(mutex);
        entries.push_back({frame.pts(), wallNow()});
    }
    std::vector<Entry> snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return entries;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        entries.clear();
    }
    double lastPts() {
        std::lock_guard<std::mutex> lock(mutex);
        return entries.empty() ? -1.0 : entries.back().pts;
    }
    int count() {
        std::lock_guard<std::mutex> lock(mutex);
        return static_cast<int>(entries.size());
    }

    std::mutex mutex;
    std::vector<Entry> entries;
};

// Accepts everything at once and measures the level of each channel.
struct ChannelEnergySink : IAudioSink {
    explicit ChannelEnergySink(int channelCount) : sumOfSquares(static_cast<size_t>(channelCount), 0.0) {
        audioFormat.sampleRate = 44100;
        audioFormat.channels = channelCount;
    }
    AudioFormat format() const override { return audioFormat; }
    size_t write(const float *samples, size_t frameCount) override {
        std::lock_guard<std::mutex> lock(mutex);
        for (size_t i = 0; i < frameCount; ++i) {
            for (size_t c = 0; c < sumOfSquares.size(); ++c) {
                const double value = samples[i * sumOfSquares.size() + c];
                sumOfSquares[c] += value * value;
            }
        }
        frames += frameCount;
        return frameCount;
    }
    double bufferedSeconds() const override { return 0.1; }
    void flush() override {}
    void setPaused(bool) override {}
    std::vector<double> rms() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<double> result;
        for (double sum : sumOfSquares) result.push_back(frames ? std::sqrt(sum / frames) * std::sqrt(2.0) : 0.0);
        return result;  // 1.0 for a full-scale sine
    }

    AudioFormat audioFormat;
    std::mutex mutex;
    std::vector<double> sumOfSquares;
    size_t frames = 0;
};

// Behaves like a sound card: holds at most 0.2 s and plays it out in real time.
struct RealTimeAudioSink : IAudioSink {
    AudioFormat format() const override { return audioFormat; }

    size_t write(const float *, size_t frameCount) override {
        std::lock_guard<std::mutex> lock(mutex);
        drainLocked();
        const double space = 0.2 - buffered;
        if (space <= 0.0) return 0;
        const size_t accepted = std::min(frameCount, static_cast<size_t>(space * audioFormat.sampleRate));
        buffered += static_cast<double>(accepted) / audioFormat.sampleRate;
        framesWritten += accepted;
        return accepted;
    }
    double bufferedSeconds() const override {
        std::lock_guard<std::mutex> lock(mutex);
        const_cast<RealTimeAudioSink *>(this)->drainLocked();
        return buffered;
    }
    void flush() override {
        std::lock_guard<std::mutex> lock(mutex);
        buffered = 0.0;
    }
    void setPaused(bool value) override {
        std::lock_guard<std::mutex> lock(mutex);
        drainLocked();
        paused = value;
    }
    void drainLocked() {
        const double now = wallNow();
        if (!paused) buffered = std::max(0.0, buffered - (now - lastDrain));
        lastDrain = now;
    }

    AudioFormat audioFormat;
    mutable std::mutex mutex;
    double buffered = 0.0;
    double lastDrain = wallNow();
    bool paused = true;
    size_t framesWritten = 0;
};

// Waits until `predicate` holds or `timeout` seconds pass.
template <typename Predicate>
bool waitFor(Predicate predicate, double timeout = 3.0) {
    const double deadline = wallNow() + timeout;
    while (wallNow() < deadline) {
        if (predicate()) return true;
        sleepSeconds(0.005);
    }
    return predicate();
}

void testPlaylist() {
    std::printf("playlist\n");
    Playlist playlist;
    CHECK(playlist.currentIndex() == -1);
    playlist.assign({"a", "b", "c"});
    CHECK(playlist.currentIndex() == 0);
    CHECK(!playlist.previous());            // stays on the first item
    CHECK(playlist.currentIndex() == 0);
    CHECK(playlist.next() && playlist.next());
    CHECK(!playlist.next());                // stays on the last item
    CHECK(playlist.currentIndex() == 2);

    CHECK(!playlist.removeAt(0));           // removing an earlier item shifts the index
    CHECK(playlist.currentIndex() == 1 && playlist.at(1) == "c");
    CHECK(playlist.removeAt(1));            // removing the current (last) item moves back
    CHECK(playlist.currentIndex() == 0 && playlist.at(0) == "b");
    CHECK(playlist.removeAt(0));
    CHECK(playlist.currentIndex() == -1 && playlist.count() == 0);

    playlist.append({"x"});
    CHECK(playlist.currentIndex() == 0 && playlist.contains("x") && !playlist.contains("y"));

    // Moving items keeps the current item current.
    playlist.assign({"a", "b", "c", "d"});
    playlist.setCurrentIndex(1);                                   // "b"
    CHECK(playlist.move(0, 3) && playlist.at(playlist.currentIndex()) == "b");
    CHECK(playlist.move(3, 0) && playlist.at(playlist.currentIndex()) == "b");
    CHECK(playlist.move(1, 2) && playlist.currentIndex() == 2 && playlist.at(2) == "b");
    CHECK(!playlist.move(0, 9));
}

void testFrameReader(const std::string &clip, HwAccel hwAccel, const char *label) {
    std::printf("frame reader (%s)\n", label);
    FrameReader reader;
    OpenOptions options;
    options.hwAccel = hwAccel;
    std::string error;
    CHECK(reader.open(clip, options, &error));
    if (!reader.isOpen()) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    std::printf("  decoder: %s\n", reader.info().hardwareDecoder.empty() ? "cpu" : reader.info().hardwareDecoder.c_str());
    CHECK(reader.info().width == 320 && reader.info().height == 240);
    CHECK(near(reader.info().duration, 3.0, 0.1));
    CHECK(near(reader.info().frameRate, 30.0, 0.01));

    // Every frame, in order.
    int count = 0;
    double previousPts = -1.0;
    bool increasing = true;
    VideoFrame frame;
    while (reader.read(frame)) {
        if (count == 0) CHECK(near(frame.pts(), 0.0, 0.001));
        if (frame.pts() <= previousPts) increasing = false;
        previousPts = frame.pts();
        ++count;
    }
    CHECK(count == 90);
    CHECK(increasing);

    // Accurate seek into the middle of frame 45 returns frame 45.
    CHECK(reader.seek(45.5 * kFrame, SeekMode::Accurate));
    CHECK(reader.read(frame));
    CHECK(near(frame.pts(), 45 * kFrame, 0.001));
    CHECK(reader.read(frame));
    CHECK(near(frame.pts(), 46 * kFrame, 0.001));

    // Keyframe seek returns the keyframe before the target (one per second).
    CHECK(reader.seek(1.7, SeekMode::Keyframe));
    CHECK(reader.read(frame));
    CHECK(near(frame.pts(), 1.0, 0.001));

    // Conversion for CV use.
    const VideoFrame bgr = frame.convert(PixelFormat::Bgr24);
    CHECK(bgr.isValid() && bgr.format() == PixelFormat::Bgr24);
    CHECK(bgr.width() == 320 && bgr.height() == 240 && bgr.stride(0) >= 320 * 3);
    const VideoFrame small = frame.convert(PixelFormat::Rgba, 160, 120);
    CHECK(small.width() == 160 && small.height() == 120);
}

void testPlayer(const std::string &clip, bool withAudio) {
    std::printf("player (%s)\n", withAudio ? "audio clock" : "free-running clock");
    RecordingVideoSink video;
    RealTimeAudioSink audio;
    std::atomic<int> endedCount{0};

    Player player;
    player.addVideoSink(&video);
    if (withAudio) player.setAudioSink(&audio);
    player.onEnded = [&endedCount] { ++endedCount; };

    std::string error;
    CHECK(player.open(clip, {}, &error));
    if (player.state() == State::Stopped) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    CHECK(player.state() == State::Paused);
    CHECK(player.info().hasAudio == withAudio);

    // Opening shows the first frame and nothing else while paused.
    CHECK(waitFor([&] { return video.lastPts() >= 0.0; }));
    sleepSeconds(0.15);
    CHECK(video.snapshot().size() == 1);
    CHECK(near(video.lastPts(), 0.0, 0.001));

    // Real-time playback: about 30 frames per second, in step with the wall clock.
    video.clear();
    player.play();
    sleepSeconds(1.0);
    player.pause();
    auto played = video.snapshot();
    std::printf("  1 s of playback presented %zu frames\n", played.size());
    CHECK(played.size() >= 25 && played.size() <= 34);
    if (played.size() > 10) {
        const double mediaSpan = played.back().pts - played[5].pts;
        const double wallSpan = played.back().wallTime - played[5].wallTime;
        std::printf("  drift over the run: %.1f ms\n", (mediaSpan - wallSpan) * 1000.0);
        CHECK(near(mediaSpan, wallSpan, 0.05));
    }
    CHECK(near(player.position(), 1.0, 0.25));

    // A paused, frame-accurate seek shows exactly the frame at the target.
    video.clear();
    player.seek(60.5 * kFrame);
    CHECK(waitFor([&] { return video.lastPts() >= 0.0; }));
    CHECK(near(video.lastPts(), 60 * kFrame, 0.001));
    CHECK(near(player.position(), 60.5 * kFrame, 0.02));

    // Several quick seeks: only the last one matters.
    for (double target : {0.5, 2.5, 1.2}) player.seek(target);
    CHECK(waitFor([&] { return near(video.lastPts(), 36 * kFrame, 0.001); }));

    // Frame stepping.
    player.stepFrame(1);
    CHECK(waitFor([&] { return near(video.lastPts(), 37 * kFrame, 0.001); }));
    player.stepFrame(1);
    CHECK(waitFor([&] { return near(video.lastPts(), 38 * kFrame, 0.001); }));
    player.stepFrame(-1);
    CHECK(waitFor([&] { return near(video.lastPts(), 37 * kFrame, 0.001); }));
    CHECK(player.state() == State::Paused);
    // Quick presses add up even while the previous step back is still seeking.
    player.stepFrame(-1);
    player.stepFrame(-1);
    player.stepFrame(-1);
    CHECK(waitFor([&] { return near(video.lastPts(), 34 * kFrame, 0.001); }));

    // Double speed: one wall second covers about two media seconds.
    player.seek(0.0);
    CHECK(waitFor([&] { return near(video.lastPts(), 0.0, 0.001); }));
    player.setRate(2.0);
    player.play();
    sleepSeconds(1.0);
    const double positionAtDoubleSpeed = player.position();
    std::printf("  position after 1 s at 2x: %.2f s\n", positionAtDoubleSpeed);
    CHECK(near(positionAtDoubleSpeed, 2.0, 0.3));

    // Playing on reaches the end exactly once.
    CHECK(waitFor([&] { return endedCount == 1; }, 4.0));
    CHECK(player.state() == State::Ended);
    CHECK(near(video.lastPts(), 89 * kFrame, 0.001));
    sleepSeconds(0.1);
    CHECK(endedCount == 1);

    // Play after the end restarts from the beginning.
    player.setRate(1.0);
    video.clear();
    player.play();
    CHECK(waitFor([&] { return video.snapshot().size() > 3; }));
    CHECK(player.state() == State::Playing);
    CHECK(video.snapshot().front().pts < 0.01);

    player.close();
    CHECK(player.state() == State::Stopped);
}

// The second clip: interlaced 360x288 shown as 16:9, two audio tracks, one subtitle track.
void testTracksAndFormats(const std::string &clip) {
    std::printf("tracks, interlacing and pixel aspect\n");
    FrameReader reader;
    std::string error;
    CHECK(reader.open(clip, {}, &error));
    if (!reader.isOpen()) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    const MediaInfo info = reader.info();
    CHECK(info.isInterlaced);
    CHECK(info.hardwareDecoder.empty());                 // interlaced video is decoded on the CPU
    CHECK(near(info.pixelAspectRatio * 360.0 / 288.0, 16.0 / 9.0, 0.01));
    CHECK(info.audioTracks.size() == 2);
    CHECK(info.subtitleTracks.size() == 1);
    if (info.audioTracks.size() == 2) {
        CHECK(info.audioTracks[0].language == "eng" && info.audioTracks[1].language == "vie");
    }

    // Deinterlacing keeps every frame and its timestamp (2 s at 25 fps).
    int count = 0;
    double previousPts = -1.0;
    bool increasing = true;
    VideoFrame frame;
    while (reader.read(frame)) {
        if (frame.pts() <= previousPts) increasing = false;
        previousPts = frame.pts();
        ++count;
    }
    CHECK(count == 50);
    CHECK(increasing);
    CHECK(near(previousPts, 49 / 25.0, 0.001));
    CHECK(reader.seek(1.0) && reader.read(frame) && near(frame.pts(), 1.0, 0.001));

    // Embedded subtitles.
    if (info.subtitleTracks.size() == 1) {
        CHECK(info.subtitleTracks[0].isText);
        const SubtitleTrackData track = readSubtitleTrack(clip, info.subtitleTracks[0].streamIndex);
        const std::vector<SubtitleCue> &cues = track.cues;
        CHECK(!track.isAss && !track.isBitmap);
        CHECK(cues.size() == 2);
        if (cues.size() == 2) {
            CHECK(near(cues[0].start, 0.2, 0.01) && near(cues[0].end, 0.9, 0.01));
            CHECK(cues[0].text == "First line");
            CHECK(cues[1].text == "Second\nline");
        }
    }

    // Playing the second audio track.
    RecordingVideoSink video;
    RealTimeAudioSink audio;
    Player player;
    player.addVideoSink(&video);
    player.setAudioSink(&audio);
    OpenOptions options;
    if (info.audioTracks.size() == 2) options.audioStreamIndex = info.audioTracks[1].streamIndex;
    CHECK(player.open(clip, options, &error));
    CHECK(player.info().audioStreamIndex == options.audioStreamIndex);
    player.play();
    sleepSeconds(0.5);
    CHECK(audio.framesWritten > 0);
    CHECK(video.snapshot().size() >= 10);  // the interlaced path delivers frames in real time
    player.close();

    // Chapters, on the frames' time line.
    CHECK(info.chapters.size() == 2);
    if (info.chapters.size() == 2) {
        CHECK(near(info.chapters[0].start, 0.0, 0.01) && near(info.chapters[0].end, 1.0, 0.01));
        CHECK(info.chapters[0].title == "Opening" && info.chapters[1].title == "Ending");
        CHECK(near(info.chapters[1].start, 1.0, 0.01));
    }

    // The 5.1 track keeps its channels on a 5.1 output and is mixed down on a stereo one.
    if (info.audioTracks.size() == 2) {
        for (int channels : {6, 2}) {
            ChannelEnergySink sink(channels);
            Player surround;
            surround.setAudioSink(&sink);
            OpenOptions surroundOptions;
            surroundOptions.audioStreamIndex = info.audioTracks[1].streamIndex;
            CHECK(surround.open(clip, surroundOptions, &error));
            surround.play();
            sleepSeconds(0.6);
            surround.close();
            const std::vector<double> rms = sink.rms();
            if (channels == 6) {
                std::printf("  5.1 output, channel levels: %.2f %.2f %.2f %.2f %.2f %.2f\n", rms[0], rms[1], rms[2],
                            rms[3], rms[4], rms[5]);
                CHECK(rms[0] > 0.5 && rms[2] > 0.25 && rms[1] < 0.01 && rms[3] < 0.01);  // FL and FC only
            } else {
                std::printf("  stereo output, channel levels: %.2f %.2f\n", rms[0], rms[1]);
                CHECK(rms[0] > rms[1] && rms[1] > 0.05);  // the centre reaches both sides
            }
        }
    }
}

}  // namespace

// Writes a subtitle-only Matroska file with two DVD (bitmap) subtitles:
// 0.5-1.5 s and 2.0-3.0 s, each a 200x40 picture at (100, 400) on a 720x480 canvas.
bool writeBitmapSubtitleFile(const std::string &path) {
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_DVD_SUBTITLE);
    AVFormatContext *output = nullptr;
    if (!codec || avformat_alloc_output_context2(&output, nullptr, "matroska", path.c_str()) < 0) return false;
    AVCodecContext *encoder = avcodec_alloc_context3(codec);
    encoder->width = 720;
    encoder->height = 480;
    encoder->time_base = {1, 1000};
    bool ok = avcodec_open2(encoder, codec, nullptr) >= 0;
    AVStream *stream = avformat_new_stream(output, nullptr);
    ok = ok && avcodec_parameters_from_context(stream->codecpar, encoder) >= 0;
    stream->time_base = {1, 1000};
    ok = ok && avio_open(&output->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0 && avformat_write_header(output, nullptr) >= 0;

    std::vector<uint8_t> indices(200 * 40);
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = (i / 200) < 4 ? 2 : 1;  // a border row and a fill
    uint32_t palette[4] = {0x00000000, 0xFFFFFFFF, 0xFF000000, 0xFF808080};
    for (int startMs : {500, 2000}) {
        if (!ok) break;
        AVSubtitleRect rect = {};
        AVSubtitleRect *rects[1] = {&rect};
        rect.type = SUBTITLE_BITMAP;
        rect.x = 100;
        rect.y = 400;
        rect.w = 200;
        rect.h = 40;
        rect.nb_colors = 4;
        rect.data[0] = indices.data();
        rect.linesize[0] = 200;
        rect.data[1] = reinterpret_cast<uint8_t *>(palette);
        AVSubtitle subtitle = {};
        subtitle.num_rects = 1;
        subtitle.rects = rects;
        subtitle.start_display_time = 0;
        subtitle.end_display_time = 1000;
        subtitle.pts = static_cast<int64_t>(startMs) * 1000;
        uint8_t buffer[65536];
        const int size = avcodec_encode_subtitle(encoder, buffer, sizeof(buffer), &subtitle);
        AVPacket *packet = av_packet_alloc();
        ok = size > 0 && av_new_packet(packet, size) >= 0;
        if (ok) {
            std::copy_n(buffer, size, packet->data);
            packet->pts = packet->dts = startMs;
            packet->duration = 1000;
            packet->stream_index = stream->index;
            ok = av_interleaved_write_frame(output, packet) >= 0;
        }
        av_packet_free(&packet);
    }
    if (ok) ok = av_write_trailer(output) >= 0;
    avcodec_free_context(&encoder);
    avio_closep(&output->pb);
    avformat_free_context(output);
    return ok;
}

void testStyledAndBitmapSubtitles(const std::string &assFile) {
    std::printf("styled and bitmap subtitles\n");
    const SubtitleTrackData styled = readSubtitleTrack(assFile, -1);
    CHECK(styled.isAss && !styled.isBitmap && styled.cues.size() == 2);
    CHECK(styled.assHeader.find("PlayResX: 640") != std::string::npos);
    CHECK(styled.assHeader.find("Style: Top") != std::string::npos);
    if (styled.cues.size() == 2) {
        CHECK(near(styled.cues[0].start, 0.5, 0.01) && near(styled.cues[0].end, 2.0, 0.01));
        CHECK(styled.cues[0].text == "Green styled line");                     // plain text still there
        CHECK(styled.cues[0].assEvent.find("{\\i1}styled") != std::string::npos);  // with its tags
        CHECK(styled.cues[1].assEvent.find(",Top,") != std::string::npos);
    }

    const std::string bitmapFile = "bitmap_subtitles_test.mkv";
    CHECK(writeBitmapSubtitleFile(bitmapFile));
    const SubtitleTrackData bitmap = readSubtitleTrack(bitmapFile, -1);
    CHECK(bitmap.isBitmap && !bitmap.isAss && bitmap.cues.size() == 2);
    if (bitmap.cues.size() == 2) {
        const SubtitleCue &first = bitmap.cues[0];
        // DVD subtitles store their duration in steps of 1024/90000 s.
        CHECK(near(first.start, 0.5, 0.01) && near(first.end, 1.5, 0.02) && near(bitmap.cues[1].start, 2.0, 0.01));
        CHECK(first.bitmaps.size() == 1 && first.text.empty());
        if (first.bitmaps.size() == 1) {
            const SubtitleBitmap &picture = first.bitmaps[0];
            std::printf("  bitmap %dx%d at (%d, %d) on %dx%d\n", picture.width, picture.height, picture.x, picture.y,
                        bitmap.canvasWidth, bitmap.canvasHeight);
            CHECK(picture.width == 200 && picture.height == 40 && picture.x == 100 && picture.y == 400);
            CHECK(picture.indices.size() == 200u * 40u && picture.palette.size() == 256);
            // The fill is opaque white, the top rows opaque black.
            const uint32_t fill = picture.palette[picture.indices[20 * 200 + 100]];
            const uint32_t border = picture.palette[picture.indices[100]];
            CHECK((fill >> 24) > 0xC0 && (fill & 0xFFFFFF) == 0xFFFFFF);
            CHECK((border >> 24) > 0xC0 && (border & 0xFFFFFF) == 0x000000);
        }
    }
    std::remove(bitmapFile.c_str());
}

// GPU frames handed on without copying: DMA-BUF planes from VAAPI, device
// planes from CUDA. Skipped for a decoder this machine does not have.
void testGpuFrameExport(const std::string &clip, HwAccel hwAccel, const char *label) {
    OpenOptions options;
    options.hwAccel = hwAccel;
    options.keepHwFrames = true;
    FrameReader reader;
    std::string error;
    VideoFrame frame;
    if (!reader.open(clip, options, &error) || !reader.read(frame) || !frame.isHardware()) {
        std::printf("  gpu export (%s): no such decoder, skipped\n", label);
        return;
    }
    if (hwAccel == HwAccel::Vaapi) {
        const DmaBufImage image = frame.exportDmaBuf();
        CHECK(image.isValid() && image.planeCount == 2 && image.format == PixelFormat::Nv12);
        CHECK(image.planes[0].fd >= 0 && image.planes[0].width == 320 && image.planes[1].height == 120);
        CHECK(!frame.cudaPlanes().isValid());
    } else {
        const CudaImage image = frame.cudaPlanes();
        CHECK(image.isValid() && image.format == PixelFormat::Nv12);
        CHECK(image.planes[0] != 0 && image.pitch[0] >= 320 && image.width[1] == 160);
        CHECK(!frame.exportDmaBuf().isValid());
    }
    CHECK(!frame.download().exportDmaBuf().isValid());  // CPU frames have nothing to share
    std::printf("  gpu export (%s): ok\n", label);
}

// Records every frame of the clip with each encoder, then reads the file back.
void testRecorder(const std::string &clip, const std::string &encoder) {
    const std::string path = "recorder_test_" + encoder + ".mp4";
    VideoRecorder recorder;
    RecorderOptions options;
    options.encoder = encoder;
    options.width = 160;
    options.height = 120;
    CHECK(recorder.start(path, options));

    FrameReader reader;
    CHECK(reader.open(clip));
    VideoFrame frame;
    while (reader.read(frame)) {
        recorder.present(frame);
        // Like a real-time source: do not outrun the encoder's queue.
        while (recorder.isRecording() && recorder.framesWritten() + 16 < static_cast<int64_t>(frame.pts() * 30)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    std::string error;
    const bool finished = recorder.stop(&error);
    if (!finished && encoder != "libx264") {  // optional encoders may be missing on this machine
        std::printf("  recorder (%s): %s, skipped\n", encoder.c_str(), error.c_str());
        return;
    }
    CHECK(finished);
    std::printf("  recorder (%s): %lld frames with %s, %lld dropped\n", encoder.c_str(),
                static_cast<long long>(recorder.framesWritten()), recorder.encoderName().c_str(),
                static_cast<long long>(recorder.droppedFrames()));
    CHECK(recorder.framesWritten() + recorder.droppedFrames() == 90);

    FrameReader check;
    CHECK(check.open(path));
    CHECK(check.info().width == 160 && check.info().height == 120);
    CHECK(near(check.info().duration, 3.0, 0.1));
    int count = 0;
    double lastPts = -1.0;
    bool increasing = true;
    while (check.read(frame)) {
        increasing = increasing && frame.pts() > lastPts;
        lastPts = frame.pts();
        ++count;
    }
    CHECK(count == recorder.framesWritten() && increasing);
    std::remove(path.c_str());
}

// A live device in low-latency mode: FFmpeg's lavfi test source, paced like a camera.
void testLiveSource() {
    std::printf("live source (lavfi device, low latency)\n");
    Player player;
    RecordingVideoSink video;
    player.addVideoSink(&video);
    OpenOptions options;
    options.inputFormat = "lavfi";
    options.lowLatency = true;
    std::string error;
    CHECK(player.open("testsrc2=size=320x240:rate=30,realtime", options, &error));
    if (!error.empty()) std::printf("  %s\n", error.c_str());
    CHECK(player.info().width == 320 && player.info().frameRate > 29.0);
    player.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const int frames = video.count();
    std::printf("  %d frames in 1 s\n", frames);
    CHECK(frames >= 25 && frames <= 33);
    // The position follows the newest frame shown.
    CHECK(std::fabs(player.position() - video.lastPts()) < 0.1);
    player.close();

    OpenOptions bad;
    bad.inputFormat = "no_such_device";
    CHECK(!player.open("x", bad, &error) && error.find("Unknown input format") != std::string::npos);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <test_clip.mp4> <test_tracks.mkv>\n", argv[0]);
        return 2;
    }
    const std::string clip = argv[1];

    testPlaylist();
    testFrameReader(clip, HwAccel::None, "cpu");
    testFrameReader(clip, HwAccel::Vaapi, "vaapi if available");
    testGpuFrameExport(clip, HwAccel::Vaapi, "vaapi");
    testGpuFrameExport(clip, HwAccel::Cuda, "cuda");
    std::printf("recorder\n");
    testRecorder(clip, "libx264");
    testRecorder(clip, "h264_nvenc");
    testLiveSource();
    if (argc > 3) testStyledAndBitmapSubtitles(argv[3]);
    testPlayer(clip, true);
    testPlayer(clip, false);
    testTracksAndFormats(argv[2]);

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
