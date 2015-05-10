/* sim_console.h: simulator console I/O library headers

   Copyright (c) 1993-2015, Robert M Supnik

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

   14-Dec-14    JDB     [4.0] Added sim_*_char externals
   02-Jan-14    RMS     Added tab stop routines
   22-Jun-06    RMS     Implemented SET/SHOW PCHAR
   22-Nov-05    RMS     Added central input/output conversion support
   05-Nov-04    RMS     Moved SET/SHOW DEBUG under CONSOLE hierarchy
   28-May-04    RMS     Added SET/SHOW CONSOLE
   02-Jan-04    RMS     Removed timer routines, added Telnet console routines
*/

#ifndef SIM_CONSOLE_H_
#define SIM_CONSOLE_H_  0

#define TTUF_V_MODE     (UNIT_V_UF + 0)
#define TTUF_W_MODE     2
#define  TTUF_MODE_7B   0
#define  TTUF_MODE_8B   1
#define  TTUF_MODE_UC   2
#define  TTUF_MODE_7P   3
#define TTUF_M_MODE     ((1u << TTUF_W_MODE) - 1)
#define TTUF_V_PAR      (TTUF_V_MODE + TTUF_W_MODE)
#define TTUF_W_PAR      2
#define  TTUF_PAR_SPACE 0
#define  TTUF_PAR_MARK  1
#define  TTUF_PAR_EVEN  2
#define  TTUF_PAR_ODD   3
#define TTUF_M_PAR      ((1u << TTUF_W_PAR) - 1)
#define  TTUF_KSR       (1u << (TTUF_W_MODE + TTUF_W_PAR))
#define TTUF_V_UF       (TTUF_V_MODE + TTUF_W_MODE + TTUF_W_PAR)
#define TT_MODE         (TTUF_M_MODE << TTUF_V_MODE)
#define  TT_MODE_7B     (TTUF_MODE_7B << TTUF_V_MODE)
#define  TT_MODE_8B     (TTUF_MODE_8B << TTUF_V_MODE)
#define  TT_MODE_UC     (TTUF_MODE_UC << TTUF_V_MODE)
#define  TT_MODE_7P     (TTUF_MODE_7P << TTUF_V_MODE)
#define  TT_MODE_KSR    (TT_MODE_UC)
/* 7 bit modes allow for an 8th bit parity mode */
#define TT_PAR          (TTUF_M_PAR << TTUF_V_PAR)
#define  TT_PAR_SPACE   (TTUF_PAR_SPACE << TTUF_V_PAR)
#define  TT_PAR_MARK    (TTUF_PAR_MARK  << TTUF_V_PAR)
#define  TT_PAR_EVEN    (TTUF_PAR_EVEN  << TTUF_V_PAR)
#define  TT_PAR_ODD     (TTUF_PAR_ODD   << TTUF_V_PAR)
/* TT_GET_MODE returns both the TT_MODE and TT_PAR fields 
   since they together are passed into sim_tt_inpcvt() */
#define TT_GET_MODE(x)  (((x) >> TTUF_V_MODE) & (TTUF_M_MODE | (TTUF_M_PAR << TTUF_W_MODE)))

t_stat sim_set_console (int32 flag, char *cptr);
t_stat sim_set_kmap (int32 flag, char *cptr);
t_stat sim_set_telnet (int32 flag, char *cptr);
t_stat sim_set_notelnet (int32 flag, char *cptr);
t_stat sim_set_logon (int32 flag, char *cptr);
t_stat sim_set_logoff (int32 flag, char *cptr);
t_stat sim_set_debon (int32 flag, char *cptr);
t_stat sim_set_deboff (int32 flag, char *cptr);
t_stat sim_set_pchar (int32 flag, char *cptr);
t_stat sim_show_console (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat sim_show_kmap (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat sim_show_telnet (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat sim_show_log (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat sim_show_debug (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat sim_show_pchar (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
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
int32 sim_tt_inpcvt (int32 c, uint32 mode);
int32 sim_tt_outcvt (int32 c, uint32 mode);
t_stat sim_tt_settabs (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat sim_tt_showtabs (FILE *st, UNIT *uptr, int32 val, void *desc);

extern int32 sim_int_char;                                  /* interrupt character */
extern int32 sim_brk_char;                                  /* break character */
extern int32 sim_tt_pchar;                                  /* printable character mask */
extern int32 sim_del_char;                                  /* delete character */

#endif
