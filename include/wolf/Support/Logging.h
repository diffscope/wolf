#ifndef WOLF_SUPPORT_LOGGING_H
#define WOLF_SUPPORT_LOGGING_H

#include <synthrt/Support/Logging.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// Returns the process-wide log category of wolf.
    ///
    /// The category is separate from the synthrt category, so that a host can adjust the
    /// verbosity of wolf diagnostics without affecting the framework diagnostics. Both categories
    /// share the stdcorelib registry, callback and filter rules.
    WOLF_EXPORT srt::LogCategory &logCategory();

}

#endif // WOLF_SUPPORT_LOGGING_H
