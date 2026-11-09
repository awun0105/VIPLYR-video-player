#include "ExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>

#include "Theme.h"

namespace {

// Crop choices: an aspect ratio, 0 for "none" and -1 for "custom".
constexpr double kNoCrop = 0.0;
constexpr double kCustomCrop = -1.0;

const char *kDialogStyle =
    "QDialog { background-color: #17171a; } QLabel, QCheckBox { color: #d0d0d6; } "
    "QDoubleSpinBox, QSpinBox, QComboBox, QLineEdit { background-color: #24242a; color: white; "
    "border: 1px solid #34343c; border-radius: 5px; padding: 4px 6px; } "
    "QComboBox QAbstractItemView { background-color: #24242a; color: white; "
    "selection-background-color: #3D9BFF; } "
    "QProgressBar { background-color: #24242a; border: none; border-radius: 5px; height: 10px; "
    "text-align: center; color: white; } QProgressBar::chunk { background-color: #3D9BFF; border-radius: 5px; }";

}  // namespace

ExportDialog::ExportDialog(const QString &videoPath, double durationSeconds, int pictureWidth,
                           int pictureHeight, QWidget *parent)
    : QDialog(parent), videoPath_(videoPath), durationSeconds_(durationSeconds),
      pictureWidth_(pictureWidth), pictureHeight_(pictureHeight) {
    setWindowTitle("Edit and export - " + QFileInfo(videoPath).fileName());
    setStyleSheet(kDialogStyle);
    setAttribute(Qt::WA_DeleteOnClose);
    buildForm();
    refreshSummary();

    process_.setProgram("ffmpeg");
    connect(&process_, &QProcess::readyReadStandardOutput, this, &ExportDialog::readProgress);
    connect(&process_, &QProcess::finished, this, &ExportDialog::onProcessFinished);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        setRunning(false);
        QMessageBox::critical(this, "Export", "ffmpeg could not be started. Is it installed?");
    });
}

ExportDialog::~ExportDialog() {
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(2000);
    }
    emit cropChanged(QRectF());
}

void ExportDialog::buildForm() {
    const auto timeSpin = [this](double value) {
        QDoubleSpinBox *spin = new QDoubleSpinBox;
        spin->setRange(0.0, durationSeconds_);
        spin->setDecimals(3);
        spin->setSingleStep(0.1);
        spin->setSuffix(" s");
        spin->setValue(value);
        connect(spin, &QDoubleSpinBox::valueChanged, this, &ExportDialog::refreshSummary);
        return spin;
    };
    // A time field with a button that fills in the player's current position.
    const auto timeRow = [this](QDoubleSpinBox *spin) {
        QPushButton *useCurrent = new QPushButton("Use current position");
        useCurrent->setStyleSheet(Theme::Button);
        connect(useCurrent, &QPushButton::clicked, this, [this, spin] {
            if (positionProvider_) spin->setValue(positionProvider_());
        });
        QHBoxLayout *row = new QHBoxLayout;
        row->addWidget(spin, 1);
        row->addWidget(useCurrent);
        return row;
    };

    startSpin_ = timeSpin(0.0);
    endSpin_ = timeSpin(durationSeconds_);
    accurateCheck_ = new QCheckBox("Cut on the exact frame (slower: re-encodes the video)");
    connect(accurateCheck_, &QCheckBox::toggled, this, &ExportDialog::refreshSummary);

    audioCombo_ = new QComboBox;
    audioCombo_->addItem("Keep the original sound", static_cast<int>(ExportOptions::Audio::Keep));
    audioCombo_->addItem("Remove the sound", static_cast<int>(ExportOptions::Audio::Remove));
    audioCombo_->addItem("Replace with an audio file", static_cast<int>(ExportOptions::Audio::Replace));
    audioPathEdit_ = new QLineEdit;
    audioPathEdit_->setPlaceholderText("Audio file");
    audioBrowseButton_ = new QPushButton("Browse...");
    audioBrowseButton_->setStyleSheet(Theme::Button);
    QHBoxLayout *audioFileRow = new QHBoxLayout;
    audioFileRow->addWidget(audioPathEdit_, 1);
    audioFileRow->addWidget(audioBrowseButton_);

    cropCombo_ = new QComboBox;
    cropCombo_->addItem("No crop", kNoCrop);
    cropCombo_->addItem("16:9 (wide)", 16.0 / 9.0);
    cropCombo_->addItem("9:16 (vertical)", 9.0 / 16.0);
    cropCombo_->addItem("1:1 (square)", 1.0);
    cropCombo_->addItem("4:3", 4.0 / 3.0);
    cropCombo_->addItem("Custom", kCustomCrop);
    QHBoxLayout *cropRow = new QHBoxLayout;
    const char *prefixes[4] = {"x ", "y ", "w ", "h "};
    const int maximums[4] = {pictureWidth_, pictureHeight_, pictureWidth_, pictureHeight_};
    const int defaults[4] = {0, 0, pictureWidth_, pictureHeight_};
    for (int i = 0; i < 4; ++i) {
        cropSpins_[i] = new QSpinBox;
        cropSpins_[i]->setRange(0, maximums[i]);
        cropSpins_[i]->setValue(defaults[i]);
        cropSpins_[i]->setPrefix(prefixes[i]);
        cropSpins_[i]->setSingleStep(2);
        cropRow->addWidget(cropSpins_[i]);
        connect(cropSpins_[i], &QSpinBox::valueChanged, this, &ExportDialog::refreshSummary);
    }

    rotateCombo_ = new QComboBox;
    for (int degrees : {0, 90, 180, 270}) {
        rotateCombo_->addItem(degrees == 0 ? "No rotation" : QString("%1° clockwise").arg(degrees), degrees);
    }

    outputCombo_ = new QComboBox;
    outputCombo_->addItem("Video file (.mp4)", static_cast<int>(ExportOptions::Output::Video));
    outputCombo_->addItem("Image sequence (one PNG per frame)",
                          static_cast<int>(ExportOptions::Output::ImageSequence));

    summaryLabel_ = new QLabel;
    summaryLabel_->setWordWrap(true);
    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 1000);
    progressBar_->hide();

    exportButton_ = new QPushButton("Export...");
    exportButton_->setStyleSheet(Theme::Button);
    cancelButton_ = new QPushButton("Cancel export");
    cancelButton_->setStyleSheet(Theme::Button);
    cancelButton_->hide();
    QHBoxLayout *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(cancelButton_);
    buttonRow->addWidget(exportButton_);

    QFormLayout *form = new QFormLayout(this);
    form->setVerticalSpacing(10);
    form->addRow("Start", timeRow(startSpin_));
    form->addRow("End", timeRow(endSpin_));
    form->addRow("", accurateCheck_);
    form->addRow("Sound", audioCombo_);
    form->addRow("", audioFileRow);
    form->addRow("Crop", cropCombo_);
    form->addRow("", cropRow);
    form->addRow("Rotate", rotateCombo_);
    form->addRow("Save as", outputCombo_);
    form->addRow(summaryLabel_);
    form->addRow(progressBar_);
    form->addRow(buttonRow);

    for (QComboBox *combo : {audioCombo_, cropCombo_, rotateCombo_, outputCombo_}) {
        connect(combo, &QComboBox::currentIndexChanged, this, &ExportDialog::refreshSummary);
    }
    connect(audioBrowseButton_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, "Choose audio", QString(), "Audio and video files (*.mp3 *.m4a *.aac *.wav *.flac *.ogg *.opus *.mp4 *.mkv);;All Files (*)");
        if (!path.isEmpty()) audioPathEdit_->setText(path);
    });
    connect(exportButton_, &QPushButton::clicked, this, &ExportDialog::startExport);
    connect(cancelButton_, &QPushButton::clicked, &process_, &QProcess::kill);
}

QRect ExportDialog::currentCrop() const {
    const double choice = cropCombo_->currentData().toDouble();
    if (choice == kNoCrop) return QRect();
    if (choice == kCustomCrop) {
        const QRect custom(cropSpins_[0]->value(), cropSpins_[1]->value(), cropSpins_[2]->value(),
                           cropSpins_[3]->value());
        return custom.intersected(QRect(0, 0, pictureWidth_, pictureHeight_));
    }
    return centredCrop(pictureWidth_, pictureHeight_, choice);
}

ExportOptions ExportDialog::currentOptions() const {
    ExportOptions options;
    options.inputPath = videoPath_;
    options.startSeconds = startSpin_->value();
    // An end at the very end means "until the end", which also lets ffmpeg copy the tail exactly.
    options.endSeconds = endSpin_->value() >= durationSeconds_ - 0.001 ? 0.0 : endSpin_->value();
    options.frameAccurateCut = accurateCheck_->isChecked();
    options.audio = static_cast<ExportOptions::Audio>(audioCombo_->currentData().toInt());
    options.replacementAudioPath = audioPathEdit_->text();
    options.crop = currentCrop();
    options.rotateDegrees = rotateCombo_->currentData().toInt();
    options.output = static_cast<ExportOptions::Output>(outputCombo_->currentData().toInt());
    return options;
}

void ExportDialog::refreshSummary() {
    const ExportOptions options = currentOptions();

    const bool replacing = options.audio == ExportOptions::Audio::Replace;
    audioPathEdit_->setEnabled(replacing);
    audioBrowseButton_->setEnabled(replacing);
    const bool customCrop = cropCombo_->currentData().toDouble() == kCustomCrop;
    for (QSpinBox *spin : cropSpins_) spin->setEnabled(customCrop);

    const double length = options.exportedSeconds(durationSeconds_);
    const QString speed = options.needsReencode()
                              ? "The video will be re-encoded, which takes a while."
                              : "Fast export without quality loss. The start snaps to the nearest keyframe.";
    summaryLabel_->setText(QString("Length: %1 s. %2").arg(length, 0, 'f', 2).arg(speed));
    exportButton_->setEnabled(length > 0.0 && process_.state() == QProcess::NotRunning);

    emit cropChanged(options.crop.isNull() ? QRectF() : QRectF(options.crop));
}

void ExportDialog::startExport() {
    ExportOptions options = currentOptions();
    if (options.audio == ExportOptions::Audio::Replace && !QFileInfo::exists(options.replacementAudioPath)) {
        QMessageBox::warning(this, "Export", "Choose the audio file to use first.");
        return;
    }

    const QFileInfo video(videoPath_);
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (options.output == ExportOptions::Output::ImageSequence) {
        options.outputPath = QFileDialog::getExistingDirectory(this, "Folder for the images", folder);
    } else {
        options.outputPath = QFileDialog::getSaveFileName(
            this, "Export video", folder + "/" + video.completeBaseName() + "_edit.mp4", "MP4 video (*.mp4)");
    }
    if (options.outputPath.isEmpty()) return;
    if (QFileInfo(options.outputPath) == video) {
        QMessageBox::warning(this, "Export", "Choose a different file: the original cannot be overwritten.");
        return;
    }

    runningOptions_ = options;
    progressBar_->setValue(0);
    setRunning(true);
    process_.setArguments(buildFfmpegArguments(options));
    process_.start();
}

void ExportDialog::readProgress() {
    // ffmpeg -progress prints "key=value" lines; out_time_us is the time written so far.
    const double total = runningOptions_.exportedSeconds(durationSeconds_);
    while (process_.canReadLine()) {
        const QByteArray line = process_.readLine().trimmed();
        if (!line.startsWith("out_time_us=") || total <= 0.0) continue;
        const double written = line.mid(qstrlen("out_time_us=")).toDouble() / 1e6;
        progressBar_->setValue(static_cast<int>(qBound(0.0, written / total, 1.0) * 1000));
    }
}

void ExportDialog::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    setRunning(false);
    if (status == QProcess::CrashExit) return;  // cancelled by the user
    if (exitCode != 0) {
        const QString details = QString::fromLocal8Bit(process_.readAllStandardError()).right(600);
        QMessageBox::critical(this, "Export failed", details.isEmpty() ? "ffmpeg reported an error." : details);
        return;
    }
    progressBar_->setValue(progressBar_->maximum());
    emit exportFinished(runningOptions_.outputPath);
}

void ExportDialog::setRunning(bool running) {
    progressBar_->setVisible(running || progressBar_->value() > 0);
    cancelButton_->setVisible(running);
    exportButton_->setEnabled(!running);
}
