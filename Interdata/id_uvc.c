/* id_uvc.c: Interdata universal clock

   Copyright (c) 2001-2012, Robert M. Supnik

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

   pic          precision incremental clock
   lfc          line frequency clock

   18-Apr-12    RMS     Added lfc_cosched routine
   18-Jun-07    RMS     Added UNIT_IDLE flag
   18-Oct-06    RMS     Changed LFC to be free running, export tmr_poll
   23-Jul-05    RMS     Fixed {} error in OC
   01-Mar-03    RMS     Added SET/SHOW LFC FREQ support
                        Changed precision clock algorithm for V7 UNIX
*/

#include "id_defs.h"
#include <ctype.h>

/* Device definitions */

#define UNIT_V_DIAG     (UNIT_V_UF + 0)                 /* diag mode */
#define UNIT_DIAG       (1 << UNIT_V_DIAG)

#define STA_OVF         0x08                            /* PIC overflow */
#define CMD_STRT        0x20                            /* start */
#define PIC_V_RATE      12                              /* rate */
#define PIC_M_RATE      0xF
#define PIC_RATE        (PIC_M_RATE << PIC_V_RATE)
#define PIC_CTR         0x0FFF                          /* PIC counters */
#define GET_RATE(x)     (((x) >> PIC_V_RATE) & PIC_M_RATE)
#define GET_CTR(x)      ((x) & PIC_CTR)
#define PIC_TPS         1000

extern uint32 int_req[INTSZ], int_enb[INTSZ];

int32 pic_db = 0;                                       /* output buf */
int32 pic_ric = 0;                                      /* reset count */
int32 pic_cic = 0;                                      /* current count */
uint32 pic_save = 0;                                    /* saved time */
uint32 pic_ovf = 0;                                     /* overflow */
uint32 pic_rdp = 0;
uint32 pic_wdp = 0;
uint32 pic_cnti = 0;                                    /* instr/timer */
uint32 pic_arm = 0;                                     /* int arm */
uint32 pic_decr = 1;                                    /* decrement */
uint16 pic_time[4] = { 1, 10, 100, 1000 };              /* delays */
uint16 pic_usec[4] = { 1, 10, 100, 1000 };              /* usec per tick */
static int32 pic_map[16] = {                            /* map rate to delay */
    0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0
    };

uint32 pic (uint32 dev, uint32 op, uint32 dat);
t_stat pic_svc (UNIT *uptr);
t_stat pic_reset (DEVICE *dptr);
void pic_sched (t_bool strt);
uint32 pic_rd_cic (void);

int32 lfc_tps = 120;                                    /* ticks per */
int32 lfc_poll = 8000;
uint32 lfc_arm = 0;                                     /* int arm */

uint32 lfc (uint32 dev, uint32 op, uint32 dat);
t_stat lfc_svc (UNIT *uptr);
t_stat lfc_reset (DEVICE *dptr);
t_stat lfc_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat lfc_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* PIC data structures

   pic_dev      PIC device descriptor
   pic_unit     PIC unit descriptor
   pic_reg      PIC register list
*/

DIB pic_dib = { d_PIC, -1, v_PIC, NULL, &pic, NULL };

UNIT pic_unit = { UDATA (&pic_svc, UNIT_IDLE, 0), 1000 };

REG pic_reg[] = {
    { HRDATA (BUF, pic_db, 16) },
    { HRDATA (RIC, pic_ric, 16) },
    { HRDATA (CIC, pic_cic, 12) },
    { FLDATA (RDP, pic_rdp, 0) },
    { FLDATA (WDP, pic_wdp, 0) },
    { FLDATA (OVF, pic_ovf, 0) },
    { FLDATA (IREQ, int_req[l_PIC], i_PIC) },
    { FLDATA (IENB, int_enb[l_PIC], i_PIC) },
    { FLDATA (IARM, pic_arm, 0) },
    { BRDATA (TIME, pic_time, 10, 16, 4), REG_NZ + PV_LEFT },
    { DRDATA (SAVE, pic_save, 32), REG_HRO + PV_LEFT },
    { DRDATA (DECR, pic_decr, 16), REG_HRO + PV_LEFT },
    { FLDATA (MODE, pic_cnti, 0), REG_HRO },
    { HRDATA (DEVNO, pic_dib.dno, 8), REG_HRO },
    { NULL }
    };

MTAB pic_mod[] = {
    { UNIT_DIAG, UNIT_DIAG, "diagnostic mode", "DIAG", NULL },
    { UNIT_DIAG, 0, NULL, "NORMAL", NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE pic_dev = {
    "PIC", &pic_unit, pic_reg, pic_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &pic_reset,
    NULL, NULL, NULL,
    &pic_dib, DEV_DISABLE
    };

/* LFC data structures

   lfc_dev      LFC device descriptor
   lfc_unit     LFC unit descriptor
   lfc_reg      LFC register list
*/

DIB lfc_dib = { d_LFC, -1, v_LFC, NULL, &lfc, NULL };

UNIT lfc_unit = { UDATA (&lfc_svc, UNIT_IDLE, 0), 8333 };

REG lfc_reg[] = {
    { FLDATA (IREQ, int_req[l_LFC], i_LFC) },
    { FLDATA (IENB, int_enb[l_LFC], i_LFC) },
    { FLDATA (IARM, lfc_arm, 0) },
    { DRDATA (TIME, lfc_unit.wait, 24), REG_NZ + PV_LEFT },
    { DRDATA (TPS, lfc_tps, 8), PV_LEFT + REG_HRO },
    { HRDATA (DEVNO, lfc_dib.dno, 8), REG_HRO },
    { NULL }
    };

MTAB lfc_mod[] = {
    { MTAB_XTD|MTAB_VDV, 100, NULL, "50HZ",
      &lfc_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 120, NULL, "60HZ",
      &lfc_set_freq, NULL, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "FREQUENCY", NULL,
      NULL, &lfc_show_freq, NULL },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO",
      &set_dev, &show_dev, NULL },
    { 0 }
    };

DEVICE lfc_dev = {
    "LFC", &lfc_unit, lfc_reg, lfc_mod,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &lfc_reset,
    NULL, NULL, NULL,
    &lfc_dib, DEV_DISABLE
    };

/* Precision clock: IO routine */

uint32 pic (uint32 dev, uint32 op, uint32 dat)
{
int32 t;

switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        return HW;                                      /* HW capable */

    case IO_RH:                                         /* read halfword */
        pic_rdp = 0;                                    /* clr ptr */
        return pic_rd_cic ();   

    case IO_RD:                                         /* read */
        t = pic_rd_cic ();                              /* get cic */
        if (pic_rdp)                                    /* 2nd? get lo */
            t = t & DMASK8;
        else t = (t >> 8) & DMASK8;                     /* 1st? get hi */
        pic_rdp = pic_rdp ^ 1;                          /* flip byte ptr */
        return t;

    case IO_WH:                                         /* write halfword */
        pic_wdp = 0;                                    /* clr ptr */
        pic_db = dat;
        break;

    case IO_WD:                                         /* write */
        if (pic_wdp)
            pic_db = (pic_db & 0xFF00) | dat;
        else pic_db = (pic_db & 0xFF) | (dat << 8);
        pic_wdp = pic_wdp ^ 1;                          /* flip byte ptr */
        break;

    case IO_SS:                                         /* sense status */
        if (pic_ovf) {                                  /* overflow? */
            pic_ovf = 0;                                /* clear flag */
            CLR_INT (v_PIC);                            /* clear intr */
            return STA_OVF;
            }
        return 0;

    case IO_OC:                                         /* output cmd */
        pic_arm = int_chg (v_PIC, dat, pic_arm);        /* upd int ctrl */
        if (dat & CMD_STRT) {                           /* start? */
            pic_ric = pic_db;                           /* new ric */
            pic_cic = GET_CTR (pic_ric);                /* new cic */
            pic_ovf = 0;                                /* clear flag */
            sim_cancel (&pic_unit);                     /* stop clock */
            pic_rdp = pic_wdp = 0;                      /* init ptrs */
            if (pic_ric & PIC_RATE)                     /* any rate? */
                pic_sched (TRUE);
            }                                           /* end if start */
        break;
        }                                               /* end case */

return 0;
}

/* Unit service */

t_stat pic_svc (UNIT *uptr)
{
t_bool rate_chg = FALSE;

if (pic_cnti)                                           /* one shot? */
    pic_cic = 0;
pic_cic = pic_cic - pic_decr;                           /* decrement */
if (pic_cic <= 0) {                                     /* overflow? */
    if (pic_wdp)                                        /* broken wr? set flag */
        pic_ovf = 1;
    if (pic_arm)                                        /* if armed, intr */
        SET_INT (v_PIC);
    if (GET_RATE (pic_ric) != GET_RATE (pic_db))        /* rate change? */
        rate_chg = TRUE;
    pic_ric = pic_db;                                   /* new ric */
    pic_cic = GET_CTR (pic_ric);                        /* new cic */
    if ((pic_ric & PIC_RATE) == 0)
        return SCPE_OK;
    }
pic_sched (rate_chg);
return SCPE_OK;
}

/* Schedule next interval

   If eff rate < 1ms, or diagnostic mode, count instructions
   If eff rate = 1ms, and not diagnostic mode, use timer
*/

void pic_sched (t_bool strt)
{
int32 r, t, intv, intv_usec;

pic_save = sim_grtime ();                               /* save start */
r = pic_map[GET_RATE (pic_ric)];                        /* get mapped rate */
intv = pic_cic? pic_cic: 1;                             /* get cntr */
intv_usec = intv * pic_usec[r];                         /* cvt to usec */
if (!(pic_unit.flags & UNIT_DIAG) &&                    /* not diag? */
    ((intv_usec % 1000) == 0)) {                        /* 1ms multiple? */
        pic_cnti = 0;                                   /* clr mode */
        pic_decr = pic_usec[3 - r];                     /* set decrement */
        if (strt)                                       /* init or */
            t = sim_rtcn_init (pic_time[3], TMR_PIC);
        else t = sim_rtcn_calb (PIC_TPS, TMR_PIC);      /* calibrate */
        }
else {
    pic_cnti = 1;                                       /* set mode */
    pic_decr = 1;                                       /* decr = 1 */
    t = pic_time[r] * intv;                             /* interval */
    if (t == 1)                                         /* for diagn */
        t++;
    }
sim_activate (&pic_unit, t);                            /* activate */
return;
}
            
/* Read (interpolated) current interval */

uint32 pic_rd_cic (void)
{
if (sim_is_active (&pic_unit) && pic_cnti) {            /* running, one shot? */
    uint32 delta = sim_grtime () - pic_save;            /* interval */
    uint32 tm = pic_time[pic_map[GET_RATE (pic_ric)]];  /* ticks/intv */
    delta = delta / tm;                                 /* ticks elapsed */
    if (delta >= ((uint32) pic_cic))                    /* cap value */
        return 0;
    return pic_cic - delta;
    }
return pic_cic;
}

/* Reset routine */

t_stat pic_reset (DEVICE *dptr)
{
sim_cancel (&pic_unit);                                 /* cancel unit */
pic_ric = pic_cic = 0;
pic_db = 0;
pic_ovf = 0;                                            /* clear state */
pic_cnti = 0;
pic_decr = 1;
pic_rdp = pic_wdp = 0;
CLR_INT (v_PIC);                                        /* clear int */
CLR_ENB (v_PIC);                                        /* disable int */
pic_arm = 0;                                            /* disarm int */
return SCPE_OK;
}

/* Line clock: IO routine */

uint32 lfc (uint32 dev, uint32 op, uint32 dat)
{
switch (op) {                                           /* case IO op */

    case IO_ADR:                                        /* select */
        return BY;                                      /* byte only */

    case IO_OC:                                         /* command */
        lfc_arm = int_chg (v_LFC, dat, lfc_arm);        /* upd int ctrl */
        break;
        }
return 0;
}

/* Unit service */

t_stat lfc_svc (UNIT *uptr)
{
lfc_poll = sim_rtcn_calb (lfc_tps, TMR_LFC);            /* calibrate */
sim_activate (uptr, lfc_poll);                          /* reactivate */
if (lfc_arm) {                                          /* armed? */
    SET_INT (v_LFC);                                    /* req intr */
    }
return SCPE_OK;
}

/* Clock coscheduling routine */

int32 lfc_cosched (int32 wait)
{
int32 t;

t = sim_activate_time (&lfc_unit);
return (t? t - 1: wait);
}

/* Reset routine */

t_stat lfc_reset (DEVICE *dptr)
{
lfc_poll = sim_rtcn_init (lfc_unit.wait, TMR_LFC);
sim_activate (&lfc_unit, lfc_poll);                     /* init clock */
CLR_INT (v_LFC);                                        /* clear int */
CLR_ENB (v_LFC);                                        /* disable int */
lfc_arm = 0;                                            /* disarm int */
return SCPE_OK;
}

/* Set frequency */

t_stat lfc_set_freq (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (cptr)
    return SCPE_ARG;
if ((val != 100) && (val != 120))
    return SCPE_IERR;
lfc_tps = val;
return SCPE_OK;
}

/* Show frequency */

t_stat lfc_show_freq (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
fprintf (st, (lfc_tps == 100)? "50Hz": "60Hz");
return SCPE_OK;
}

