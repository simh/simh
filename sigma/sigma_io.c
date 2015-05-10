/* sigma_io.c: XDS Sigma IO simulator

   Copyright (c) 2007-2008, Robert M Supnik

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
*/

#include "sigma_io_defs.h"

#define VALID_DVA(c,d)  \
    (((c) < chan_num) && ((d) < CHAN_N_DEV) && (chan[c].disp[d] != NULL))

uint32 int_hiact = NO_INT;                              /* hi act int */
uint32 int_hireq = NO_INT;                              /* hi int req */
uint32 chan_ctl_time = 5;
uint32 ei_bmax = EIGRP_DFLT;                            /* ext int grps */
uint32 s9_snap = 0;
uint32 s9_marg = 0;
uint32 chan_num = CHAN_DFLT;                            /* num chan */
uint32 s5x0_ireg[] = { 0 };
uint16 int_arm[INTG_MAX];                               /* int grps: arm */
uint16 int_enb[INTG_MAX];                               /* enable */
uint16 int_req[INTG_MAX];                               /* request */
uint8 int_lnk[INTG_MAX] = {                             /* pri chain */
    INTG_OVR,  INTG_CTR,  INTG_IO,   0
    };

/* Interrupt group priority chain templates */

#define I_STD           0x80

static uint8 igrp_dflt_5x0[] = {
    I_STD|INTG_OVR, I_STD|INTG_CTR, I_STD|INTG_IO,    INTG_E2,
    INTG_E3,        INTG_E3+1,      INTG_E3+2,      0
    };

static uint8 igrp_dflt_S56789[] = {
    I_STD|INTG_OVR, I_STD|INTG_CTR, I_STD|INTG_IO,  INTG_E2,
    INTG_E3,        INTG_E3+1,      INTG_E3+2,      INTG_E3+3,
    INTG_E3+4,      INTG_E3+5,      INTG_E3+6,      INTG_E3+7,
    INTG_E3+9,      INTG_E3+9,      INTG_E3+10,     INTG_E3+11,
    INTG_E3+12,     0
    };

chan_t chan[CHAN_N_CHAN];
uint32 (*dio_disp[DIO_N_MOD])(uint32, uint32, uint32);

int_grp_t int_tab[INTG_MAX] = {
/*    PSW inh #bits vec   grp regbit */
    { 0,        6, 0x052, 0x0, 16 },
    { PSW2_CI,  4, 0x058, 0x0, 22 },
    { PSW2_II,  2, 0x05C, 0x0, 26 },
    { PSW2_EI, 16, 0x060, 0x2, 16 },
    { PSW2_EI, 16, 0x070, 0x3, 16 },
    { PSW2_EI, 16, 0x080, 0x4, 16 },
    { PSW2_EI, 16, 0x090, 0x5, 16 },
    { PSW2_EI, 16, 0x0A0, 0x6, 16 },
    { PSW2_EI, 16, 0x0B0, 0x7, 16 },
    { PSW2_EI, 16, 0x0C0, 0x8, 16 },
    { PSW2_EI, 16, 0x0D0, 0x9, 16 },
    { PSW2_EI, 16, 0x0E0, 0xA, 16 },
    { PSW2_EI, 16, 0x0F0, 0xB, 16 },
    { PSW2_EI, 16, 0x100, 0xC, 16 },
    { PSW2_EI, 16, 0x110, 0xD, 16 },
    { PSW2_EI, 16, 0x120, 0xE, 16 },
    { PSW2_EI, 16, 0x130, 0xF, 16 }
    };

extern uint32 *R;
extern uint32 PSW1, PSW2;
extern uint32 CC, SSW;
extern uint32 stop_op;
extern uint32 cpu_model;
extern uint32 cons_alarm, cons_pcf;
extern UNIT cpu_unit;
extern cpu_var_t cpu_tab[];

void io_eval_ioint (void);
t_bool io_init_inst (uint32 ad, uint32 rn, uint32 ch, uint32 dev, uint32 r0);
uint32 io_set_status (uint32 rn, uint32 ch, uint32 dev, uint32 dvst, t_bool tdv);
uint32 io_rwd_m0 (uint32 op, uint32 rn, uint32 ad);
uint32 io_rwd_m1 (uint32 op, uint32 rn, uint32 ad);
t_stat io_set_eiblks (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat io_show_eiblks (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat int_reset (DEVICE *dptr);
t_stat chan_reset (DEVICE *dptr);
uint32 chan_new_cmd (uint32 ch, uint32 dev, uint32 clc);
void io_set_eimax (uint32 max);
uint32 chan_proc_prolog (uint32 dva, uint32 *ch, uint32 *dev);
uint32 chan_proc_epilog (uint32 dva, int32 cnt);

extern uint32 cpu_new_PSD (uint32 lrp, uint32 p1, uint32 p2);

/* IO data structures

   io_dev      IO device descriptor
   io_unit     IO unit
   io_reg      IO register list
   io_mod      IO modifier list
*/

dib_t int_dib = { 0, NULL, 1, io_rwd_m1 };

UNIT int_unit = { UDATA (NULL, 0, 0) };

REG int_reg[] = {
    { HRDATA (IHIACT, int_hiact, 9) },
    { HRDATA (IHIREQ, int_hireq, 9) },
    { BRDATA (IREQ, int_req, 16, 16, INTG_MAX) },
    { BRDATA (IENB, int_enb, 16, 16, INTG_MAX) },
    { BRDATA (IARM, int_arm, 16, 16, INTG_MAX) },
    { BRDATA (ILNK, int_lnk, 10, 8, INTG_MAX), REG_HRO },
    { DRDATA (EIBLKS, ei_bmax, 4), REG_HRO },
    { HRDATA (S9_SNAP, s9_snap, 32) },
    { HRDATA (S9_MARG, s9_marg, 32) },
    { BRDATA (S5X0_IREG, s5x0_ireg, 16, 32, 32) },
    { NULL }
    };

MTAB int_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "EIBLKS", "EIBLKS",
      &io_set_eiblks, &io_show_eiblks },
    { 0 }
    };

DEVICE int_dev = {
    "INT", &int_unit, int_reg, int_mod,
    1, 16, 16, 1, 16, 32,
    NULL, NULL, &int_reset,
    NULL, NULL, NULL,
    &int_dib, 0
    };

/* Channel data structures */

UNIT chan_unit[] = {
    { UDATA (NULL, 0, 0) },
    { UDATA (NULL, 0, 0) },
    { UDATA (NULL, 0, 0) },
    { UDATA (NULL, 0, 0) },
    { UDATA (NULL, 0, 0) },
    { UDATA (NULL, 0, 0) },
    { UDATA (NULL, 0, 0) },
    { UDATA (NULL, 0, 0) }
    };

REG chana_reg[] = {
    { BRDATA (CLC, chan[0].clc, 16, 20, CHAN_N_DEV) },
    { BRDATA (CMD, chan[0].cmd, 16, 8, CHAN_N_DEV) },
    { BRDATA (CMF, chan[0].cmf, 16, 8, CHAN_N_DEV) },
    { BRDATA (BA, chan[0].ba, 16, 24, CHAN_N_DEV) },
    { BRDATA (BC, chan[0].bc, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHF, chan[0].chf, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHI, chan[0].chi, 16, 8, CHAN_N_DEV) },
    { BRDATA (CHSF, chan[0].chsf, 16, 8, CHAN_N_DEV) },
    { NULL }
    };

REG chanb_reg[] = {
    { BRDATA (CLC, chan[1].clc, 16, 20, CHAN_N_DEV) },
    { BRDATA (CMD, chan[1].cmd, 16, 8, CHAN_N_DEV) },
    { BRDATA (CMF, chan[1].cmf, 16, 8, CHAN_N_DEV) },
    { BRDATA (BA, chan[1].ba, 16, 24, CHAN_N_DEV) },
    { BRDATA (BC, chan[1].bc, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHF, chan[1].chf, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHI, chan[1].chi, 16, 8, CHAN_N_DEV) },
    { BRDATA (CHSF, chan[1].chsf, 16, 8, CHAN_N_DEV) },
    { NULL }
    };

REG chanc_reg[] = {
    { BRDATA (CLC, chan[2].clc, 16, 20, CHAN_N_DEV) },
    { BRDATA (CMD, chan[2].cmd, 16, 8, CHAN_N_DEV) },
    { BRDATA (CMF, chan[2].cmf, 16, 8, CHAN_N_DEV) },
    { BRDATA (BA, chan[2].ba, 16, 24, CHAN_N_DEV) },
    { BRDATA (BC, chan[2].bc, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHF, chan[2].chf, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHI, chan[2].chi, 16, 8, CHAN_N_DEV) },
    { BRDATA (CHSF, chan[2].chsf, 16, 8, CHAN_N_DEV) },
    { NULL }
    };

REG chand_reg[] = {
    { BRDATA (CLC, chan[3].clc, 16, 20, CHAN_N_DEV) },
    { BRDATA (CMD, chan[3].cmd, 16, 8, CHAN_N_DEV) },
    { BRDATA (CMF, chan[3].cmf, 16, 8, CHAN_N_DEV) },
    { BRDATA (BA, chan[3].ba, 16, 24, CHAN_N_DEV) },
    { BRDATA (BC, chan[3].bc, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHF, chan[3].chf, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHI, chan[3].chi, 16, 8, CHAN_N_DEV) },
    { BRDATA (CHSF, chan[3].chsf, 16, 8, CHAN_N_DEV) },
    { NULL }
    };

REG chane_reg[] = {
    { BRDATA (CLC, chan[4].clc, 16, 20, CHAN_N_DEV) },
    { BRDATA (CMD, chan[4].cmd, 16, 8, CHAN_N_DEV) },
    { BRDATA (CMF, chan[4].cmf, 16, 8, CHAN_N_DEV) },
    { BRDATA (BA, chan[4].ba, 16, 24, CHAN_N_DEV) },
    { BRDATA (BC, chan[4].bc, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHF, chan[4].chf, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHI, chan[4].chi, 16, 8, CHAN_N_DEV) },
    { BRDATA (CHSF, chan[4].chsf, 16, 8, CHAN_N_DEV) },
    { NULL }
    };

REG chanf_reg[] = {
    { BRDATA (CLC, chan[5].clc, 16, 20, CHAN_N_DEV) },
    { BRDATA (CMD, chan[5].cmd, 16, 8, CHAN_N_DEV) },
    { BRDATA (CMF, chan[5].cmf, 16, 8, CHAN_N_DEV) },
    { BRDATA (BA, chan[5].ba, 16, 24, CHAN_N_DEV) },
    { BRDATA (BC, chan[5].bc, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHF, chan[5].chf, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHI, chan[5].chi, 16, 8, CHAN_N_DEV) },
    { BRDATA (CHSF, chan[5].chsf, 16, 8, CHAN_N_DEV) },
    { NULL }
    };

REG chang_reg[] = {
    { BRDATA (CLC, chan[6].clc, 16, 20, CHAN_N_DEV) },
    { BRDATA (CMD, chan[6].cmd, 16, 8, CHAN_N_DEV) },
    { BRDATA (CMF, chan[6].cmf, 16, 8, CHAN_N_DEV) },
    { BRDATA (BA, chan[6].ba, 16, 24, CHAN_N_DEV) },
    { BRDATA (BC, chan[6].bc, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHF, chan[6].chf, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHI, chan[6].chi, 16, 8, CHAN_N_DEV) },
    { BRDATA (CHSF, chan[6].chsf, 16, 8, CHAN_N_DEV) },
    { NULL }
    };

REG chanh_reg[] = {
    { BRDATA (CLC, chan[7].clc, 16, 20, CHAN_N_DEV) },
    { BRDATA (CMD, chan[7].cmd, 16, 8, CHAN_N_DEV) },
    { BRDATA (CMF, chan[7].cmf, 16, 8, CHAN_N_DEV) },
    { BRDATA (BA, chan[7].ba, 16, 24, CHAN_N_DEV) },
    { BRDATA (BC, chan[7].bc, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHF, chan[7].chf, 16, 16, CHAN_N_DEV) },
    { BRDATA (CHI, chan[7].chi, 16, 8, CHAN_N_DEV) },
    { BRDATA (CHSF, chan[7].chsf, 16, 8, CHAN_N_DEV) },
    { NULL }
    };

DEVICE chan_dev[] = {
    {
    "CHANA", &chan_unit[0], chana_reg, NULL,
    1, 16, 16, 1, 16, 32,
    NULL, NULL, &chan_reset,
    NULL, NULL, NULL,
    NULL, CHAN_MIOP, 0
    },
    {
    "CHANB", &chan_unit[1], chanb_reg, NULL,
    1, 16, 16, 1, 16, 32,
    NULL, NULL, &chan_reset,
    NULL, NULL, NULL,
    NULL, CHAN_MIOP, 0
    },
    {
    "CHANC", &chan_unit[2], chanc_reg, NULL,
    1, 16, 16, 1, 16, 32,
    NULL, NULL, &chan_reset,
    NULL, NULL, NULL,
    NULL, CHAN_SIOP, 0
    },
    {
    "CHAND", &chan_unit[3], chand_reg, NULL,
    1, 16, 16, 1, 16, 32,
    NULL, NULL, &chan_reset,
    NULL, NULL, NULL,
    NULL, CHAN_SIOP, 0
    },
    {
    "CHANE", &chan_unit[4], chane_reg, NULL,
    1, 16, 16, 1, 16, 32,
    NULL, NULL, &chan_reset,
    NULL, NULL, NULL,
    NULL, CHAN_SIOP|DEV_DIS, 0
    },
    {
    "CHANF", &chan_unit[5], chanf_reg, NULL,
    1, 16, 16, 1, 16, 32,
    NULL, NULL, &chan_reset,
    NULL, NULL, NULL,
    NULL, CHAN_SIOP|DEV_DIS, 0
    },
    {
    "CHANG", &chan_unit[6], chang_reg, NULL,
    1, 16, 16, 1, 16, 32,
    NULL, NULL, &chan_reset,
    NULL, NULL, NULL,
    NULL, CHAN_SIOP|DEV_DIS, 0
    },
    {
    "CHANH", &chan_unit[7], chanh_reg, NULL,
    1, 16, 16, 1, 16, 32,
    NULL, NULL, &chan_reset,
    NULL, NULL, NULL,
    NULL, CHAN_SIOP|DEV_DIS, 0
    }
    };


/* Read direct */

uint32 io_rwd (uint32 op, uint32 rn, uint32 bva)
{
uint32 ad = bva >> 2;
uint32 mode = DIO_GETMOD (ad);                          /* mode */

if (dio_disp[mode] != NULL)                             /* if defined */
    return dio_disp[mode] (op, rn, ad);                 /* dispatch */
return (stop_op)? STOP_ILLEG: 0;                        /* ill inst */
}

/* Start IO */

uint32 io_sio (uint32 rn, uint32 bva)
{
uint32 ad = bva >> 2;
uint32 ch, dev, dvst;
uint32 st;

CC &= ~cpu_tab[cpu_model].iocc;                         /* clear CC's */
ch = DVA_GETCHAN (ad);                                  /* get chan, dev */
dev = DVA_GETDEV (ad);
if (!io_init_inst (rn, ad, ch, dev, R[0])) {            /* valid inst? */
    CC |= CC1|CC2;
    return 0;
    }
if (chan[ch].chf[dev] & CHF_INP) {                      /* int pending? */
    chan[ch].disp[dev] (OP_TIO, ad, &dvst);             /* get status */
    CC |= (CC2 | io_set_status (rn, ch, dev, dvst, 0)); /* set status */
    return 0;
    }
st = chan[ch].disp[dev] (OP_SIO, ad, &dvst);            /* start I/O */
CC |= io_set_status (rn, ch, dev, dvst, 0);             /* set status */
if (CC & cpu_tab[cpu_model].iocc)                       /* error? */
    return 0;
chan[ch].chf[dev] = 0;                                  /* clear flags */
chan[ch].chi[dev] = 0;                                  /* clear intrs */
chan[ch].chsf[dev] |= CHSF_ACT;                         /* set chan active */
chan_new_cmd (ch, dev, R[0]);                           /* new command */
return st;
}

/* Test IO */

uint32 io_tio (uint32 rn, uint32 bva)
{
uint32 ad = bva >> 2;
uint32 ch, dev, dvst;
uint32 st;

CC &= ~cpu_tab[cpu_model].iocc;                         /* clear CC's */
ch = DVA_GETCHAN (ad);                                  /* get chan, dev */
dev = DVA_GETDEV (ad);
if (!io_init_inst (rn, ad, ch, dev, 0)) {               /* valid inst? */
    CC |= CC1|CC2;
    return 0;
    }
st = chan[ch].disp[dev] (OP_TIO, ad, &dvst);            /* test status */
CC |= io_set_status (rn, ch, dev, dvst, 0);             /* set status */
return st;
}

/* Test device status */

uint32 io_tdv (uint32 rn, uint32 bva)
{
uint32 ad = bva >> 2;
uint32 ch, dev, dvst;
uint32 st;

CC &= ~cpu_tab[cpu_model].iocc;                         /* clear CC's */
ch = DVA_GETCHAN (ad);                                  /* get chan, dev */
dev = DVA_GETDEV (ad);
if (!io_init_inst (rn, ad, ch, dev, 0)) {               /* valid inst? */
    CC |= CC1|CC2;
    return 0;
    }
st = chan[ch].disp[dev] (OP_TDV, ad, &dvst);            /* test status */
CC |= io_set_status (rn, ch, dev, dvst, 1);             /* set status */
return st;
}

/* Halt IO */

uint32 io_hio (uint32 rn, uint32 bva)
{
uint32 ad = bva >> 2;
uint32 ch, dev, subop, dvst;
uint32 st;

CC &= ~cpu_tab[cpu_model].iocc;
ad = bva >> 2;
ch = DVA_GETCHAN (ad);                                  /* get chan, dev */
dev = DVA_GETDEV (ad);
subop = (ad >> 13) & 0x7;
if (subop) {                                            /* extended fnc? */
    if (!QCPU_S89_5X0 || (subop > 3))                   /* S9, 5X0 only */
        return (stop_op? STOP_ILLEG: 0);
    if (ch >= chan_num) {                               /* valid channel? */
        CC |= CC1|CC2;
        return 0;
        }
    switch (subop) {

    case 1:                                             /* reset channel */
        chan_reset (&chan_dev[ch]);
        break;

    case 2: case 3:                                     /* poll processor */
        if (rn)                                         /* NI */
            R[rn] = 0;
        break;
        }
    }
else {                                                  /* normal HIO */
    if (!io_init_inst (rn, ad, ch, dev, 0)) {           /* valid inst? */
        CC |= CC1|CC2;
        return 0;
        }
    st = chan[ch].disp[dev] (OP_HIO, ad, &dvst);        /* halt IO */
    CC |= io_set_status (rn, ch, dev, dvst, 0);         /* set status */
    }
return st;
}

/* Acknowledge interrupt (ignores device address) */

uint32 io_aio (uint32 rn, uint32 bva)
{
uint32 i, j, dva, dvst;
uint32 st;

if (DVA_GETCHAN (bva >> 2) != 0)                        /* non std I/O addr? */
    return (stop_op? STOP_ILLEG: 0);
CC = CC & ~cpu_tab[cpu_model].iocc;                     /* clear CC's */
for (i = 0; i < chan_num; i++) {                        /* loop thru chan */
    for (j = 0; j < CHAN_N_DEV; j++) {                  /* loop thru dev */
        if (chan[i].chf[j] & CHF_INP) {                 /* intr pending? */
            if (chan[i].disp[j] == NULL) {              /* false interrupt? */
                chan[i].chf[j] &= ~CHF_INP;             /* clear intr */
                continue;
                }
            dva = (i << DVA_V_CHAN) |                   /* chan number */
                ((chan[i].chsf[j] & CHSF_MU)?           /* device number */
                    ((j << DVA_V_DEVMU) | DVA_MU):
                    (j << DVA_V_DEVSU));
            st = chan[i].disp[j] (OP_AIO, dva, &dvst);  /* get AIO status */
            dva |= DVT_GETUN (dvst);                    /* finish dev addr */
            if (rn)                                     /* want status? */
                R[rn] = (DVT_GETDVS (dvst) << 24) |     /* device status */
                ((uint32) ((chan[i].chf[j] & (CHF_LNTE|CHF_XMDE)) |
                CHI_GETINT (chan[i].chi[j])) << 16) | dva;
            if (chan[i].chi[j] & CHI_UEN)               /* unusual end? */
                CC |= CC2;                              /* set CC2 */
            return st;
            }
        }                                               /* end for dev */
    }                                                   /* end for chan */
CC |= CC1|CC2;                                          /* no recognition */
return 0;
}

/* Initiate I/O instruction */

t_bool io_init_inst (uint32 rn, uint32 ad, uint32 ch, uint32 dev, uint32 r0)
{
uint32 loc20;

if (ch >= chan_num)                                     /* bad chan? */
    return FALSE;
loc20 = ((ad & 0xFF) << 24) |                           /* <0:7> = dev ad */
    ((rn & 1) | (rn? 3: 0) << 22) |                     /* <8:9> = reg ind */
    (r0 & (cpu_tab[cpu_model].pamask >> 1));            /* <14/16:31> = r0 */
WritePW (0x20, loc20);
return (chan[ch].disp[dev] != NULL)? TRUE: FALSE;
}

/* Set status for I/O instruction */

uint32 io_set_status (uint32 rn, uint32 ch, uint32 dev, uint32 dvst, t_bool tdv)
{
uint32 mrgst;
uint32 odd = rn & 1;

if ((rn != 0) && !(dvst & DVT_NOST)) {                  /* return status? */
    if (tdv)
        mrgst = (DVT_GETDVS (dvst) << 8) | (chan[ch].chf[dev] & 0xFF);
    else mrgst = ((DVT_GETDVS(dvst) << 8) & ~CHF_ALL) | (chan[ch].chf[dev] & CHF_ALL);
    R[rn] = chan[ch].clc[dev];                          /* even reg */
    if (!odd)                                           /* even pair? */
        WritePW (0x20, R[rn]);                          /* write to 20 */
    R[rn|1] = (mrgst << 16) | chan[ch].bc[dev];         /* odd reg */
    WritePW (0x20 + odd, R[rn|1]);                      /* write to 20/21 */
    }
return DVT_GETCC (dvst);
}

/* Channel support routines */

/* Get new command */

uint32 chan_get_cmd (uint32 dva, uint32 *cmd)
{
uint32 ch, dev;
t_stat st;

if ((st = chan_proc_prolog (dva, &ch, &dev)) != 0)      /* valid, active? */
    return st;
*cmd = chan[ch].cmd[dev];                               /* return cmd */
return 0;
}

/* Channel end */

uint32 chan_end (uint32 dva)
{
uint32 ch, dev;
uint32 st;

if ((st = chan_proc_prolog (dva, &ch, &dev)) != 0)      /* valid, active? */
    return st;
if (chan[ch].cmf[dev] & CMF_ICE)                        /* int on chan end? */
    chan_set_chi (dva, CHI_END);
if ((chan[ch].cmf[dev] & CMF_CCH) &&                    /* command chain? */
    !chan_new_cmd (ch, dev, chan[ch].clc[dev] + 1))     /* next command? */
    return CHS_CCH;
else chan[ch].chsf[dev] &= ~CHSF_ACT;                   /* channel inactive */
return 0;
}

/* Channel error */

uint32 chan_set_chf (uint32 dva, uint32 fl)
{
uint32 ch, dev;

ch = DVA_GETCHAN (dva);                                 /* get chan, dev */
dev = DVA_GETDEV (dva);
if (!VALID_DVA (ch, dev))                               /* valid? */
    return SCPE_IERR;
fl &= ~CHF_INP;                                         /* ignore int pend */
chan[ch].chf[dev] |= fl;
if ((fl & CHF_LNTE) &&                                  /* length error */
    ((chan[ch].cmf[dev] & CMF_SIL) ||                   /* suppressed? */
    !(chan[ch].cmf[dev] & CMF_HTE)))                    /* or don't stop? */
    fl &= ~CHF_LNTE;                                    /* ignore it */
if ((fl & CHF_XMDE) &&                                  /* data error? */
    !(chan[ch].cmf[dev] & CMF_HTE))                     /* don't stop? */
    fl &= ~CHF_XMDE;                                    /* ignore it */
if ((fl & CHF_XMME) &&                                  /* memory error? */
    !(chan[ch].cmf[dev] & CMF_HTE))                     /* don't stop? */
    fl &= ~CHF_XMME;                                    /* ignore it */
if (fl)                                                 /* fatal error? */
    return chan_uen (dva);                              /* unusual end */
return 0;
}

/* Channel test command flags */

t_bool chan_tst_cmf (uint32 dva, uint32 fl)
{
uint32 ch, dev;

ch = DVA_GETCHAN (dva);                                 /* get chan, dev */
dev = DVA_GETDEV (dva);
if (VALID_DVA (ch, dev) &&                              /* valid? */
    (chan[ch].cmf[dev] & fl))
    return TRUE;
return FALSE;
}

/* Channel unusual end */

uint32 chan_uen (uint32 dva)
{
uint32 ch, dev;

ch = DVA_GETCHAN (dva);                                 /* get chan, dev */
dev = DVA_GETDEV (dva);
if (!VALID_DVA (ch, dev))                               /* valid? */
    return SCPE_IERR;
if (chan[ch].cmf[dev] & CMF_IUE)                        /* int on uend? */
    chan_set_chi (dva, CHI_UEN);
chan[ch].chf[dev] |= CHF_UEN;                           /* flag uend */
chan[ch].chsf[dev] &= ~CHSF_ACT;
return CHS_INACTV;                                      /* done */
}

/* Channel read processes */

uint32 chan_RdMemB (uint32 dva, uint32 *dat)
{
uint32 ch, dev;
uint32 st;

if ((st = chan_proc_prolog (dva, &ch, &dev)) != 0)      /* valid, active? */
    return st;
if (chan[ch].cmf[dev] & CMF_SKP)                        /* skip? */
    *dat = 0;
else if (ReadPB (chan[ch].ba[dev], dat)) {              /* read data, nxm? */
    chan[ch].chf[dev] |= CHF_XMAE;                      /* addr error */
    return CHS_NXM;                                     /* dev will uend */
    }
return chan_proc_epilog (dva, 1);                       /* adjust counts */
}

uint32 chan_RdMemW (uint32 dva, uint32 *dat)
{
uint32 ch, dev;
uint32 st;

if ((st = chan_proc_prolog (dva, &ch, &dev)) != 0)      /* valid, active? */
    return st;
if (chan[ch].cmf[dev] & CMF_SKP)                        /* skip? */
    *dat = 0;
else if ((chan[ch].bc[dev] < 4) ||                      /* unaligned? */
         ((chan[ch].ba[dev] & 0x3) != 0)) {
    uint32 i, wd;
    for (i = 0, *dat = 0, wd = 0; i < 4; i++) {         /* up to 4 bytes */
        st = chan_RdMemB (dva, &wd);                    /* get byte */
        *dat |= ((wd & 0xFF) << (24 - (i * 8)));        /* pack */
        if (st != 0)                                    /* stop if error */
            return st;
        }
    return 0;
    }
else if (ReadPW (chan[ch].ba[dev] >> 2, dat)) {         /* read word, nxm? */
    chan[ch].chf[dev] |= CHF_XMAE;                      /* addr error */
    return CHS_NXM;                                     /* dev will uend */
    }
return chan_proc_epilog (dva, 4);                       /* adjust counts */
}

/* Channel write processes */

uint32 chan_WrMemB (uint32 dva, uint32 dat)
{
uint32 ch, dev;
uint32 st;

if ((st = chan_proc_prolog (dva, &ch, &dev)) != 0)      /* valid, active? */
    return st;
if (((chan[ch].cmf[dev] & CMF_SKP) == 0) &&             /* skip? */
    WritePB (chan[ch].ba[dev], dat)) {                  /* write data, nxm? */
    chan[ch].chf[dev] |= CHF_XMAE;                      /* addr error */
    return CHS_NXM;                                     /* dev will uend */
    }
return chan_proc_epilog (dva, 1);                       /* adjust counts */
}

uint32 chan_WrMemBR (uint32 dva, uint32 dat)
{
uint32 ch, dev;
uint32 st;

if ((st = chan_proc_prolog (dva, &ch, &dev)) != 0)      /* valid, active? */
    return st;
if (((chan[ch].cmf[dev] & CMF_SKP) == 0) &&             /* skip? */
    WritePB (chan[ch].ba[dev], dat)) {                  /* write data, nxm? */
    chan[ch].chf[dev] |= CHF_XMAE;                      /* addr error */
    return CHS_NXM;                                     /* dev will uend */
    }
return chan_proc_epilog (dva, -1);                      /* adjust counts */
}

uint32 chan_WrMemW (uint32 dva, uint32 dat)
{
uint32 ch, dev;
uint32 st;

if ((st = chan_proc_prolog (dva, &ch, &dev)) != 0)      /* valid, active? */
    return st;
if ((chan[ch].bc[dev] < 4) ||                           /* unaligned? */
    ((chan[ch].ba[dev] & 0x3) != 0)) {
    uint32 i, wd;
    for (i = 0; i < 4; i++) {                           /* up to 4 bytes */
        wd = (dat >> (24 - (i * 8))) & 0xFF;            /* get byte */
        if ((st = chan_WrMemB (dva, wd)) != 0)          /* write */
            return st;                                  /* stop if error */
        }
    return 0;
    }
if (((chan[ch].cmf[dev] & CMF_SKP) == 0) &&             /* skip? */
    WritePW (chan[ch].ba[dev] >> 2, dat)) {             /* write word, nxm? */
    chan[ch].chf[dev] |= CHF_XMAE;                      /* addr error */
    return CHS_NXM;                                     /* dev will uend */
    }
return chan_proc_epilog (dva, 4);                       /* adjust counts */
}

/* Channel process common code */

uint32 chan_proc_prolog (uint32 dva, uint32 *ch, uint32 *dev)
{
*ch = DVA_GETCHAN (dva);                                /* get chan, dev */
*dev = DVA_GETDEV (dva);
if (!VALID_DVA (*ch, *dev))                             /* valid? */
    return SCPE_IERR;
if ((chan[*ch].chsf[*dev] & CHSF_ACT) == 0)             /* active? */
    return CHS_INACTV;
return 0;
}

uint32 chan_proc_epilog (uint32 dva, int32 cnt)
{
uint32 ch = DVA_GETCHAN (dva);                          /* get ch, dev */
uint32 dev = DVA_GETDEV (dva);

chan[ch].ba[dev] = (chan[ch].ba[dev] + cnt) & CHBA_MASK;
chan[ch].bc[dev] = (chan[ch].bc[dev] - abs (cnt)) & CHBC_MASK;
if (chan[ch].bc[dev] != 0)                              /* more to do? */
    return 0;
if (chan[ch].cmf[dev] & CMF_IZC)                        /* int on zero?*/
    chan_set_chi (dva, CHI_ZBC);
if (chan[ch].cmf[dev] & CMF_DCH) {                      /* data chaining? */
    if (chan_new_cmd (ch, dev, chan[ch].clc[dev] + 1))
        return CHS_ZBC;
    return 0;
    }
return CHS_ZBC;
}

/* New channel command */

uint32 chan_new_cmd (uint32 ch, uint32 dev, uint32 clc)
{
uint32 i, ccw1, ccw2, cmd;

for (i = 0; i < 2; i++) {                               /* max twice */
    clc = clc & (cpu_tab[cpu_model].pamask >> 1);       /* mask clc */
    chan[ch].clc[dev] = clc;                            /* and save */
    if (ReadPW (clc << 1, &ccw1)) {                     /* get ccw1, nxm? */
        chan[ch].chf[dev] |= CHF_IOME;                  /* memory error */
        chan[ch].chsf[dev] &= ~CHSF_ACT;                /* stop channel */
        return CHS_INACTV;
        }
    ReadPW ((clc << 1) + 1, &ccw2);                     /* get ccw2 */
    cmd = CCW1_GETCMD (ccw1);                           /* get chan cmd */
    if ((cmd & 0xF) == CMD_TIC)                         /* transfer? */
        clc = ccw1;                                     /* try again */
    else {
        chan[ch].cmd[dev] = cmd;                        /* decompose CCW */
        chan[ch].ba[dev] = CCW1_GETBA (ccw1);
        chan[ch].cmf[dev] = CCW2_GETCMF (ccw2);
        chan[ch].bc[dev] = CCW2_GETBC (ccw2);
        return 0;
        }
    }
chan[ch].chf[dev] |= CHF_IOCE;                          /* control error */
chan[ch].chsf[dev] &= ~CHSF_ACT;                        /* stop channel */
return CHS_INACTV;
}

/* Set, clear, test channel interrupt */

void chan_set_chi (uint32 dva, uint32 fl)
{
uint32 ch = DVA_GETCHAN (dva);                          /* get ch, dev */
uint32 dev = DVA_GETDEV (dva);
uint32 un = DVA_GETUNIT (dva);                          /* get unit */

chan[ch].chf[dev] |= CHF_INP;                           /* int pending */
chan[ch].chi[dev] = (chan[ch].chi[dev] & CHI_FLAGS) |   /* update status */
    fl | CHI_CTL | un;                                  /* save unit */
return;
}

int32 chan_clr_chi (uint32 dva)
{
uint32 ch = DVA_GETCHAN (dva);                          /* get ch, dev */
uint32 dev = DVA_GETDEV (dva);
uint32 old_chi = chan[ch].chi[dev];

chan[ch].chf[dev] &= ~CHF_INP;                          /* clr int pending */
chan[ch].chi[dev] &= CHI_FLAGS;                         /* clr ctl int */
if (old_chi & CHI_CTL)
    return CHI_GETUN (old_chi);
else return -1;
}

int32 chan_chk_chi (uint32 dva)
{
uint32 ch = DVA_GETCHAN (dva);                          /* get ch, dev */
uint32 dev = DVA_GETDEV (dva);

if (chan[ch].chi[dev] & CHI_CTL)                        /* ctl int pending? */
    return CHI_GETUN (chan[ch].chi[dev]);
else return -1;
}

/* Set device interrupt */

void chan_set_dvi (uint32 dva)
{
uint32 ch = DVA_GETCHAN (dva);                          /* get ch, dev */
uint32 dev = DVA_GETDEV (dva);

chan[ch].chf[dev] |= CHF_INP;                           /* int pending */
return;
}

/* Called by device reset to reset channel registers */

t_stat chan_reset_dev (uint32 dva)
{
uint32 ch, dev;

ch = DVA_GETCHAN (dva);                                 /* get chan, dev */
dev = DVA_GETDEV (dva);
if (!VALID_DVA (ch, dev))                               /* valid? */
    return SCPE_IERR;
chan[ch].chf[dev] &= ~CHF_INP;                          /* clear intr */
chan[ch].chsf[dev] &= ~CHSF_ACT;                        /* clear active */
return SCPE_OK;
}

/* Find highest priority pending interrupt
   Question: must an interrupt be armed to be recognized?
   Answer: yes; req'arm = 11 signifies waiting state */

uint32 io_eval_int (void)
{
uint32 i, j, t, curr, mask, newi;

if (int_arm[INTG_IO] & INTGIO_IO)                       /* I/O armed? */
    io_eval_ioint ();                                   /* eval I/O interrupt */
for (i = 0, curr = 0; i < INTG_MAX; i++) {              /* loop thru groups */
    t = int_req[curr] & int_arm[curr] & int_enb[curr];  /* req, armed, enb */
    if ((t != 0) &&                                     /* any waiting req? */
        ((PSW2 & int_tab[curr].psw2_inh) == 0)) {       /* group not inh? */
        for (j = 0; j < int_tab[curr].nbits; j++) {     /* loop thru reqs */
            mask = 1u << (int_tab[curr].nbits - j - 1);
            if (t & mask) {                             /* request active? */
                newi = INTV (curr, j);                  /* get int number */
                if (newi < int_hiact)                   /* higher priority? */
                    return newi;                        /* new highest actv */
                return NO_INT;                          /* no pending intr */
                }
            }
        sim_printf ("%%int eval consistency error = %X\r\n", t);
        int_req[curr] = 0;                              /* "impossible" */
        }
    if (curr == INT_GETGRP (int_hiact))                 /* at active group? */
        return NO_INT;                                  /* no pending intr */
    curr = int_lnk[curr];                               /* next group */
    if (curr == 0)                                      /* end of list? */
        return NO_INT;                                  /* no pending intr */
    }
sim_printf ("%%int eval consistency error, list end not found\r\n");
return NO_INT;
}

/* See if any interrupt is possible (used by WAIT) */

t_bool io_poss_int (void)
{
uint32 i, curr;

for (i = 0, curr = 0; i < INTG_MAX; i++) {              /* loop thru groups */
    if (((int_arm[curr] & int_enb[curr]) != 0) &&
        ((PSW2 & int_tab[curr].psw2_inh) == 0))         /* group not inh? */
        return TRUE;                                    /* int can occur */
    curr = int_lnk[curr];                               /* next group */
    if (curr == 0)                                      /* end of list? */
        return FALSE;                                   /* no int possible */
    }
sim_printf ("%%int possible consistency error, list end not found\r\n");
return FALSE;
}

/* Evaluate I/O interrupts */

void io_eval_ioint (void)
{
uint32 i, j;

for (i = 0; i < chan_num; i++) {                        /* loop thru chan */
    for (j = 0; j < CHAN_N_DEV; j++) {                  /* loop thru dev */
        if (chan[i].chf[j] & CHF_INP) {                 /* intr pending? */
            int_req[INTG_IO] |= INTGIO_IO;              /* set I/O intr */
            return;
            }                                           /* end if int pend */
        }                                               /* end for dev */
    }                                                   /* end for chan */
return;
}

/* Find highest priority active interrupt
   Question: is an inhibited or disabled interrupt recognized?
   Answer: yes; req'arm = 10 signifies active state */

uint32 io_actv_int (void)
{
uint32 i, j, t, curr, mask;

for (i = 0, curr = 0; i < INTG_MAX; i++) {              /* loop thru groups */
    if ((t = int_req[curr] & ~int_arm[curr]) != 0) {    /* req active? */
        for (j = 0; j < int_tab[curr].nbits; j++) {     /* loop thru reqs */
            mask = 1u << (int_tab[curr].nbits - j - 1);
            if (t & mask)                               /* req active? */
                return INTV (curr, j);                  /* return int num */
            }
        sim_printf ("%%int actv consistency error = %X\r\n", t);
        int_req[curr] = 0;                              /* "impossible" */
        }
    curr = int_lnk[curr];                               /* next group */
    if (curr == 0)                                      /* end of list? */
        return NO_INT;                                  /* no pending interupt */
    }
sim_printf ("%%int actv consistency error, list end not found\r\n");
return NO_INT;
}

/* Acknowledge interrupt and get vector */

uint32 io_ackn_int (uint32 hireq)
{
uint32 grp, bit, mask;

if (hireq >= NO_INT)                                    /* none pending? */
    return 0;
grp = INT_GETGRP (hireq);                               /* get grp, bit */
bit = INT_GETBIT (hireq);
if (bit >= int_tab[grp].nbits) {                        /* validate bit */
    sim_printf ("%%int ack consistency error, hireq=%X\r\n", hireq);
    return 0;
    }
mask = 1u << (int_tab[grp].nbits - bit - 1);
int_arm[grp] &= ~mask;                                  /* clear armed */
int_hiact = hireq;                                      /* now active */
int_hireq = io_eval_int ();                             /* paranoia */
if (int_hireq != NO_INT)
    sim_printf ("%%int ack consistency error, post iack req=%X\r\n", int_hireq);
return int_tab[grp].vecbase + bit;
}

/* Release interrupt and set new armed/disarmed state */

extern uint32 io_rels_int (uint32 hiact, t_bool arm)
{
uint32 grp, bit, mask;

if (hiact < NO_INT) {                                   /* intr active? */
    grp = INT_GETGRP (hiact);                           /* get grp, bit */
    bit = INT_GETBIT (hiact);
    if (bit >= int_tab[grp].nbits) {                    /* validate bit */
        sim_printf ("%%int release consistency error, hiact=%X\r\n", hiact);
        return 0;
        }
    mask = 1u << (int_tab[grp].nbits - bit - 1);
    int_req[grp] &= ~mask;                              /* clear req */
    if (arm)                                            /* rearm? */
        int_arm[grp] |= mask;
    else int_arm[grp] &= ~mask;
    }
int_hiact = io_actv_int ();                             /* new highest actv */
return io_eval_int ();                                  /* new request */
}

/* Set panel interrupt */

t_stat io_set_pint (void)
{
int_req[INTG_IO] |= INTGIO_PANEL;
return SCPE_OK;
}

/* Set or clear interrupt status flags */

void io_sclr_req (uint32 inum, uint32 val)
{
uint32 grp, bit, mask;

if (inum < NO_INT) {                                    /* valid? */
    grp = INT_GETGRP (inum);                            /* get grp, bit */
    bit = INT_GETBIT (inum);
    if (bit >= int_tab[grp].nbits) {                    /* validate bit */
        sim_printf ("%%intreq set/clear consistency error, inum=%X\r\n", inum);
        return;
        }
    mask = 1u << (int_tab[grp].nbits - bit - 1);
    if (val) {                                          /* set req? */
        if (int_arm[grp] & mask)                        /* must be armed */
            int_req[grp] |= mask;
        }
    else int_req[grp] &= ~mask;                         /* clr req */
    }
return;
}

void io_sclr_arm (uint32 inum, uint32 val)
{
uint32 grp, bit, mask;

if (inum < NO_INT) {                                    /* valid? */
    grp = INT_GETGRP (inum);                            /* get grp, bit */
    bit = INT_GETBIT (inum);
    if (bit >= int_tab[grp].nbits) {                    /* validate bit */
        sim_printf ("%%intarm set/clear consistency error, inum=%X\r\n", inum);
        return;
        }
    mask = 1u << (int_tab[grp].nbits - bit - 1);
    if (val)                                            /* set or clr arm */
        int_arm[grp] |= mask;
    else int_arm[grp] &= ~mask;
    }
return;
}

/* Read/write direct mode 0 - processor miscellaneous */

uint32 io_rwd_m0 (uint32 op, uint32 rn, uint32 ad)
{
uint32 wd;
uint32 fnc = DIO_GET0FNC (ad);
uint32 dat = rn? R[rn]: 0;

if (op == OP_RD) {                                      /* read direct? */
    if (fnc == 0x000) {                                 /* copy SSW to SC */
        CC = SSW;
        }
    else if (fnc == 0x010) {                            /* read mem fault */
        if (rn)
            R[rn] = 0;
        CC = SSW;
        }
    else if (QCPU_S89_5X0 && (fnc == 0x040)) {          /* S89, 5X0 only */
        if (rn)                                         /* read inhibits */
            R[rn] = PSW2_GETINH (PSW2);
        }
    else if (QCPU_S89 && (fnc == 0x045)) {              /* S89 only */
        if (rn)
            R[rn] = s9_marg & 0x00C00000 |              /* <8,9> = margins */
                (QCPU_S9? 0x00100000: 0x00200000);      /* S8 sets 10, S9 11 */            
        }
    else if (QCPU_S89 && (fnc == 0x049)) {              /* S89 only */
        if (rn)                                         /* read snapshot */
            R[rn] = s9_snap;
        }
    else if (QCPU_5X0 && ((fnc & 0xFC0) == 0x100)) {    /* 5X0 only */
        ReadPW (fnc & 0x1F, &wd);                       /* read low mem */
        if (rn)
            R[rn] = wd;
        }
    else if (QCPU_5X0 && ((fnc & 0xFC0) == 0x300)) {    /* 5X0 only */
        if (rn)                                         /* read int reg */
            R[rn] = s5x0_ireg[fnc & 0x1F];
        }
    else return (stop_op)? STOP_ILLEG: 0;
    }
else {                                                  /* write direct */
    if (QCPU_5X0 && (fnc == 0x000))                     /* 5X0 only */
        SSW = dat & 0xF;                                /* write SSW */
    else if (QCPU_5X0 && (fnc == 0x002))                /* 5X0 only */
        return TR_47;                                   /* trap to 47 */
    else if ((fnc & 0xFF0) == 0x020)                    /* bit clear inh */
        PSW2 &= ~((ad & PSW2_M_INH) << PSW2_V_INH);
    else if ((fnc & 0xFF0) == 0x030)                    /* bit set inh */
        PSW2 |= ((ad & PSW2_M_INH) << PSW2_V_INH);
    else if (fnc == 0x040)                              /* alarm off */
        cons_alarm = 0;
    else if (fnc == 0x041)                              /* alarm on */
        cons_alarm = 1;
    else if (fnc == 0x042) {                            /* toggle freq */
        cons_alarm = 0;
        cons_pcf ^= 1;
        }
    else if (fnc == 0x044) ;                            /* S5 reset IIOP */
    else if (QCPU_S89 && (fnc == 0x045))                /* S89 only */
        s9_marg = dat;                                  /* write margins */
    else if (QCPU_S89_5X0 && (fnc == 0x046))            /* S89, 5X0 only */
        PSW2 &= ~(PSW2_MA9|PSW2_MA5X0);                 /* clr mode altered */
    else if (QCPU_S9 && (fnc == 0x047))                 /* S9 set mode alt */
        PSW2 |= PSW2_MA9;
    else if (QCPU_5X0 && (fnc == 0x047))                /* 5X0 set mode alt */
        PSW2 |= PSW2_MA5X0;
    else if (QCPU_S89 && (fnc == 0x049))                /* S9 only */
        s9_snap = dat;                                  /* write snapshot */
    else if (QCPU_5X0 && ((fnc & 0xFC0) == 0x100))      /* 5X0 only */
        WritePW (fnc & 0x1F, dat);                      /* write low mem */
    else if (QCPU_5X0 && ((fnc & 0xFC0) == 0x300))      /* 5X0 only */
        s5x0_ireg[fnc & 0x1F] = dat;                    /* write int reg */
    else return (stop_op)? STOP_ILLEG: 0;
    }
return 0;
}

/* Read/write direct mode 1 - interrupt flags
   This is the only routine that has to map between architecturally
   defined interrupts groups and the internal representation. */

uint32 io_rwd_m1 (uint32 op, uint32 rn, uint32 ad)
{
uint32 i, beg, end, mask, sc;
uint32 grp = DIO_GET1GRP (ad);
uint32 fnc = DIO_GET1FNC (ad);

if (grp == 0) {                                         /* overrides? */
    beg = INTG_OVR;
    end = INTG_IO;
    }
else if (grp == 1)                                      /* group 1? */
    return 0;                                           /* not there */
else beg = end = grp + 1;

if (op == OP_RD) {                                      /* read direct? */
    if (!QCPU_S89_5X0)                                  /* S89, 5X0 only */
        return (stop_op? STOP_ILLEG: 0);
    if (rn == 0)                                        /* want result? */
        return 0;
    R[rn] = 0;                                          /* clear reg */
    }
for (i = beg; i <= end; i++) {                          /* loop thru grps */
    mask = (1u << int_tab[i].nbits) - 1;
    sc = 32 - int_tab[i].regbit - int_tab[i].nbits;
    if (op == OP_RD) {                                  /* read direct? */
        if (fnc & 0x1)
            R[rn] |= ((mask & int_arm[i]) << sc);
        if (fnc & 0x2)
            R[rn] |= ((mask & int_req[i]) << sc);
        if (fnc & 0x4)
            R[rn] |= ((mask & int_enb[i]) << sc);
        }
    else {                                              /* write direct */
        mask = (R[rn] >> sc) & mask;
        switch (fnc) {

        case 0x0:                                       /* armed||wait->act */
            if (QCPU_S89_5X0) {
                int_req[i] |= (mask & int_arm[i]);
                int_arm[i] &= mask;
                }
            else return (stop_op? STOP_ILLEG: 0);
            break;

        case 0x1:                                       /* disarm, clr req */
            int_arm[i] &= ~mask;
            int_req[i] &= ~mask;
            break;

        case 0x2:                                       /* arm, enb, clr req */
            int_arm[i] |= mask;
            int_enb[i] |= mask;
            int_req[i] &= ~mask;
            break;

        case 0x3:                                       /* arm, dsb, clr req */
            int_arm[i] |= mask;
            int_enb[i] &= ~mask;
            int_req[i] &= ~mask;
            break;

        case 0x4:                                       /* enable */
            int_enb[i] |= mask;
            break;

        case 0x5:                                       /* disable */
            int_enb[i] &= ~mask;
            break;

        case 0x6:                                       /* direct set enb */
            int_enb[i] = mask;
            break;

        case 0x7:                                       /* armed->waiting */
            int_req[i] |= (mask & int_arm[i]);
            }
        }
    }
return 0;
}

/* Reset routines */

t_stat int_reset (DEVICE *dptr)
{
uint32 i;

if (int_lnk[0] == 0)                                    /* int chain not set up? */
    io_set_eimax (ei_bmax);
for (i = 0; i < INTG_MAX; i++) {
    int_arm[i] = 0;
    int_enb[i] = 0;
    int_req[i] = 0;
    }
int_hiact = NO_INT;
int_hireq = NO_INT;
return SCPE_OK;
}

t_stat chan_reset (DEVICE *dptr)
{
uint32 ch = dptr - &chan_dev[0];
uint32 i, j;
DEVICE *devp;

if (ch >= CHAN_N_CHAN)
    return SCPE_IERR;
for (i = 0; i < CHAN_N_DEV; i++) {
    chan[ch].clc[i] = 0;
    chan[ch].cmd[i] = 0;
    chan[ch].cmf[i] = 0;
    chan[ch].ba[i] = 0;
    chan[ch].bc[i] = 0;
    chan[ch].chf[i] = 0;
    chan[ch].chi[i] = 0;
    chan[ch].chsf[i] &= ~CHSF_ACT;
    for (j = 0; (devp = sim_devices[j]) != NULL; j++) { /* loop thru dev */
        if (devp->ctxt != NULL) {
            dib_t *dibp = (dib_t *) devp->ctxt;
            if ((DVA_GETCHAN (dibp->dva) == ch) && (devp->reset))
                devp->reset (devp);
            }
        }
    }
return SCPE_OK;
}

/* Universal boot routine */

static uint32 boot_rom[] = {
    0x00000000, 0x00000000, 0x020000A8, 0x0E000058,
    0x00000011, 0x00000000, 0x32000024, 0xCC000025,
    0xCD000025, 0x69C00028, 0x00000000, 0x00000000
    };

t_stat io_boot (int32 u, DEVICE *dptr)
{
uint32 i;
dib_t *dibp = (dib_t *) dptr->ctxt;

for (i = 0; i < MEMSIZE; i++)                           /* boot clrs mem */
    WritePW (i, 0);
if ((dibp == NULL) ||
    ((u != 0) &&
    ((dibp->dva & DVA_MU) == 0)))
    return SCPE_ARG;
for (i = 0; i < BOOT_LNT; i++)
    WritePW (BOOT_SA + i, boot_rom[i]);
WritePW (BOOT_DEV, dibp->dva | u);
cpu_new_PSD (1, BOOT_PC, 0);
return SCPE_OK;
}

/* I/O table initialization routine */

t_stat io_init (void)
{
uint32 i, j, ch, dev, dio;
DEVICE *dptr;
dib_t *dibp;

for (i = 0; i < CHAN_N_CHAN; i++) {
    for (j = 0; j < CHAN_N_DEV; j++) {
        chan[i].chsf[j] &= ~CHSF_MU;
        chan[i].disp[j] = NULL;
        }
    }
dio_disp[0] = &io_rwd_m0;
for (i = 1; i < DIO_N_MOD; i++)
    dio_disp[i] = NULL;

for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {
    if ((dibp = (dib_t *) dptr->ctxt) != NULL) {
        ch = DVA_GETCHAN (dibp->dva);
        dev = DVA_GETDEV (dibp->dva);
        dio = dibp->dio;
        if ((ch >= chan_num) ||
            (dev >= CHAN_N_DEV) ||
            (dio >= DIO_N_MOD)) {
            sim_printf ("%s: invalid device address, chan = %d, dev = %X, dio = %X\n",
                    sim_dname (dptr), ch, DVA_GETDEV (dibp->dva), dio);
            return SCPE_STOP;
            }
        if ((dibp->disp != NULL) && (chan[ch].disp[dev] != NULL)) {
            sim_printf ("%s: device address conflict, chan = %d, dev = %X\n",
                    sim_dname (dptr), ch, DVA_GETDEV (dibp->dva));
            return SCPE_STOP;
            }
        if ((dibp->dio_disp != NULL) && (dio_disp[dio] != NULL)) {
            sim_printf ("%s: direct I/O address conflict, dio = %X\n",
                    sim_dname (dptr), dio);
            return SCPE_STOP;
            }
        if (dibp->disp)
            chan[ch].disp[dev] = dibp->disp;
        if (dibp->dio_disp)
            dio_disp[dio] = dibp->dio_disp;
        if (dibp->dva & DVA_MU)
            chan[ch].chsf[dev] |= CHSF_MU;
        }
    }
return SCPE_OK;
}

/* Set/show external interrupt blocks */

t_stat io_set_eiblks (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 lnt;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
lnt = (int32) get_uint (cptr, 10, cpu_tab[cpu_model].eigrp_max, &r);
if ((r != SCPE_OK) || (lnt == 0))
    return SCPE_ARG;
int_reset (&int_dev);
io_set_eimax (lnt);
return SCPE_OK;
}

t_stat io_show_eiblks (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "eiblks=%d", ei_bmax);
return SCPE_OK;
}

/* Change the number of external I/O blocks, and restore the default
   chain configuration */

void io_set_eimax (uint32 max)
{
uint32 i, curr, ngrp;
uint8 *dflt_p;

ei_bmax = max;
if (QCPU_5X0)
    dflt_p = igrp_dflt_5x0;
else dflt_p = igrp_dflt_S56789;
curr = dflt_p[0] & ~I_STD;
for (i = 1, ngrp = 0; dflt_p[i] != 0; i++) {
    if (dflt_p[i] & I_STD) {
        int_lnk[curr] = dflt_p[i] & ~I_STD;
        curr = int_lnk[curr];
        }
    else if (ngrp < ei_bmax) {
        int_lnk[curr] = dflt_p[i];
        curr = int_lnk[curr];
        ngrp++;
        }
    else int_lnk[curr] = 0;
    }
return;
}

/* Set or show number of channels */

t_stat io_set_nchan (UNIT *uptr, int32 val, char *cptr, void *desc)
{
int32 i, num;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;
num = (int32) get_uint (cptr, 10, cpu_tab[cpu_model].chan_max, &r);
if ((r != SCPE_OK) || (num == 0))
    return SCPE_ARG;
chan_num = num;
for (i = 0; i < CHAN_N_CHAN; i++) {
    if (i < num)
        chan_dev[i].flags &= ~DEV_DIS;
    else chan_dev[i].flags |= DEV_DIS;
    chan_reset (&chan_dev[i]);
    }
return SCPE_OK;
}

t_stat io_show_nchan (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fprintf (st, "channels=%d", chan_num);
return SCPE_OK;
}

/* Set or show device channel assignment */

t_stat io_set_dvc (UNIT* uptr, int32 val, char *cptr, void *desc)
{
int32 num;
DEVICE *dptr;
dib_t *dibp;

if (((dptr = find_dev_from_unit (uptr)) == NULL) ||
    ((dibp = (dib_t *) dptr->ctxt) == NULL))
    return SCPE_IERR;
if ((cptr == NULL) || (*cptr == 0) || (*(cptr + 1) != 0))
    return SCPE_ARG;
num = *cptr - 'A';
if ((num < 0) || (num >= (int32) chan_num))
    return SCPE_ARG;
dibp->dva = (dibp->dva & ~DVA_CHAN) | (num << DVA_V_CHAN);
return SCPE_OK;
}

t_stat io_show_dvc (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
dib_t *dibp;

if (((dptr = find_dev_from_unit (uptr)) == NULL) ||
    ((dibp = (dib_t *) dptr->ctxt) == NULL))
    return SCPE_IERR;
fprintf (st, "channel=%c", DVA_GETCHAN (dibp->dva) + 'A');
return SCPE_OK;
}

/* Set or show device address */

t_stat io_set_dva (UNIT* uptr, int32 val, char *cptr, void *desc)
{
int32 num;
DEVICE *dptr;
dib_t *dibp;
t_stat r;

if (((dptr = find_dev_from_unit (uptr)) == NULL) ||
    ((dibp = (dib_t *) dptr->ctxt) == NULL))
    return SCPE_IERR;
if (cptr == NULL)
    return SCPE_ARG;
num = (int32) get_uint (cptr, 16, CHAN_N_DEV, &r);
if (r != SCPE_OK)
    return SCPE_ARG;
if (dibp->dva & DVA_MU)
    dibp->dva = (dibp->dva & ~DVA_DEVMU) | ((num & DVA_M_DEVMU) << DVA_V_DEVMU);
else dibp->dva = (dibp->dva & ~DVA_DEVSU) | ((num & DVA_M_DEVSU) << DVA_V_DEVSU);
return SCPE_OK;
}

t_stat io_show_dva (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
dib_t *dibp;

if (((dptr = find_dev_from_unit (uptr)) == NULL) ||
    ((dibp = (dib_t *) dptr->ctxt) == NULL))
    return SCPE_IERR;
fprintf (st, "address=%02X", DVA_GETDEV (dibp->dva));
return SCPE_OK;
}

/* Show channel state */

t_stat io_show_cst (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
dib_t *dibp;
uint32 ch, dva;

if (((dptr = find_dev_from_unit (uptr)) == NULL) ||
    ((dibp = (dib_t *) dptr->ctxt) == NULL))
    return SCPE_IERR;
ch = DVA_GETCHAN (dibp->dva);
dva = DVA_GETDEV (dibp->dva);
fprintf (st, "Status for device %s, channel=%02X, address=%02X:\n",
         sim_dname(dptr), ch, dva);
fprintf (st, "CLC:\t%06X\nBA:\t%06X\nBC:\t%04X\nCMD:\t%02X\n",
         chan[ch].clc[dva], chan[ch].ba[dva],
         chan[ch].bc[dva], chan[ch].cmd[dva]);
fprintf (st, "CMF:\t%02X\nCHF\t%04X\nCHI:\t%02X\nCHSF:\t%02X\n",
         chan[ch].cmf[dva], chan[ch].chf[dva],
         chan[ch].chi[dva], chan[ch].chsf[dva]);
return SCPE_OK;
}
