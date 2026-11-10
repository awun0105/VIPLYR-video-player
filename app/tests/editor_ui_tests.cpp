// Run on a display: editor_ui_tests <test_clip.mp4> <test_tracks.mkv> [screenshot.png]
#include <QApplication>
#include <QCryptographicHash>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QMimeData>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cstdio>
#include <cmath>
#include <functional>
#include "EditorDialog.h"
#include "EditTimeline.h"

namespace {
int failures = 0;
void check(bool ok, const char *label) {
    if (!ok) { ++failures; std::printf("FAILED: %s\n", label); }
}
bool waitFor(const std::function<bool()> &done, int timeout = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeout) QTest::qWait(20);
    return done();
}
QPushButton *button(EditorDialog &editor, const QString &text) {
    for (auto *b : editor.findChildren<QPushButton *>()) if (b->text() == text) return b;
    return nullptr;
}
void selectFileWhenDialogOpens(const QString &path) {
    QTimer::singleShot(100, qApp, [path] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        check(dialog != nullptr, "file dialog is active");
        if (!dialog) return;
        dialog->setOption(QFileDialog::DontConfirmOverwrite);
        dialog->setDirectory(QFileInfo(path).absolutePath());
        dialog->selectFile(QFileInfo(path).fileName());
        QTimer::singleShot(200, dialog, [dialog, path] {
            auto *name = dialog->findChild<QLineEdit *>("fileNameEdit");
            check(name != nullptr, "file dialog filename field exists");
            if (name) name->setText(path);
            QMetaObject::invokeMethod(dialog, "accept");
        });
    });
}
}

int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QApplication::setApplicationName("ViplyrEditorTests");
    if (argc < 3) return 2;
    const QString source = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QString other = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    const auto metadata = probeEditClips({source, other});
    check(metadata.size() == 2 && metadata[0].hasAudio && metadata[1].hasAudio, "import detects audio tracks without opening an audio decoder");
    QTemporaryDir folder;
    EditorDialog editor;
    editor.show();
    editor.activateWindow();
    editor.importPaths({source});
    check(waitFor([&] { return editor.playbackClips().size() == 1; }), "initial import");
    auto *timeline = editor.findChild<EditTimeline *>("editorTimeline");
    check(timeline != nullptr, "timeline widget exists");
    if (!timeline) return 1;
    const auto before = editor.playbackClips().first();
    const int right = 24 + qRound((before.outMs - before.inMs) / 1000.0 * timeline->zoom());
    QTest::mousePress(timeline, Qt::LeftButton, {}, QPoint(right - 2, 105));
    QTest::mouseMove(timeline, QPoint(right - 92, 105), 40);
    QTest::mouseRelease(timeline, Qt::LeftButton, {}, QPoint(right - 92, 105));
    check(editor.playbackClips().first().outMs < before.outMs && editor.playbackClips().first().inMs == 0,
          "drag right handle trims the end");
    button(editor, "Undo")->click();
    QTest::mousePress(timeline, Qt::LeftButton, {}, QPoint(25, 105));
    QTest::mouseMove(timeline, QPoint(125, 105), 40);
    QTest::mouseRelease(timeline, Qt::LeftButton, {}, QPoint(125, 105));
    auto clips = editor.playbackClips();
    check(clips.first().inMs > before.inMs && clips.first().outMs == before.outMs, "drag left handle trims the original range");
    const qint64 trimmedDuration = clips.first().outMs - clips.first().inMs;
    const int middle = 24 + qRound(trimmedDuration / 2000.0 * timeline->zoom());
    QTest::mousePress(timeline, Qt::LeftButton, {}, QPoint(24, 38));
    QTest::mouseMove(timeline, QPoint(middle, 90), 40);
    QTest::mouseRelease(timeline, Qt::LeftButton, {}, QPoint(middle, 90));
    editor.findChild<QPushButton *>("editorSplit")->click();
    clips = editor.playbackClips();
    check(clips.size() == 2 && clips[0].outMs == clips[1].inMs, "playhead split creates adjacent cuts");
    button(editor, "Undo")->click();
    check(editor.playbackClips().size() == 1, "undo split");
    button(editor, "Redo")->click();
    check(editor.playbackClips().size() == 2, "redo split");

    QMimeData files;
    files.setUrls({QUrl::fromLocalFile(other)});
    QDragEnterEvent enter(QPoint(timeline->width() - 3, 100), Qt::CopyAction, &files, Qt::LeftButton, {});
    QApplication::sendEvent(timeline, &enter);
    QDropEvent drop(QPointF(timeline->width() - 3, 100), Qt::CopyAction, &files, Qt::LeftButton, {});
    QApplication::sendEvent(timeline, &drop);
    check(waitFor([&] { return editor.playbackClips().size() == 3; }), "drop another source onto timeline");
    check(editor.playbackClips().last().path == other, "dropped source is appended at chosen boundary");
    editor.importPaths({other}, 1);
    check(waitFor([&] { return editor.playbackClips().size() == 4; }), "insert another video between existing cuts");
    check(editor.playbackClips()[1].path == other && editor.playbackClips()[2].path == source, "insert retains surrounding source order");
    button(editor, "Undo")->click();
    button(editor, "Duplicate clip")->click();
    check(editor.playbackClips().size() == 4 && editor.playbackClips().last().path == other, "duplicate selected clip");
    button(editor, "Delete clip  ·  Del")->click();
    check(editor.playbackClips().size() == 3, "delete clip without deleting source");
    button(editor, "Fit timeline")->click();

    const QString projectPath = folder.filePath("edit.viplyr.json");
    selectFileWhenDialogOpens(projectPath);
    button(editor, "Save project")->click();
    QFile saved(projectPath);
    check(saved.open(QIODevice::ReadOnly), "project saved");
    const QByteArray originalProject = saved.readAll();
    check(QJsonDocument::fromJson(originalProject).object().value("clips").toArray().size() == 3, "saved project contains ordered cuts");
    selectFileWhenDialogOpens(projectPath);
    button(editor, "Open project…")->click();
    check(waitFor([&] { return editor.playbackClips().size() == 3 && !button(editor, "Undo")->isEnabled() &&
                              editor.findChild<QPushButton *>("editorExport")->isEnabled(); }), "project loads and resets undo history");
    check(editor.playbackClips().first().inMs == clips.first().inMs, "loaded trim retained");

    int exports = 0;
    ExportOptions captured;
    QObject::connect(&editor, &EditorDialog::exportRequested, [&](const ExportOptions &options, double) { ++exports; captured = options; });
    selectFileWhenDialogOpens(folder.filePath("output.mp4"));
    editor.findChild<QPushButton *>("editorExport")->click();
    check(exports == 1 && captured.segments.size() == 3 && captured.outputSize.isValid(), "export snapshots the ordered timeline");
    selectFileWhenDialogOpens(source);
    editor.findChild<QPushButton *>("editorExport")->click();
    check(exports == 1, "source overwrite is rejected");
    if (argc > 3) editor.grab().save(QString::fromLocal8Bit(argv[3]));

    std::printf("multi-source timeline playback\n");
    const QString resumeKey = "resume/" + QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha1).toHex());
    QSettings().setValue(resumeKey, 123456);
    {
        PlayerController preview(nullptr, false);
        preview.setVolume(0);
        QStringList opened;
        qint64 latest = 0;
        QObject::connect(&preview, &PlayerController::mediaOpened, [&](const QString &path) { opened.append(path); });
        QObject::connect(&preview, &PlayerController::timelinePositionChanged, [&](qint64 ms) { latest = ms; });
        preview.setTimeline({{source, 1000, 1500}, {other, 100, 600}, {source, 2000, 2500}});
        check(waitFor([&] { return preview.hasMedia(); }), "preview opens at source in point");
        preview.toggleTimeline();
        check(waitFor([&] { return latest == 1500 && !preview.isPlaying(); }, 8000), "preview traverses sources and stops at timeline end");
        check(opened == QStringList{source, other, source}, "preview source order matches timeline");
        preview.seekTimeline(600);
        check(waitFor([&] { return preview.hasMedia() && preview.currentPath() == other; }), "seek on global timeline selects correct source");
        check(!preview.isPlaying() && std::abs(preview.position() - 200) < 100, "timeline offset maps to source offset while paused");
        preview.setTimeline({{source, 0, 1000}});
        preview.toggleTimeline();
        preview.toggleTimeline();
        check(waitFor([&] { return preview.hasMedia() && preview.currentPath() == source; }), "rapid playback requests wait for the latest open");
        check(!preview.isPlaying(), "second play/pause click during loading cancels autoplay");
        preview.setTimeline({});
        check(!preview.hasMedia(), "empty timeline closes preview");
    }
    check(QSettings().value(resumeKey).toLongLong() == 123456, "editor does not overwrite watch resume position");
    QSettings().remove(resumeKey);
    std::printf(failures ? "%d FAILURE(S)\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
