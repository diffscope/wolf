#ifndef WOLF_SUPPORT_ERRORCODE_H
#define WOLF_SUPPORT_ERRORCODE_H

#include <system_error>
#include <type_traits>

#include <synthrt/Support/Error.h>

#include <wolf/wolf_global.h>

namespace wolf {

    /// Error conditions owned by wolf rather than by the SynthRT framework.
    enum class ErrorCode {
        /// No worker thread could be started for an asynchronous execution.
        NoWorker = 1,

        /// A resource parser failed by throwing instead of returning its documented error.
        ResourceInvalid = 2,
    };

    /// Returns the category that supplies the names and messages of these codes.
    WOLF_EXPORT const std::error_category &errorCategory() noexcept;

    /// Returns the std::error_code of \a code in errorCategory().
    inline std::error_code make_error_code(ErrorCode code) noexcept {
        return {static_cast<int>(code), errorCategory()};
    }

}

template <>
struct std::is_error_code_enum<wolf::ErrorCode> : std::true_type {};

#endif // WOLF_SUPPORT_ERRORCODE_H
