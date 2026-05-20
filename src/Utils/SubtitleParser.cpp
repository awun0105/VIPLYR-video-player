#include "SubtitleParser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

QList<SubtitleItem> SubtitleParser::parse(const QString &filePath) {
    QList<SubtitleItem> subs;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return subs;

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    // Regex to match SRT block: index, time range, and text
    // Format: \d+\n(\d{2}:\d{2}:\d{2},\d{3}) --> (\d{2}:\d{2}:\d{2},\d{3})\n(.*?)\n\n
    // Use multi-line and greedy matching for text
    QRegularExpression re("(\\d+)\\s+(\\d{2}:\\d{2}:\\d{2},\\d{3})\\s*-->\\s*(\\d{2}:\\d{2}:\\d{2},\\d{3})\\s*(.*?)(?=\\n\\s*\\d+\\s+\\d{2}:\\d{2}:\\d{2},\\d{3}|\\s*$)", 
                          QRegularExpression::DotMatchesEverythingOption);

    QRegularExpressionMatchIterator i = re.globalMatch(content);
    while (i.hasNext()) {
        QRegularExpressionMatch match = i.next();
        SubtitleItem item;
        item.startTime = parseTime(match.captured(2));
        item.endTime = parseTime(match.captured(3));
        item.text = match.captured(4).trimmed();
        subs.append(item);
    }

    return subs;
}

qint64 SubtitleParser::parseTime(const QString &timeStr) {
    // Format: 00:00:01,000
    QString cleaned = timeStr.trimmed();
    QTime time = QTime::fromString(cleaned.left(8), "HH:mm:ss");
    int ms = cleaned.mid(9).toInt();
    return (qint64)time.msecsSinceStartOfDay() + ms;
}
