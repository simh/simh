/* public domain */

#ifndef COMPILER_H
#define COMPILER_H

#include "config-host.h"

/*----------------------------------------------------------------------------
| The macro QEMU_GNUC_PREREQ tests for minimum version of the GNU C compiler.
| The code is a copy of SOFTFLOAT_GNUC_PREREQ, see softfloat-macros.h.
*----------------------------------------------------------------------------*/
#if defined(__GNUC__) && defined(__GNUC_MINOR__)
# define QEMU_GNUC_PREREQ(maj, min) \
         ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
# define QEMU_GNUC_PREREQ(maj, min) 0
#endif

#ifndef container_of
#define container_of(ptr, type, member)                         \
        ((type *) ((char *)(ptr) - offsetof(type, member)))
#endif

#ifndef always_inline
#if !((__GNUC__ < 3) || defined(__APPLE__))
#ifdef __OPTIMIZE__
#undef inline
#define inline __attribute__ (( always_inline )) __inline__
#endif
#endif
#else
#undef inline
#define inline always_inline
#endif
#ifdef _MSC_VER
#undef inline
#define inline __inline
#endif

#define register

#if defined __GNUC__
# if !QEMU_GNUC_PREREQ(4, 4)
   /* gcc versions before 4.4.x don't support gnu_printf, so use printf. */
#  define GCC_FMT_ATTR(n, m) __attribute__((format(printf, n, m)))
# else
   /* Use gnu_printf when supported (qemu uses standard format strings). */
#  define GCC_FMT_ATTR(n, m) __attribute__((format(gnu_printf, n, m)))
#  if defined(_WIN32)
    /* Map __printf__ to __gnu_printf__ because we want standard format strings
     * even when MinGW or GLib include files use __printf__. */
#   define __printf__ __gnu_printf__
#  endif
# endif
#else
#define GCC_FMT_ATTR(n, m)
#endif

#if defined (__clang__)
  #pragma clang diagnostic ignored "-Wunknown-pragmas"
  #pragma clang diagnostic ignored "-Waddress-of-packed-member"
#endif

#endif /* COMPILER_H */
