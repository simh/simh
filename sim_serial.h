/* sim_serial.h: OS-dependent serial port routines header file

   Copyright (c) 2008, J. David Bryan

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

   07-Oct-08    JDB     [serial] Created file
*/


#ifndef SIM_SERIAL_H_
#define SIM_SERIAL_H_    0

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef SIMH_SERHANDLE_DEFINED
#define SIMH_SERHANDLE_DEFINED 0
typedef struct SERPORT *SERHANDLE;
#endif /* SERHANDLE_DEFINED */

#if defined (_WIN32)                        /* Windows definitions */

/* We need the basic Win32 definitions, but including "windows.h" also includes
   "winsock.h" as well.  However, "sim_sock.h" explicitly includes "winsock2.h,"
   and this file cannot coexist with "winsock.h".  So we set a guard definition
   that prevents "winsock.h" from being included.
*/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#if !defined(INVALID_HANDLE)
#define INVALID_HANDLE  (SERHANDLE)INVALID_HANDLE_VALUE
#endif /* !defined(INVALID_HANDLE) */

#elif defined (__unix__) || defined (__APPLE__) || defined (__hpux) /* UNIX definitions */

#include <fcntl.h>
#ifdef __hpux
#include <sys/modem.h>
#endif
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#if !defined(INVALID_HANDLE)
#define INVALID_HANDLE  ((SERHANDLE)(void *)-1)
#endif /* !defined(INVALID_HANDLE) */

#elif defined (VMS)                             /* VMS definitions */
#if !defined(INVALID_HANDLE)
#define INVALID_HANDLE  ((SERHANDLE)(void *)-1)
#endif /* !defined(INVALID_HANDLE) */

#else                                           /* Non-implemented definitions */

#if !defined(INVALID_HANDLE)
#define INVALID_HANDLE  ((SERHANDLE)(void *)-1)
#endif /* !defined(INVALID_HANDLE) */

#endif  /* OS variants */


/* Common definitions */

/* Global routines */
#include "sim_tmxr.h"                           /* need TMLN definition and modem definitions */

extern SERHANDLE sim_open_serial    (char *name, TMLN *lp, t_stat *status);
extern t_stat    sim_config_serial  (SERHANDLE port, CONST char *config);
extern t_stat    sim_control_serial (SERHANDLE port, int32 bits_to_set, int32 bits_to_clear, int32 *incoming_bits);
extern int32     sim_read_serial    (SERHANDLE port, char *buffer, int32 count, char *brk);
extern int32     sim_write_serial   (SERHANDLE port, char *buffer, int32 count);
extern void      sim_close_serial   (SERHANDLE port);
extern t_stat    sim_show_serial    (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, CONST char* desc);

#ifdef  __cplusplus
}
#endif

#endif
