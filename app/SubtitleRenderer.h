#pragma once
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QSize>
#include <memory>

#include "viplyr/SubtitleReader.h"

struct ass_library;
struct ass_renderer;
struct ass_track;

// Draws subtitles that are pictures rather than plain text: styled ASS/SSA
// (rendered with libass: fonts, colours, positions, karaoke...) and bitmap
// subtitles (DVD, Blu-ray PGS, DVB). Plain text subtitles are shown as a label
// by VideoWidget instead.
class SubtitleRenderer {
public:
    SubtitleRenderer();
    ~SubtitleRenderer();
    SubtitleRenderer(const SubtitleRenderer &) = delete;
    SubtitleRenderer &operator=(const SubtitleRenderer &) = delete;

    void setTrack(std::shared_ptr<const viplyr::SubtitleTrackData> track);  // null clears
    bool isEmpty() const { return !track_; }

    // Draws what is visible at `seconds`. The painter maps video pixels (0,0)-(videoSize)
    // onto the picture on screen; `screenSize` is the picture's size on screen, the
    // resolution styled text is rendered at.
    void draw(QPainter &painter, double seconds, QSize videoSize, QSize screenSize);

private:
    void drawBitmaps(QPainter &painter, double seconds);
    void drawStyled(QPainter &painter, double seconds, QSize screenSize);
    const QImage &bitmapImage(int cueIndex, int bitmapIndex);

    std::shared_ptr<const viplyr::SubtitleTrackData> track_;
    QSize videoSize_;

    // Bitmap subtitles, converted to QImage the first time each is shown.
    QHash<qint64, QImage> bitmapCache_;

    // libass state for a styled track.
    ass_library *library_ = nullptr;
    ass_renderer *renderer_ = nullptr;
    ass_track *assTrack_ = nullptr;
    QImage styledImage_;     // the last rendering, reused while libass reports no change
    QSize styledSize_;
};
