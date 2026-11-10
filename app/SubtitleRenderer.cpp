#include "SubtitleRenderer.h"

#include <algorithm>

extern "C" {
#include <ass/ass.h>
}

namespace {

constexpr int kMaxRenderSide = 4096;

}  // namespace

SubtitleRenderer::SubtitleRenderer() = default;

SubtitleRenderer::~SubtitleRenderer() { setTrack(nullptr); }

void SubtitleRenderer::setTrack(std::shared_ptr<const viplyr::SubtitleTrackData> track) {
    if (assTrack_) ass_free_track(assTrack_);
    assTrack_ = nullptr;
    if (renderer_) ass_renderer_done(renderer_);
    renderer_ = nullptr;
    if (library_) ass_library_done(library_);
    library_ = nullptr;
    bitmapCache_.clear();
    styledImage_ = QImage();
    styledSize_ = QSize();

    track_ = std::move(track);
    videoSize_ = QSize();
    if (!track_ || !track_->isAss) return;

    library_ = ass_library_init();
    renderer_ = library_ ? ass_renderer_init(library_) : nullptr;
    if (!renderer_) return;
    // Fonts the script names, else the system's sans-serif (found through fontconfig).
    ass_set_fonts(renderer_, nullptr, "sans-serif", ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
    assTrack_ = ass_new_track(library_);
    std::string header = track_->assHeader;
    ass_process_codec_private(assTrack_, header.data(), static_cast<int>(header.size()));
    for (const viplyr::SubtitleCue &cue : track_->cues) {
        if (cue.assEvent.empty()) continue;
        // One cue can hold several events, one per line.
        size_t begin = 0;
        while (begin < cue.assEvent.size()) {
            size_t end = cue.assEvent.find('\n', begin);
            if (end == std::string::npos) end = cue.assEvent.size();
            std::string event = cue.assEvent.substr(begin, end - begin);
            ass_process_chunk(assTrack_, event.data(), static_cast<int>(event.size()),
                              static_cast<long long>(cue.start * 1000.0),
                              static_cast<long long>((cue.end - cue.start) * 1000.0));
            begin = end + 1;
        }
    }
}

void SubtitleRenderer::draw(QPainter &painter, double seconds, QSize videoSize, QSize screenSize) {
    if (!track_ || videoSize.isEmpty()) return;
    if (videoSize != videoSize_) {
        videoSize_ = videoSize;
        styledSize_ = QSize();  // makes libass take the new storage size
    }
    if (track_->isAss) drawStyled(painter, seconds, screenSize);
    else if (track_->isBitmap) drawBitmaps(painter, seconds);
}

// ------------------------------------------------------------------- bitmaps

const QImage &SubtitleRenderer::bitmapImage(int cueIndex, int bitmapIndex) {
    const qint64 key = (static_cast<qint64>(cueIndex) << 8) | bitmapIndex;
    auto found = bitmapCache_.find(key);
    if (found != bitmapCache_.end()) return *found;

    const viplyr::SubtitleBitmap &bitmap = track_->cues[static_cast<size_t>(cueIndex)].bitmaps[static_cast<size_t>(bitmapIndex)];
    // Palette pictures map straight onto QImage's indexed format: no pixel conversion.
    QImage image(bitmap.indices.data(), bitmap.width, bitmap.height, bitmap.width, QImage::Format_Indexed8);
    QVector<QRgb> colors(bitmap.palette.begin(), bitmap.palette.end());
    image.setColorTable(colors);
    return *bitmapCache_.insert(key, image.copy());  // copy(): own the pixels
}

void SubtitleRenderer::drawBitmaps(QPainter &painter, double seconds) {
    const std::vector<viplyr::SubtitleCue> &cues = track_->cues;
    // Cues are sorted by start; look back from the last one that has started.
    const auto after = std::upper_bound(cues.begin(), cues.end(), seconds,
                                        [](double time, const viplyr::SubtitleCue &cue) { return time < cue.start; });
    const int canvasWidth = track_->canvasWidth > 0 ? track_->canvasWidth : videoSize_.width();
    const int canvasHeight = track_->canvasHeight > 0 ? track_->canvasHeight : videoSize_.height();
    const double scaleX = static_cast<double>(videoSize_.width()) / canvasWidth;
    const double scaleY = static_cast<double>(videoSize_.height()) / canvasHeight;

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    int checked = 0;
    for (auto cue = after; cue != cues.begin() && checked < 8; ++checked) {
        --cue;
        if (seconds >= cue->end) continue;
        const int cueIndex = static_cast<int>(cue - cues.begin());
        for (size_t i = 0; i < cue->bitmaps.size(); ++i) {
            const viplyr::SubtitleBitmap &bitmap = cue->bitmaps[i];
            const QRectF target(bitmap.x * scaleX, bitmap.y * scaleY, bitmap.width * scaleX, bitmap.height * scaleY);
            painter.drawImage(target, bitmapImage(cueIndex, static_cast<int>(i)));
        }
    }
    painter.restore();
}

// -------------------------------------------------------------------- styled

void SubtitleRenderer::drawStyled(QPainter &painter, double seconds, QSize screenSize) {
    if (!renderer_ || !assTrack_ || screenSize.isEmpty()) return;
    const QSize size = screenSize.boundedTo(QSize(kMaxRenderSide, kMaxRenderSide));
    if (size != styledSize_) {
        ass_set_storage_size(renderer_, videoSize_.width(), videoSize_.height());
        ass_set_frame_size(renderer_, size.width(), size.height());
        styledSize_ = size;
        styledImage_ = QImage();
    }

    int changed = 0;
    ASS_Image *parts = ass_render_frame(renderer_, assTrack_, static_cast<long long>(seconds * 1000.0), &changed);
    if (changed || styledImage_.isNull()) {
        styledImage_ = QImage(size, QImage::Format_ARGB32_Premultiplied);
        styledImage_.fill(Qt::transparent);
        // Each part is an alpha mask in one colour (0xRRGGBBTT, TT = transparency).
        for (const ASS_Image *part = parts; part; part = part->next) {
            const uint32_t red = part->color >> 24, green = (part->color >> 16) & 0xFF, blue = (part->color >> 8) & 0xFF;
            const uint32_t opacity = 255 - (part->color & 0xFF);
            for (int y = 0; y < part->h; ++y) {
                const int row = part->dst_y + y;
                if (row < 0 || row >= size.height()) continue;
                auto *destination = reinterpret_cast<QRgb *>(styledImage_.scanLine(row));
                const unsigned char *mask = part->bitmap + static_cast<size_t>(y) * part->stride;
                for (int x = 0; x < part->w; ++x) {
                    const int column = part->dst_x + x;
                    if (column < 0 || column >= size.width() || mask[x] == 0) continue;
                    const uint32_t alpha = mask[x] * opacity / 255;
                    const QRgb below = destination[column];
                    const uint32_t keep = 255 - alpha;
                    destination[column] = qRgba(static_cast<int>((red * alpha + qRed(below) * keep) / 255),
                                                static_cast<int>((green * alpha + qGreen(below) * keep) / 255),
                                                static_cast<int>((blue * alpha + qBlue(below) * keep) / 255),
                                                static_cast<int>(alpha + qAlpha(below) * keep / 255));
                }
            }
        }
    }
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRectF(QPointF(0, 0), QSizeF(videoSize_)), styledImage_);
    painter.restore();
}
