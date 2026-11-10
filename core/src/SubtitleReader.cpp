#include "viplyr/SubtitleReader.h"

#include <algorithm>

#include "CodecUtil.h"

namespace viplyr {

namespace {

// An ASS event looks like "ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text".
// Returns the text with override tags ({\i1}) removed and \N turned into line breaks.
std::string plainTextFromAss(const std::string &event) {
    size_t textStart = 0;
    for (int comma = 0; comma < 8; ++comma) {
        const size_t next = event.find(',', textStart);
        if (next == std::string::npos) break;
        textStart = next + 1;
    }

    std::string text;
    bool insideTag = false;
    for (size_t i = textStart; i < event.size(); ++i) {
        const char c = event[i];
        if (c == '{') insideTag = true;
        else if (c == '}') insideTag = false;
        else if (insideTag) continue;
        else if (c == '\\' && i + 1 < event.size() && (event[i + 1] == 'N' || event[i + 1] == 'n')) {
            text += '\n';
            ++i;
        } else {
            text += c;
        }
    }
    return text;
}

std::string textOf(const AVSubtitle &subtitle) {
    std::string text;
    for (unsigned i = 0; i < subtitle.num_rects; ++i) {
        const AVSubtitleRect *rect = subtitle.rects[i];
        std::string line;
        if (rect->type == SUBTITLE_ASS && rect->ass) line = plainTextFromAss(rect->ass);
        else if (rect->type == SUBTITLE_TEXT && rect->text) line = rect->text;
        if (line.empty()) continue;
        if (!text.empty()) text += '\n';
        text += line;
    }
    return text;
}


std::vector<SubtitleBitmap> bitmapsOf(const AVSubtitle &subtitle) {
    std::vector<SubtitleBitmap> bitmaps;
    for (unsigned i = 0; i < subtitle.num_rects; ++i) {
        const AVSubtitleRect *rect = subtitle.rects[i];
        if (rect->type != SUBTITLE_BITMAP || rect->w <= 0 || rect->h <= 0 || !rect->data[0]) continue;
        SubtitleBitmap bitmap;
        bitmap.x = rect->x;
        bitmap.y = rect->y;
        bitmap.width = rect->w;
        bitmap.height = rect->h;
        bitmap.indices.resize(static_cast<size_t>(rect->w) * rect->h);
        for (int row = 0; row < rect->h; ++row) {
            std::copy_n(rect->data[0] + static_cast<size_t>(row) * rect->linesize[0], rect->w,
                        bitmap.indices.begin() + static_cast<ptrdiff_t>(row) * rect->w);
        }
        const auto *palette = reinterpret_cast<const uint32_t *>(rect->data[1]);
        bitmap.palette.assign(palette, palette + std::max(0, std::min(rect->nb_colors, 256)));
        bitmap.palette.resize(256, 0);  // indices past nb_colors are transparent
        bitmaps.push_back(std::move(bitmap));
    }
    return bitmaps;
}

std::string assEventsOf(const AVSubtitle &subtitle) {
    std::string events;
    for (unsigned i = 0; i < subtitle.num_rects; ++i) {
        const AVSubtitleRect *rect = subtitle.rects[i];
        if (rect->type != SUBTITLE_ASS || !rect->ass) continue;
        if (!events.empty()) events += '\n';
        events += rect->ass;
    }
    return events;
}

}  // namespace

SubtitleTrackData readSubtitleTrack(const std::string &url, int streamIndex, const std::atomic<bool> *cancel,
                                    std::string *error) {
    SubtitleTrackData track;
    AVFormatContext *format = openInput(url, {}, error);
    if (!format) return track;

    // A subtitle file has one track; take it when no index is given.
    if (streamIndex < 0) streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_SUBTITLE, -1, -1, nullptr, 0);
    const bool validIndex = streamIndex >= 0 && streamIndex < static_cast<int>(format->nb_streams) &&
                            format->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE;
    AVCodecContext *decoder = validIndex ? openDecoder(format->streams[streamIndex], HwAccel::None,
                                                       nullptr, error)
                                         : nullptr;
    if (!decoder) {
        if (error && error->empty()) *error = "Not a subtitle track";
        avformat_close_input(&format);
        return track;
    }

    const AVStream *stream = format->streams[streamIndex];
    const AVCodecID codecId = stream->codecpar->codec_id;
    track.isAss = codecId == AV_CODEC_ID_ASS || codecId == AV_CODEC_ID_SSA;
    track.isBitmap = (avcodec_descriptor_get(codecId)->props & AV_CODEC_PROP_BITMAP_SUB) != 0;
    if (track.isAss && decoder->subtitle_header) {
        track.assHeader.assign(reinterpret_cast<const char *>(decoder->subtitle_header),
                               static_cast<size_t>(decoder->subtitle_header_size));
    }
    track.canvasWidth = decoder->width;
    track.canvasHeight = decoder->height;

    // Let the demuxer skip the audio and video data: only the subtitle packets matter.
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        format->streams[i]->discard = static_cast<int>(i) == streamIndex ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }

    // Use the same time origin as Player and FrameReader, so cues line up with the picture.
    const int videoIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const double startOffset =
        startOffsetSeconds(format, videoIndex >= 0 ? format->streams[videoIndex] : nullptr);
    PacketPtr packet(av_packet_alloc());

    while (!(cancel && cancel->load()) && av_read_frame(format, packet.get()) >= 0) {
        if (packet->stream_index == streamIndex && packet->pts != AV_NOPTS_VALUE) {
            AVSubtitle subtitle = {};
            int gotSubtitle = 0;
            if (avcodec_decode_subtitle2(decoder, &subtitle, &gotSubtitle, packet.get()) >= 0 && gotSubtitle) {
                const double packetTime = toSeconds(packet->pts, stream->time_base) - startOffset;
                SubtitleCue cue;
                cue.start = packetTime + subtitle.start_display_time / 1000.0;
                // Some formats give the length on the packet instead of the subtitle.
                cue.end = subtitle.end_display_time > 0 && subtitle.end_display_time != UINT32_MAX
                              ? packetTime + subtitle.end_display_time / 1000.0
                              : packetTime + toSeconds(packet->duration, stream->time_base);
                cue.text = textOf(subtitle);
                if (track.isAss) cue.assEvent = assEventsOf(subtitle);
                cue.bitmaps = bitmapsOf(subtitle);
                const bool hasContent = !cue.text.empty() || !cue.bitmaps.empty();
                // A bitmap track clears the screen with an empty subtitle, which also ends
                // the previous one when its length was not known.
                if (track.isBitmap && !track.cues.empty() && track.cues.back().end <= track.cues.back().start) {
                    track.cues.back().end = cue.start;
                }
                if (hasContent && (cue.end > cue.start || track.isBitmap)) track.cues.push_back(std::move(cue));
                avsubtitle_free(&subtitle);
            }
        }
        av_packet_unref(packet.get());
    }

    // A last bitmap cue without a length stays up for a few seconds.
    if (track.isBitmap && !track.cues.empty() && track.cues.back().end <= track.cues.back().start) {
        track.cues.back().end = track.cues.back().start + 5.0;
    }
    avcodec_free_context(&decoder);
    avformat_close_input(&format);
    std::sort(track.cues.begin(), track.cues.end(),
              [](const SubtitleCue &a, const SubtitleCue &b) { return a.start < b.start; });
    return track;
}

}  // namespace viplyr
