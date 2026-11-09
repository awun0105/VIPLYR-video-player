#pragma once
#include <QAudioSink>
#include <QIODevice>
#include <memory>
#include <mutex>
#include <vector>

#include "viplyr/Sinks.h"

// Plays the engine's audio through the default output device.
//
// The engine writes samples from its own thread into a small ring buffer; Qt's
// audio backend pulls them out again through read().
class AudioOutput : public QIODevice, public viplyr::IAudioSink {
    Q_OBJECT
public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput() override;

    bool isAvailable() const { return sink_ != nullptr; }
    void setVolume(qreal volume);  // 0.0 .. 1.0, perceptual scale

    // viplyr::IAudioSink
    viplyr::AudioFormat format() const override { return format_; }
    size_t write(const float *samples, size_t frameCount) override;
    double bufferedSeconds() const override;
    void flush() override;
    void setPaused(bool paused) override;

protected:
    // QIODevice: called by the audio backend when it wants more samples.
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *, qint64) override { return 0; }

private:
    size_t bufferedFrames() const;  // call with mutex_ held

    std::unique_ptr<QAudioSink> sink_;
    viplyr::AudioFormat format_;
    double deviceLatencySeconds_ = 0.0;

    mutable std::mutex mutex_;
    std::vector<float> ring_;   // interleaved samples
    size_t readIndex_ = 0;      // in samples
    size_t sampleCount_ = 0;    // samples currently stored
    bool paused_ = true;
};
