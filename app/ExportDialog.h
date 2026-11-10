#pragma once
#include <QDialog>
#include <QRectF>
#include <functional>

#include "ExportCommand.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

// Lets the user pick the parts of a video to keep, crop, rotate and re-voice it,
// and choose how to save it. The export itself runs in the main window (ExportJob),
// so this dialog can be closed while it works. It stays open beside the player so
// positions can be picked there.
class ExportDialog : public QDialog {
    Q_OBJECT
public:
    // `pictureWidth`/`pictureHeight` are the video's size as displayed.
    ExportDialog(const QString &videoPath, double durationSeconds, int pictureWidth, int pictureHeight,
                 bool hasAudio, QWidget *parent = nullptr);
    ~ExportDialog() override;

    // Supplies the player's current position (seconds) for the "use current" buttons.
    void setPositionProvider(std::function<double()> provider) { positionProvider_ = std::move(provider); }
    // Whether an export is running; the dialog then waits before starting another.
    void setExportRunning(bool running);

signals:
    void cropChanged(const QRectF &crop);  // null when no crop is selected
    void segmentsChanged(const QVector<ExportOptions::Segment> &segments);  // what will be kept
    void previewRequested(const QVector<ExportOptions::Segment> &segments);
    void exportRequested(const ExportOptions &options, double totalSeconds);

private:
    void buildForm();
    ExportOptions currentOptions() const;
    QVector<ExportOptions::Segment> currentSegments() const;
    QRect currentCrop() const;
    void addSegment();
    void refreshSummary();
    void startExport();
    void addGpuEncoders(const QVector<ExportOptions::Encoder> &encoders);

    const QString videoPath_;
    const double durationSeconds_;
    const int pictureWidth_;
    const int pictureHeight_;
    const bool hasAudio_;
    std::function<double()> positionProvider_;
    bool exportRunning_ = false;

    QVector<ExportOptions::Segment> segments_;  // parts added to the list
    QDoubleSpinBox *startSpin_;
    QDoubleSpinBox *endSpin_;
    QListWidget *segmentList_;
    QPushButton *removeSegmentButton_;
    QCheckBox *accurateCheck_;
    QComboBox *audioCombo_;
    QLineEdit *audioPathEdit_;
    QPushButton *audioBrowseButton_;
    QComboBox *cropCombo_;
    QSpinBox *cropSpins_[4];  // x, y, width, height
    QComboBox *rotateCombo_;
    QComboBox *encoderCombo_;
    QComboBox *outputCombo_;
    QLabel *summaryLabel_;
    QPushButton *exportButton_;
};
