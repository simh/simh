/* gri_stddev.c: GRI-909 standard devices

   Copyright (c) 2001-2008, Robert M Supnik

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

   tti          S42-001 terminal input
   tto          S42-002 terminal output
   hsr          S42-004 high speed reader
   hsp          S42-006 high speed punch
   rtc          real time clock

   31-May-08    RMS     Fixed declarations (Peter Schorn)
   30-Sep-06    RMS     Fixed handling of non-printable characters in KSR mode
   22-Nov-05    RMS     Revised for new terminal processing routines
   29-Dec-03    RMS     Added support for console backpressure
   25-Apr-03    RMS     Revised for extended file support
   22-Dec-02    RMS     Added break support
   01-Nov-02    RMS     Added 7b/8B support to terminal
*/

#include "gri_defs.h"
#include "sim_tmxr.h"
#include <ctype.h>

uint32 hsr_stopioe = 1, hsp_stopioe = 1;

extern uint16 M[];
extern uint32 dev_done, ISR;

t_stat tti_svc (UNIT *uhsr);
t_stat tto_svc (UNIT *uhsr);
t_stat tti_reset (DEVICE *dhsr);
t_stat tto_reset (DEVICE *dhsr);
t_stat tty_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat hsr_svc (UNIT *uhsr);
t_stat hsp_svc (UNIT *uhsr);
t_stat hsr_reset (DEVICE *dhsr);
t_stat hsp_reset (DEVICE *dhsr);
t_stat rtc_svc (UNIT *uhsr);
t_stat rtc_reset (DEVICE *dhsr);
int32 rtc_tps = 1000;

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit descriptor
   tti_reg      TTI register list
   tti_mod      TTI modifiers list
*/

UNIT tti_unit = { UDATA (&tti_svc, TT_MODE_KSR, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
    { ORDATA (BUF, tti_unit.buf, 8) },
    { FLDATA (IRDY, dev_done, INT_V_TTI) },
    { FLDATA (IENB, ISR, INT_V_TTI) },
    { DRDATA (POS, tti_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tti_unit.wait, 24), REG_NZ + PV_LEFT },
    { NULL }
    };

MTAB tti_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", &tty_set_mode },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  &tty_set_mode },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  &tty_set_mode },
    { TT_MODE, TT_MODE_7P,  "7b",  NULL,  NULL },
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, tti_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tti_reset,
    NULL, NULL, NULL
    };

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit descriptor
   tto_reg      TTO register list
*/

UNIT tto_unit = { UDATA (&tto_svc, TT_MODE_KSR, 0), SERIAL_OUT_WAIT };

REG tto_reg[] = {
    { ORDATA (BUF, tto_unit.buf, 8) },
    { FLDATA (ORDY, dev_done, INT_V_TTO) },
    { FLDATA (IENB, ISR, INT_V_TTO) },
    { DRDATA (POS, tto_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, tto_unit.wait, 24), PV_LEFT },
    { NULL }
    };

MTAB tto_mod[] = {
    { TT_MODE, TT_MODE_KSR, "KSR", "KSR", &tty_set_mode },
    { TT_MODE, TT_MODE_7B,  "7b",  "7B",  &tty_set_mode },
    { TT_MODE, TT_MODE_8B,  "8b",  "8B",  &tty_set_mode },
    { TT_MODE, TT_MODE_7P,  "7p",  "7P",  &tty_set_mode },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, tto_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tto_reset, 
    NULL, NULL, NULL
    };

/* HSR data structures

   hsr_dev      HSR device descriptor
   hsr_unit     HSR unit descriptor
   hsr_reg      HSR register list
   hsr_mod      HSR modifiers list
*/

UNIT hsr_unit = {
    UDATA (&hsr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0), SERIAL_IN_WAIT
    };

REG hsr_reg[] = {
    { ORDATA (BUF, hsr_unit.buf, 8) },
    { FLDATA (IRDY, dev_done, INT_V_HSR) },
    { FLDATA (IENB, ISR, INT_V_HSR) },
    { DRDATA (POS, hsr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, hsr_unit.wait, 24), REG_NZ + PV_LEFT },
    { FLDATA (STOP_IOE, hsr_stopioe, 0) },
    { NULL }
    };

DEVICE hsr_dev = {
    "HSR", &hsr_unit, hsr_reg, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &hsr_reset,
    NULL, NULL, NULL
    };

/* HSP data structures

   hsp_dev      HSP device descriptor
   hsp_unit     HSP unit descriptor
   hsp_reg      HSP register list
*/

UNIT hsp_unit = {
    UDATA (&hsp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT
    };

REG hsp_reg[] = {
    { ORDATA (BUF, hsp_unit.buf, 8) },
    { FLDATA (ORDY, dev_done, INT_V_HSP) },
    { FLDATA (IENB, ISR, INT_V_HSP) },
    { DRDATA (POS, hsp_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TIME, hsp_unit.wait, 24), PV_LEFT },
    { FLDATA (STOP_IOE, hsp_stopioe, 0) },
    { NULL }
    };

DEVICE hsp_dev = {
    "HSP", &hsp_unit, hsp_reg, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &hsp_reset,
    NULL, NULL, NULL
    };

/* RTC data structures

   rtc_dev      RTC device descriptor
   rtc_unit     RTC unit descriptor
   rtc_reg      RTC register list
*/

UNIT rtc_unit = { UDATA (&rtc_svc, 0, 0), 16000 };

REG rtc_reg[] = {
    { FLDATA (RDY, dev_done, INT_V_RTC) },
    { FLDATA (IENB, ISR, INT_V_RTC) },
    { DRDATA (TIME, rtc_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (TPS, rtc_tps, 8), REG_NZ + PV_LEFT + REG_HIDDEN },
    { NULL }
    };

DEVICE rtc_dev = {
    "RTC", &rtc_unit, rtc_reg, NULL,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &rtc_reset,
    NULL, NULL, NULL
    };

/* Console terminal function processors */

uint32 tty_rd (int32 src, int32 ea)
{
return tti_unit.buf;                                    /* return data */
}

t_stat tty_wr (uint32 dst, uint32 val)
{
tto_unit.buf = val & 0377;                              /* save char */
dev_done = dev_done & ~INT_TTO;                         /* clear ready */
sim_activate (&tto_unit, tto_unit.wait);                /* activate unit */
return SCPE_OK;
}

t_stat tty_fo (uint32 op)
{
if (op & TTY_IRDY)
    dev_done = dev_done & ~INT_TTI;
if (op & TTY_ORDY)
    dev_done = dev_done & ~INT_TTO;
return SCPE_OK;
}

uint32 tty_sf (uint32 op)
{
if (((op & TTY_IRDY) && (dev_done & INT_TTI)) ||
    ((op & TTY_ORDY) && (dev_done & INT_TTO)))
    return 1;
return 0;
}

/* Service routines */

t_stat tti_svc (UNIT *uptr)
{
int32 c;

sim_activate (uptr, uptr->wait);                        /* continue poll */
if ((c = sim_poll_kbd ()) < SCPE_KFLAG)                 /* no char or error? */
    return c;
if (c & SCPE_BREAK)                                     /* break? */
    uptr->buf = 0;
else uptr->buf = sim_tt_inpcvt (c, TT_GET_MODE (uptr->flags) | TTUF_KSR);
dev_done = dev_done | INT_TTI;                          /* set ready */
uptr->pos = uptr->pos + 1;
return SCPE_OK;
}

t_stat tto_svc (UNIT *uptr)
{
int32 c;
t_stat r;

c = sim_tt_outcvt (uptr->buf, TT_GET_MODE (uptr->flags) | TTUF_KSR);
if (c >= 0) {
    if ((r = sim_putchar_s (c)) != SCPE_OK) {           /* output; error? */
        sim_activate (uptr, uptr->wait);                /* try again */
        return ((r == SCPE_STALL)? SCPE_OK: r);         /* !stall? report */
        }
    }
dev_done = dev_done | INT_TTO;                          /* set ready */
uptr->pos = uptr->pos + 1;
return SCPE_OK;
}

/* Reset routines */

t_stat tti_reset (DEVICE *dptr)
{
tmxr_set_console_units (&tti_unit, &tto_unit);
tti_unit.buf = 0;                                       /* clear buffer */
dev_done = dev_done & ~INT_TTI;                         /* clear ready */
sim_activate (&tti_unit, tti_unit.wait);                /* activate unit */
return SCPE_OK;
}

t_stat tto_reset (DEVICE *dptr)
{
tto_unit.buf = 0;                                       /* clear buffer */
dev_done = dev_done | INT_TTO;                          /* set ready */
sim_cancel (&tto_unit);                                 /* deactivate unit */
return SCPE_OK;
}

t_stat tty_set_mode (UNIT *uptr, int32 val, char *cptr, void *desc)
{
tti_unit.flags = (tti_unit.flags & ~TT_MODE) | val;
tto_unit.flags = (tto_unit.flags & ~TT_MODE) | val;
return SCPE_OK;
}

/* High speed paper tape function processors */

uint32 hsrp_rd (int32 src, int32 ea)
{
return hsr_unit.buf;                                    /* return data */
}

t_stat hsrp_wr (uint32 dst, uint32 val)
{
hsp_unit.buf = val & 0377;                              /* save char */
dev_done = dev_done & ~INT_HSP;                         /* clear ready */
sim_activate (&hsp_unit, hsp_unit.wait);                /* activate unit */
return SCPE_OK;
}

t_stat hsrp_fo (uint32 op)
{
if (op & PT_IRDY)
    dev_done = dev_done & ~INT_HSR;
if (op & PT_ORDY)
    dev_done = dev_done & ~INT_HSP;
if (op & PT_STRT)
    sim_activate (&hsr_unit, hsr_unit.wait);
return SCPE_OK;
}

uint32 hsrp_sf (uint32 op)
{
if (((op & PT_IRDY) && (dev_done & INT_HSR)) ||
    ((op & PT_ORDY) && (dev_done & INT_HSP)))
    return 1;
return 0;
}

t_stat hsr_svc (UNIT *uptr)
{
int32 temp;

if ((hsr_unit.flags & UNIT_ATT) == 0)                   /* attached? */
    return IORETURN (hsr_stopioe, SCPE_UNATT);
if ((temp = getc (hsr_unit.fileref)) == EOF) {          /* read char */
    if (feof (hsr_unit.fileref)) {                      /* err or eof? */
        if (hsr_stopioe)
            sim_printf ("HSR end of file\n");
        else return SCPE_OK;
        }
    else perror ("HSR I/O error");
    clearerr (hsr_unit.fileref);
    return SCPE_IOERR;
    }
dev_done = dev_done | INT_HSR;                          /* set ready */
hsr_unit.buf = temp & 0377;                             /* save char */
hsr_unit.pos = hsr_unit.pos + 1;
return SCPE_OK;
}

t_stat hsp_svc (UNIT *uptr)
{
dev_done = dev_done | INT_HSP;                          /* set ready */
if ((hsp_unit.flags & UNIT_ATT) == 0)                   /* attached? */
    return IORETURN (hsp_stopioe, SCPE_UNATT);
if (putc (hsp_unit.buf, hsp_unit.fileref) == EOF) {     /* write char */
    perror ("HSP I/O error");                           /* error? */
    clearerr (hsp_unit.fileref);
    return SCPE_IOERR;
    }
hsp_unit.pos = hsp_unit.pos + 1;
return SCPE_OK;
}

/* Reset routines */

t_stat hsr_reset (DEVICE *dptr)
{
hsr_unit.buf = 0;                                       /* clear buffer */
dev_done = dev_done & ~INT_HSR;                         /* clear ready */
sim_cancel (&hsr_unit);                                 /* deactivate unit */
return SCPE_OK;
}

t_stat hsp_reset (DEVICE *dptr)
{
hsp_unit.buf = 0;                                       /* clear buffer */
dev_done = dev_done | INT_HSP;                          /* set ready */
sim_cancel (&hsp_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Clock function processors */

t_stat rtc_fo (int32 op)
{
if (op & RTC_OFF)                                       /* clock off? */
    sim_cancel (&rtc_unit);
if ((op & RTC_ON) && !sim_is_active (&rtc_unit))        /* clock on? */
    sim_activate (&rtc_unit, sim_rtc_init (rtc_unit.wait));
if (op & RTC_OV)                                        /* clr ovflo? */
    dev_done = dev_done & ~INT_RTC;
return SCPE_OK;
}

uint32 rtc_sf (int32 op)
{
if ((op & RTC_OV) && (dev_done & INT_RTC))
    return 1;
return 0;
}

t_stat rtc_svc (UNIT *uptr)
{
M[RTC_CTR] = (M[RTC_CTR] + 1) & DMASK;                  /* incr counter */
if (M[RTC_CTR] == 0)                                    /* ovflo? set ready */
    dev_done = dev_done | INT_RTC;
sim_activate (&rtc_unit, sim_rtc_calb (rtc_tps));       /* reactivate */
return SCPE_OK;
}

t_stat rtc_reset (DEVICE *dptr)
{
sim_register_clock_unit (&rtc_unit);                    /* declare clock unit */
dev_done = dev_done & ~INT_RTC;                         /* clear ready */
sim_cancel (&rtc_unit);                                 /* stop clock */
return SCPE_OK;
}
