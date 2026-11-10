#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace viplyr {

// One picture of a bitmap subtitle (DVD, Blu-ray PGS, DVB): 8-bit palette indices.
struct SubtitleBitmap {
    int x = 0;  // position on the track's canvas (SubtitleTrackData::canvasWidth/Height)
    int y = 0;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> indices;        // width * height, row by row
    std::vector<uint32_t> palette;       // 0xAARRGGBB per index (straight alpha)
};

struct SubtitleCue {
    double start = 0.0;  // seconds, on the same time line as frame pts
    double end = 0.0;
    std::string text;      // plain text, lines separated by '\n' (text tracks)
    std::string assEvent;  // the raw ASS event line, for ASS/SSA tracks (styled rendering)
    std::vector<SubtitleBitmap> bitmaps;  // for bitmap tracks
};

struct SubtitleTrackData {
    std::vector<SubtitleCue> cues;  // sorted by start time
    bool isAss = false;             // styled ASS/SSA: render assHeader + assEvent with libass
    bool isBitmap = false;
    std::string assHeader;          // [Script Info], [V4+ Styles]... for ASS tracks
    int canvasWidth = 0;            // the space bitmap positions refer to; 0 = the video size
    int canvasHeight = 0;
};

// Reads a whole subtitle track that is embedded in a media file (see
// MediaInfo::subtitleTracks), or the first subtitle track of a subtitle file
// (.srt, .ass, .sup...). This scans the entire file, so call it from a worker
// thread; set `*cancel` to true from another thread to stop early.
SubtitleTrackData readSubtitleTrack(const std::string &url, int streamIndex,
                                    const std::atomic<bool> *cancel = nullptr, std::string *error = nullptr);

}  // namespace viplyr
