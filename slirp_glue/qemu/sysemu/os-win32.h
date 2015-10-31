/*
 * win32 specific declarations
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010 Jes Sorensen <Jes.Sorensen@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QEMU_OS_WIN32_H
#define QEMU_OS_WIN32_H

#include <windows.h>
#include <winsock2.h>

#if defined(_WIN64)
/* MinGW-w64 has a 32 bit off_t, but we want 64 bit off_t. */
# define off_t off64_t

/* MinGW-w64 stdio.h defines SYS_OPEN. Allow a redefinition in arm-semi.c. */
# undef SYS_OPEN
#endif

/* Workaround for older versions of MinGW. */
#ifndef ECONNREFUSED
# define ECONNREFUSED WSAECONNREFUSED
#endif
#ifndef EINPROGRESS
# define EINPROGRESS  WSAEINPROGRESS
#endif
#ifndef EHOSTUNREACH
# define EHOSTUNREACH WSAEHOSTUNREACH
#endif
#ifndef EINTR
# define EINTR        WSAEINTR
#endif
#ifndef EINPROGRESS
# define EINPROGRESS  WSAEINPROGRESS
#endif
#ifndef ENETUNREACH
# define ENETUNREACH  WSAENETUNREACH
#endif
#ifndef ENOTCONN
# define ENOTCONN     WSAENOTCONN
#endif
#ifndef EWOULDBLOCK
# define EWOULDBLOCK  WSAEWOULDBLOCK
#endif

#if defined(_WIN64)
/* On w64, setjmp is implemented by _setjmp which needs a second parameter.
 * If this parameter is NULL, longjump does no stack unwinding.
 * That is what we need for QEMU. Passing the value of register rsp (default)
 * lets longjmp try a stack unwinding which will crash with generated code. */
# undef setjmp
# define setjmp(env) _setjmp(env, NULL)
#endif
/* QEMU uses sigsetjmp()/siglongjmp() as the portable way to specify
 * "longjmp and don't touch the signal masks". Since we know that the
 * savemask parameter will always be zero we can safely define these
 * in terms of setjmp/longjmp on Win32.
 */
#define sigjmp_buf jmp_buf
#define sigsetjmp(env, savemask) setjmp(env)
#define siglongjmp(env, val) longjmp(env, val)

/* Missing POSIX functions. Don't use MinGW-w64 macros. */
#ifndef CONFIG_LOCALTIME_R
#undef gmtime_r
struct tm *gmtime_r(const time_t *timep, struct tm *result);
#undef localtime_r
struct tm *localtime_r(const time_t *timep, struct tm *result);
#endif /* CONFIG_LOCALTIME_R */


static inline void os_setup_signal_handling(void) {}
static inline void os_daemonize(void) {}
static inline void os_setup_post(void) {}
void os_set_line_buffering(void);
static inline void os_set_proc_name(const char *dummy) {}

size_t getpagesize(void);

#if !defined(EPROTONOSUPPORT)
# define EPROTONOSUPPORT EINVAL
#endif

int setenv(const char *name, const char *value, int overwrite);

typedef struct {
    long tv_sec;
    long tv_usec;
} qemu_timeval;
int qemu_gettimeofday(qemu_timeval *tp);

#endif
