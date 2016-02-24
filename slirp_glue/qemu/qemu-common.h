
/* Common header file that is included by all of QEMU.
 *
 * This file is supposed to be included only by .c files. No header file should
 * depend on qemu-common.h, as this would easily lead to circular header
 * dependencies.
 *
 * If a header file uses a definition from qemu-common.h, that definition
 * must be moved to a separate header file, and the header that uses it
 * must include that header.
 */
#ifndef QEMU_COMMON_H
#define QEMU_COMMON_H

#include "qemu/osdep.h"
#include "qemu/typedefs.h"

#include "glib.h"
#include "config-host.h"

#if defined(O_BINARY)   /* O_BINARY isn't used in slirp */
#undef O_BINARY         /* Avoid potential redefinition elsewhere */
#endif

/* HOST_LONG_BITS is the size of a native pointer in bits. */
#if UINTPTR_MAX == UINT32_MAX
# define HOST_LONG_BITS 32
#elif UINTPTR_MAX == UINT64_MAX
# define HOST_LONG_BITS 64
#else
# error Unknown pointer size
#endif

/* util/cutils.c */
/**
 * pstrcpy:
 * @buf: buffer to copy string into
 * @buf_size: size of @buf in bytes
 * @str: string to copy
 *
 * Copy @str into @buf, including the trailing NUL, but do not
 * write more than @buf_size bytes. The resulting buffer is
 * always NUL terminated (even if the source string was too long).
 * If @buf_size is zero or negative then no bytes are copied.
 *
 * This function is similar to strncpy(), but avoids two of that
 * function's problems:
 *  * if @str fits in the buffer, pstrcpy() does not zero-fill the
 *    remaining space at the end of @buf
 *  * if @str is too long, pstrcpy() will copy the first @buf_size-1
 *    bytes and then add a NUL
 */
void pstrcpy(char *buf, int buf_size, const char *str);
#endif
