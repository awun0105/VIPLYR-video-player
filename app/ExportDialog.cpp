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
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>

#include "ExportJob.h"
#include "MediaFiles.h"
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
    "QListWidget { background-color: #24242a; color: white; border: 1px solid #34343c; border-radius: 5px; } "
    "QListWidget::item:selected { background-color: #3D9BFF; }";

QString describeSegment(const ExportOptions::Segment &segment, double durationSeconds) {
    const double end = segment.endSeconds > 0.0 ? segment.endSeconds : durationSeconds;
    return QString("%1  -  %2   (%3 s)")
        .arg(MediaFiles::formatTimePrecise(qRound64(segment.startSeconds * 1000.0)),
             MediaFiles::formatTimePrecise(qRound64(end * 1000.0)))
        .arg(end - segment.startSeconds, 0, 'f', 2);
}

}  // namespace

ExportDialog::ExportDialog(const QString &videoPath, double durationSeconds, int pictureWidth,
                           int pictureHeight, bool hasAudio, QWidget *parent)
    : QDialog(parent), videoPath_(videoPath), durationSeconds_(durationSeconds),
      pictureWidth_(pictureWidth), pictureHeight_(pictureHeight), hasAudio_(hasAudio) {
    setWindowTitle("Edit and export - " + QFileInfo(videoPath).fileName());
    setStyleSheet(kDialogStyle);
    setAttribute(Qt::WA_DeleteOnClose);
    buildForm();
    refreshSummary();
    ExportJob::detectGpuEncoders().then(this, [this](const QVector<ExportOptions::Encoder> &encoders) {
        addGpuEncoders(encoders);
    });
}

ExportDialog::~ExportDialog() {
    emit cropChanged(QRectF());
    emit segmentsChanged({});
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

    // Parts to keep, joined in list order. With none listed, Start-End is exported.
    segmentList_ = new QListWidget;
    segmentList_->setMaximumHeight(110);
    QPushButton *addSegmentButton = new QPushButton("Add Start-End as a part");
    removeSegmentButton_ = new QPushButton("Remove part");
    QPushButton *previewButton = new QPushButton("Preview");
    previewButton->setToolTip("Plays the parts one after another in the player");
    for (QPushButton *button : {addSegmentButton, removeSegmentButton_, previewButton}) button->setStyleSheet(Theme::Button);
    QHBoxLayout *segmentButtons = new QHBoxLayout;
    segmentButtons->addWidget(addSegmentButton);
    segmentButtons->addWidget(removeSegmentButton_);
    segmentButtons->addStretch(1);
    segmentButtons->addWidget(previewButton);
    connect(addSegmentButton, &QPushButton::clicked, this, &ExportDialog::addSegment);
    connect(removeSegmentButton_, &QPushButton::clicked, this, [this] {
        const int row = segmentList_->currentRow();
        if (row < 0) return;
        segments_.removeAt(row);
        delete segmentList_->takeItem(row);
        refreshSummary();
    });
    connect(segmentList_, &QListWidget::currentRowChanged, this, &ExportDialog::refreshSummary);
    connect(previewButton, &QPushButton::clicked, this, [this] { emit previewRequested(currentSegments()); });

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

    // GPU encoders are added once a quick test shows they work here.
    encoderCombo_ = new QComboBox;
    encoderCombo_->addItem("CPU (x264): best quality per size", static_cast<int>(ExportOptions::Encoder::Cpu));

    outputCombo_ = new QComboBox;
    outputCombo_->addItem("Video file (.mp4)", static_cast<int>(ExportOptions::Output::Video));
    outputCombo_->addItem("Image sequence (one PNG per frame)",
                          static_cast<int>(ExportOptions::Output::ImageSequence));

    summaryLabel_ = new QLabel;
    summaryLabel_->setWordWrap(true);

    exportButton_ = new QPushButton("Export...");
    exportButton_->setStyleSheet(Theme::Button);
    QHBoxLayout *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(exportButton_);

    QFormLayout *form = new QFormLayout(this);
    form->setVerticalSpacing(10);
    form->addRow("Start", timeRow(startSpin_));
    form->addRow("End", timeRow(endSpin_));
    form->addRow("Parts", segmentList_);
    form->addRow("", segmentButtons);
    form->addRow("", accurateCheck_);
    form->addRow("Sound", audioCombo_);
    form->addRow("", audioFileRow);
    form->addRow("Crop", cropCombo_);
    form->addRow("", cropRow);
    form->addRow("Rotate", rotateCombo_);
    form->addRow("Encoder", encoderCombo_);
    form->addRow("Save as", outputCombo_);
    form->addRow(summaryLabel_);
    form->addRow(buttonRow);

    for (QComboBox *combo : {audioCombo_, cropCombo_, rotateCombo_, encoderCombo_, outputCombo_}) {
        connect(combo, &QComboBox::currentIndexChanged, this, &ExportDialog::refreshSummary);
    }
    connect(audioBrowseButton_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, "Choose audio", QString(), "Audio and video files (*.mp3 *.m4a *.aac *.wav *.flac *.ogg *.opus *.mp4 *.mkv);;All Files (*)");
        if (!path.isEmpty()) audioPathEdit_->setText(path);
    });
    connect(exportButton_, &QPushButton::clicked, this, &ExportDialog::startExport);
}

void ExportDialog::addGpuEncoders(const QVector<ExportOptions::Encoder> &encoders) {
    for (ExportOptions::Encoder encoder : encoders) {
        const QString name = encoder == ExportOptions::Encoder::Nvenc ? "NVIDIA GPU (NVENC): fast"
                                                                      : "Intel/AMD GPU (VAAPI): fast";
        encoderCombo_->addItem(name, static_cast<int>(encoder));
    }
}

void ExportDialog::addSegment() {
    ExportOptions::Segment segment;
    segment.startSeconds = startSpin_->value();
    segment.endSeconds = endSpin_->value();
    if (segment.endSeconds <= segment.startSeconds) {
        QMessageBox::information(this, "Parts", "End must be after Start.");
        return;
    }
    segments_.append(segment);
    segmentList_->addItem(describeSegment(segment, durationSeconds_));
    refreshSummary();
}

QVector<ExportOptions::Segment> ExportDialog::currentSegments() const {
    if (!segments_.isEmpty()) return segments_;
    // An end at the very end means "until the end", which also lets ffmpeg copy the tail exactly.
    ExportOptions::Segment whole;
    whole.startSeconds = startSpin_->value();
    whole.endSeconds = endSpin_->value() >= durationSeconds_ - 0.001 ? 0.0 : endSpin_->value();
    return {whole};
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
    options.segments = currentSegments();
    if (options.segments.size() == 1 && options.segments[0].startSeconds <= 0.0 && options.segments[0].endSeconds <= 0.0) {
        options.segments.clear();  // the whole video
    }
    options.inputHasAudio = hasAudio_;
    options.encoder = static_cast<ExportOptions::Encoder>(encoderCombo_->currentData().toInt());
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

    removeSegmentButton_->setEnabled(segmentList_->currentRow() >= 0);
    const double length = options.exportedSeconds(durationSeconds_);
    QString speed = options.needsReencode()
                        ? "The video will be re-encoded, which takes a while."
                        : "Fast export without quality loss. The start snaps to the nearest keyframe.";
    if (options.joinsSegments()) speed = QString("%1 parts joined in list order. ").arg(options.segments.size()) + speed;
    summaryLabel_->setText(QString("Length: %1 s. %2").arg(length, 0, 'f', 2).arg(speed));
    exportButton_->setEnabled(length > 0.0 && !exportRunning_);
    exportButton_->setText(exportRunning_ ? "Another export is running" : "Export...");

    emit cropChanged(options.crop.isNull() ? QRectF() : QRectF(options.crop));
    emit segmentsChanged(currentSegments());
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

    emit exportRequested(options, options.exportedSeconds(durationSeconds_));
}

void ExportDialog::setExportRunning(bool running) {
    exportRunning_ = running;
    refreshSummary();
}
