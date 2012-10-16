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


#ifndef _SIM_SERIAL_H_
#define _SIM_SERIAL_H_    0

/* Windows definitions */

#if defined (_WIN32)


/* We need the basic Win32 definitions, but including "windows.h" also includes
   "winsock.h" as well.  However, "sim_sock.h" explicitly includes "winsock2.h,"
   and this file cannot coexist with "winsock.h".  So we set a guard definition
   that prevents "winsock.h" from being included.
*/

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

typedef HANDLE SERHANDLE;

#define INVALID_HANDLE  INVALID_HANDLE_VALUE


/* UNIX definitions */

#elif defined (__unix__)


#include <fcntl.h>
#include <termio.h>
#include <unistd.h>
#include <sys/ioctl.h>

typedef int SERHANDLE;

#define INVALID_HANDLE  -1


/* Non-implemented definitions */

#else

typedef int SERHANDLE;

#endif

/* Common definitions */

typedef struct serial_config {                          /* serial port configuration */
    uint32 baudrate;                                    /* baud rate */
    uint32 charsize;                                    /* character size in bits */
    char   parity;                                      /* parity (N/O/E/M/S) */
    uint32 stopbits;                                    /* 0/1/2 stop bits (0 implies 1.5) */
    } SERCONFIG;

/* Global routines */
#include "sim_tmxr.h"                                   /* need TMLN definition */

extern SERHANDLE sim_open_serial    (char *name, TMLN *lp);
extern t_stat    sim_config_serial  (SERHANDLE port, SERCONFIG config);
extern t_bool    sim_control_serial (SERHANDLE port, t_bool connect);
extern int32     sim_read_serial    (SERHANDLE port, char *buffer, int32 count, char *brk);
extern int32     sim_write_serial   (SERHANDLE port, char *buffer, int32 count);
extern void      sim_close_serial   (SERHANDLE port);
extern t_stat    sim_show_serial    (FILE* st, DEVICE *dptr, UNIT* uptr, int32 val, char* desc);

#endif
