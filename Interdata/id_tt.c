/* id_tt.c: Interdata teletype

   Copyright (c) 2000-2012, Robert M. Supnik

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

   tt           console

   18-Apr-12    RMS     Revised to use clock coscheduling
   18-Jun-07    RMS     Added UNIT_IDLE flag to console input
   18-Oct-06    RMS     Sync keyboard to LFC clock
   30-Sep-06    RMS     Fixed handling of non-printable characters in KSR mode
   22-Nov-05    RMS     Revised for new terminal processing routines
   29-Dec-03    RMS     Added support for console backpressure
   25-Apr-03    RMS     Revised for extended file support
   11-Jan-03    RMS     Added TTP support
   22-Dec-02    RMS     Added break support
*/

#include "id_defs.h"
#include <ctype.h>

/* Device definitions */

#define TTI             0
#define TTO             1

#define STA_OVR         0x80                            /* overrun */
#define STA_BRK         0x20                            /* break */
#define STA_MASK        (STA_OVR | STA_BRK | STA_BSY)   /* status mask */
#define SET_EX          (STA_OVR | STA_BRK)             /* set EX */

#define CMD_V_FDPX      4                               /* full/half duplex */
#define CMD_V_RD        2                               /* read/write */

extern uint32 int_req[INTSZ], int_enb[INTSZ];
extern int32 lfc_poll;

uint32 tt_sta = STA_BSY;                                /* status */
uint32 tt_fdpx = 1;                                     /* tt mode */
uint32 tt_rd = 1, tt_chp = 0;                           /* tt state */
uint32 tt_arm = 0;                                      /* int arm */

uint32 tt (uint32 dev, uint32 op, uint32 dat);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tt_reset (DEVICE *dptr);
t_stat tt_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tt_set_break (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tt_set_enbdis (UNIT *uptr, int32 val, CONST char *cptr, void *desc);

/* TT data structures

   tt_dev       TT device descriptor
   tt_unit      TT unit descriptors
   tt_reg       TT register list
   tt_mod       TT modifiers list
*/

DIB tt_dib = { d_TT, -1, v_TT, NULL, &tt, NULL };

UNIT tt_unit[] = {
    { UDATA (&tti_svc, TT_MODE_KSR|UNIT_IDLE, 0), 0 },
    { UDATA (&tto_svc, TT_MODE_KSR, 0), SERIAL_OUT_WAIT }
    };

REG tt_reg[] = {
    { HRDATA (STA, tt_sta, 8) },
    { HRDATA (KBUF, tt_unit[TTI].buf, 8) },
    { DRDATA (KPOS, tt_unit[TTI].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (KTIME, tt_unit[TTI].wait, 24), PV_LEFT },
    { HRDATA (TBUF, tt_unit[TTO].buf, 8) },
    { DRDATA (TPOS, tt_unit[TTO].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TTIME, tt_unit[TTO].wait, 24), REG_NZ + PV_LEFT },
    { FLDATA (IREQ, int_req[l_TT], i_TT) },
    { FLDATA (IENB, int_enb[l_TT], i_TT) },
    { FLDATA (IARM, tt_arm, 0) },
    { FLDATA (RD, tt_rd, 0) },
    { FLDATA (FDPX, tt_fdpx, 0) },
    { FLDATA (CHP, tt_chp, 0) },
    { HRDATA (DEVNO, tt_dib.dno, 8), REG_HRO },
    { NULL }
    };

MTAB tt_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", &tt_set_mode },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  &tt_set_mode },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  &tt_set_mode },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  &tt_set_mode },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, NULL, "ENABLED",
      &tt_set_enbdis, NULL, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, DEV_DIS, NULL, "DISABLED",
      &tt_set_enbdis, NULL, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, NULL, "BREAK",
      &tt_set_break, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, &tt_dib },
    { 0 }
    };

DEVICE tt_dev = {
    "TT", tt_unit, tt_reg, tt_mod,
    2, 10, 31, 1, 16, 8,
    NULL, NULL, &tt_reset,
    NULL, NULL, NULL,
    &tt_dib, 0
    };

/* Terminal: IO routine */

uint32 tt (uint32 dev, uint32 op, uint32 dat)
{
uint32 old_rd, t;

switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        return BY;                                      /* byte only */

    case IO_OC:                                         /* command */
        old_rd = tt_rd;
        tt_arm = int_chg (v_TT, dat, tt_arm);           /* upd int ctrl */
        tt_fdpx = io_2b (dat, CMD_V_FDPX, tt_fdpx);     /* upd full/half */
        tt_rd = io_2b (dat, CMD_V_RD, tt_rd);           /* upd rd/write */
        if (tt_rd != old_rd) {                          /* rw change? */
            if (tt_rd? tt_chp: !sim_is_active (&tt_unit[TTO])) {
                tt_sta = 0;                             /* busy = 0 */
                if (tt_arm)                             /* req intr */
                    SET_INT (v_TT);
                }
            else {
                tt_sta = STA_BSY;                       /* busy = 1 */
                CLR_INT (v_TT);                         /* clr int */
                }
            }
        else tt_sta = tt_sta & ~STA_OVR;                /* clr ovflo */
        break;

    case IO_RD:                                         /* read */
        tt_chp = 0;                                     /* clear pend */
        if (tt_rd)
            tt_sta = (tt_sta | STA_BSY) & ~STA_OVR;
        return (tt_unit[TTI].buf & 0xFF);

    case IO_WD:                                         /* write */
        tt_unit[TTO].buf = dat & 0xFF;                  /* save char */
        if (!tt_rd)                                     /* set busy */
            tt_sta = tt_sta | STA_BSY;
        sim_activate (&tt_unit[TTO], tt_unit[TTO].wait);
        break;

    case IO_SS:                                         /* status */
        t = tt_sta & STA_MASK;                          /* get status */
        if (t & SET_EX)                                 /* test for EX */
            t = t | STA_EX;
        return t;
        }

return 0;
}

/* Unit service routines */

t_stat tti_svc (UNIT *uptr)
{
int32 out, temp;

sim_activate (uptr, KBD_WAIT (uptr->wait, lfc_cosched (lfc_poll)));
                                                        /* continue poll */
tt_sta = tt_sta & ~STA_BRK;                             /* clear break */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)              /* no char or error? */
    return temp;
if (tt_rd) {                                            /* read mode? */
    tt_sta = tt_sta & ~STA_BSY;                         /* clear busy */
    if (tt_arm)                                         /* if armed, intr */
        SET_INT (v_TT);
    if (tt_chp)                                         /* got char? overrun */
        tt_sta = tt_sta | STA_OVR;
    }
tt_chp = 1;                                             /* char pending */
out = temp & 0x7F;                                      /* echo is 7B */
if (temp & SCPE_BREAK) {                                /* break? */
    tt_sta = tt_sta | STA_BRK;                          /* set status */
    uptr->buf = 0;                                      /* no character */
    }
else uptr->buf = sim_tt_inpcvt (temp, TT_GET_MODE (uptr->flags) | TTUF_KSR);
uptr->pos = uptr->pos + 1;                              /* incr count */
if (!tt_fdpx) {                                         /* half duplex? */
    out = sim_tt_outcvt (out, TT_GET_MODE (uptr->flags) | TTUF_KSR);
    if (out >= 0) {                                     /* valid echo? */
        sim_putchar (out);                              /* write char */
        tt_unit[TTO].pos = tt_unit[TTO].pos + 1;
        }
    }
return SCPE_OK;
}

t_stat tto_svc (UNIT *uptr)
{
int32 ch;
t_stat r;

ch = sim_tt_outcvt (uptr->buf, TT_GET_MODE (uptr->flags) | TTUF_KSR);
if (ch >= 0) {
    if ((r = sim_putchar_s (ch)) != SCPE_OK) {          /* output; error? */
        sim_activate (uptr, uptr->wait);                /* try again */
        return ((r == SCPE_STALL)? SCPE_OK: r);
        }
    }
if (!tt_rd) {                                           /* write mode? */
    tt_sta = tt_sta & ~STA_BSY;                         /* clear busy */
    if (tt_arm)                                         /* if armed, intr */
        SET_INT (v_TT);
    }
uptr->pos = uptr->pos + 1;                              /* incr count */
return SCPE_OK;
}

/* Reset routine */

t_stat tt_reset (DEVICE *dptr)
{
if (dptr->flags & DEV_DIS)                              /* dis? cancel poll */
    sim_cancel (&tt_unit[TTI]);
else sim_activate (&tt_unit[TTI], KBD_WAIT (tt_unit[TTI].wait, lfc_poll));
sim_cancel (&tt_unit[TTO]);                             /* cancel output */
tt_rd = tt_fdpx = 1;                                    /* read, full duplex */
tt_chp = 0;                                             /* no char */
tt_sta = STA_BSY;                                       /* buffer empty */
CLR_INT (v_TT);                                         /* clear int */
CLR_ENB (v_TT);                                         /* disable int */
tt_arm = 0;                                             /* disarm int */
return SCPE_OK;
}

/* Make mode flags uniform */

t_stat tt_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
tt_unit[TTO].flags = (tt_unit[TTO].flags & ~TT_MODE) | val;
if (val == TT_MODE_7P)
    val = TT_MODE_7B;
tt_unit[TTI].flags = (tt_unit[TTI].flags & ~TT_MODE) | val;
return SCPE_OK;
}

/* Set input break */

t_stat tt_set_break (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (tt_dev.flags & DEV_DIS)
    return SCPE_NOFNC;
tt_sta = tt_sta | STA_BRK;
if (tt_rd) {                                            /* read mode? */
    tt_sta = tt_sta & ~STA_BSY;                         /* clear busy */
    if (tt_arm)                                         /* if armed, intr */
        SET_INT (v_TT);
    }
sim_cancel (&tt_unit[TTI]);                             /* restart TT poll */
sim_activate (&tt_unit[TTI], tt_unit[TTI].wait);        /* so brk is seen */
return SCPE_OK;
}

/* Set enabled/disabled */

t_stat tt_set_enbdis (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
extern DEVICE ttp_dev;
extern t_stat ttp_reset (DEVICE *dptr);

tt_dev.flags = (tt_dev.flags & ~DEV_DIS) | val;
ttp_dev.flags = (ttp_dev.flags & ~DEV_DIS) | (val ^ DEV_DIS);
tt_reset (&tt_dev);
ttp_reset (&ttp_dev);
return SCPE_OK;
}
