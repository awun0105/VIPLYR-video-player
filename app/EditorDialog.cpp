#include "EditorDialog.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include "EditTimeline.h"
#include "ExportJob.h"
#include "MediaFiles.h"
#include "VideoWidget.h"

namespace {
bool isSourcePath(const EditProject &project, const QString &path) {
    for (const auto &clip : project.clips) {
        const auto source = QFileInfo(clip.path);
        const auto target = QFileInfo(path);
        if (target.absoluteFilePath() == source.absoluteFilePath() ||
            (!target.canonicalFilePath().isEmpty() && target.canonicalFilePath() == source.canonicalFilePath())) return true;
    }
    return false;
}
}

EditorDialog::EditorDialog(QWidget *parent) : QDialog(parent) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Viplyr · Video editor");
    resize(1180, 880);
    setMinimumSize(860, 640);
    setAcceptDrops(true);
    setStyleSheet(
        "QDialog { background:#171a20; color:#e9edf4; }"
        "QLabel { color:#dfe5ef; background:transparent; }"
        "QPushButton { color:#e9edf4; background:#2b303b; padding:8px 13px; border:1px solid #3b4351; border-radius:6px; }"
        "QPushButton:hover { background:#3b4658; } QPushButton:disabled { color:#6f7784; background:#232730; }"
        "QComboBox { color:#e9edf4; background:#2b303b; padding:6px; border:1px solid #3b4351; border-radius:5px; }"
        "QComboBox QAbstractItemView { color:#e9edf4; background:#2b303b; selection-background-color:#41668b; }"
        "QScrollArea { border:1px solid #343c49; border-radius:6px; background:#121419; }");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(12);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel("VIDEO EDITOR");
    title->setStyleSheet("font-size:15px; font-weight:bold; letter-spacing:2px; color:#f8cf55;");
    header->addWidget(title);
    header->addStretch();
    auto *open = new QPushButton("Open project…");
    auto *save = new QPushButton("Save project");
    auto *import = new QPushButton("+ Import videos");
    import->setObjectName("editorImport");
    exportButton_ = new QPushButton("Export video…");
    exportButton_->setObjectName("editorExport");
    exportButton_->setStyleSheet("QPushButton { background:#f8cf55; color:#211d10; font-weight:bold; }"
                                 "QPushButton:hover { background:#ffdc6e; }"
                                 "QPushButton:disabled { background:#4a4530; color:#8a8464; }");
    for (auto *button : {open, save, import, exportButton_}) header->addWidget(button);
    layout->addLayout(header);

    auto *previewRow = new QHBoxLayout;
    video_ = new VideoWidget;
    video_->setMinimumSize(400, 240);
    previewRow->addWidget(video_, 1);
    auto *inspectorPanel = new QWidget;
    auto *inspector = new QVBoxLayout(inspectorPanel);
    inspector->setContentsMargins(4, 0, 4, 0);
    inspector->setSpacing(6);
    inspector->setSizeConstraint(QLayout::SetMinimumSize);
    auto *selectionTitle = new QLabel("SELECTED CLIP");
    selectionTitle->setStyleSheet("color:#9aa8ba; font-size:11px; font-weight:bold;");
    inspector->addWidget(selectionTitle);
    clipLabel_ = new QLabel("Select a clip on the timeline");
    clipLabel_->setTextFormat(Qt::PlainText);
    clipLabel_->setWordWrap(true);
    clipLabel_->setMinimumWidth(205);
    clipLabel_->setMaximumWidth(230);
    clipLabel_->setMinimumHeight(100);
    inspector->addWidget(clipLabel_);
    splitButton_ = new QPushButton("Split at playhead  ·  B");
    splitButton_->setObjectName("editorSplit");
    duplicateButton_ = new QPushButton("Duplicate clip");
    deleteButton_ = new QPushButton("Delete clip  ·  Del");
    for (auto *button : {splitButton_, duplicateButton_, deleteButton_}) inspector->addWidget(button);
    inspector->addSpacing(16);
    inspector->addWidget(new QLabel("Output canvas"));
    canvas_ = new QComboBox;
    canvas_->addItem("Match first clip", QSize());
    canvas_->addItem("16:9 · 1080p", QSize(1920, 1080));
    canvas_->addItem("9:16 · Portrait", QSize(1080, 1920));
    canvas_->addItem("1:1 · Square", QSize(1080, 1080));
    canvas_->addItem("16:9 · 720p", QSize(1280, 720));
    inspector->addWidget(canvas_);
    inspector->addWidget(new QLabel("Export encoder"));
    encoder_ = new QComboBox;
    encoder_->addItem("CPU · H.264", int(ExportOptions::Encoder::Cpu));
    inspector->addWidget(encoder_);
    auto *hint = new QLabel("Clips are fitted inside the canvas. Original files are never changed.");
    hint->setWordWrap(true);
    hint->setMaximumWidth(230);
    hint->setStyleSheet("color:#8e9caf; font-size:11px;");
    inspector->addWidget(hint);
    inspector->addStretch();
    auto *quick = new QPushButton("Quick trim / crop / rotate…");
    quick->setToolTip("Opens the original advanced export controls for the video in the player");
    inspector->addWidget(quick);
    auto *inspectorScroll = new QScrollArea;
    inspectorScroll->setWidget(inspectorPanel);
    inspectorPanel->setAutoFillBackground(false);  // setWidget() turns this on: a light panel in a dark dialog
    inspectorScroll->viewport()->setAutoFillBackground(false);
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setFixedWidth(240);
    inspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inspectorScroll->setStyleSheet("QScrollArea { border:none; background:transparent; }");
    previewRow->addWidget(inspectorScroll);
    layout->addLayout(previewRow, 1);

    auto *transport = new QHBoxLayout;
    undoButton_ = new QPushButton("Undo");
    redoButton_ = new QPushButton("Redo");
    play_ = new QPushButton("▶  Play");
    play_->setObjectName("editorPlay");
    timeLabel_ = new QLabel;
    timeLabel_->setStyleSheet("font-family:monospace; font-size:13px;");
    for (auto *button : {undoButton_, redoButton_, play_}) transport->addWidget(button);
    transport->addWidget(timeLabel_);
    transport->addStretch();
    auto *minus = new QPushButton("−");
    auto *plus = new QPushButton("+");
    auto *fit = new QPushButton("Fit timeline");
    transport->addWidget(minus);
    transport->addWidget(plus);
    transport->addWidget(fit);
    layout->addLayout(transport);

    timeline_ = new EditTimeline;
    timeline_->setObjectName("editorTimeline");
    scroll_ = new QScrollArea;
    scroll_->setWidget(timeline_);
    scroll_->setWidgetResizable(true);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scroll_->setFixedHeight(194);
    layout->addWidget(scroll_);

    auto *footer = new QHBoxLayout;
    statusLabel_ = new QLabel("Import videos or drop files on the timeline. Space: play/pause · B: split · Ctrl+Z: undo");
    statusLabel_->setTextFormat(Qt::PlainText);
    statusLabel_->setStyleSheet("color:#9aa8ba; font-size:11px;");
    statusLabel_->setWordWrap(true);
    footer->addWidget(statusLabel_, 1);
    progress_ = new QProgressBar;
    progress_->setRange(0, 1000);
    progress_->setFixedWidth(130);
    progress_->hide();
    cancelButton_ = new QPushButton("Cancel export");
    cancelButton_->hide();
    footer->addWidget(progress_);
    footer->addWidget(cancelButton_);
    layout->addLayout(footer);
    // Keys go to the editor shortcuts, never to whichever button happened to be focused
    // (Space on a focused "Delete clip" would otherwise delete the clip).
    for (auto *button : findChildren<QPushButton *>()) button->setFocusPolicy(Qt::NoFocus);

    connect(import, &QPushButton::clicked, this, [this] {
        importPaths(QFileDialog::getOpenFileNames(this, "Import videos", {}, MediaFiles::fileDialogFilter()));
    });
    connect(open, &QPushButton::clicked, this, &EditorDialog::loadProject);
    connect(save, &QPushButton::clicked, this, [this] { saveProject(); });
    connect(exportButton_, &QPushButton::clicked, this, &EditorDialog::exportVideo);
    connect(quick, &QPushButton::clicked, this, &EditorDialog::quickExportRequested);
    connect(cancelButton_, &QPushButton::clicked, this, &EditorDialog::cancelExportRequested);
    connect(play_, &QPushButton::clicked, this, &EditorDialog::playRequested);
    connect(undoButton_, &QPushButton::clicked, this, &EditorDialog::undo);
    connect(redoButton_, &QPushButton::clicked, this, &EditorDialog::redo);
    connect(splitButton_, &QPushButton::clicked, this, &EditorDialog::split);
    connect(duplicateButton_, &QPushButton::clicked, this, &EditorDialog::duplicate);
    connect(deleteButton_, &QPushButton::clicked, this, &EditorDialog::remove);
    connect(fit, &QPushButton::clicked, this, &EditorDialog::fitTimeline);
    connect(minus, &QPushButton::clicked, this, [this] { timeline_->setZoom(timeline_->zoom() / 1.5); });
    connect(plus, &QPushButton::clicked, this, [this] { timeline_->setZoom(timeline_->zoom() * 1.5); });
    connect(timeline_, &EditTimeline::zoomRequested, timeline_, &EditTimeline::setZoom);
    connect(timeline_, &EditTimeline::clipSelected, this, [this](int index) { selected_ = index; refresh(false); });
    connect(timeline_, &EditTimeline::seekRequested, this, [this](qint64 ms) {
        setPosition(ms);
        emit seekRequested(ms);
    });
    connect(timeline_, &EditTimeline::filesDropped, this, &EditorDialog::importPaths);
    connect(timeline_, &EditTimeline::moveRequested, this, &EditorDialog::move);
    connect(timeline_, &EditTimeline::trimStarted, this, [this] { gestureBefore_ = state(); gestureChanged_ = false; });
    connect(timeline_, &EditTimeline::trimRequested, this, [this](int index, qint64 in, qint64 out, bool right) {
        if (loadingProject_) return;
        if (!project_.trim(index, in, out)) return;
        gestureChanged_ = true;
        positionMs_ = project_.startOf(index) + (right ? std::max<qint64>(0, project_.clips[index].duration() - 1) : 0);
        refresh();
    });
    connect(timeline_, &EditTimeline::trimFinished, this, [this] { if (gestureChanged_) commit(gestureBefore_); });
    const auto shortcut = [this](const QKeySequence &key, auto action) {
        auto *s = new QShortcut(key, this);
        connect(s, &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence(Qt::Key_Space), [this] { emit playRequested(); });
    shortcut(QKeySequence(Qt::Key_B), [this] { split(); });
    shortcut(QKeySequence(Qt::Key_Delete), [this] { remove(); });
    shortcut(QKeySequence::Undo, [this] { undo(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), [this] { redo(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), [this] { redo(); });
    shortcut(QKeySequence::Save, [this] { saveProject(); });
    shortcut(QKeySequence::Open, [this] { loadProject(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_I), [this, import] { import->click(); });
    shortcut(QKeySequence(Qt::Key_Left), [this] { emit seekRequested(std::max<qint64>(0, positionMs_ - 33)); });
    shortcut(QKeySequence(Qt::Key_Right), [this] { emit seekRequested(std::min(project_.duration(), positionMs_ + 33)); });
    ExportJob::detectGpuEncoders().then(this, [this](const QVector<ExportOptions::Encoder> &encoders) {
        for (auto encoder : encoders)
            encoder_->addItem(encoder == ExportOptions::Encoder::Nvenc ? "NVIDIA · H.264" : "VAAPI · H.264", int(encoder));
    });
    savedJson_ = projectJson();
    refresh(false);
}

EditorDialog::~EditorDialog() { *importCancelled_ = true; emit closing(); }

QVector<PlayerController::TimelineClip> EditorDialog::playbackClips() const {
    QVector<PlayerController::TimelineClip> clips;
    for (const auto &clip : project_.clips) clips.append({clip.path, clip.inMs, clip.outMs});
    return clips;
}

EditorDialog::State EditorDialog::state() const { return {project_, selected_, positionMs_}; }
void EditorDialog::restore(const State &value) {
    project_ = value.project;
    selected_ = value.selected;
    positionMs_ = value.position;
    refresh();
}

void EditorDialog::commit(const State &before) {
    undo_.append(before);
    if (undo_.size() > 100) undo_.removeFirst();
    redo_.clear();
    refresh();
}

void EditorDialog::refresh(bool preview) {
    selected_ = project_.clips.isEmpty() ? -1 : std::clamp(selected_, 0, int(project_.clips.size()) - 1);
    positionMs_ = std::clamp(positionMs_, qint64(0), project_.duration());
    timeline_->setProject(project_, selected_);
    timeline_->setEnabled(!loadingProject_);
    canvas_->setEnabled(!loadingProject_);
    setPosition(positionMs_);
    if (selected_ >= 0) {
        const auto &clip = project_.clips[selected_];
        clipLabel_->setText(QFileInfo(clip.path).fileName() + "\n\nIn   " + MediaFiles::formatTimePrecise(clip.inMs) +
                            "\nOut  " + MediaFiles::formatTimePrecise(clip.outMs) +
                            "\n\n" + MediaFiles::formatTimePrecise(clip.duration()) + " kept");
    } else clipLabel_->setText("Select a clip on the timeline");
    for (auto *button : {deleteButton_, duplicateButton_}) button->setEnabled(selected_ >= 0 && !loadingProject_);
    splitButton_->setEnabled(!loadingProject_ && !project_.clips.isEmpty() && positionMs_ > 0 && positionMs_ < project_.duration());
    play_->setEnabled(!project_.clips.isEmpty());
    undoButton_->setEnabled(!loadingProject_ && !undo_.isEmpty());
    redoButton_->setEnabled(!loadingProject_ && !redo_.isEmpty());
    exportButton_->setEnabled(!project_.clips.isEmpty() && !importing_ && !exportRunning_);
    if (preview) emit timelineChanged();
}

void EditorDialog::setPosition(qint64 ms) {
    positionMs_ = std::clamp(ms, qint64(0), project_.duration());
    timeline_->setPosition(positionMs_);
    timeLabel_->setText(MediaFiles::formatTimePrecise(positionMs_) + " / " + MediaFiles::formatTimePrecise(project_.duration()));
    splitButton_->setEnabled(!loadingProject_ && positionMs_ > 0 && positionMs_ < project_.duration());
}
void EditorDialog::setPlaying(bool playing) { play_->setText(playing ? "Ⅱ  Pause" : "▶  Play"); }
void EditorDialog::setMessage(const QString &message) { statusLabel_->setText(message); }
void EditorDialog::setProgress(double fraction) { progress_->setValue(qRound(fraction * 1000)); }
void EditorDialog::setExportRunning(bool running) {
    exportRunning_ = running;
    progress_->setVisible(running);
    cancelButton_->setVisible(running);
    progress_->setValue(0);
    refresh(false);
}

void EditorDialog::importPaths(const QStringList &paths, int before) {
    if (paths.isEmpty()) return;
    if (importing_) { setMessage("An import is already running. You can keep editing while it finishes."); return; }
    importing_ = true;
    setMessage("Reading videos and thumbnails…");
    refresh(false);
    importCancelled_ = std::make_shared<std::atomic<bool>>(false);
    const auto cancelled = importCancelled_;
    const QString anchor = before >= 0 && before < project_.clips.size() ? project_.clips[before].id : QString();
    QtConcurrent::run([paths, cancelled] { return probeEditClips(MediaFiles::collectVideoFiles(paths), cancelled.get()); })
        .then(this, [this, anchor](const QVector<EditClip> &clips) {
            importing_ = false;
            if (clips.isEmpty()) { setMessage("No readable local video files were found."); refresh(false); return; }
            const State old = state();
            const bool wasEmpty = project_.clips.isEmpty();
            int at = project_.clips.size();
            for (int i = 0; i < project_.clips.size(); ++i) if (project_.clips[i].id == anchor) { at = i; break; }
            for (int i = 0; i < clips.size(); ++i) project_.clips.insert(at + i, clips[i]);
            selected_ = at;
            positionMs_ = project_.startOf(at);
            commit(old);
            if (wasEmpty) fitTimeline();
            setMessage(QString("Imported %1 clip(s). Drag yellow edges to trim, or press B at the playhead to split.").arg(clips.size()));
        });
}

void EditorDialog::undo() {
    if (loadingProject_ || undo_.isEmpty()) return;
    redo_.append(state());
    restore(undo_.takeLast());
}
void EditorDialog::redo() {
    if (loadingProject_ || redo_.isEmpty()) return;
    undo_.append(state());
    restore(redo_.takeLast());
}
void EditorDialog::split() {
    if (loadingProject_) return;
    const auto before = state();
    const int index = project_.clipAt(positionMs_);
    if (!project_.split(positionMs_)) { setMessage("Place the playhead inside a clip to split it."); return; }
    selected_ = index + 1;
    positionMs_ = project_.startOf(selected_);
    commit(before);
}
void EditorDialog::remove() {
    if (loadingProject_ || selected_ < 0) return;
    const auto before = state();
    positionMs_ = project_.startOf(selected_);
    project_.clips.removeAt(selected_);
    commit(before);
}
void EditorDialog::duplicate() {
    if (loadingProject_ || selected_ < 0) return;
    const auto before = state();
    auto clip = project_.clips[selected_];
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    project_.clips.insert(selected_ + 1, clip);
    ++selected_;
    positionMs_ = project_.startOf(selected_);
    commit(before);
}
void EditorDialog::move(int from, int before) {
    if (loadingProject_) return;
    const auto old = state();
    if (!project_.move(from, before)) return;
    selected_ = before > from ? before - 1 : before;
    positionMs_ = project_.startOf(selected_);
    commit(old);
}
void EditorDialog::fitTimeline() {
    if (project_.duration() > 0) timeline_->setZoom((scroll_->viewport()->width() - 50) * 1000.0 / project_.duration());
}

void EditorDialog::exportVideo() {
    if (project_.clips.isEmpty() || exportRunning_) return;
    QString output = QFileDialog::getSaveFileName(this, "Export edited video", "edited.mp4", "MP4 video (*.mp4)");
    if (output.isEmpty()) return;
    if (QFileInfo(output).suffix().isEmpty()) output += ".mp4";
    if (isSourcePath(project_, output)) {
        setMessage("Choose a new output file; source videos must be preserved.");
        return;
    }
    ExportOptions options;
    options.inputPath = project_.clips.first().path;
    options.outputPath = output;
    // One clip on its own canvas needs no scaling or padding: a plain frame-accurate cut.
    options.outputSize = canvas_->currentData().toSize();
    if (!options.outputSize.isValid() && project_.clips.size() > 1) options.outputSize = project_.clips.first().pictureSize;
    options.outputFrameRate = std::clamp(project_.clips.first().frameRate, 1.0, 60.0);
    options.frameAccurateCut = true;
    options.encoder = static_cast<ExportOptions::Encoder>(encoder_->currentData().toInt());
    options.inputHasAudio = false;
    for (const auto &clip : project_.clips) {
        options.segments.append({clip.inMs / 1000.0, clip.outMs / 1000.0, clip.path,
                                 clip.hasAudio, clip.sourceDurationMs / 1000.0});
        options.inputHasAudio = options.inputHasAudio || clip.hasAudio;
    }
    setMessage("Exporting a snapshot of this timeline. You can continue editing.");
    emit exportRequested(options, project_.duration() / 1000.0);
}

QByteArray EditorDialog::projectJson() const {
    QJsonArray clips;
    for (const auto &clip : project_.clips)
        clips.append(QJsonObject{{"path", clip.path}, {"inMs", clip.inMs}, {"outMs", clip.outMs}});
    return QJsonDocument(QJsonObject{{"version", 1}, {"clips", clips}, {"canvas", canvas_->currentIndex()}}).toJson();
}

bool EditorDialog::saveProject() {
    if (projectPath_.isEmpty())
        projectPath_ = QFileDialog::getSaveFileName(this, "Save editing project", "edit.viplyr.json", "Viplyr project (*.viplyr.json)");
    if (projectPath_.isEmpty()) return false;
    if (QFileInfo(projectPath_).suffix().toLower() != "json") projectPath_ += ".viplyr.json";
    if (isSourcePath(project_, projectPath_)) {
        projectPath_.clear();
        setMessage("Choose a project filename; source videos must be preserved.");
        return false;
    }
    QSaveFile file(projectPath_);
    const QByteArray data = projectJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        setMessage("Could not save the project: " + file.errorString());
        return false;
    }
    savedJson_ = data;
    setMessage("Project saved. Keep its source videos at their original paths.");
    return true;
}

void EditorDialog::loadProject() {
    if (importing_) return;
    if (projectJson() != savedJson_) {
        const auto answer = QMessageBox::question(this, "Open another project", "Save the current project first?",
                                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveProject())) return;
    }
    const QString path = QFileDialog::getOpenFileName(this, "Open editing project", {}, "Viplyr project (*.viplyr.json *.json)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { setMessage(file.errorString()); return; }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto records = document.object().value("clips").toArray();
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        !document.object().value("clips").isArray() || document.object().value("version").toInt() != 1 || records.size() > 1000) {
        setMessage("This is not a supported Viplyr editing project."); return;
    }
    QStringList paths;
    for (const auto record : records) {
        const QString source = record.toObject().value("path").toString();
        if (!QFileInfo(source).isFile()) { setMessage("Source video is missing: " + source); return; }
        paths.append(source);
    }
    importing_ = true;
    setMessage("Opening project and checking source videos…");
    loadingProject_ = true;
    refresh(false);
    const int canvas = document.object().value("canvas").toInt();
    importCancelled_ = std::make_shared<std::atomic<bool>>(false);
    const auto cancelled = importCancelled_;
    QtConcurrent::run([paths, cancelled] { return probeEditClips(paths, cancelled.get()); }).then(this,
        [this, path, records, canvas](QVector<EditClip> clips) {
            importing_ = false;
            loadingProject_ = false;
            if (clips.size() != records.size()) { setMessage("A source video could not be read; the current edit was kept."); refresh(false); return; }
            for (int i = 0; i < clips.size(); ++i) {
                const qint64 in = records[i].toObject().value("inMs").toInteger(-1);
                const qint64 out = records[i].toObject().value("outMs").toInteger(-1);
                if (in < 0 || out <= in || out > clips[i].sourceDurationMs) {
                    setMessage("A clip range is invalid or its source duration changed; the current edit was kept."); refresh(false); return;
                }
                clips[i].inMs = in;
                clips[i].outMs = out;
            }
            project_.clips = clips;
            positionMs_ = 0;
            selected_ = clips.isEmpty() ? -1 : 0;
            undo_.clear(); redo_.clear();
            canvas_->setCurrentIndex(std::clamp(canvas, 0, canvas_->count() - 1));
            projectPath_ = path;
            savedJson_ = projectJson();
            refresh(); fitTimeline();
            setMessage("Project opened.");
        });
}

void EditorDialog::closeEvent(QCloseEvent *event) {
    if (projectJson() != savedJson_) {
        const auto answer = QMessageBox::question(this, "Close editor", "Save this editing project before closing?",
                                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !saveProject())) { event->ignore(); return; }
    }
    event->accept();
}
void EditorDialog::dragEnterEvent(QDragEnterEvent *event) { if (event->mimeData()->hasUrls()) event->acceptProposedAction(); }
void EditorDialog::dropEvent(QDropEvent *event) { importPaths(MediaFiles::localPaths(event->mimeData())); event->acceptProposedAction(); }
