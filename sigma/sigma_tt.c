/* sigma_tt.c: Sigma 7012 console teletype

   Copyright (c) 2007-2008, Robert M. Supnik

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

   tt           7012 console

   The 7012 has the following special cases on input and output:

   CR           input, mapped to NEWLINE and echoes CR-LF
   ^H           input, mapped to EOM and not echoed
   HT           input or output, simulates tabbing with fixed 8 character stops
*/

#include "sigma_io_defs.h"
#include <ctype.h>

/* Device definitions */

#define TTI             0
#define TTO             1

/* Device states */

#define TTS_IDLE        0x0
#define TTS_INIT        0x1
#define TTS_END         0x2
#define TTS_WRITE       0x5
#define TTS_READ        0x6
#define TTS_READS       0x86

/* EBCDIC special characters for input */

#define E_EOM           0x08                            /* end of medium */
#define E_HT            0x05                            /* tab */
#define E_NL            0x15                            /* new line */

uint32 tt_cmd = TTS_IDLE;
uint32 tti_tps = RTC_HZ_100;
uint32 tti_panel = 020;                                 /* panel int char */
uint32 tto_pos = 0;                                     /* char position */

extern uint32 chan_ctl_time;
extern uint8 ascii_to_ebcdic[128];
extern uint8 ebcdic_to_ascii[256];

uint32 tt_disp (uint32 op, uint32 dva, uint32 *dvst);
uint32 tt_tio_status (void);
t_stat tt_chan_err (uint32 st);
t_stat tti_rtc_svc (uint32 tm);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tt_reset (DEVICE *dptr);
t_stat tt_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
void tto_echo (int32 c);

extern t_stat io_set_pint (void);

/* TT data structures

   tt_dev       TT device descriptor
   tt_unit      TT unit descriptors
   tt_reg       TT register list
   tt_mod       TT modifiers list
*/

dib_t tt_dib = { DVA_TT, tt_disp };

UNIT tt_unit[] = {
    { UDATA (&tti_svc, TT_MODE_UC, 0), 0 },
    { UDATA (&tto_svc, TT_MODE_UC, 0), SERIAL_OUT_WAIT }
    };

REG tt_reg[] = {
    { HRDATA (CMD, tt_cmd, 9) },
    { DRDATA (KPOS, tt_unit[TTI].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (KTPS, tti_tps, 8), REG_HRO },
    { DRDATA (TPOS, tt_unit[TTO].pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TTIME, tt_unit[TTO].wait, 24), REG_NZ + PV_LEFT },
    { HRDATA (PANEL, tti_panel, 8) },
    { HRDATA (DEVNO, tt_dib.dva, 12), REG_HRO },
    { NULL }
    };

MTAB tt_mod[] = {
    { TT_MODE, TT_MODE_UC,  "UC",  "UC",  &tt_set_mode },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  &tt_set_mode },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, RTC_TTI, "POLL", "POLL",
      &rtc_set_tps, &rtc_show_tps, (void *) &tti_tps },
    { MTAB_XTD|MTAB_VDV, 0, "CHAN", "CHAN",
      &io_set_dvc, &io_show_dvc, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DVA", "DVA",
      &io_set_dva, &io_show_dva, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "CSTATE", NULL,
      NULL, &io_show_cst, NULL },
    { 0 }
    };

DEVICE tt_dev = {
    "TT", tt_unit, tt_reg, tt_mod,
    2, 10, 31, 1, 16, 8,
    NULL, NULL, &tt_reset,
    NULL, NULL, NULL,
    &tt_dib, 0
    };

/* Terminal: IO dispatch routine */

uint32 tt_disp (uint32 op, uint32 dva, uint32 *dvst)
{
switch (op) {                                           /* case on op */

    case OP_SIO:                                        /* start I/O */
        *dvst = tt_tio_status ();                       /* get status */
        if ((*dvst & DVS_DST) == 0) {                   /* idle? */
            tt_cmd = TTS_INIT;                          /* start dev thread */
            sim_activate (&tt_unit[TTO], chan_ctl_time);
            }
        break;

    case OP_TIO:                                        /* test status */
        *dvst = tt_tio_status ();                       /* return status */
        break;

    case OP_HIO:                                        /* halt I/O */
        chan_clr_chi (tt_dib.dva);                      /* clr int*/
        *dvst = tt_tio_status ();                       /* get status */
        if ((*dvst & DVS_DST) != 0) {                   /* busy? */
            sim_cancel (&tt_unit[TTO]);                 /* stop dev thread */
            tt_cmd = TTS_IDLE;
            chan_uen (tt_dib.dva);                      /* uend */
            }
        break;

    case OP_AIO:                                        /* acknowledge int */
        chan_clr_chi (tt_dib.dva);                      /* clr int*/
    case OP_TDV:                                        /* test status */
        *dvst = 0;                                      /* no status */
        break;

    default:
        *dvst = 0;
        return SCPE_IERR;
        }

return 0;
}

/* Timed input service routine - runs continuously
   Only accepts input in TTS_READx state */

t_stat tti_svc (UNIT *uptr)
{
int32 c, ebcdic;
uint32 st;

if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or err? */
    return c;
if (c & SCPE_BREAK) {                                   /* break? */
    if (tt_cmd == TTS_WRITE) {                          /* during write? */
        tt_cmd = TTS_IDLE;
        sim_cancel (&tt_unit[TTO]);                     /* cancel write */
        chan_uen (tt_dib.dva);                          /* uend */
        }
    return SCPE_OK;
    }
c = c & 0x7F;
if (c == tti_panel)                                     /* panel interrupt? */
    return io_set_pint ();
uptr->pos = uptr->pos + 1;                              /* incr count */
if (c == '\r')                                          /* map CR to NL */
    c = '\n';
if (c == 0x7F)                                          /* map ^H back */
    c = 0x08;
c = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags));       /* input conversion */
ebcdic = ascii_to_ebcdic[c];                            /* then to EBCDIC */
tto_echo (c);                                           /* echo character */
if ((tt_cmd & 0x7F) == TTS_READ) {                      /* waiting for input? */
    st = chan_WrMemB (tt_dib.dva, ebcdic);              /* write to memory */
    if (CHS_IFERR (st))                                 /* channel error? */
       return tt_chan_err (st);
    if ((st == CHS_ZBC) || (ebcdic == E_EOM) ||         /* channel end? */
        ((tt_cmd == TTS_READS) && ((ebcdic == E_HT) || (ebcdic == E_NL)))) {
        tt_cmd = TTS_END;                               /* new state */
        sim_activate (&tt_unit[TTO], chan_ctl_time);    /* start dev thread */
        }
    }
return SCPE_OK;
}

/* Output service routine - also acts as overall device thread
   Because of possible retry, channel status and converted character
   must be preserved across calls. */

t_stat tto_svc (UNIT *uptr)
{
uint32 c, cmd;
uint32 st;

switch (tt_cmd) {                                       /* case on state */

    case TTS_INIT:                                      /* I/O init */
        st = chan_get_cmd (tt_dib.dva, &cmd);           /* get command */
        if (CHS_IFERR (st))                             /* channel error? */
            return tt_chan_err (st);
        if ((cmd == TTS_WRITE) ||                       /* valid command? */
            ((cmd & 0x7F) == TTS_READ))
            tt_cmd = cmd;                               /* next state */
        else tt_cmd = TTS_END;                          /* no, end state */
        sim_activate (uptr, chan_ctl_time);             /* continue thread */
        break;

    case TTS_WRITE:                                     /* char output */
        st = chan_RdMemB (tt_dib.dva, &c);              /* get char */
        if (CHS_IFERR (st))                             /* channel error? */
            return tt_chan_err (st);
        c = ebcdic_to_ascii[c & 0xFF];                  /* convert to ASCII */
        tto_echo (c);                                   /* echo character */
        sim_activate (uptr, uptr->wait);                /* continue thread */
        if (st == CHS_ZBC)                              /* st = zbc? */
            tt_cmd = TTS_END;                           /* next is end */
        else tt_cmd = TTS_WRITE;                        /* next is write */
        break;

    case TTS_END:                                       /* command done */
        st = chan_end (tt_dib.dva);                     /* set channel end */
        if (CHS_IFERR (st))                             /* channel error? */
            return tt_chan_err (st);
        if (st == CHS_CCH) {                            /* command chain? */
            tt_cmd = TTS_INIT;                          /* restart thread */
            sim_activate (uptr, chan_ctl_time);
            }
        else tt_cmd = TTS_IDLE;                         /* all done */
        break;
        }

return SCPE_OK;
}

/* Actual tty output routines; simulates horizontal tabs */

void tto_echo (int32 c)
{
uint32 cnt;

cnt = 1;
if (c == '\r')
    tto_pos = 0;
else if (c == '\n') {
    tto_pos = 0;
    sim_putchar ('\r');
    tt_unit[TTO].pos = tt_unit[TTO].pos + 1;
    }
else if (c == '\t') {
    c = ' ';
    cnt = 8 - (tto_pos % 8);
    }
else c = sim_tt_outcvt (c, TT_GET_MODE (tt_unit[TTO].flags));
if (c >= 0) {
    while (cnt-- > 0) {
        sim_putchar (c);
        tto_pos++;
        tt_unit[TTO].pos = tt_unit[TTO].pos + 1;
        }
    }
return;
}

/* TTY status routine */

uint32 tt_tio_status (void)
{
if (tt_cmd == TTS_IDLE)
    return DVS_AUTO;
return (CC2 << DVT_V_CC) | DVS_DBUSY | DVS_CBUSY | DVS_AUTO;
}

/* Channel error */

t_stat tt_chan_err (uint32 st)
{
tt_cmd = TTS_IDLE;
sim_cancel (&tt_unit[TTO]);                             /* stop dev thread */
chan_uen (tt_dib.dva);                                  /* uend */
if (st < CHS_ERR)
    return st;
return SCPE_OK;
}

/* Reset routine */

t_stat tt_reset (DEVICE *dptr)
{
rtc_register (RTC_TTI, tti_tps, &tt_unit[TTI]);         /* register timer */
sim_cancel (&tt_unit[TTO]);                             /* stop dev thread */
tt_cmd = TTS_IDLE;                                      /* idle */
chan_reset_dev (tt_dib.dva);                            /* clr int, active */
tto_pos = 0;
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
