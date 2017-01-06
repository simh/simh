/* glib_qemu_stubs.c:
  ------------------------------------------------------------------------------
   Copyright (c) 2015, Mark Pizzolato

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

  ------------------------------------------------------------------------------

   This module provides the minimal aspects of glib and qemu which are referenced
   by the current qemu SLiRP code and are needed to get SLiRP functionality for
   the simh network code.

*/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif
#include "qemu/compiler.h"
#include "qemu/typedefs.h"
#include <sockets.h>
#include <stdarg.h>
#include "glib.h"

gpointer
g_malloc (gsize n_bytes)
{
gpointer ret = (gpointer)malloc (n_bytes);

if (!ret)
    exit (errno);
return ret;
}

gpointer
g_malloc0 (gsize n_bytes)
{
gpointer ret = (gpointer)calloc (1, n_bytes);

if (!ret)
    exit (errno);
return ret;
}

gpointer
g_realloc (gpointer mem, gsize n_bytes)
{
gpointer ret = (gpointer)realloc (mem, n_bytes);

if (!ret)
    exit (errno);
return ret;
}

void
g_free (gpointer mem)
{
free (mem);
}

gchar *
g_strdup (const gchar *str)
{
gchar *nstr = NULL;

if (str) {
    nstr = (gchar *)malloc (strlen(str)+1);
    if (!nstr)
        exit (errno);
    strcpy (nstr, str);
    }
return nstr;
}

void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

int qemu_socket(int domain, int type, int protocol)
{
return socket (domain, type, protocol);
}

int qemu_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
return accept (s, addr, addrlen);
}

int qemu_setsockopt (int s, int level, int optname, void *optval, int optlen)
{
return setsockopt ((SOCKET)s, level, optname, (char *)optval, optlen);
}

int qemu_recv (int s, void *buf, size_t len, int flags)
{
return recv ((SOCKET)s, (char *)buf, len, flags);
}

int socket_set_nodelay(int fd)
{
    int v = 1;
    return setsockopt((SOCKET)fd, IPPROTO_TCP, TCP_NODELAY, (char *)&v, sizeof(v));
}

#ifdef _WIN32

void qemu_set_nonblock(int fd)
{
unsigned long non_block = 1;

    ioctlsocket ((SOCKET)fd, FIONBIO, &non_block);         /* set nonblocking */
}
#else
#include <fcntl.h>
void qemu_set_nonblock(int fd)
{
    int f;
    f = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, f | O_NONBLOCK);
}
#endif

int socket_set_fast_reuse(int fd)
{
    int val = 1, ret;

    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                     (const char *)&val, sizeof(val));
    return ret;
}

#if defined(__cplusplus)
extern "C" {
#endif
#include <time.h>
#ifdef _WIN32
int64_t qemu_clock_get_ns(int type)
{
uint64_t now, unixbase;

unixbase = 116444736;
unixbase *= 1000000000;
GetSystemTimeAsFileTime((FILETIME*)&now);
now -= unixbase;
return now*100;
}

#else

#if !defined(CLOCK_REALTIME) && !defined(__hpux)
#define CLOCK_REALTIME 1
int clock_gettime(int clk_id, struct timespec *tp);
#endif

int64_t qemu_clock_get_ns(int type)
{
    struct timespec tv;

    clock_gettime(CLOCK_REALTIME, &tv);
    return tv.tv_sec * 1000000000LL + tv.tv_nsec;
}
#endif
#if defined(__cplusplus)
}
#endif

void monitor_printf(Monitor *mon, const char *fmt, ...)
{
va_list arglist;

    va_start (arglist, fmt);
    vfprintf ((FILE *)mon, fmt, arglist);
    va_end (arglist);
}

void g_log (const gchar *log_domain,
            GLogLevelFlags log_level,
            const gchar *format,
            ...)
{
va_list arglist;

    fprintf (stderr, "%s(%X): ", log_domain ? log_domain : "", log_level);
    va_start (arglist, format);
    vfprintf (stderr, format, arglist);
    va_end (arglist);
}

int qemu_chr_fe_write(CharDriverState *s, const uint8_t *buf, int len)
{
fprintf (stderr, "qemu_chr_fe_write() called\n");
return 0;
}

void qemu_notify_event(void)
{
}

#if defined(_WIN32)
int
inet_aton(const char *arg, struct in_addr *addr)
{
(*addr).s_addr = inet_addr (arg);
return (*addr).s_addr != INADDR_BROADCAST;
}

#endif

/* glib GArray functionality is needed */

typedef struct {
    gchar *data;
    guint len;
    guint _element_size;            /* element size */
    guint _size;                    /* allocated element count size */
    gboolean _zero_terminated;
    gboolean _clear;
} GArrayInternal;

GArray *
g_array_sized_new (gboolean zero_terminated,
                   gboolean clear,
                   guint element_size,
                   guint reserved_size)
{
GArrayInternal *ar = (GArrayInternal *)g_malloc (sizeof (*ar));

ar->_zero_terminated = zero_terminated ? 1 : 0;
ar->_clear = clear ? 1 : 0;
ar->_element_size = element_size;
ar->_size = reserved_size;
ar->len = 0;
ar->data = clear ? (gchar *)g_malloc0 (element_size*(reserved_size + zero_terminated)) :
                   (gchar *)g_malloc (element_size*(reserved_size + zero_terminated));
if (ar->_zero_terminated && !ar->_clear)
    memset (ar->data + (ar->len * ar->_element_size), 0, ar->_element_size);
return (GArray *)ar;
}

gchar *
g_array_free (GArray *array,
              gboolean free_segment)
{
gchar *result = ((array == NULL) || free_segment) ? NULL : array->data;

if (array != NULL) {
    if (free_segment)
        free (array->data); 
    free (array);
    }
return result;
}
 
GArray *
g_array_set_size (GArray *array,
                  guint length)
{
GArrayInternal *ar = (GArrayInternal *)array;

if (length > ar->_size) {
    ar->data = (gchar *)g_realloc (ar->data, (length + ar->_zero_terminated) * ar->_element_size);
    if (ar->_clear)
        memset (ar->data + (ar->len * ar->_element_size), 0, (length + ar->_zero_terminated - ar->len) * ar->_element_size);
    ar->_size = length;
    }
ar->len = length;
if (ar->_zero_terminated)
    memset (ar->data + (ar->len * ar->_element_size), 0, ar->_element_size);
return array;
}
      
GArray *
g_array_append_vals (GArray *array,
                     gconstpointer data,
                     guint len)
{
GArrayInternal *ar = (GArrayInternal *)array;

if ((ar->len + len) > ar->_size) {
    ar->data = (gchar *)g_realloc (ar->data, (ar->len + len + ar->_zero_terminated) * ar->_element_size);
    ar->_size = ar->len + len;
    }
memcpy (ar->data + (ar->len * ar->_element_size), data, len * ar->_element_size);
ar->len += len;
if (ar->_zero_terminated)
    memset (ar->data + (ar->len * ar->_element_size), 0, ar->_element_size);
return array;
}

guint
g_array_get_element_size (GArray *array)
{
GArrayInternal *ar = (GArrayInternal *)array;

return ar->_element_size;
}

#if defined(_WIN32)
char *socket_strerror(int errnum)
{
static char buf[512];

if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL, errnum, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)buf, sizeof(buf), NULL))
    sprintf(buf, "Error Code: %d", errno);
return buf;
}
#endif
