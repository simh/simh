/* i1401_dp.c: IBM 1311 disk simulator

   Copyright (c) 2002-2008, Robert M. Supnik

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

   dp           1311 disk pack

   18-Oct-02    RMS     Fixed bug in address comparison logic
   19-Sep-02    RMS     Minor edit for consistency with 1620
   15-Jun-02    RMS     Reworked address comparison logic

   The 1311 disk pack has 100 cylinders, 10 tracks/cylinder, 20 sectors/track.
   Each sector contains 106 characters of information:

   6c           sector address
   100c         sector data

   By default, a sector's address field will be '000000', which is illegal.
   This is interpreted to mean the implied sector number that would be in
   place if the disk pack had been formatted with sequential sector numbers.

   The sector data can be 100 characters without word marks, or 90 characters
   with word marks.  Load mode transfers 90 characters per sector with
   word marks, move mode transfers 100 characters per sector without word
   marks.  No attempt is made to catch incompatible writes (eg, load mode
   write followed by move mode read).
*/

#include "i1401_defs.h"

#define DP_NUMDR        5                               /* #drives */
#define UNIT_V_WAE      (UNIT_V_UF + 0)                 /* write addr enab */
#define UNIT_WAE        (1 << UNIT_V_WAE)

/* Disk format */

#define DP_ADDR         6                               /* address */
#define DP_DATA         100                             /* data */
#define DP_NUMCH        (DP_ADDR + DP_DATA)

#define DP_NUMSC        20                              /* #sectors */
#define DP_NUMSF        10                              /* #surfaces */
#define DP_NUMCY        100                             /* #cylinders */
#define DP_TOTSC        (DP_NUMCY*DP_NUMSF*DP_NUMSC)
#define DP_SIZE         (DP_TOTSC*DP_NUMCH)

/* Disk control field */

#define DCF_DRV         0                               /* drive select */
#define DCF_SEC         1                               /* sector addr */
#define DCF_SEC_LEN     6
#define DCF_CNT         (DCF_SEC + DCF_SEC_LEN)         /* sector count */
#define DCF_CNT_LEN     3
#define DCF_LEN         (DCF_CNT + DCF_CNT_LEN)
#define DCF_DIR         1                               /* direct seek */
#define DCF_DIR_LEN     4
#define DCF_DIR_FL      (DCF_DIR + DCF_DIR_LEN)         /* direct seek flag */
#define DCF_DSEEK       0xB

/* Functions */

#define FNC_SEEK        0                               /* seek */
#define FNC_CHECK       3                               /* check */
#define FNC_READ        1                               /* read sectors */
#define FNC_RSCO        5                               /* read sec cnt overlay */
#define FNC_RTRK        6                               /* read track */
#define FNC_WOFF        10                              /* offset for write */
#define FNC_WRITE       11                              /* write sectors */
#define FNC_WRSCO       15                              /* write sec cnt overlay */
#define FNC_WRTRK       16                              /* write track */

#define CYL             u3                              /* current cylinder */

extern uint8 M[];                                       /* memory */
extern int32 ind[64];
extern int32 AS, BS, iochk;
extern const int32 bcd_to_bin[16];
extern const int32 bin_to_bcd[16];
extern UNIT cpu_unit;

int32 dp_lastf = 0;                                     /* prior function */
int32 dp_time = 0;                                      /* seek time */

t_stat dp_reset (DEVICE *dptr);
t_stat dp_rdadr (UNIT *uptr, int32 sec, int32 flg, int32 wchk);
t_stat dp_rdsec (UNIT *uptr, int32 sec, int32 flg, int32 wchk);
t_stat dp_wradr (UNIT *uptr, int32 sec, int32 flg);
t_stat dp_wrsec (UNIT *uptr, int32 sec, int32 flg);
int32 dp_fndsec (UNIT *uptr, int32 sec, int32 dcf);
t_stat dp_nexsec (UNIT *uptr, int32 psec, int32 dcf);
t_bool dp_zeroad (uint8 *ap);
t_bool dp_cmp_ad (uint8 *ap, int32 dcf);
int32 dp_trkop (int32 drv, int32 sec);
int32 dp_cvt_bcd (int32 ad, int32 len);
void dp_cvt_bin (int32 ad, int32 len, int32 val, int32 flg);
int32 dp_get_cnt (int32 dcf);
void dp_fill (UNIT *uptr, uint32 da, int32 cnt);

/* DP data structures

   dp_dev       DSK device descriptor
   dp_unit      DSK unit list
   dp_reg       DSK register list
   dp_mod       DSK modifier list
*/

UNIT dp_unit[] = {
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_ATTABLE +
             UNIT_BUFABLE + UNIT_MUSTBUF + UNIT_BCD, DP_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_ATTABLE +
             UNIT_BUFABLE + UNIT_MUSTBUF + UNIT_BCD, DP_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_ATTABLE +
             UNIT_BUFABLE + UNIT_MUSTBUF + UNIT_BCD, DP_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_ATTABLE +
             UNIT_BUFABLE + UNIT_MUSTBUF + UNIT_BCD, DP_SIZE) },
    { UDATA (NULL, UNIT_FIX + UNIT_DISABLE + UNIT_ATTABLE +
             UNIT_BUFABLE + UNIT_MUSTBUF + UNIT_BCD, DP_SIZE) }
    };

REG dp_reg[] = {
    { FLDATA (ACC, ind[IN_ACC], 0) },
    { FLDATA (PWC, ind[IN_DPW], 0) },
    { FLDATA (WLR, ind[IN_LNG], 0) },
    { FLDATA (UNA, ind[IN_UNA], 0) },
    { FLDATA (ERR, ind[IN_DSK], 0) },
    { FLDATA (BSY, ind[IN_DBY], 0) },
    { DRDATA (LASTF, dp_lastf, 3) },
    { DRDATA (TIME, dp_time, 24), PV_LEFT },
    { URDATA (CYL, dp_unit[0].CYL, 10, 8, 0,
              DP_NUMDR, PV_LEFT + REG_RO) },
    { NULL }
    };

MTAB dp_mod[] = {
    { UNIT_WAE, 0, "write address disabled", "ADDROFF", NULL },
    { UNIT_WAE, UNIT_WAE, "write address enabled", "ADDRON", NULL }, 
    { 0 }
    };

DEVICE dp_dev = {
    "DP", dp_unit, dp_reg, dp_mod,
    DP_NUMDR, 10, 21, 1, 8, 7,
    NULL, NULL, &dp_reset,
    NULL, NULL, NULL
    };

/* Disk IO routine

   Inputs:
        fnc     =       function character
        flg     =       load vs move mode
        mod     =       modifier character
   Outputs:
        status  =       status
*/

t_stat dp_io (int32 fnc, int32 flg, int32 mod)
{
int32 dcf, drv, sec, psec, cnt, qwc, qzr, diff;
UNIT *uptr;
t_stat r;

dcf = BS;                                               /* save DCF addr */
qwc = 0;                                                /* not wcheck */
ind[IN_DPW] = ind[IN_LNG] = ind[IN_UNA] = 0;            /* clr indicators */
ind[IN_DSK] = ind[IN_ACC] = ind[IN_DBY] = 0;
if (sim_is_active (&dp_unit[0])) {                      /* ctlr busy? */
    ind[IN_DBY] = ind[IN_DSK] = 1;                      /* set indicators */
    return SCPE_OK;                                     /* done */
    }

AS = dcf + 6;                                           /* AS for most ops */
BS = dcf + DCF_CNT - 1;                                 /* minimum DCF */
if (ADDR_ERR (BS))                                      /* DCF in memory? */
    return STOP_WRAP;
if (M[dcf] & BBIT)                                      /* impl sel? cyl 8-4-2 */
    drv = M[dcf + DCF_SEC + 1] & 0xE;
else drv = M[dcf] & DIGIT;                              /* get drive sel */
if ((drv == 0) || (drv & 1) || (drv > BCD_ZERO))        /* bad drive #? */
    return STOP_INVDSK;
drv = bcd_to_bin[drv] >> 1;                             /* convert */
uptr = dp_dev.units + drv;                              /* get unit ptr */
if ((uptr->flags & UNIT_ATT) == 0) {                    /* attached? */
    ind[IN_DSK] = ind[IN_ACC] = 1;                      /* no, error */
    CRETIOE (iochk, SCPE_UNATT);
    }

if ((fnc == FNC_SEEK) &&                                /* seek and */
    (M[dcf + DCF_DIR_FL] & DCF_DSEEK) == DCF_DSEEK) {   /* direct flag? */
    diff = dp_cvt_bcd (dcf + DCF_DIR, DCF_DIR_LEN);     /* cvt diff */
    if (diff < 0)                                       /* error? */
        return STOP_INVDSC;
    diff = diff >> 1;                                   /* diff is *2 */
    if ((M[dcf + DCF_DIR + DCF_DIR_LEN - 1] & ZONE) == BBIT)
        diff = -diff;                                   /* get sign */
    uptr->CYL = uptr->CYL + diff;                       /* bound seek */
    if (uptr->CYL < 0)
        uptr->CYL = 0;
    else if (uptr->CYL >= DP_NUMCY) {                   /* too big? */
        uptr->CYL = 0;                                  /* system hangs */
        return STOP_INVDCY;
        }
    sim_activate (&dp_unit[0], dp_time);                /* set ctlr busy */
    return SCPE_OK;                                     /* done! */
    }

sec = dp_cvt_bcd (dcf + DCF_SEC, DCF_SEC_LEN);          /* cvt sector */
if ((sec < 0) || (sec >= (DP_NUMDR * DP_TOTSC)))        /* bad sector? */
    return STOP_INVDSC;
if (fnc == FNC_SEEK) {                                  /* seek? */
    uptr->CYL = (sec / (DP_NUMSF * DP_NUMSC)) %         /* set cyl # */
        DP_NUMCY;
    sim_activate (&dp_unit[0], dp_time);                /* set ctlr busy */
    return SCPE_OK;                                     /* done! */
    }

BS = dcf + DCF_LEN;                                     /* full DCF */
if (ADDR_ERR (BS))                                      /* DCF in memory? */
    return STOP_WRAP;
cnt = dp_get_cnt (dcf);                                 /* get count */
if (cnt < 0)                                            /* bad count? */
    return STOP_INVDCN;

if (fnc >= FNC_WOFF)                                    /* invalid func */
    return STOP_INVDFN;
if (mod == BCD_W) {                                     /* write? */
    if (fnc == FNC_CHECK) {                             /* write check? */
        qwc = 1;                                        /* special read */
        fnc = dp_lastf;                                 /* use last func */
        }
    else {
        dp_lastf = fnc;                                 /* save func */         
        fnc = fnc + FNC_WOFF;                           /* change to write */
        }
    }
else if (mod == BCD_R)                                  /* read? save func */
    dp_lastf = fnc;
else return STOP_INVM;                                  /* other? error */

switch (fnc) {                                          /* case on function */

    case FNC_RSCO:                                      /* read sec cnt ov */
        BS = dcf + DCF_CNT;                             /* set count back */
                                                        /* fall thru */
    case FNC_READ:                                      /* read */
        psec = dp_fndsec (uptr, sec, dcf);              /* find sector */
        if (psec < 0)                                   /* addr cmp error? */
            CRETIOE (iochk, STOP_INVDAD);
        for (;;) {                                      /* loop */
            qzr = (--cnt == 0);                         /* set zero latch */
            dp_cvt_bin (dcf + DCF_CNT, DCF_CNT_LEN, cnt, MD_WM); /* redo count */
            if ((r = dp_rdsec (uptr, psec, flg, qwc)))  /* read sector */
                break;
            cnt = dp_get_cnt (dcf);                     /* get new count */
            if (cnt < 0)                                /* bad count? */
                return STOP_INVDCN;
            if (qzr)                                    /* zero latch? done */
                break;
            sec++; psec++;                              /* next sector */
            dp_cvt_bin (dcf + DCF_SEC, DCF_SEC_LEN, sec, flg); /* rewr sec */
            if ((r = dp_nexsec (uptr, psec, dcf)))      /* find next */
                break;
            }
        break;                                          /* done, clean up */

    case FNC_RTRK:                                      /* read track */
        AS = dcf + 9;                                   /* special AS */
        psec = dp_trkop (drv, sec);                     /* start of track */
        for (;;) {                                      /* loop */
            qzr = (--cnt == 0);                         /* set zero latch */
            dp_cvt_bin (dcf + DCF_CNT, DCF_CNT_LEN, cnt, MD_WM); /* redo count */
            if ((r = dp_rdadr (uptr, psec, flg, qwc)))  /* read addr */
                break;                                  /* error? */
            if ((r = dp_rdsec (uptr, psec, flg, qwc)))  /* read data */
                break;                                  /* error? */
            cnt = dp_get_cnt (dcf);                     /* get new count */
            if (cnt < 0)                                /* bad count? */
                return STOP_INVDCN;
            if (qzr)                                    /* zero latch? done */
                break;
            psec = dp_trkop (drv, sec) + ((psec + 1) % DP_NUMSC);
            }
        break;                                          /* done, clean up */        

    case FNC_WRSCO:                                     /* write sec cnt ov */
        BS = dcf + DCF_CNT;                             /* set count back */
                                                        /* fall through */
    case FNC_WRITE:                                     /* read */
        psec = dp_fndsec (uptr, sec, dcf);              /* find sector */
        if (psec < 0)                                   /* addr cmp error? */
            CRETIOE (iochk, STOP_INVDAD);
        for (;;) {                                      /* loop */
            qzr = (--cnt == 0);                         /* set zero latch */
            dp_cvt_bin (dcf + DCF_CNT, DCF_CNT_LEN, cnt, MD_WM); /* rewr cnt */
            if ((r = dp_wrsec (uptr, psec, flg)))       /* write data */
                break;
            if (qzr)                                    /* zero latch? done */
                break;
            sec++; psec++;                              /* next sector */
            dp_cvt_bin (dcf + DCF_SEC, DCF_SEC_LEN, sec, flg); /* rewr sec */
            if ((r = dp_nexsec (uptr, psec, dcf)))      /* find next */
                break;
            }
        break;                                          /* done, clean up */

    case FNC_WRTRK:                                     /* write track */
        if ((uptr->flags & UNIT_WAE) == 0)              /* enabled? */
            return STOP_WRADIS;
        AS = dcf + 9;                                   /* special AS */
        psec = dp_trkop (drv, sec);                     /* start of track */
        for (;;) {                                      /* loop */
            qzr = (--cnt == 0);                         /* set zero latch */
            dp_cvt_bin (dcf + DCF_CNT, DCF_CNT_LEN, cnt, MD_WM); /* redo count */
            if ((r = dp_wradr (uptr, psec, flg)))       /* write addr */
                break;
            if ((r = dp_wrsec (uptr, psec, flg)))       /* write data */
                break;
            if (qzr)                                    /* zero latch? done */
                break;
            psec = dp_trkop (drv, sec) + ((psec + 1) % DP_NUMSC);
            }
        break;                                          /* done, clean up */

    default:                                            /* unknown */
        return STOP_INVDFN;
        }

if (r == SCPE_OK) {                                     /* normal so far? */
    BS++;                                               /* advance BS */
    if (ADDR_ERR (BS))                                  /* address error? */
        return STOP_WRAP;
    if (M[BS - 1] != (WM + BCD_GRPMRK)) {               /* GM + WM at end? */
        ind[IN_LNG] = ind[IN_DSK] = 1;                  /* no, error */
        r = STOP_INVDLN;
        }
    }
CRETIOE (iochk || !ind[IN_DSK], r);                     /* return status */
}

/* Read or compare address with memory */

t_stat dp_rdadr (UNIT *uptr, int32 sec, int32 flg, int32 qwc)
{
int32 i;
uint8 ac;
int32 da = (sec % DP_TOTSC) * DP_NUMCH;                 /* char number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da;             /* buf ptr */
t_bool zad = dp_zeroad (ap);                            /* zero address */
static const int32 dec_tab[DP_ADDR] = {                 /* powers of 10 */
    100000, 10000, 1000, 100, 10, 1
    } ;

for (i = 0; i < DP_ADDR; i++) {                         /* copy address */
    if (M[BS] == (WM | BCD_GRPMRK)) {                   /* premature GWM? */
        ind[IN_LNG] = ind[IN_DSK] = 1;                  /* error */
        return STOP_INVDLN;
        }
    if (zad) {                                          /* addr zero? */
        ac = sec / dec_tab[i];                          /* get addr digit */
        sec = sec % dec_tab[i];                         /* get remainder */
        ac = bcd_to_bin[ac];                            /* cvt to BCD */
        }
    else ac = *ap;                                      /* addr char */
    if (qwc) {                                          /* wr chk? skip if zad */
        if (!zad && (flg? (M[BS] != ac):                /* L? cmp with WM */
            ((M[BS] & CHAR) != (ac & CHAR)))) {         /* M? cmp w/o WM */
            ind[IN_DPW] = ind[IN_DSK] = 1;
            return STOP_WRCHKE;
            }
        }
    else if (flg)                                       /* load mode */
        M[BS] = ac & CHAR;
    else M[BS] = (M[BS] & WM) | (ac & CHAR);            /* move mode */
    ap++; BS++;                                         /* adv ptrs */
    if (ADDR_ERR (BS))
        return STOP_WRAP;
    }
return SCPE_OK;
}

/* Read or compare data with memory */

t_stat dp_rdsec (UNIT *uptr, int32 sec, int32 flg, int32 qwc)
{
int32 i, lim;
int32 da = (sec % DP_TOTSC) * DP_NUMCH;                 /* char number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da + DP_ADDR;   /* buf ptr */

lim = flg? (DP_DATA - 10): DP_DATA;                     /* load vs move */
for (i = 0; i < lim; i++) {                             /* copy data */
    if (M[BS] == (WM | BCD_GRPMRK)) {                   /* premature GWM? */
        ind[IN_LNG] = ind[IN_DSK] = 1;                  /* error */
        return STOP_INVDLN;
        }
    if (qwc) {                                          /* write check? */
        if (flg? (M[BS] != *ap):                        /* load mode cmp */
            ((M[BS] & CHAR) != (*ap & CHAR))) {         /* move mode cmp */
            ind[IN_DPW] = ind[IN_DSK] = 1;              /* error */
            return STOP_WRCHKE;
            }
        }
    else if (flg)                                       /* load mode */
        M[BS] = *ap & (WM | CHAR);
    else M[BS] = (M[BS] & WM) | (*ap & CHAR);           /* word mode */
    ap++;                                               /* adv ptrs */
    BS++;
    if (ADDR_ERR (BS))
        return STOP_WRAP;
    }
return SCPE_OK;
}

/* Write address to disk */

t_stat dp_wradr (UNIT *uptr, int32 sec, int32 flg)
{
int32 i;
uint32 da = (sec % DP_TOTSC) * DP_NUMCH;                /* char number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da;             /* buf ptr */

for (i = 0; i < DP_ADDR; i++) {                         /* copy address */
    if (M[BS] == (WM | BCD_GRPMRK)) {                   /* premature GWM? */
        dp_fill (uptr, da, DP_NUMCH - i);               /* fill, set err */ 
        ind[IN_LNG] = ind[IN_DSK] = 1;                  /* error */
        return STOP_INVDLN;
        }
    if (flg)                                            /* L? copy WM */
        *ap = M[BS] & (WM | CHAR);
    else *ap = M[BS] & CHAR;                            /* M? strip WM */
    if (da >= uptr->hwmark)
        uptr->hwmark = da + 1;
    da++;                                               /* adv ptrs */
    ap++;
    BS++;
    if (ADDR_ERR (BS))
        return STOP_WRAP;
    }
return SCPE_OK;
}

/* Write data to disk */

t_stat dp_wrsec (UNIT *uptr, int32 sec, int32 flg)
{
int32 i, lim;
uint32 da = ((sec % DP_TOTSC) * DP_NUMCH) + DP_ADDR;    /* char number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da;             /* buf ptr */

lim = flg? (DP_DATA - 10): DP_DATA;                     /* load vs move */
for (i = 0; i < lim; i++) {                             /* copy data */
    if (M[BS] == (WM | BCD_GRPMRK)) {                   /* premature GWM? */
        dp_fill (uptr, da, DP_DATA - i);                /* fill, set err */
        ind[IN_LNG] = ind[IN_DSK] = 1;                  /* error */
        return STOP_INVDLN;
        }
    if (flg)                                            /* load, copy WM */
        *ap = M[BS] & (WM | CHAR);
    else *ap = M[BS] & CHAR;                            /* move, strip WM */
    if (da >= uptr->hwmark)
        uptr->hwmark = da + 1;
    da++;                                               /* adv ptrs */
    ap++;
    BS++;
    if (ADDR_ERR (BS))
        return STOP_WRAP;
    }
return SCPE_OK;
}

/* Find sector */

int32 dp_fndsec (UNIT *uptr, int32 sec, int32 dcf)
{
int32 ctrk = sec % (DP_NUMSF * DP_NUMSC);               /* curr trk-sec */
int32 psec = ((uptr->CYL) * (DP_NUMSF * DP_NUMSC)) + ctrk;
int32 da = psec * DP_NUMCH;                             /* char number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da;             /* buf ptr */
int32 i;

if (dp_zeroad (ap))                                     /* addr zero? ok */
    return psec;
if (dp_cmp_ad (ap, dcf))                                /* addr comp? ok */
    return psec;
psec = psec - (psec % DP_NUMSC);                        /* sector 0 */
for (i = 0; i < DP_NUMSC; i++, psec++) {                /* check track */
    da = psec * DP_NUMCH;                               /* char number */
    ap = ((uint8 *) uptr->filebuf) + da;                /* word pointer */
    if (dp_zeroad (ap))                                 /* no implicit match */
        continue;
    if (dp_cmp_ad (ap, dcf))                            /* match? */
        return psec;
    }
ind[IN_UNA] = ind[IN_DSK] = 1;                          /* no match */
return -1;
}

/* Find next sector - must be sequential, cannot cross cylinder boundary */

t_stat dp_nexsec (UNIT *uptr, int32 psec, int32 dcf)
{
int32 ctrk = psec % (DP_NUMSF * DP_NUMSC);              /* curr trk-sec */
int32 da = psec * DP_NUMCH;                             /* word number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da;             /* buf ptr */

if (ctrk) {                                             /* not trk zero? */
    if (dp_zeroad (ap))                                 /* addr zero? ok */
        return SCPE_OK;
    if (dp_cmp_ad (ap, dcf))                            /* addr comp? ok */
        return SCPE_OK;
    }
ind[IN_UNA] = ind[IN_DSK] = 1;                          /* no, error */
return STOP_INVDAD;
}

/* Test for zero address */

t_bool dp_zeroad (uint8 *ap)
{
int32 i;

for (i = 0; i < DP_ADDR; i++, ap++) {                   /* loop thru addr */
    if (*ap & CHAR)                                     /* nonzero? lose */
        return FALSE;
    }
return TRUE;                                            /* all zeroes */
}

/* Compare disk address to memory sector address - always omit word marks */

t_bool dp_cmp_ad (uint8 *ap, int32 dcf)
{
int32 i;
uint8 c;

for (i = 0; i < DP_ADDR; i++, ap++) {                   /* loop thru addr */
    c = M[dcf + DCF_SEC + i];                           /* sector addr char */
    if ((c & CHAR) != (*ap & CHAR))                     /* cmp w/o WM */
        return FALSE;
    }
return TRUE;                                            /* compare ok */
}

/* Track operation setup */

int32 dp_trkop (int32 drv, int32 sec)
{
int32 ctrk = (sec / DP_NUMSC) % DP_NUMSF;

return ((drv * DP_TOTSC) + (dp_unit[drv].CYL * DP_NUMSF * DP_NUMSC) +
    (ctrk * DP_NUMSC));
}

/* Convert DCF BCD field to binary */

int32 dp_cvt_bcd (int32 ad, int32 len)
{
uint8 c;
int32 r;

for (r = 0; len > 0; len--) {                           /* loop thru char */
    c = M[ad] & DIGIT;                                  /* get digit */
    if ((c == 0) || (c > BCD_ZERO))                     /* invalid? */
        return -1;
    r = (r * 10) + bcd_to_bin[c];                       /* cvt to bin */
    ad++;                                               /* next digit */
    }
return r;
}

/* Convert binary to DCF BCD field */

void dp_cvt_bin (int32 ad, int32 len, int32 val, int32 flg)
{
int32 r;

for ( ; len > 0; len--) {                               /* loop thru char */
    r = val % 10;                                       /* get digit */
    if (flg)                                            /* load mode? */
        M[ad + len - 1] = bin_to_bcd[r];
    else M[ad + len - 1] = (M[ad + len - 1] & WM) | bin_to_bcd[r];
    val = val / 10;
    }
return;
}       

/* Get and validate count */

int32 dp_get_cnt (int32 dcf)
{
int32 cnt = dp_cvt_bcd (dcf + DCF_CNT, DCF_CNT_LEN);    /* get new count */
if (cnt < 0)                                            /* bad count? */
    return -1;
if (cnt == 0)                                           /* 0 => 1000 */
    return 1000;
return cnt;
}

/* Fill sector buffer with blanks */

void dp_fill (UNIT *uptr, uint32 da, int32 cnt)
{
while (cnt-- > 0) {                                     /* fill with blanks */
    *(((uint8 *) uptr->filebuf) + da) = BCD_BLANK;
    if (da >= uptr->hwmark)
        uptr->hwmark = da + 1;
    da++;
    }
return;
}

/* Reset routine */

t_stat dp_reset (DEVICE *dptr)
{
int32 i;

for (i = 0; i < DP_NUMDR; i++)                          /* reset cylinder */
    dp_unit[i].CYL = 0;
dp_lastf = 0;                                           /* clear state */
ind[IN_DPW] = ind[IN_LNG] = ind[IN_UNA] = 0;            /* clr indicators */
ind[IN_DSK] = ind[IN_ACC] = ind[IN_DBY] = 0;
sim_cancel (&dp_unit[0]);                               /* cancel timer */
return SCPE_OK;
}
