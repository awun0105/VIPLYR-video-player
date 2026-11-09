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

}  // namespace

std::vector<SubtitleCue> readSubtitleTrack(const std::string &url, int streamIndex,
                                           const std::atomic<bool> *cancel, std::string *error) {
    std::vector<SubtitleCue> cues;
    AVFormatContext *format = openInput(url, {}, error);
    if (!format) return cues;

    const bool validIndex = streamIndex >= 0 && streamIndex < static_cast<int>(format->nb_streams) &&
                            format->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE;
    AVCodecContext *decoder = validIndex ? openDecoder(format->streams[streamIndex], HwAccel::None,
                                                       nullptr, error)
                                         : nullptr;
    if (!decoder) {
        if (error && error->empty()) *error = "Not a subtitle track";
        avformat_close_input(&format);
        return cues;
    }

    // Let the demuxer skip the audio and video data: only the subtitle packets matter.
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        format->streams[i]->discard = static_cast<int>(i) == streamIndex ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }

    const AVStream *stream = format->streams[streamIndex];
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
                cue.end = subtitle.end_display_time > 0
                              ? packetTime + subtitle.end_display_time / 1000.0
                              : packetTime + toSeconds(packet->duration, stream->time_base);
                cue.text = textOf(subtitle);
                if (!cue.text.empty() && cue.end > cue.start) cues.push_back(std::move(cue));
                avsubtitle_free(&subtitle);
            }
        }
        av_packet_unref(packet.get());
    }

    avcodec_free_context(&decoder);
    avformat_close_input(&format);
    std::sort(cues.begin(), cues.end(),
              [](const SubtitleCue &a, const SubtitleCue &b) { return a.start < b.start; });
    return cues;
}

}  // namespace viplyr
