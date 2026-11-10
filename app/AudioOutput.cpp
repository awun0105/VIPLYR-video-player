#include "AudioOutput.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>
#include <algorithm>
#include <cstring>

namespace {
constexpr double kRingSeconds = 0.2;     // how much audio the ring buffer holds
constexpr double kDeviceSeconds = 0.05;  // requested size of the device's own buffer
}

AudioOutput::AudioOutput(QObject *parent) : QIODevice(parent) {
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) return;

    const QAudioFormat preferred = device.preferredFormat();
    format_.sampleRate = preferred.sampleRate() > 0 ? preferred.sampleRate() : 48000;
    // As many channels as the device is set up for: 5.1 and 7.1 systems get every channel,
    // stereo devices get a downmix (made by the engine's audio filter). FFmpeg's default
    // 5.1/7.1 channel orders match Qt's, so only these counts are used.
    const int deviceChannels = preferred.channelCount();
    format_.channels = deviceChannels == 1 || deviceChannels == 6 || deviceChannels == 8 ? deviceChannels : 2;

    QAudioFormat audioFormat;
    audioFormat.setSampleRate(format_.sampleRate);
    audioFormat.setChannelCount(format_.channels);
    audioFormat.setChannelConfig(QAudioFormat::defaultChannelConfigForChannelCount(format_.channels));
    audioFormat.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(audioFormat)) return;

    ring_.resize(static_cast<size_t>(kRingSeconds * format_.sampleRate) * format_.channels);

    sink_.reset(new QAudioSink(device, audioFormat));
    sink_->setBufferSize(static_cast<qsizetype>(kDeviceSeconds * format_.sampleRate) *
                         audioFormat.bytesPerFrame());
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    sink_->start(this);
    deviceLatencySeconds_ = static_cast<double>(sink_->bufferSize()) /
                            (audioFormat.bytesPerFrame() * format_.sampleRate);
}

AudioOutput::~AudioOutput() {
    if (sink_) sink_->stop();
}

void AudioOutput::setVolume(qreal volume) {
    if (!sink_) return;
    sink_->setVolume(QAudio::convertVolume(volume, QAudio::LogarithmicVolumeScale,
                                           QAudio::LinearVolumeScale));
}

size_t AudioOutput::bufferedFrames() const {
    return sampleCount_ / static_cast<size_t>(format_.channels);
}

size_t AudioOutput::write(const float *samples, size_t frameCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t channels = static_cast<size_t>(format_.channels);
    const size_t freeSamples = ring_.size() - sampleCount_;
    const size_t count = std::min(frameCount, freeSamples / channels) * channels;

    size_t writeIndex = (readIndex_ + sampleCount_) % ring_.size();
    const size_t firstPart = std::min(count, ring_.size() - writeIndex);
    std::memcpy(&ring_[writeIndex], samples, firstPart * sizeof(float));
    std::memcpy(&ring_[0], samples + firstPart, (count - firstPart) * sizeof(float));
    sampleCount_ += count;
    return count / channels;
}

qint64 AudioOutput::readData(char *data, qint64 maxSize) {
    const size_t wanted = static_cast<size_t>(maxSize) / sizeof(float);
    float *output = reinterpret_cast<float *>(data);

    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!paused_) {
            count = std::min(wanted, sampleCount_);
            const size_t firstPart = std::min(count, ring_.size() - readIndex_);
            std::memcpy(output, &ring_[readIndex_], firstPart * sizeof(float));
            std::memcpy(output + firstPart, &ring_[0], (count - firstPart) * sizeof(float));
            readIndex_ = (readIndex_ + count) % ring_.size();
            sampleCount_ -= count;
        }
    }
    // Fill the rest with silence so the device keeps running while paused or starved.
    std::fill(output + count, output + wanted, 0.0f);
    return static_cast<qint64>(wanted * sizeof(float));
}

double AudioOutput::bufferedSeconds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<double>(bufferedFrames()) / format_.sampleRate + deviceLatencySeconds_;
}

void AudioOutput::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    readIndex_ = 0;
    sampleCount_ = 0;
}

void AudioOutput::setPaused(bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_ = paused;
}
