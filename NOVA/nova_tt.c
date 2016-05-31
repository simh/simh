/* nova_tt.c: NOVA console terminal simulator

   Copyright (c) 1993-2015, Robert M. Supnik

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

   tti          terminal input
   tto          terminal output

   31-Mar-15    RMS     Backported parity capability from GitHub master
   04-Jul-07    BKR     fixed Dasher CR/LF swap function in 'tti_svc()',
                        DEV_SET/CLR macros now used,
                        TTO device may now be DISABLED
   29-Dec-03    RMS     Added console backpressure support
   25-Apr-03    RMS     Revised for extended file support
   05-Jan-02    RMS     Fixed calling sequence for setmod
   03-Oct-02    RMS     Added DIBs
   30-May-02    RMS     Widened POS to 32b
   30-Nov-01    RMS     Added extended SET/SHOW support
   17-Sep-01    RMS     Removed multiconsole support
   07-Sep-01    RMS     Moved function prototypes
   31-May-01    RMS     Added multiconsole support

   Notes:
    - TTO "Dasher" attribute sends '\b' to console instead of '\031'
    - TTO may be disabled
    - TTI "Dasher" attribute swaps <CR> and <LF>
    - TTI may not be disabled
*/

#include "nova_defs.h"
#include "sim_tmxr.h"

#define UNIT_V_DASHER   (TTUF_V_UF)                 /* Dasher mode */
#define UNIT_DASHER     (1 << UNIT_V_DASHER)

extern int32 int_req, dev_busy, dev_done, dev_disable;

int32 tti (int32 pulse, int32 code, int32 AC);
int32 tto (int32 pulse, int32 code, int32 AC);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tti_reset (DEVICE *dptr);
t_stat tto_reset (DEVICE *dptr);
t_stat ttx_setmod (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ttx_setpar (UNIT *uptr, int32 val, CONST char *cptr, void *desc);

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
   ttx_mod      TTI/TTO modifiers list
*/

DIB tti_dib = { DEV_TTI, INT_TTI, PI_TTI, &tti };

UNIT tti_unit = { UDATA (&tti_svc, 0, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
    { ORDATA (BUF, tti_unit.buf, 8) },
    { FLDATA (BUSY, dev_busy, INT_V_TTI) },
    { FLDATA (DONE, dev_done, INT_V_TTI) },
    { FLDATA (DISABLE, dev_disable, INT_V_TTI) },
    { FLDATA (INT, int_req, INT_V_TTI) },
    { DRDATA (POS, tti_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tti_unit.wait, 24), REG_NZ + PV_LEFT },
    { NULL }
    };

MTAB ttx_mod[] = {
    { UNIT_DASHER, 0, "ANSI", "ANSI", &ttx_setmod },
    { UNIT_DASHER, UNIT_DASHER, "Dasher", "DASHER", &ttx_setmod },
    { TT_PAR, TT_PAR_EVEN,  "even parity", "EVEN",  &ttx_setpar },
    { TT_PAR, TT_PAR_ODD,   "odd parity",  "ODD",   &ttx_setpar },
    { TT_PAR, TT_PAR_MARK,  "mark parity", "MARK",  &ttx_setpar },
    { TT_PAR, TT_PAR_SPACE, "no parity",   "NONE",  &ttx_setpar },
    { 0 }
    } ;

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, ttx_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tti_reset,
    NULL, NULL, NULL,
    &tti_dib, 0
    };

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit descriptor
   tto_reg      TTO register list
*/

DIB tto_dib = { DEV_TTO, INT_TTO, PI_TTO, &tto };

UNIT tto_unit = { UDATA (&tto_svc, 0, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
    { ORDATA (BUF, tto_unit.buf, 8) },
    { FLDATA (BUSY, dev_busy, INT_V_TTO) },
    { FLDATA (DONE, dev_done, INT_V_TTO) },
    { FLDATA (DISABLE, dev_disable, INT_V_TTO) },
    { FLDATA (INT, int_req, INT_V_TTO) },
    { DRDATA (POS, tto_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tto_unit.wait, 24), PV_LEFT },
    { NULL }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, ttx_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tto_reset,
    NULL, NULL, NULL,
    &tto_dib, DEV_DISABLE
    };

/* Terminal input: IOT routine */

int32 tti (int32 pulse, int32 code, int32 AC)
{
int32 iodata;


if (code == ioDIA)
    iodata = tti_unit.buf & 0377;
else iodata = 0;

switch (pulse)
    {                                                   /* decode IR<8:9> */
  case iopS:                                            /* start */
    DEV_SET_BUSY( INT_TTI ) ;
    DEV_CLR_DONE( INT_TTI ) ;
    DEV_UPDATE_INTR ;
    break;

  case iopC:                                            /* clear */
    DEV_CLR_BUSY( INT_TTI ) ;
    DEV_CLR_DONE( INT_TTI ) ;
    DEV_UPDATE_INTR ;
    break;
    }                                                   /* end switch */

return iodata;
}

/* Unit service */

t_stat tti_svc (UNIT *uptr)
{
int32 temp;

sim_activate (&tti_unit, tti_unit.wait);                /* continue poll */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)
    return temp;                                        /* no char or error? */
tti_unit.buf = temp & 0177;
if (tti_unit.flags & UNIT_DASHER) {
    if (tti_unit.buf == '\r')
        tti_unit.buf = '\n';                            /* Dasher: cr -> nl */
    else if (tti_unit.buf == '\n')
        tti_unit.buf = '\r' ;                           /* Dasher: nl -> cr */
    }
tti_unit.buf = sim_tt_inpcvt (tti_unit.buf, TT_GET_MODE (uptr->flags));
DEV_CLR_BUSY( INT_TTI ) ;
DEV_SET_DONE( INT_TTI ) ;
DEV_UPDATE_INTR ;
++(uptr->pos) ;
return SCPE_OK;
}

/* Reset routine */

t_stat tti_reset (DEVICE *dptr)
{
tmxr_set_console_units (&tti_unit, &tto_unit);
tti_unit.buf = 0;                                       /* <not DG compatible>  */
DEV_CLR_BUSY( INT_TTI ) ;
DEV_CLR_DONE( INT_TTI ) ;
DEV_UPDATE_INTR ;
sim_activate (&tti_unit, tti_unit.wait);                /* activate unit */
return SCPE_OK;
}

/* Terminal output: IOT routine */

int32 tto (int32 pulse, int32 code, int32 AC)
{
if (code == ioDOA)
    tto_unit.buf = AC & 0377; 

switch (pulse)
    {                                                   /* decode IR<8:9> */
  case iopS:                                            /* start */
    DEV_SET_BUSY( INT_TTO ) ;
    DEV_CLR_DONE( INT_TTO ) ;
    DEV_UPDATE_INTR ;
    sim_activate (&tto_unit, tto_unit.wait);            /* activate unit */
    break;

  case iopC:                                            /* clear */
    DEV_CLR_BUSY( INT_TTO ) ;
    DEV_CLR_DONE( INT_TTO ) ;
    DEV_UPDATE_INTR ;
    sim_cancel (&tto_unit);                             /* deactivate unit */
    break;
    }                                                   /* end switch */
return 0;
}


/* Unit service */

t_stat tto_svc (UNIT *uptr)
{
int32   c;
t_stat  r;

c = tto_unit.buf & 0177;
if ((tto_unit.flags & UNIT_DASHER) && (c == 031))
    c = '\b';
if ((r = sim_putchar_s (c)) != SCPE_OK) {               /* output; error? */
    sim_activate (uptr, uptr->wait);                    /* try again */
    return ((r == SCPE_STALL)? SCPE_OK : r);            /* !stall? report */
    }
DEV_CLR_BUSY( INT_TTO ) ;
DEV_SET_DONE( INT_TTO ) ;
DEV_UPDATE_INTR ;
++(tto_unit.pos);
return SCPE_OK;
}

/* Reset routine */

t_stat tto_reset (DEVICE *dptr)
{
tto_unit.buf = 0;                                       /* <not DG compatible!>  */
DEV_CLR_BUSY( INT_TTO ) ;
DEV_CLR_DONE( INT_TTO ) ;
DEV_UPDATE_INTR ;
sim_cancel (&tto_unit);                                 /* deactivate unit */
return SCPE_OK;
}

t_stat ttx_setmod (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
tti_unit.flags = (tti_unit.flags & ~UNIT_DASHER) | val;
tto_unit.flags = (tto_unit.flags & ~UNIT_DASHER) | val;
return SCPE_OK;
}

t_stat ttx_setpar (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
tti_unit.flags = (tti_unit.flags & ~TT_PAR) | val;
tto_unit.flags = (tto_unit.flags & ~TT_PAR) | val;
return SCPE_OK;
}
