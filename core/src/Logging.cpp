#include "viplyr/Logging.h"

extern "C" {
#include <libavutil/log.h>
}

namespace viplyr {

void setLogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Quiet:   av_log_set_level(AV_LOG_QUIET); break;
        case LogLevel::Error:   av_log_set_level(AV_LOG_ERROR); break;
        case LogLevel::Warning: av_log_set_level(AV_LOG_WARNING); break;
        case LogLevel::Info:    av_log_set_level(AV_LOG_INFO); break;
        case LogLevel::Debug:   av_log_set_level(AV_LOG_DEBUG); break;
    }
}

}  // namespace viplyr
