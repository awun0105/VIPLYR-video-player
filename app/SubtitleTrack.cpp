#include "SubtitleTrack.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>

namespace {

// "01:02:03,456" -> milliseconds. Hours may exceed 23.
qint64 parseTimestamp(const QString &text) {
    const QStringList clock = text.left(text.indexOf(',')).split(':');
    if (clock.size() != 3) return 0;
    const qint64 seconds = clock[0].toLongLong() * 3600 + clock[1].toLongLong() * 60 + clock[2].toLongLong();
    return seconds * 1000 + text.mid(text.indexOf(',') + 1).toLongLong();
}

}  // namespace

QString SubtitleTrack::externalFileFor(const QString &videoPath) {
    const QFileInfo video(videoPath);
    return video.path() + "/" + video.completeBaseName() + ".srt";
}

void SubtitleTrack::setCues(const std::vector<viplyr::SubtitleCue> &cues) {
    cues_.clear();
    for (const viplyr::SubtitleCue &cue : cues) {
        cues_.append({qRound64(cue.start * 1000.0), qRound64(cue.end * 1000.0),
                      QString::fromStdString(cue.text)});
    }
}

void SubtitleTrack::loadForVideo(const QString &videoPath) {
    cues_.clear();
    QFile file(externalFileFor(videoPath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    cues_ = parseSrt(QTextStream(&file).readAll());
    std::sort(cues_.begin(), cues_.end(), [](const Cue &a, const Cue &b) { return a.start < b.start; });
}

QVector<SubtitleTrack::Cue> SubtitleTrack::parseSrt(const QString &content) {
    // One block: an index line, a "start --> end" line, then text up to the next block.
    static const QRegularExpression blockPattern(
        "\\d+\\s+(\\d{2,}:\\d{2}:\\d{2},\\d{3})\\s*-->\\s*(\\d{2,}:\\d{2}:\\d{2},\\d{3})[^\\n]*\\n"
        "(.*?)(?=\\n\\s*\\n\\s*\\d+\\s+\\d{2,}:\\d{2}:\\d{2},\\d{3}|\\s*$)",
        QRegularExpression::DotMatchesEverythingOption);

    QVector<Cue> cues;
    QRegularExpressionMatchIterator matches = blockPattern.globalMatch(content);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        cues.append({parseTimestamp(match.captured(1)), parseTimestamp(match.captured(2)),
                     match.captured(3).trimmed()});
    }
    return cues;
}

QString SubtitleTrack::textAt(qint64 milliseconds) const {
    // Find the last cue that starts at or before this position.
    auto after = std::upper_bound(cues_.begin(), cues_.end(), milliseconds,
                                  [](qint64 position, const Cue &cue) { return position < cue.start; });
    if (after == cues_.begin()) return QString();
    const Cue &cue = *std::prev(after);
    return milliseconds <= cue.end ? cue.text : QString();
}
