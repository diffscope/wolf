#ifndef WOLF_LOGGING_H
#define WOLF_LOGGING_H

#include <synthrt/Support/Logging.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// The process-wide log category wolf writes to.
    ///
    /// Its own rather than synthrt's, so a host can turn wolf's diagnostics up or down without
    /// touching the framework's. The two share stdcorelib's registry, callback and filter rules.
    WOLF_EXPORT srt::LogCategory &logCategory();

}

#endif // WOLF_LOGGING_H
