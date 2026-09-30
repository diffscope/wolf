#ifndef WOLF_LOGGING_H
#define WOLF_LOGGING_H

#include <synthrt/Support/Logging.h>

#include <wolf/Support/SupportGlobal.h>

namespace wolf {

    /// Returns the process-wide log category of wolf.
    ///
    /// The category is separate from the synthrt category, so that a host can adjust the
    /// verbosity of wolf diagnostics without affecting the framework diagnostics. Both categories
    /// share the stdcorelib registry, callback and filter rules.
    WOLF_INTERNAL_EXPORT srt::LogCategory &logCategory();

}

#endif // WOLF_LOGGING_H
