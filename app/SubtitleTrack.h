#pragma once
#include <QString>
#include <QVector>
#include <vector>

#include "viplyr/SubtitleReader.h"

// The subtitles of one video: from the .srt file next to it, or from a track inside it.
class SubtitleTrack {
public:
    // Path of the .srt file that belongs to a video: same folder, same base name.
    static QString externalFileFor(const QString &videoPath);

    // Loads that .srt file. Leaves the track empty when there is none.
    void loadForVideo(const QString &videoPath);
    // Uses cues read from a subtitle track embedded in the video.
    void setCues(const std::vector<viplyr::SubtitleCue> &cues);
    void clear() { cues_.clear(); }
    bool isEmpty() const { return cues_.isEmpty(); }

    // The text to show at this position; empty between cues.
    QString textAt(qint64 milliseconds) const;

private:
    struct Cue {
        qint64 start;  // ms
        qint64 end;    // ms
        QString text;
    };
    static QVector<Cue> parseSrt(const QString &content);

    QVector<Cue> cues_;  // sorted by start time
};
