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

    std::mutex mutex;
    std::vector<Entry> entries;
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
        const std::vector<SubtitleCue> cues = readSubtitleTrack(clip, info.subtitleTracks[0].streamIndex);
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
}

}  // namespace

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
    testPlayer(clip, true);
    testPlayer(clip, false);
    testTracksAndFormats(argv[2]);

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
