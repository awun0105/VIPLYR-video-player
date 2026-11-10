#pragma once
#include <QObject>
#include <QThread>
#include <mutex>
#include <vector>

#include "viplyr/Sinks.h"

// Plays the engine's audio through the default output device.
//
// The engine writes samples from its own thread into a small ring buffer; Qt's
// audio backend pulls them out on a dedicated Qt thread, independently of GUI work.
class AudioOutput : public QObject, public viplyr::IAudioSink {
    Q_OBJECT
public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput() override;

    bool isAvailable() const { return available_; }
    void setVolume(qreal volume);  // 0.0 .. 1.0, perceptual scale

    // viplyr::IAudioSink
    viplyr::AudioFormat format() const override { return format_; }
    size_t write(const float *samples, size_t frameCount) override;
    double bufferedSeconds() const override;
    void flush() override;
    void setPaused(bool paused) override;

private:
    class Device;
    qint64 readData(char *data, qint64 maxSize);
    size_t bufferedFrames() const;  // call with mutex_ held

    QThread audioThread_;
    Device *device_ = nullptr;  // pull device and sink live on audioThread_
    bool available_ = false;
    viplyr::AudioFormat format_;
    double deviceLatencySeconds_ = 0.0;

    mutable std::mutex mutex_;
    std::vector<float> ring_;   // interleaved samples
    size_t readIndex_ = 0;      // in samples
    size_t sampleCount_ = 0;    // samples currently stored
    bool paused_ = true;
};
