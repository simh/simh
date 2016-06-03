/* id_ttp.c: Interdata PASLA console interface

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

   ttp          console (on PAS)

   18-Apr-12    RMS     Revised to use clock coscheduling
   18-Jun-07    RMS     Added UNIT_IDLE flag to console input
   18-Oct-06    RMS     Sync keyboard to LFC clock
   22-Nov-05    RMS     Revised for new terminal processing routines
   29-Dec-03    RMS     Added support for console backpressure
   25-Apr-03    RMS     Revised for extended file support
*/

#include "id_defs.h"
#include <ctype.h>

#define TTI             0
#define TTO             1

/* Status byte */

#define STA_OVR         0x80                            /* overrun RO */
#define STA_PF          0x40                            /* parity err RO */
#define STA_FR          0x20                            /* framing err RO */
#define STA_RCV         (STA_OVR|STA_PF|STA_FR)
#define SET_EX          (STA_OVR|STA_PF|STA_FR)
#define STA_XMT         (STA_BSY)

/* Command bytes 1,0 */

#define CMD_ECHO        (0x10 << 8)                     /* echoplex */
#define CMD_WRT         (0x02 << 8)                     /* write/read */
#define CMD_TYP         0x01                            /* command type */

extern uint32 int_req[INTSZ], int_enb[INTSZ];
extern int32 pas_par (int32 cmd, int32 c);
extern int32 lfc_poll;

uint32 ttp_sta = 0;                                     /* status */
uint32 ttp_cmd = 0;                                     /* command */
uint32 ttp_kchp = 0;                                    /* rcvr chr pend */
uint32 ttp_karm = 0;                                    /* rcvr int armed */
uint32 ttp_tarm = 0;                                    /* xmt int armed */
uint8 ttp_tplte[] = { 0, 1, TPL_END };

uint32 ttp (uint32 dev, uint32 op, uint32 dat);
t_stat ttpi_svc (UNIT *uptr);
t_stat ttpo_svc (UNIT *uptr);
t_stat ttp_reset (DEVICE *dptr);
t_stat ttp_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ttp_set_break (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ttp_set_enbdis (UNIT *uptr, int32 val, CONST char *cptr, void *desc);

/* TTP data structures */

DIB ttp_dib = { d_TTP, -1, v_TTP, ttp_tplte, &ttp, NULL };

UNIT ttp_unit[] = {
    { UDATA (&ttpi_svc, UNIT_IDLE, 0), 0 },
    { UDATA (&ttpo_svc, 0, 0), SERIAL_OUT_WAIT }
    };

REG ttp_reg[] = {
    { HRDATA (CMD, ttp_cmd, 16) },
    { HRDATA (KBUF, ttp_unit[TTI].buf, 8) },
    { DRDATA (KPOS, ttp_unit[TTI].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (KTIME, ttp_unit[TTI].wait, 24), REG_NZ + PV_LEFT + REG_HRO },
    { FLDATA (KIREQ, int_req[l_TTP], i_TTP) },
    { FLDATA (KIENB, int_enb[l_TTP], i_TTP) },
    { FLDATA (KARM, ttp_karm, 0) },
    { FLDATA (CHP, ttp_kchp, 0) },
    { HRDATA (TBUF, ttp_unit[TTO].buf, 8) },
    { DRDATA (TPOS, ttp_unit[TTO].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TTIME, ttp_unit[TTO].wait, 24), REG_NZ + PV_LEFT },
    { FLDATA (TIREQ, int_req[l_TTP], i_TTP + 1) },
    { FLDATA (TIENB, int_enb[l_TTP], i_TTP + 1) },
    { FLDATA (TARM, ttp_tarm, 0) },
    { HRDATA (DEVNO, ttp_dib.dno, 8), REG_HRO },
    { NULL }
    };

MTAB ttp_mod[] = {
    { TT_MODE, TT_MODE_UC, "UC", "UC", &ttp_set_mode },
    { TT_MODE, TT_MODE_7B, "7b", "7B", &ttp_set_mode },
    { TT_MODE, TT_MODE_8B, "8b", "8B", &ttp_set_mode },
    { TT_MODE, TT_MODE_7P, "7p", "7P", &ttp_set_mode },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, NULL, "ENABLED",
      &ttp_set_enbdis, NULL, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, DEV_DIS, NULL, "DISABLED",
      &ttp_set_enbdis, NULL, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, NULL, "BREAK",
      &ttp_set_break, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE ttp_dev = {
    "TTP", ttp_unit, ttp_reg, ttp_mod,
    2, 10, 31, 1, 16, 8,
    NULL, NULL, &ttp_reset,
    NULL, NULL, NULL,
    &ttp_dib, DEV_DIS
    };

/* Terminal: I/O routine */

uint32 ttp (uint32 dev, uint32 op, uint32 dat)
{
int32 xmt = dev & 1;
int32 t;

switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        return BY;                                      /* byte only */

    case IO_RD:                                         /* read */
        ttp_kchp = 0;                                   /* clr chr pend */
        ttp_sta = ttp_sta & ~STA_OVR;                   /* clr overrun */
        return ttp_unit[TTI].buf;                       /* return buf */

    case IO_WD:                                         /* write */
        ttp_unit[TTO].buf = dat & 0xFF;                 /* store char */
        ttp_sta = ttp_sta | STA_BSY;                    /* set busy */
        sim_activate (&ttp_unit[TTO], ttp_unit[TTO].wait);
        break;

    case IO_SS:                                         /* status */
        if (xmt) t = ttp_sta & STA_XMT;                 /* xmt? just busy */
        else {                                          /* rcv */
            t = ttp_sta & STA_RCV;                      /* get static */
            if (!ttp_kchp)                              /* no char? busy */
                t = t | STA_BSY;
            if (t & SET_EX)                             /* test for ex */
                t = t | STA_EX;
            }
        return t;

    case IO_OC:                                         /* command */
        if (dat & CMD_TYP) {                            /* type 1? */
            ttp_cmd = (ttp_cmd & 0xFF) | (dat << 8);
            if (ttp_cmd & CMD_WRT)                      /* write? */
                ttp_tarm = int_chg (v_TTP + 1, dat, ttp_tarm);
            else ttp_karm = int_chg (v_TTP, dat, ttp_karm);
            }
        else ttp_cmd = (ttp_cmd & ~0xFF) | dat;
        break;
        }

return 0;
}

/* Unit service */

t_stat ttpi_svc (UNIT *uptr)
{
int32 c, out;

sim_activate (uptr, KBD_WAIT (uptr->wait, lfc_cosched (lfc_poll)));
                                                        /* continue poll */
ttp_sta = ttp_sta & ~STA_FR;                            /* clear break */
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
ttp_sta = ttp_sta & ~STA_PF;                            /* clear parity err */
if (ttp_kchp)                                           /* overrun? */
    ttp_sta = ttp_sta | STA_OVR;
if (ttp_karm)
    SET_INT (v_TTP);
if (c & SCPE_BREAK) {                                   /* break? */
    ttp_sta = ttp_sta | STA_FR;                         /* framing error */
    uptr->buf = 0;                                      /* no character */
    }
else {
    out = c & 0x7F;                                     /* echo is 7b */
    c = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));
    if (TT_GET_MODE (uptr->flags) != TT_MODE_8B)        /* not 8b mode? */
        c = pas_par (ttp_cmd, c);                       /* apply parity */
    uptr->buf = c;                                      /* save char */
    uptr->pos = uptr->pos + 1;                          /* incr count */
    ttp_kchp = 1;                                       /* char pending */
    if (ttp_cmd & CMD_ECHO) {
        out = sim_tt_outcvt (out, TT_GET_MODE (uptr->flags));
        if (c >= 0)
            sim_putchar (out);
        ttp_unit[TTO].pos = ttp_unit[TTO].pos + 1;
        }
    }
return SCPE_OK;
}

t_stat ttpo_svc (UNIT *uptr)
{
int32 c;
t_stat r;

if (TT_GET_MODE (uptr->flags) == TT_MODE_8B)            /* 8b? */
    c = pas_par (ttp_cmd, uptr->buf);                   /* apply parity */
else c = sim_tt_outcvt (uptr->buf, TT_GET_MODE (uptr->flags));
if (c >= 0) {
    if ((r = sim_putchar_s (c)) != SCPE_OK) {           /* output; error? */
        sim_activate (uptr, uptr->wait);                /* try again */
        return ((r == SCPE_STALL)? SCPE_OK: r);
        }
    }
ttp_sta = ttp_sta & ~STA_BSY;                           /* not busy */
if (ttp_tarm)                                           /* set intr */
    SET_INT (v_TTP + 1);
uptr->pos = uptr->pos + 1;                              /* incr count */
return SCPE_OK;
}

/* Reset routine */

t_stat ttp_reset (DEVICE *dptr)
{
if (dptr->flags & DEV_DIS)
    sim_cancel (&ttp_unit[TTI]);
else sim_activate (&ttp_unit[TTI], KBD_WAIT (ttp_unit[TTI].wait, lfc_poll));
sim_cancel (&ttp_unit[TTO]);
CLR_INT (v_TTP);                                        /* clear int */
CLR_ENB (v_TTP);
CLR_INT (v_TTP + 1);                                    /* disable int */
CLR_ENB (v_TTP + 1);
ttp_karm = ttp_tarm = 0;                                /* disarm int */
ttp_cmd = 0;
ttp_sta = 0;
ttp_kchp = 0;
return SCPE_OK;
}

/* Make mode flags uniform */

t_stat ttp_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
ttp_unit[TTO].flags = (ttp_unit[TTO].flags & ~TT_MODE) | val;
if (val == TT_MODE_7P)
    val = TT_MODE_7B;
ttp_unit[TTI].flags = (ttp_unit[TTI].flags & ~TT_MODE) | val;
return SCPE_OK;
}

/* Set input break */

t_stat ttp_set_break (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (ttp_dev.flags & DEV_DIS)
    return SCPE_NOFNC;
ttp_sta = ttp_sta | STA_FR;
if (ttp_karm)                                           /* if armed, intr */
    SET_INT (v_TTP);
sim_cancel (&ttp_unit[TTI]);                            /* restart TT poll */
sim_activate (&ttp_unit[TTI], ttp_unit[TTI].wait);
return SCPE_OK;
}

/* Set enabled/disabled */

t_stat ttp_set_enbdis (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
extern DEVICE tt_dev;
extern t_stat tt_reset (DEVICE *dptr);

ttp_dev.flags = (ttp_dev.flags & ~DEV_DIS) | val;
tt_dev.flags = (tt_dev.flags & ~DEV_DIS) | (val ^ DEV_DIS);
ttp_reset (&ttp_dev);
tt_reset (&tt_dev);
return SCPE_OK;
}
