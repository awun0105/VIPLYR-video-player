// Needs a running audio server and an output device; the test stream is muted.
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "AudioOutput.h"
#include "viplyr/Player.h"

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAILED line %d: %s\n", __LINE__, #condition); ++failures; \
} } while (0)

void wait(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    AudioOutput audio;
    if (!audio.isAvailable()) {
        std::printf("No audio output device; skipped\n");
        return 77;
    }
    audio.setVolume(0);
    const auto format = audio.format();
    const size_t chunkFrames = 480;
    std::vector<float> samples(chunkFrames * format.channels);
    for (size_t i = 0; i < chunkFrames; ++i) {
        for (int c = 0; c < format.channels; ++c)
            samples[i * format.channels + c] = 0.1f * std::sin(i * 0.05);
    }
    std::atomic<bool> stop{false};
    std::atomic<size_t> produced{0};
    std::thread producer([&] {
        while (!stop) {
            produced += audio.write(samples.data(), chunkFrames);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    audio.setPaused(false);
    wait(500);  // let the device and ring buffer reach steady state
    for (int i = 0; i < 3; ++i) {
        const size_t before = produced;
        // Deliberately stop processing GUI events, longer than either buffer.
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        const size_t written = produced - before;
        std::printf("GUI blocked 600 ms: %.1f ms of audio accepted\n",
                    1000.0 * written / format.sampleRate);
        CHECK(written > static_cast<size_t>(format.sampleRate * 0.4));
        wait(100);
    }

    audio.setPaused(true);
    stop = true;
    producer.join();
    const double pausedBuffer = audio.bufferedSeconds();
    wait(150);
    CHECK(std::fabs(audio.bufferedSeconds() - pausedBuffer) < 1.0 / format.sampleRate);
    audio.flush();
    const double emptyBuffer = audio.bufferedSeconds();
    CHECK(emptyBuffer < pausedBuffer);
    CHECK(audio.write(samples.data(), chunkFrames) == chunkFrames);
    CHECK(std::fabs(audio.bufferedSeconds() - emptyBuffer -
                    static_cast<double>(chunkFrames) / format.sampleRate) < 1e-6);
    audio.setVolume(0.25);
    audio.setVolume(0);
    audio.setPaused(false);
    wait(150);
    CHECK(std::fabs(audio.bufferedSeconds() - emptyBuffer) < 1e-6);

    for (int i = 0; i < 3; ++i) {
        AudioOutput next;
        CHECK(next.isAvailable());
        next.setVolume(0);
        next.setPaused(false);
        next.write(samples.data(), chunkFrames);
    }  // destruction must stop/join its audio thread without GUI event processing

    if (argc > 1) {
        std::printf("player pause, seek, rate and end-of-stream\n");
        viplyr::Player player;
        player.setAudioSink(&audio);
        viplyr::OpenOptions options;
        options.hwAccel = viplyr::HwAccel::None;
        CHECK(player.open(argv[1], options));
        player.play();
        wait(200);
        player.pause();
        const double pausedAt = player.position();
        wait(100);
        CHECK(std::fabs(player.position() - pausedAt) < 0.03);
        player.seek(1.0);
        wait(100);
        player.setRate(1.5);
        player.play();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        while (player.state() != viplyr::State::Ended &&
               std::chrono::steady_clock::now() < deadline) wait(20);
        CHECK(player.state() == viplyr::State::Ended);
        player.close();
    }
    std::printf(failures ? "%d FAILURE(S)\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
