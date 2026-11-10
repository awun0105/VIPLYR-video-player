#include "ExportJob.h"

#include <QtConcurrent>

ExportJob::ExportJob(QObject *parent) : QObject(parent) {
    process_.setProgram("ffmpeg");
    connect(&process_, &QProcess::readyReadStandardOutput, this, &ExportJob::readProgress);
    connect(&process_, &QProcess::finished, this, &ExportJob::onFinished);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit finished(false, options_.outputPath, "ffmpeg could not be started. Is it installed?");
        }
    });
}

ExportJob::~ExportJob() {
    if (!isRunning()) return;
    process_.kill();
    process_.waitForFinished(2000);
}

void ExportJob::start(const ExportOptions &options, double totalSeconds) {
    if (isRunning()) return;
    options_ = options;
    totalSeconds_ = totalSeconds;
    cancelled_ = false;
    emit progressChanged(0.0);
    process_.setArguments(buildFfmpegArguments(options));
    process_.start();
}

void ExportJob::cancel() {
    if (!isRunning()) return;
    cancelled_ = true;
    process_.kill();
}

void ExportJob::readProgress() {
    // ffmpeg -progress prints "key=value" lines; out_time_us is the time written so far.
    while (process_.canReadLine()) {
        const QByteArray line = process_.readLine().trimmed();
        if (!line.startsWith("out_time_us=") || totalSeconds_ <= 0.0) continue;
        const double written = line.mid(qstrlen("out_time_us=")).toDouble() / 1e6;
        emit progressChanged(qBound(0.0, written / totalSeconds_, 1.0));
    }
}

void ExportJob::onFinished(int exitCode, QProcess::ExitStatus status) {
    if (cancelled_) {
        emit finished(false, options_.outputPath, QString());
        return;
    }
    if (status == QProcess::NormalExit && exitCode == 0) {
        emit progressChanged(1.0);
        emit finished(true, options_.outputPath, QString());
        return;
    }
    const QString details = QString::fromLocal8Bit(process_.readAllStandardError()).trimmed().right(600);
    emit finished(false, options_.outputPath, details.isEmpty() ? "ffmpeg reported an error." : details);
}

QFuture<QVector<ExportOptions::Encoder>> ExportJob::detectGpuEncoders() {
    return QtConcurrent::run([] {
        QVector<ExportOptions::Encoder> working;
        for (ExportOptions::Encoder encoder : {ExportOptions::Encoder::Nvenc, ExportOptions::Encoder::Vaapi}) {
            QProcess probe;
            probe.start("ffmpeg", encoderProbeArguments(encoder));
            if (probe.waitForFinished(15000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) {
                working.append(encoder);
            }
        }
        return working;
    });
}
