#ifndef WOLF_SUPPORTGLOBAL_H
#define WOLF_SUPPORTGLOBAL_H

#include <wolf/wolf_global.h>

/// Marks a symbol that libwolf exports only for the plugins built in this repository.
///
/// The Support headers are not installed. No symbol declared with this macro is part of the public
/// API of wolf, and no such symbol is covered by any ABI guarantee, before 1.0 or after it. The
/// macro applies to a symbol for one reason only: the symbol holds state that must exist once per
/// process and be shared by every plugin that wolf ships, and it therefore cannot be compiled into
/// each plugin separately. The resource cache is the principal example. Two plugins that read one
/// dictionary share one parse only because both reach the same cache in libwolf, and the cache
/// re-owns every parse product behind a control block of libwolf because the cache outlives the
/// plugins that produced the products.
///
/// Stateless helpers (manifest and contract readers, the verifier, file errors, input rules) are
/// not exported. They are compiled into the static library wolfsupport, which libwolf and each
/// plugin link privately, with hidden visibility.
#define WOLF_INTERNAL_EXPORT WOLF_EXPORT

#endif // WOLF_SUPPORTGLOBAL_H
