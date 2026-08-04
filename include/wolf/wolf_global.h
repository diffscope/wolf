#ifndef WOLF_WOLF_GLOBAL_H
#define WOLF_WOLF_GLOBAL_H

#include <stdcorelib/stdc_global.h>

#ifndef WOLF_EXPORT
#  ifdef WOLF_STATIC
#    define WOLF_EXPORT
#  else
#    ifdef WOLF_LIBRARY
#      define WOLF_EXPORT STDCORELIB_DECL_EXPORT
#    else
#      define WOLF_EXPORT STDCORELIB_DECL_IMPORT
#    endif
#  endif
#endif

#endif // WOLF_WOLF_GLOBAL_H
