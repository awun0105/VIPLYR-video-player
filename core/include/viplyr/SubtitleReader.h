#pragma once
#include <atomic>
#include <string>
#include <vector>

namespace viplyr {

struct SubtitleCue {
    double start = 0.0;  // seconds
    double end = 0.0;    // seconds
    std::string text;    // plain text, lines separated by '\n'
};

// Reads a whole text subtitle track that is embedded in a media file (see
// MediaInfo::subtitleTracks). This scans the entire file, so call it from a
// worker thread; set `*cancel` to true from another thread to stop early.
// Bitmap subtitles (DVD, Blu-ray) are not supported and give an empty result.
std::vector<SubtitleCue> readSubtitleTrack(const std::string &url, int streamIndex,
                                           const std::atomic<bool> *cancel = nullptr,
                                           std::string *error = nullptr);

}  // namespace viplyr
