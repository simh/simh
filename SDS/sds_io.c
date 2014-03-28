/* sds_io.c: SDS 940 I/O simulator

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

   19-Mar-2012  RMS     Fixed various declarations (Mark Pizzolato)
*/

#include "sds_defs.h"

/* Data chain word */

#define CHD_INT         040                             /* int on chain */
#define CHD_PAGE        037                             /* new page # */

/* Interlace POT */

#define CHI_V_WC        14                              /* word count */
#define CHI_M_WC        01777
#define CHI_GETWC(x)    (((x) >> CHI_V_WC) & CHI_M_WC)
#define CHI_V_MA        0                               /* mem address */
#define CHI_M_MA        037777
#define CHI_GETMA(x)    (((x) >> CHI_V_MA) & CHI_M_MA)

/* System interrupt POT */

#define SYI_V_GRP       18                              /* group */
#define SYI_M_GRP       077
#define SYI_GETGRP(x)   (((x) >> SYI_V_GRP) & SYI_M_GRP)
#define SYI_DIS         (1 << 17)                       /* disarm if 0 */
#define SYI_ARM         (1 << 16)                       /* arm if 1 */
#define SYI_M_INT       0177777                         /* interrupt */

/* Pseudo-device number for EOM/SKS mode 3 */

#define I_GETDEV3(x)    ((((x) & 020046000) != 020046000)? ((x) & DEV_MASK): DEV_MASK)

#define TST_XFR(d,c)    (xfr_req && dev_map[d][c])
#define SET_XFR(d,c)    xfr_req = xfr_req | dev_map[d][c]
#define CLR_XFR(d,c)    xfr_req = xfr_req & ~dev_map[d][c]
#define INV_DEV(d,c)    (dev_dsp[d][c] == NULL)
#define VLD_DEV(d,c)    (dev_dsp[d][c] != NULL)
#define TST_EOR(c)      (chan_flag[c] & CHF_EOR)
#define QAILCE(a)       (((a) >= POT_ILCY) && ((a) < (POT_ILCY + NUM_CHAN)))

uint8 chan_uar[NUM_CHAN];                               /* unit addr */
uint16 chan_wcr[NUM_CHAN];                              /* word count */
uint16 chan_mar[NUM_CHAN];                              /* mem addr */
uint8 chan_dcr[NUM_CHAN];                               /* data chain */
uint32 chan_war[NUM_CHAN];                              /* word assembly */
uint8 chan_cpw[NUM_CHAN];                               /* char per word */
uint8 chan_cnt[NUM_CHAN];                               /* char count */
uint16 chan_mode[NUM_CHAN];                             /* mode */
uint16 chan_flag[NUM_CHAN];                             /* flags */
static const char *chname[NUM_CHAN] = {
    "W", "Y", "C", "D", "E", "F", "G", "H"
    };

extern uint32 M[MAXMEMSIZE];                            /* memory */
extern uint32 int_req;                                  /* int req */
extern uint32 xfr_req;                                  /* xfer req */
extern uint32 alert;                                    /* pin/pot alert */
extern uint32 X, EM2, EM3, OV, ion, bpt;
extern uint32 nml_mode, usr_mode;
extern int32 rtc_pie;
extern int32 stop_invins, stop_invdev, stop_inviop;
extern uint32 mon_usr_trap;
extern UNIT cpu_unit;

t_stat chan_reset (DEVICE *dptr);
t_stat chan_read (int32 ch);
t_stat chan_write (int32 ch);
void chan_write_mem (int32 ch);
void chan_flush_war (int32 ch);
uint32 chan_mar_inc (int32 ch);
t_stat chan_eor (int32 ch);
t_stat pot_ilc (uint32 num, uint32 *dat);
t_stat pot_dcr (uint32 num, uint32 *dat);
t_stat pin_adr (uint32 num, uint32 *dat);
t_stat pot_fork (uint32 num, uint32 *dat);
t_stat dev_disc (uint32 ch, uint32 dev);
t_stat dev_wreor (uint32 ch, uint32 dev);
extern t_stat pot_RL1 (uint32 num, uint32 *dat);
extern t_stat pot_RL2 (uint32 num, uint32 *dat);
extern t_stat pot_RL4 (uint32 num, uint32 *dat);
extern t_stat pin_rads (uint32 num, uint32 *dat);
extern t_stat pot_rada (uint32 num, uint32 *dat);
extern t_stat pin_dsk (uint32 num, uint32 *dat);
extern t_stat pot_dsk (uint32 num, uint32 *dat);
t_stat pin_mux (uint32 num, uint32 *dat);
t_stat pot_mux (uint32 num, uint32 *dat);
extern void set_dyn_map (void);

/* SDS I/O model

   A device is modeled by its interactions with a channel.  Devices can only be
   accessed via channels.  Each channel has its own device address space.  This
   means devices can only be accessed from a specific channel.

   I/O operations start with a channel connect.  The EOM instruction is passed
   to the device via the conn routine.  This routine is also used for non-channel
   EOM's to the device.  For channel connects, the device must remember the
   channel number.

   The device responds (after a delay) by setting its XFR_RDY flag.  This causes
   the channel to invoke either the read or write routine (for input or output)
   to get or put the next character.  If the device is an asynchronous output
   device, it calls routine chan_set_ordy to see if there is output available.
   If there is, XFR_RDY is set; if not, the channel is marked to wake the
   attached device when output is available.  This prevents invalid rate errors.

   Output may be terminated by a write end of record, a disconnect, or both.
   Write end of record occurs when the word count reaches zero on an IORD or IORP
   operation.  It also occurs if a TOP instruction is issued.  The device is
   expected to respond by setting the end of record indicator in the channel,
   which will in turn trigger an end of record interrupt.

   When the channel operation completes, the channel disconnects and calls the
   disconnect processor to perform any device specific cleanup.  The differences
   between write end of record and disconnect are subtle.  On magtape output,
   for example, both signal end of record; but write end of record allows the
   magtape to continue moving, while disconnect halts its motion.

   Valid devices supply a routine to handle potentially all I/O operations
   (connect, disconnect, read, write, write end of record, sks).  There are
   separate routines for PIN and POT.

   Channels could, optionally, handle 12b or 24b characters.  The simulator can
   support all widths.
*/

t_stat chan_show_reg (FILE *st, UNIT *uptr, int32 val, void *desc);

struct aldisp {
    t_stat      (*pin) (uint32 num, uint32 *dat);       /* altnum, *dat */
    t_stat      (*pot) (uint32 num, uint32 *dat);       /* altnum, *dat */
    };

/* Channel data structures

   chan_dev     channel device descriptor
   chan_unit    channel unit descriptor
   chan_reg     channel register list
*/

UNIT chan_unit = { UDATA (NULL, 0, 0) };

REG chan_reg[] = {
    { BRDATA (UAR, chan_uar, 8, 6, NUM_CHAN) },
    { BRDATA (WCR, chan_wcr, 8, 15, NUM_CHAN) },
    { BRDATA (MAR, chan_mar, 8, 16, NUM_CHAN) },
    { BRDATA (DCR, chan_dcr, 8, 6, NUM_CHAN) },
    { BRDATA (WAR, chan_war, 8, 24, NUM_CHAN) },
    { BRDATA (CPW, chan_cpw, 8, 2, NUM_CHAN) },
    { BRDATA (CNT, chan_cnt, 8, 3, NUM_CHAN) },
    { BRDATA (MODE, chan_mode, 8, 12, NUM_CHAN) },
    { BRDATA (FLAG, chan_flag, 8, CHF_N_FLG, NUM_CHAN) },
    { NULL }
    };

MTAB chan_mod[] = {
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, CHAN_W, "W", NULL,
      NULL, &chan_show_reg, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, CHAN_Y, "Y", NULL,
      NULL, &chan_show_reg, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, CHAN_C, "C", NULL,
      NULL, &chan_show_reg, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, CHAN_D, "D", NULL,
      NULL, &chan_show_reg, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, CHAN_E, "E", NULL,
      NULL, &chan_show_reg, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, CHAN_F, "F", NULL,
      NULL, &chan_show_reg, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, CHAN_G, "G", NULL,
      NULL, &chan_show_reg, NULL },
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, CHAN_H, "H", NULL,
      NULL, &chan_show_reg, NULL }
    };

DEVICE chan_dev = {
    "CHAN", &chan_unit, chan_reg, chan_mod,
    1, 8, 8, 1, 8, 8,
    NULL, NULL, &chan_reset,
    NULL, NULL, NULL
    };

/* Tables */

static const uint32 int_zc[8] = {
    INT_WZWC, INT_YZWC, INT_CZWC, INT_DZWC,
    INT_EZWC, INT_FZWC, INT_GZWC, INT_HZWC
    };

static const uint32 int_er[8] = {
    INT_WEOR, INT_YEOR, INT_CEOR, INT_DEOR,
    INT_EEOR, INT_FEOR, INT_GEOR, INT_HEOR
    };

/* dev_map maps device and channel numbers to a transfer flag masks */

uint32 dev_map[64][NUM_CHAN];

/* dev_dsp maps device and channel numbers to dispatch routines */

t_stat (*dev_dsp[64][NUM_CHAN])() = { {NULL} };

/* dev3_dsp maps system device numbers to dispatch routines */

t_stat (*dev3_dsp[64])() = { NULL };

/* dev_alt maps alert numbers to dispatch routines */

struct aldisp dev_alt[] = {
    { NULL, NULL },
    { NULL, &pot_ilc }, { NULL, &pot_ilc },
    { NULL, &pot_ilc }, { NULL, &pot_ilc },
    { NULL, &pot_ilc }, { NULL, &pot_ilc },
    { NULL, &pot_ilc }, { NULL, &pot_ilc },
    { NULL, &pot_dcr }, { NULL, &pot_dcr },
    { NULL, &pot_dcr }, { NULL, &pot_dcr },
    { NULL, &pot_dcr }, { NULL, &pot_dcr },
    { NULL, &pot_dcr }, { NULL, &pot_dcr },
    { &pin_adr, NULL }, { &pin_adr, NULL },
    { &pin_adr, NULL }, { &pin_adr, NULL },
    { &pin_adr, NULL }, { &pin_adr, NULL },
    { &pin_adr, NULL }, { &pin_adr, NULL },
    { NULL, &pot_RL1 }, { NULL, &pot_RL2 },
    { NULL, &pot_RL4 },
    { &pin_rads, NULL }, { NULL, &pot_rada },
    { &pin_dsk, &pot_dsk }, { NULL, &pot_fork },
    { &pin_mux, &pot_mux }
    };

/* Single word I/O instructions */

t_stat op_wyim (uint32 inst, uint32 *dat)
{
int32 ch, dev;

ch = (inst & 000200000)? CHAN_W: CHAN_Y;                /* get chan# */
dev = chan_uar[ch] & DEV_MASK;                          /* get dev # */
if (chan_cnt[ch] <= chan_cpw[ch]) {                     /* buffer empty? */
    if (dev == 0)                                       /* no device? dead */
        return STOP_INVIOP;
    return STOP_IONRDY;                                 /* hang until full */
    }
*dat = chan_war[ch];                                    /* get data */
chan_war[ch] = 0;                                       /* reset war */
chan_cnt[ch] = 0;                                       /* reset cnt */
return SCPE_OK;
}

t_stat op_miwy (uint32 inst, uint32 dat)
{
int32 ch, dev;

ch = (inst & 000200000)? CHAN_W: CHAN_Y;                /* get chan# */
dev = chan_uar[ch] & DEV_MASK;                          /* get dev # */
if (chan_cnt[ch] != 0) {                                /* buffer full? */
    if (dev == 0)                                       /* no device? dead */
        return STOP_INVIOP;
    return STOP_IONRDY;                                 /* hang until full */
    }
chan_war[ch] = dat;                                     /* get data */
chan_cnt[ch] = chan_cpw[ch] + 1;                        /* buffer full */
if (chan_flag[ch] & CHF_OWAK) {                         /* output wake? */
    if (VLD_DEV (dev, ch))                              /* wake channel */
        SET_XFR (dev, ch);
    chan_flag[ch] = chan_flag[ch] & ~CHF_OWAK;          /* clear wake */
    }
return SCPE_OK;
}

t_stat op_pin (uint32 *dat)
{
uint32 al = alert;                                      /* local copy */

alert = 0;                                              /* clear alert */
if ((al == 0) || (dev_alt[al].pin == NULL))             /* inv alert? */
    CRETIOP;
return dev_alt[al].pin (al, dat);                       /* PIN from dev */
}

t_stat op_pot (uint32 dat)
{
uint32 al = alert;                                      /* local copy */

alert = 0;                                              /* clear alert */
if ((al == 0) || (dev_alt[al].pot == NULL))             /* inv alert? */
    CRETIOP;
return dev_alt[al].pot (al, &dat);                      /* POT to dev */
}

/* EOM/EOD */

t_stat op_eomd (uint32 inst)
{
uint32 mod = I_GETIOMD (inst);                          /* get mode */
uint32 ch = I_GETEOCH (inst);                           /* get chan # */
uint32 dev = inst & DEV_MASK;                           /* get dev # */
uint32 ch_dev = chan_uar[ch] & DEV_MASK;                /* chan curr dev # */
t_stat r;

switch (mod) {

    case 0:                                             /* IO control */
        if (dev) {                                      /* new dev? */
            if (ch_dev)                                 /* chan act? err */
                CRETIOP;
            if (INV_DEV (dev, ch))                      /* inv dev? err */
                CRETDEV;
            chan_war[ch] = chan_cnt[ch] = 0;            /* init chan */
            chan_dcr[ch] = 0;
            chan_uar[ch] = 0;
            if (!(chan_flag[ch] & CHF_ILCE) &&          /* ignore if ilc */
                !QAILCE (alert)) {                      /* already alerted */
                chan_flag[ch] = chan_mode[ch] = 0;
                if (ch >= CHAN_E)
                    chan_mode[ch] = CHM_CE;
                }
            if ((r = dev_dsp[dev][ch] (IO_CONN, inst, NULL)))/* connect */
                return r;
            if (!(chan_flag[ch] & CHF_ILCE) &&          /* ignore if ilc */
                !QAILCE (alert)) {                      /* already alerted */
                if ((inst & I_IND) || (ch >= CHAN_C)) { /* C-H? alert ilc */
                    alert = POT_ILCY + ch;
                    chan_mar[ch] = chan_wcr[ch] = 0;
                    }
                }
            if (chan_flag[ch] & CHF_24B)                /* 24B? 1 ch/wd */
                chan_cpw[ch] = 0;
            else if (chan_flag[ch] & CHF_12B)           /* 12B? 2 ch/wd */
                chan_cpw[ch] = CHC_GETCPW (inst) & 1;
            else chan_cpw[ch] = CHC_GETCPW (inst);      /* 6b, 1-4 ch/wd */
            chan_uar[ch] = dev;                         /* connected */
            if ((dev & DEV_OUT) && ion && !QAILCE (alert)) /* out, prog IO? */
                int_req = int_req | int_zc[ch];         /* initial intr */
            }
        else return dev_disc (ch, ch_dev);              /* disconnect */
        break;

    case 1:                                             /* buf control */
        if (QAILCE (alert)) {                           /* ilce alerted? */
            ch = alert - POT_ILCY;                      /* derive chan */
            if (ch >= CHAN_E)                           /* DACC? ext */
                inst = inst | CHM_CE;
            chan_mode[ch] = inst;                       /* save mode */
            chan_mar[ch] = (CHM_GETHMA (inst) << 14) |  /* get hi mar */
                (chan_mar[ch] & CHI_M_MA);
            chan_wcr[ch] = (CHM_GETHWC (inst) << 10) |  /* get hi wc */
                (chan_wcr[ch] & CHI_M_WC);
            }
        else if (dev) {                                 /* dev EOM */
            if (INV_DEV (dev, ch))                      /* inv dev? err */
                CRETDEV;
            return dev_dsp[dev][ch] (IO_EOM1, inst, NULL);
            }
        else {                                          /* chan EOM */
            inst = inst & 047677;
            if (inst == 040000) {                       /* alert ilce */
                alert = POT_ILCY + ch;
                chan_mar[ch] = chan_wcr[ch] = 0;
                }
            else if (inst == 002000)                    /* alert addr */
                alert = POT_ADRY + ch;
            else if (inst == 001000)                    /* alert DCR */
                alert = POT_DCRY + ch;
            else if (inst == 004000) {                  /* term output */
                if (ch_dev & DEV_OUT) {                 /* to output dev? */
                    if (chan_cnt[ch] || (chan_flag[ch] & CHF_ILCE)) /* busy, DMA? */
                        chan_flag[ch] = chan_flag[ch] | CHF_TOP;    /* TOP pending */
                    else return dev_wreor (ch, ch_dev); /* idle, write EOR */
                    }                                   /* end else TOP */
                else if (ch_dev & DEV_MT) {             /* change to scan? */
                    chan_uar[ch] = chan_uar[ch] | DEV_MTS;    /* change dev addr */
                    chan_flag[ch] = chan_flag[ch] | CHF_SCAN; /* set scan flag */
                    }                                   /* end else change scan */
                }                                       /* end else term output */
            }                                           /* end else chan EOM */
        break;

    case 2:                                             /* internal */
        if (ch >= CHAN_E) {                             /* EOD? */
            if (inst & 00300) {                         /* set EM? */
                if (inst & 00100)
                    EM2 = inst & 07;
                if (inst & 00200)
                    EM3 = (inst >> 3) & 07;
                set_dyn_map ();
                }
            break;
            }                                           /* end if EOD */
        if (inst & 00001)                               /* clr OV */
            OV = 0;
        if (inst & 00002)                               /* ion */
            ion = 1;
        else if (inst & 00004)                          /* iof */
            ion = 0;
        if ((inst & 00010) && (((X >> 1) ^ X) & EXPS))
            OV = 1;
        if (inst & 00020)                               /* alert sys int */
            alert = POT_SYSI;
        if (inst & 00100)                               /* arm clk pls */
            rtc_pie = 1;
        else if (inst & 00200)                          /* disarm pls */
            rtc_pie = 0;
        if ((inst & 01400) == 01400)                    /* alert RL4 */
            alert = POT_RL4;
        else if (inst & 00400)                          /* alert RL1 */
            alert = POT_RL1;
        else if (inst & 01000)                          /* alert RL2 */
            alert = POT_RL2;
        if (inst & 02000) {                             /* nml to mon */
            nml_mode = usr_mode = 0;
            if (inst & 00400)
                mon_usr_trap = 1;
            }
        break;

    case 3:                                             /* special */
        dev = I_GETDEV3 (inst);                         /* special device */
        if (dev == DEV3_SMUX && !(cpu_unit.flags & UNIT_GENIE))
            dev = DEV3_GMUX;
        if (dev3_dsp[dev])                              /* defined? */
            return dev3_dsp[dev] (IO_CONN, inst, NULL);
        CRETINS;
        }                                               /* end case */

return SCPE_OK;
}

/* Skip if not signal */

t_stat op_sks (uint32 inst, uint32 *dat)
{
uint32 mod = I_GETIOMD (inst);                          /* get mode */
uint32 ch = I_GETSKCH (inst);                           /* get chan # */
uint32 dev = inst & DEV_MASK;                           /* get dev # */

*dat = 0;
if ((ch == 4) && !(inst & 037774)) {                    /* EM test */
    if (((inst & 0001) && (EM2 != 2)) ||
        ((inst & 0002) && (EM3 != 3)))
        *dat = 1;
    return SCPE_OK;
    }
switch (mod) {

    case 1:                                             /* ch, dev */
        if (dev) {                                      /* device */
            if (INV_DEV (dev, ch))                      /* inv dev? err */
                CRETDEV;
            dev_dsp[dev][ch] (IO_SKS, inst, dat);       /* do test */
            }
        else {                                          /* channel */
            if (((inst & 04000) && (chan_uar[ch] == 0)) ||
                ((inst & 02000) && (chan_wcr[ch] == 0)) ||
                ((inst & 01000) && ((chan_flag[ch] & CHF_ERR) == 0)) ||
                ((inst & 00400) && (chan_flag[ch] & CHF_IREC)))
                *dat = 1;
            }
        break;

    case 2:                                             /* internal test */
        if (inst & 0001) {                              /* test OV */
            *dat = OV ^ 1;                              /* skip if off */
            OV = 0;                                     /* and reset */
            break;
            }
        if (((inst & 00002) && !ion) ||                 /* ion, bpt test */
            ((inst & 00004) && ion) ||
            ((inst & 00010) && ((chan_flag[CHAN_W] & CHF_ERR) == 0)) ||
            ((inst & 00020) && ((chan_flag[CHAN_Y] & CHF_ERR) == 0)) ||
            ((inst & 00040) && ((bpt & 001) == 0)) ||
            ((inst & 00100) && ((bpt & 002) == 0)) ||
            ((inst & 00200) && ((bpt & 004) == 0)) ||
            ((inst & 00400) && ((bpt & 010) == 0)) ||
            ((inst & 01000) && (chan_uar[CHAN_W] == 0)) ||
            ((inst & 02000) && (chan_uar[CHAN_Y] == 0)))
            *dat = 1;
        break;

    case 3:                                             /* special */
        dev = I_GETDEV3 (inst);                         /* special device */
        if (dev == DEV3_SMUX && !(cpu_unit.flags & UNIT_GENIE))
            dev = DEV3_GMUX;
        if (dev3_dsp[dev])
            dev3_dsp[dev] (IO_SKS, inst, dat);
        else CRETINS;
        }                                               /* end case */

return SCPE_OK;
}

/* PIN/POT routines */

t_stat pot_ilc (uint32 num, uint32 *dat)
{
uint32 ch = num - POT_ILCY;

chan_mar[ch] = (chan_mar[ch] & ~CHI_M_MA) | CHI_GETMA (*dat);
chan_wcr[ch] = (chan_wcr[ch] & ~CHI_M_WC) | CHI_GETWC (*dat);
chan_flag[ch] = chan_flag[ch] | CHF_ILCE;
return SCPE_OK;
}

t_stat pot_dcr (uint32 num, uint32 *dat)
{
uint32 ch = num - POT_DCRY;

chan_dcr[ch] = (*dat) & (CHD_INT | CHD_PAGE);
chan_flag[ch] = chan_flag[ch] | CHF_DCHN;
return SCPE_OK;
}

t_stat pin_adr (uint32 num, uint32 *dat)
{
uint32 ch = num - POT_ADRY;

*dat = chan_mar[ch] & PAMASK;
return SCPE_OK;
}

/* System interrupt POT.

   The SDS 940 timesharing system uses a permanently asserted
   system interrupt as a way of forking the teletype input
   interrupt handler to a lower priority.  The interrupt is
   armed to set up the fork, and disarmed in the fork routine */

t_stat pot_fork (uint32 num, uint32 *dat)
{
uint32 igrp = SYI_GETGRP (*dat);                        /* get group */
uint32 fbit = (0100000 >> (VEC_FORK & 017));            /* bit in group */

if (igrp == ((VEC_FORK-0200) / 020)) {                  /* right group? */
    if ((*dat & SYI_ARM) && (*dat & fbit))              /* arm, bit set? */
        int_req = int_req | INT_FORK;
    if ((*dat & SYI_DIS) && !(*dat & fbit))             /* disarm, bit clr? */
        int_req = int_req & ~INT_FORK;
    }
return SCPE_OK;
}

/* Channel read invokes the I/O device to get the next character and,
   if not end of record, assembles it into the word assembly register.
   If the interlace is on, the full word is stored in memory.
   The key difference points for the various terminal functions are

        end of record   comp: EOT interrupt
                        IORD, IOSD: EOR interrupt, disconnect
                        IORP, IOSP: EOR interrupt, interrecord
        interlace off:  comp: EOW interrupt
                        IORD, IORP: ignore
                        IOSD, IOSP: overrun error
        --wcr == 0:     comp: clear interlace
                        IORD, IORP, IOSP: ZWC interrupt
                        IOSD: ZWC interrupt, EOR interrupt, disconnect

   Note that the channel can be disconnected if CHN_EOR is set, but must
   not be if XFR_REQ is set */

t_stat chan_read (int32 ch)
{
uint32 dat = 0;
uint32 dev = chan_uar[ch] & DEV_MASK;
uint32 tfnc = CHM_GETFNC (chan_mode[ch]);
t_stat r = SCPE_OK;

if (dev && TST_XFR (dev, ch)) {                         /* ready to xfr? */
    if (INV_DEV (dev, ch)) CRETIOP;                     /* can't read? */
    r = dev_dsp[dev][ch] (IO_READ, dev, &dat);          /* read data */
    if (r)                                              /* error? */
        chan_flag[ch] = chan_flag[ch] | CHF_ERR;
    if (chan_flag[ch] & CHF_24B)                        /* 24B? */
        chan_war[ch] = dat;
    else if (chan_flag[ch] & CHF_12B)                   /* 12B? */
        chan_war[ch] = ((chan_war[ch] << 12) | (dat & 07777)) & DMASK;
    else chan_war[ch] = ((chan_war[ch] << 6) | (dat & 077)) & DMASK;
    if (chan_flag[ch] & CHF_SCAN)                       /* scanning? */
        chan_cnt[ch] = chan_cpw[ch];                    /* never full */
    else chan_cnt[ch] = chan_cnt[ch] + 1;               /* insert char */
    if (chan_cnt[ch] > chan_cpw[ch]) {                  /* full? */
        if (chan_flag[ch] & CHF_ILCE) {                 /* interlace on? */
            chan_write_mem (ch);                        /* write to mem */
            if (chan_wcr[ch] == 0) {                    /* wc zero? */
                chan_flag[ch] = chan_flag[ch] & ~CHF_ILCE; /* clr interlace */
                if ((tfnc != CHM_COMP) && (chan_mode[ch] & CHM_ZC))
                    int_req = int_req | int_zc[ch];     /* zwc interrupt */
                if (tfnc == CHM_IOSD) {                 /* IOSD? also EOR */
                    if (chan_mode[ch] & CHM_ER)
                        int_req = int_req | int_er[ch];
                    dev_disc (ch, dev);                 /* disconnect */
                    }                                   /* end if IOSD */
                }                                       /* end if wcr == 0 */
            }                                           /* end if ilce on */
        else {                                          /* interlace off */
            if (TST_EOR (ch))                           /* eor? */
                return chan_eor (ch);
            if (tfnc == CHM_COMP) {                     /* C: EOW, intr */
                if (ion)
                    int_req = int_req | int_zc[ch];
                }
            else if (tfnc & CHM_SGNL)                   /* Sx: error */
                chan_flag[ch] = chan_flag[ch] | CHF_ERR;
            else chan_cnt[ch] = chan_cpw[ch];           /* Rx: ignore */
            }                                           /* end else ilce */
        }                                               /* end if full */
    }                                                   /* end if xfr */
if (TST_EOR (ch)) {                                     /* end record? */
    if (tfnc == CHM_COMP)                               /* C: fill war */
        chan_flush_war (ch);
    else if (chan_cnt[ch]) {                            /* RX, CX: fill? */
        chan_flush_war (ch);                            /* fill war */
        if (chan_flag[ch] & CHF_ILCE)                   /* ilce on? store */
            chan_write_mem (ch);
        }                                               /* end else if cnt */
    return chan_eor (ch);                               /* eot/eor int */
    }
return r;
}

void chan_write_mem (int32 ch)
{
WriteP (chan_mar[ch], chan_war[ch]);                    /* write to mem */
chan_mar[ch] = chan_mar_inc (ch);                       /* incr mar */
chan_wcr[ch] = (chan_wcr[ch] - 1) & 077777;             /* decr wcr */
chan_war[ch] = 0;                                       /* reset war */
chan_cnt[ch] = 0;                                       /* reset cnt */
return;
}

void chan_flush_war (int32 ch)
{
int32 i = (chan_cpw[ch] - chan_cnt[ch]) + 1;

if (i) {
    if (chan_flag[ch] & CHF_24B)
        chan_war[ch] = 0;
    else if (chan_flag[ch] & CHF_12B)
        chan_war[ch] = (chan_war[ch] << 12) & DMASK;
    else chan_war[ch] = (chan_war[ch] << (i * 6)) & DMASK;
    chan_cnt[ch] = chan_cpw[ch] + 1;
    }
return;
}

/* Channel write gets the next character and sends it to the I/O device.
   If this is the last character in an interlace operation, the end of
   record operation is invoked.
   The key difference points for the various terminal functions are

        end of record:  comp: EOT interrupt
                        IORD, IOSD: EOR interrupt, disconnect
                        IORP, IOSP: EOR interrupt, interrecord
        interlace off:  if not end of record, EOW interrupt
        --wcr == 0:     comp: EOT interrupt, disconnect
                        IORD, IORP: ignore
                        IOSD: ZWC interrupt, disconnect
                        IOSP: ZWC interrupt, interrecord
*/
t_stat chan_write (int32 ch)
{
uint32 dat = 0;
uint32 dev = chan_uar[ch] & DEV_MASK;
uint32 tfnc = CHM_GETFNC (chan_mode[ch]);
t_stat r = SCPE_OK;

if (dev && TST_XFR (dev, ch)) {                         /* ready to xfr? */
    if (INV_DEV (dev, ch))                              /* invalid dev? */
        CRETIOP;
    if (chan_cnt[ch] == 0) {                            /* buffer empty? */
        if (chan_flag[ch] & CHF_ILCE) {                 /* interlace on? */
            chan_war[ch] = ReadP (chan_mar[ch]);
            chan_mar[ch] = chan_mar_inc (ch);           /* incr mar */
            chan_wcr[ch] = (chan_wcr[ch] - 1) & 077777; /* decr mar */
            chan_cnt[ch] = chan_cpw[ch] + 1;            /* set cnt */
            }
        else {                                          /* ilce off */
            CLR_XFR (dev, ch);                          /* cant xfr */
            if (TST_EOR (dev))                          /* EOR? */
                return chan_eor (ch);
            chan_flag[ch] = chan_flag[ch] | CHF_ERR;    /* rate err */
            return SCPE_OK;
            }                                           /* end else ilce */
        }                                               /* end if cnt */
    chan_cnt[ch] = chan_cnt[ch] - 1;                    /* decr cnt */
    if (chan_flag[ch] & CHF_24B)                        /* 24B? */
        dat = chan_war[ch];
    else if (chan_flag[ch] & CHF_12B) {                 /* 12B? */
        dat = (chan_war[ch] >> 12) & 07777;             /* get halfword */
        chan_war[ch] = (chan_war[ch] << 12) & DMASK;    /* remove from war */
        }
    else {                                              /* 6B */
        dat = (chan_war[ch] >> 18) & 077;               /* get char */
        chan_war[ch] = (chan_war[ch] << 6) & DMASK;     /* remove from war */
        }
    r = dev_dsp[dev][ch] (IO_WRITE, dev, &dat);         /* write */
    if (r)                                              /* error? */
        chan_flag[ch] = chan_flag[ch] | CHF_ERR;
    if (chan_cnt[ch] == 0) {                            /* buf empty? */
        if (chan_flag[ch] & CHF_ILCE) {                 /* ilce on? */
            if (chan_wcr[ch] == 0) {                    /* wc now 0? */
                chan_flag[ch] = chan_flag[ch] & ~CHF_ILCE; /* ilc off */
                if (tfnc == CHM_COMP) {                 /* compatible? */
                    if (ion)
                        int_req = int_req | int_zc[ch];
                    dev_disc (ch, dev);                 /* disconnnect */
                    }                                   /* end if comp */
                else {                                  /* extended */
                    if (chan_mode[ch] & CHM_ZC)         /* ZWC int */
                        int_req = int_req | int_zc[ch];
                    if (tfnc == CHM_IOSD) {             /* SD */
                        if (chan_mode[ch] & CHM_ER)     /* EOR int */
                            int_req = int_req | int_er[ch];
                        dev_disc (ch, dev);             /* disconnnect */
                        }                               /* end if SD */
                    else if (!(tfnc && CHM_SGNL) ||     /* IORx or IOSP TOP? */
                        (chan_flag[ch] & CHF_TOP))
                        dev_wreor (ch, dev);            /* R: write EOR */
                    chan_flag[ch] = chan_flag[ch] & ~CHF_TOP;
                    }                                   /* end else comp */
                }                                       /* end if wcr */
            }                                           /* end if ilce */
        else if (chan_flag[ch] & CHF_TOP) {             /* off, TOP pending? */
            chan_flag[ch] = chan_flag[ch] & ~CHF_TOP;   /* clear TOP */
            dev_wreor (ch, dev);                        /* write EOR */
            }
        else if (ion)                                   /* no TOP, EOW intr */
            int_req = int_req | int_zc[ch];
        }                                               /* end if cnt */
   }                                                    /* end if xfr */
if (TST_EOR (ch))                                       /* eor rcvd? */
    return chan_eor (ch);
return r;
}

/* MAR increment */

uint32 chan_mar_inc (int32 ch)
{
uint32 t = (chan_mar[ch] + 1) & PAMASK;                 /* incr mar */

if ((chan_flag[ch] & CHF_DCHN) && ((t & VA_POFF) == 0)) { /* chain? */
    chan_flag[ch] = chan_flag[ch] & ~CHF_DCHN;          /* clr flag */
    if (chan_dcr[ch] & CHD_INT)                         /* if armed, intr */
        int_req = int_req | int_zc[ch];
    t = (chan_dcr[ch] & CHD_PAGE) << VA_V_PN;           /* new mar */
    }
return t;
}

/* End of record action */

t_stat chan_eor (int32 ch)
{
uint32 tfnc = CHM_GETFNC (chan_mode[ch]);
uint32 dev = chan_uar[ch] & DEV_MASK;

chan_flag[ch] = chan_flag[ch] & ~(CHF_EOR | CHF_ILCE);  /* clr eor, ilce */
if (((tfnc == CHM_COMP) && ion) || (chan_mode[ch] & CHM_ER))
    int_req = int_req | int_er[ch];                     /* EOT/EOR? */
if (dev && (tfnc & CHM_PROC))                           /* P, still conn? */
    chan_flag[ch] = chan_flag[ch] | CHF_IREC;           /* interrecord */
else return dev_disc (ch, dev);                         /* disconnect */
return SCPE_OK;
}

/* Utility routines */

t_stat dev_disc (uint32 ch, uint32 dev)
{
chan_uar[ch] = 0;                                       /* disconnect */
if (dev_dsp[dev][ch])
    return dev_dsp[dev][ch] (IO_DISC, dev, NULL);
return SCPE_OK;
}

t_stat dev_wreor (uint32 ch, uint32 dev)
{
if (dev_dsp[dev][ch])
    return dev_dsp[dev][ch] (IO_WREOR, dev, NULL);
chan_flag[ch] = chan_flag[ch] | CHF_EOR;                /* set eor */
return SCPE_OK;
}

/* Externally visible routines */
/* Channel driver */

t_stat chan_process (void)
{
int32 i, dev;
t_stat r;

for (i = 0; i < NUM_CHAN; i++) {                        /* loop thru */
    dev = chan_uar[i] & DEV_MASK;                       /* get dev */
    if ((dev && TST_XFR (dev, i)) || TST_EOR (i)) {     /* chan active? */
        if (dev & DEV_OUT)                              /* write */
            r = chan_write (i);
        else r = chan_read (i);                         /* read */
        if (r)
            return r;
        }
    }
return SCPE_OK;
}

/* Test for channel active */

t_bool chan_testact (void)
{
int32 i, dev;

for (i = 0; i < NUM_CHAN; i++) {
    dev = chan_uar[i] & DEV_MASK;
    if ((dev && TST_XFR (dev, i)) || TST_EOR (i))
        return 1;
    }
return 0;
}

/* Async output device ready for more data */

void chan_set_ordy (int32 ch)
{
if ((ch >= 0) && (ch < NUM_CHAN)) {
    int32 dev = chan_uar[ch] & DEV_MASK;                /* get dev */
    if (chan_cnt[ch] || (chan_flag[ch] & CHF_ILCE))     /* buf or ilce? */
        SET_XFR (dev, ch);                              /* set xfr flg */
    else chan_flag[ch] = chan_flag[ch] | CHF_OWAK;      /* need wakeup */
    }
return;
}

/* Set flag in channel */

void chan_set_flag (int32 ch, uint32 fl)
{
if ((ch >= 0) && (ch < NUM_CHAN))
    chan_flag[ch] = chan_flag[ch] | fl;
return;
}

/* Set UAR in channel */

void chan_set_uar (int32 ch, uint32 dev)
{
if ((ch >= 0) && (ch < NUM_CHAN))
    chan_uar[ch] = dev & DEV_MASK;
return;
}

/* Disconnect channel */

void chan_disc (int32 ch)
{
if ((ch >= 0) && (ch < NUM_CHAN))
    chan_uar[ch] = 0;
return;
}

/* Reset channels */

t_stat chan_reset (DEVICE *dptr)
{
int32 i;

xfr_req = 0;
for (i = 0; i < NUM_CHAN; i++) {
    chan_uar[i] = 0;
    chan_wcr[i] = 0;
    chan_mar[i] = 0;
    chan_dcr[i] = 0;
    chan_war[i] = 0;
    chan_cpw[i] = 0;
    chan_cnt[i] = 0;
    chan_mode[i] = 0;
    chan_flag[i] = 0;
    }
return SCPE_OK;
}

/* Channel assignment routines */

t_stat set_chan (UNIT *uptr, int32 val, char *sptr, void *desc)
{
DEVICE *dptr;
DIB *dibp;
int32 i;

if (sptr == NULL)                                        /* valid args? */
    return SCPE_ARG;
if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
for (i = 0; i < NUM_CHAN; i++) {                        /* match input */
    if (strcmp (sptr, chname[i]) == 0) {                /* find string */
        if (val && !(val & (1 << i)))                   /* legal? */
            return SCPE_ARG;
        dibp->chan = i;                                 /* store new */
        return SCPE_OK;
        }
    }
return SCPE_ARG;
}

t_stat show_chan (FILE *st, UNIT *uptr, int32 val, void *desc)
{
DEVICE *dptr;
DIB *dibp;

if (uptr == NULL)
    return SCPE_IERR;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;
dibp = (DIB *) dptr->ctxt;
if (dibp == NULL)
    return SCPE_IERR;
fprintf (st, "channel=%s", chname[dibp->chan]);
return SCPE_OK;
}

/* Init device tables */

t_bool io_init (void)
{
DEVICE *dptr;
DIB *dibp;
DSPT *tplp;
int32 ch;
uint32 i, j, dev, doff;

/* Clear dispatch table, device map */

for (i = 0; i < NUM_CHAN; i++) {
    for (j = 0; j < (DEV_MASK + 1); j++) {
        dev_dsp[j][i] = NULL;
        dev_map[j][i] = 0;
        }
    }

/* Test each device for conflict; add to map; init tables */

for (i = 0; (dptr = sim_devices[i]); i++) {             /* loop thru devices */
    dibp = (DIB *) dptr->ctxt;                          /* get DIB */
    if ((dibp == NULL) || (dptr->flags & DEV_DIS))      /* exist, enabled? */
        continue;
    ch = dibp->chan;                                    /* get channel */
    dev = dibp->dev;                                    /* get device num */
    if (ch < 0)                                         /* special device */
        dev3_dsp[dev] = dibp->iop;
    else {
        if (dibp->tplt == NULL)                         /* must have template */
            return TRUE;
        for (tplp = dibp->tplt; tplp->num; tplp++) {    /* loop thru templates */
            for (j = 0; j < tplp->num; j++) {           /* repeat as needed */
                doff = dev + tplp->off + j;             /* get offset dnum */
                if (dev_map[doff][ch]) {                /* slot in use? */
                    sim_printf ("Device number conflict, chan = %s, devno = %02o\n",
                                chname[ch], doff);
                    return TRUE;
                    }
                dev_map[doff][ch] = dibp->xfr;          /* set xfr flag */
                dev_dsp[doff][ch] = dibp->iop;          /* set dispatch */
                }                                       /* end for j */
            }                                           /* end for tplt */
        }                                               /* end else */
    }                                                   /* end for i */
return FALSE;
}

/* Display channel state */

t_stat chan_show_reg (FILE *st, UNIT *uptr, int32 val, void *desc)
{
if ((val < 0) || (val >= NUM_CHAN)) return SCPE_IERR;
fprintf (st, "UAR:      %02o\n", chan_uar[val]);
fprintf (st, "WCR:      %05o\n", chan_wcr[val]);
fprintf (st, "MAR:      %06o\n", chan_mar[val]);
fprintf (st, "DCR:      %02o\n", chan_dcr[val]);
fprintf (st, "WAR:      %08o\n", chan_war[val]);
fprintf (st, "CPW:      %o\n", chan_cpw[val]);
fprintf (st, "CNT:      %o\n", chan_cnt[val]);
fprintf (st, "MODE:     %03o\n", chan_mode[val]);
fprintf (st, "FLAG:     %04o\n", chan_flag[val]);
return SCPE_OK;
}
