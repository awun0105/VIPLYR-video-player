#pragma once
#include <QDialog>
#include <QProcess>
#include <QRectF>
#include <functional>

#include "ExportCommand.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

// Lets the user trim, crop, rotate and re-voice a video and export the result
// with ffmpeg. It stays open beside the player so positions can be picked there.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    // `pictureWidth`/`pictureHeight` are the video's size as displayed.
    ExportDialog(const QString &videoPath, double durationSeconds, int pictureWidth, int pictureHeight,
                 QWidget *parent = nullptr);
    ~ExportDialog() override;

    // Supplies the player's current position (seconds) for the "use current" buttons.
    void setPositionProvider(std::function<double()> provider) { positionProvider_ = std::move(provider); }

signals:
    void cropChanged(const QRectF &crop);  // null when no crop is selected
    void exportFinished(const QString &outputPath);

private:
    void buildForm();
    ExportOptions currentOptions() const;
    QRect currentCrop() const;
    void refreshSummary();
    void startExport();
    void readProgress();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void setRunning(bool running);

    const QString videoPath_;
    const double durationSeconds_;
    const int pictureWidth_;
    const int pictureHeight_;
    std::function<double()> positionProvider_;

    QDoubleSpinBox *startSpin_;
    QDoubleSpinBox *endSpin_;
    QCheckBox *accurateCheck_;
    QComboBox *audioCombo_;
    QLineEdit *audioPathEdit_;
    QPushButton *audioBrowseButton_;
    QComboBox *cropCombo_;
    QSpinBox *cropSpins_[4];  // x, y, width, height
    QComboBox *rotateCombo_;
    QComboBox *outputCombo_;
    QLabel *summaryLabel_;
    QProgressBar *progressBar_;
    QPushButton *exportButton_;
    QPushButton *cancelButton_;

    QProcess process_;
    ExportOptions runningOptions_;
};
