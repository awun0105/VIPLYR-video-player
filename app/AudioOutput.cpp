#include "AudioOutput.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <algorithm>
#include <cstring>

namespace {
constexpr double kRingSeconds = 0.2;     // how much audio the ring buffer holds
constexpr double kDeviceSeconds = 0.05;  // keep the existing device latency; isolate scheduling instead
}

// Both the pull device and QAudioSink belong to the audio thread. In particular,
// Qt's PulseAudio backend feeds the device from a QObject timer on that thread.
class AudioOutput::Device : public QIODevice {
public:
    explicit Device(AudioOutput &owner) : owner_(owner) {}

    bool start(const QAudioDevice &device, const QAudioFormat &format) {
        sink_ = new QAudioSink(device, format, this);
        sink_->setBufferSize(static_cast<qsizetype>(kDeviceSeconds * format.sampleRate()) *
                             format.bytesPerFrame());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        sink_->start(this);
        owner_.deviceLatencySeconds_ = static_cast<double>(sink_->bufferSize()) /
                                       (format.bytesPerFrame() * format.sampleRate());
        return sink_->state() != QAudio::StoppedState;
    }

    void stop() {
        sink_->stop();
        close();
    }

    void setVolume(qreal linearVolume) { sink_->setVolume(linearVolume); }

    bool isSequential() const override { return true; }
    // This is a continuous source: pauses and starvation yield silence, never EOF.
    bool atEnd() const override { return false; }

protected:
    qint64 readData(char *data, qint64 maxSize) override { return owner_.readData(data, maxSize); }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    AudioOutput &owner_;
    QAudioSink *sink_ = nullptr;  // owned by this device
};

AudioOutput::AudioOutput(QObject *parent) : QObject(parent) {
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

    device_ = new Device(*this);
    device_->moveToThread(&audioThread_);
    connect(&audioThread_, &QThread::finished, device_, &QObject::deleteLater);
    audioThread_.setObjectName(QStringLiteral("ViplyrAudio"));
    audioThread_.start();
    QMetaObject::invokeMethod(device_, [&] {
        available_ = device_->start(device, audioFormat);
    }, Qt::BlockingQueuedConnection);
}

AudioOutput::~AudioOutput() {
    if (!device_) return;
    QMetaObject::invokeMethod(device_, [this] { device_->stop(); }, Qt::BlockingQueuedConnection);
    audioThread_.quit();
    audioThread_.wait();  // finished also deletes the device and sink on their own thread
}

void AudioOutput::setVolume(qreal volume) {
    if (!available_) return;
    const qreal linearVolume = QAudio::convertVolume(volume, QAudio::LogarithmicVolumeScale,
                                                    QAudio::LinearVolumeScale);
    QMetaObject::invokeMethod(device_, [this, linearVolume] {
        device_->setVolume(linearVolume);
    }, Qt::QueuedConnection);
}

size_t AudioOutput::bufferedFrames() const {
    return sampleCount_ / static_cast<size_t>(format_.channels);
}

size_t AudioOutput::write(const float *samples, size_t frameCount) {
    if (!available_ || frameCount == 0) return 0;
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
    if (maxSize <= 0) return 0;
    // Consume whole interleaved frames so a partial backend request cannot swap channels.
    const size_t channels = static_cast<size_t>(format_.channels);
    const size_t wanted = static_cast<size_t>(maxSize) / (sizeof(float) * channels) * channels;

    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!paused_) {
            count = std::min(wanted, sampleCount_);
            const size_t firstPart = std::min(count, ring_.size() - readIndex_);
            std::memcpy(data, &ring_[readIndex_], firstPart * sizeof(float));
            std::memcpy(data + firstPart * sizeof(float), &ring_[0], (count - firstPart) * sizeof(float));
            readIndex_ = (readIndex_ + count) % ring_.size();
            sampleCount_ -= count;
        }
    }
    // Fill the rest with silence so the device keeps running while paused or starved.
    std::memset(data + count * sizeof(float), 0, (wanted - count) * sizeof(float));
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
