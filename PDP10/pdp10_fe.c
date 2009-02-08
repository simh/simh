/* pdp10_fe.c: PDP-10 front end (console terminal) simulator

   Copyright (c) 1993-2007, Robert M Supnik

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

   fe           KS10 console front end

   18-Jun-07    RMS     Added UNIT_IDLE flag to console input
   17-Oct-06    RMS     Synced keyboard to clock for idling
   28-May-04    RMS     Removed SET FE CTRL-C
   29-Dec-03    RMS     Added console backpressure support
   25-Apr-03    RMS     Revised for extended file support
   22-Dec-02    RMS     Added break support
   30-May-02    RMS     Widened COUNT to 32b
   30-Nov-01    RMS     Added extended SET/SHOW support
   23-Oct-01    RMS     New IO page address constants
   07-Sep-01    RMS     Moved function prototypes
*/

#include "pdp10_defs.h"
#define UNIT_DUMMY      (1 << UNIT_V_UF)

extern d10 *M;
extern int32 apr_flg;
extern int32 tmxr_poll;
t_stat fei_svc (UNIT *uptr);
t_stat feo_svc (UNIT *uptr);
t_stat fe_reset (DEVICE *dptr);
t_stat fe_stop_os (UNIT *uptr, int32 val, char *cptr, void *desc);

/* FE data structures

   fe_dev       FE device descriptor
   fe_unit      FE unit descriptor
   fe_reg       FE register list
*/

#define fei_unit        fe_unit[0]
#define feo_unit        fe_unit[1]

UNIT fe_unit[] = {
    { UDATA (&fei_svc, UNIT_IDLE, 0), 0 },
    { UDATA (&feo_svc, 0, 0), SERIAL_OUT_WAIT }
    };

REG fe_reg[] = {
    { ORDATA (IBUF, fei_unit.buf, 8) },
    { DRDATA (ICOUNT, fei_unit.pos, T_ADDR_W), REG_RO + PV_LEFT },
    { DRDATA (ITIME, fei_unit.wait, 24), PV_LEFT },
    { ORDATA (OBUF, feo_unit.buf, 8) },
    { DRDATA (OCOUNT, feo_unit.pos, T_ADDR_W), REG_RO + PV_LEFT },
    { DRDATA (OTIME, feo_unit.wait, 24), REG_NZ + PV_LEFT },
    { NULL }
    };

MTAB fe_mod[] = {
    { UNIT_DUMMY, 0, NULL, "STOP", &fe_stop_os },
    { 0 }
    };

DEVICE fe_dev = {
    "FE", fe_unit, fe_reg, fe_mod,
    2, 10, 31, 1, 8, 8,
    NULL, NULL, &fe_reset,
    NULL, NULL, NULL
    };

/* Front end processor (console terminal)

   Communications between the KS10 and its front end is based on an in-memory
   status block and two interrupt lines: interrupt-to-control (APR_ITC) and
   interrupt-from-console (APR_CON).  When the KS10 wants to print a character
   on the terminal,

   1. It places a character, plus the valid flag, in FE_CTYOUT.
   2. It interrupts the front end processor.
   3. The front end processor types the character and then zeroes FE_CTYOUT.
   4. The front end procesor interrupts the KS10.

   When the front end wants to send an input character to the KS10,

   1. It places a character, plus the valid flag, in FE_CTYIN.
   2. It interrupts the KS10.
   3. It waits for the KS10 to take the character and clear the valid flag.
   4. It can then send more input (the KS10 may signal this by interrupting
      the front end).

   Note that the protocol has both ambiguity (interrupt to the KS10 may mean
   character printed, or input character available, or both) and lack of
   symmetry (the KS10 does not inform the front end that it has taken an
   input character).  
*/

void fe_intr (void)
{
if (M[FE_CTYOUT] & FE_CVALID) {                         /* char to print? */
    feo_unit.buf = (int32) M[FE_CTYOUT] & 0177;         /* pick it up */
    feo_unit.pos = feo_unit.pos + 1;
    sim_activate (&feo_unit, feo_unit.wait);            /* sched completion */
    }
else if ((M[FE_CTYIN] & FE_CVALID) == 0) {              /* input char taken? */
    sim_cancel (&fei_unit);                             /* sched immediate */
    sim_activate (&fei_unit, 0);                        /* keyboard poll */
    }
return;
}

t_stat feo_svc (UNIT *uptr)
{
t_stat r;

if ((r = sim_putchar_s (uptr->buf)) != SCPE_OK) {       /* output; error? */
    sim_activate (uptr, uptr->wait);                    /* try again */
    return ((r == SCPE_STALL)? SCPE_OK: r);             /* !stall? report */
    }
M[FE_CTYOUT] = 0;                                       /* clear char */
apr_flg = apr_flg | APRF_CON;                           /* interrupt KS10 */
return SCPE_OK;
}

t_stat fei_svc (UNIT *uptr)
{
int32 temp;

sim_activate (uptr, KBD_WAIT (uptr->wait, tmxr_poll));  /* continue poll */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)              /* no char or error? */
    return temp;
if (temp & SCPE_BREAK)                                  /* ignore break */
    return SCPE_OK;
uptr->buf = temp & 0177;
uptr->pos = uptr->pos + 1;
M[FE_CTYIN] = uptr->buf | FE_CVALID;                    /* put char in mem */
apr_flg = apr_flg | APRF_CON;                           /* interrupt KS10 */
return SCPE_OK;
}

/* Reset */

t_stat fe_reset (DEVICE *dptr)
{
fei_unit.buf = feo_unit.buf = 0;
M[FE_CTYIN] = M[FE_CTYOUT] = 0;
apr_flg = apr_flg & ~(APRF_ITC | APRF_CON);
sim_activate_abs (&fei_unit, KBD_WAIT (fei_unit.wait, tmxr_poll));
return SCPE_OK;
}

/* Stop operating system */

t_stat fe_stop_os (UNIT *uptr, int32 val, char *cptr, void *desc)
{
M[FE_SWITCH] = IOBA_RP;                                 /* tell OS to stop */
return SCPE_OK;
}
