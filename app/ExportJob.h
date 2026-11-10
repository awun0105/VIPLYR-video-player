#pragma once
#include <QFuture>
#include <QObject>
#include <QProcess>
#include <QVector>

#include "ExportCommand.h"

// Runs one export with ffmpeg in the background and reports its progress. Owned by
// the main window, so an export keeps going while the user watches something else.
class ExportJob : public QObject {
    Q_OBJECT
public:
    explicit ExportJob(QObject *parent = nullptr);
    ~ExportJob() override;  // stops a running export

    bool isRunning() const { return process_.state() != QProcess::NotRunning; }
    QString outputPath() const { return options_.outputPath; }

    // `totalSeconds` is the length being written, for the progress fraction.
    void start(const ExportOptions &options, double totalSeconds);
    void cancel();

    // The GPU encoders that work on this machine, tested with short encodes on a
    // worker thread (takes about a second).
    static QFuture<QVector<ExportOptions::Encoder>> detectGpuEncoders();

signals:
    void progressChanged(double fraction);  // 0..1
    void finished(bool succeeded, const QString &outputPath, const QString &error);  // error empty when cancelled

private:
    void readProgress();
    void onFinished(int exitCode, QProcess::ExitStatus status);

    QProcess process_;
    ExportOptions options_;
    double totalSeconds_ = 0.0;
    bool cancelled_ = false;
};
