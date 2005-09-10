/* sim_console.h: simulator console I/O library headers

   Copyright (c) 1993-2005, Robert M Supnik

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   05-Nov-04    RMS     Moved SET/SHOW DEBUG under CONSOLE hierarchy
   28-May-04    RMS     Added SET/SHOW CONSOLE
   02-Jan-04    RMS     Removed timer routines, added Telnet console routines
*/

#ifndef _SIM_CONSOLE_H_
#define _SIM_CONSOLE_H_ 0

t_stat sim_set_console (int32 flag, char *cptr);
t_stat sim_set_kmap (int32 flag, char *cptr);
t_stat sim_set_telnet (int32 flag, char *cptr);
t_stat sim_set_notelnet (int32 flag, char *cptr);
t_stat sim_set_logon (int32 flag, char *cptr);
t_stat sim_set_logoff (int32 flag, char *cptr);
t_stat sim_set_debon (int32 flag, char *cptr);
t_stat sim_set_deboff (int32 flag, char *cptr);
t_stat sim_show_console (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat sim_show_kmap (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat sim_show_telnet (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat sim_show_log (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat sim_show_debug (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat sim_check_console (int32 sec);
t_stat sim_poll_kbd (void);
t_stat sim_putchar (int32 c);
t_stat sim_putchar_s (int32 c);
t_stat sim_ttinit (void);
t_stat sim_ttrun (void);
t_stat sim_ttcmd (void);
t_stat sim_ttclose (void);
t_stat sim_os_poll_kbd (void);
t_stat sim_os_putchar (int32 out);

#endif
