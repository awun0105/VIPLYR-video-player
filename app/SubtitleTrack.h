#pragma once
#include <QString>
#include <QVector>

// The subtitles of one video, loaded from an .srt file.
class SubtitleTrack {
public:
    // Loads the .srt file that sits next to the video and has the same base name.
    // Leaves the track empty when there is none.
    void loadForVideo(const QString &videoPath);
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
