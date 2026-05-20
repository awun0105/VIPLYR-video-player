#pragma once
#include <QString>
#include <QList>
#include <QTime>

struct SubtitleItem {
    qint64 startTime; // ms
    qint64 endTime;   // ms
    QString text;
};

class SubtitleParser {
public:
    static QList<SubtitleItem> parse(const QString &filePath);
private:
    static qint64 parseTime(const QString &timeStr);
};
