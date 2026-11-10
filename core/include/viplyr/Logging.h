#pragma once

namespace viplyr {

// How much FFmpeg (and so the engine's decoders and encoders) prints to stderr.
// This is process-wide. FFmpeg's default is Info.
enum class LogLevel { Quiet, Error, Warning, Info, Debug };

void setLogLevel(LogLevel level);

}  // namespace viplyr
