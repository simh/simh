/* i1620_dp.c: IBM 1311 disk simulator

   Copyright (c) 2002-2015, Robert M. Supnik

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

   The 1311 disk pack has 100 cylinders, 10 tracks/cylinder, 20 sectors/track.
   Each sector contains 105 characters of information:

   5c           sector address
   100c         sector data

   By default, a sector's address field will be '00000', which is interpreted
   to mean the implied sector number that would be in place if the disk pack
   had been formatted with sequential sector numbers.

   18-Oct-02    RMS     Fixed bug in error testing (Hans Pufal)
*/

#include "i1620_defs.h"

#define DP_NUMDR        4                               /* #drives */
#define UNIT_V_WAE      (UNIT_V_UF + 0)                 /* write addr enab */
#define UNIT_WAE        (1 << UNIT_V_WAE)

/* Disk format */

#define DP_ADDR         5                               /* address */
#define DP_DATA         100                             /* data */
#define DP_NUMCH        (DP_ADDR + DP_DATA)

#define DP_NUMSC        20                              /* #sectors */
#define DP_NUMSF        10                              /* #surfaces */
#define DP_NUMCY        100                             /* #cylinders */
#define DP_TOTSC        (DP_NUMCY * DP_NUMSF * DP_NUMSC)
#define DP_SIZE         (DP_TOTSC * DP_NUMCH)

/* Disk control field */

#define DCF_DRV         0                               /* drive select */
#define DCF_SEC         1                               /* sector addr */
#define DCF_SEC_LEN     5
#define DCF_CNT         (DCF_SEC + DCF_SEC_LEN)         /* sector count */
#define DCF_CNT_LEN     3
#define DCF_ADR         (DCF_CNT + DCF_CNT_LEN)         /* buffer address */
#define DCF_ADR_LEN     5
#define DCF_LEN         (DCF_ADR + DCF_ADR_LEN)

/* Functions */

#define FNC_SEEK        1                               /* seek */
#define FNC_SEC         0                               /* sectors */
#define FNC_WCH         1                               /* write check */
#define FNC_NRL         2                               /* no rec lnt chk */
#define FNC_TRK         4                               /* tracks */
#define FNC_WRI         8                               /* write offset */

#define CYL             u3                              /* current cylinder */

extern uint8 M[MAXMEMSIZE];                             /* memory */
extern uint8 ind[NUM_IND];
extern UNIT cpu_unit;

int32 dp_stop = 1;                                      /* disk err stop */
uint32 dp_ba = 0;                                       /* buffer addr */

t_stat dp_reset (DEVICE *dptr);
t_stat dp_rdadr (UNIT *uptr, int32 sec, int32 qnr, int32 qwc);
t_stat dp_rdsec (UNIT *uptr, int32 sec, int32 qnr, int32 qwc);
t_stat dp_wradr (UNIT *uptr, int32 sec, int32 qnr);
t_stat dp_wrsec (UNIT *uptr, int32 sec, int32 qnr);
int32 dp_fndsec (UNIT *uptr, int32 sec, t_bool rd);
t_stat dp_nexsec (UNIT *uptr, int32 sec, int32 psec, t_bool rd);
t_bool dp_zeroad (uint8 *ap);
int32 dp_cvt_ad (uint8 *ap);
int32 dp_trkop (int32 drv, int32 sec);
int32 dp_cvt_bcd (uint32 ad, int32 len);
void dp_fill (UNIT *uptr, uint32 da, int32 cnt);
t_stat dp_tstgm (uint32 c, int32 qnr);

/* DP data structures

   dp_dev       DP device descriptor
   dp_unit      DP unit list
   dp_reg       DP register list
   dp_mod       DP modifier list
*/

UNIT dp_unit[] = {
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
    { FLDATA (ADCHK, ind[IN_DACH], 0) },
    { FLDATA (WLRC, ind[IN_DWLR], 0) },
    { FLDATA (CYLO, ind[IN_DCYO], 0) },
    { FLDATA (ERR, ind[IN_DERR], 0) },
    { FLDATA (DPSTOP, dp_stop, 0) },
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
    DP_NUMDR, 10, 21, 1, 16, 5,
    NULL, NULL, &dp_reset,
    NULL, NULL, NULL
    };

/* Disk IO routine */

t_stat dp (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
int32 drv, sa, sec, psec, cnt, qwc, qnr, t;
UNIT *uptr;
t_stat r;

if (pa & 1)                                             /* dcf must be even */
    return STOP_INVDCF;
ind[IN_DACH] = ind[IN_DWLR] = 0;                        /* clr indicators */
ind[IN_DERR] = ind[IN_DCYO] = 0;
sa = ADDR_A (pa, DCF_SEC);                              /* ptr to sector */
if (((dp_unit[0].flags & UNIT_DIS) == 0) &&             /* only drive 0? */
     (dp_unit[1].flags & UNIT_DIS) &&
     (dp_unit[2].flags & UNIT_DIS) &&
     (dp_unit[3].flags & UNIT_DIS)) drv = 0;            /* ignore drv select */
else drv = (((M[pa] & 1)? M[pa]: M[sa]) & 0xE) >> 1;    /* drive # */
if (drv >= DP_NUMDR)                                    /* invalid? */
    return STOP_INVDRV;
uptr = dp_dev.units + drv;                              /* get unit ptr */
if ((uptr->flags & UNIT_ATT) == 0) {                    /* attached? */
    ind[IN_DERR] = 1;                                   /* no, error */
    CRETIOE (dp_stop, SCPE_UNATT);
    }

sec = dp_cvt_bcd (sa, DCF_SEC_LEN);                     /* cvt sector */
if ((sec < 0) || (sec >= (DP_NUMDR * DP_TOTSC)))        /* bad sector? */
    return STOP_INVDSC;
if (op == OP_K) {                                       /* seek? */
    if (f1 != FNC_SEEK)                                 /* really? */
        return STOP_INVFNC;
    uptr->CYL = (sec / (DP_NUMSF * DP_NUMSC)) %         /* set cyl # */
        DP_NUMCY;
    return SCPE_OK;                                     /* done! */
    }

cnt = dp_cvt_bcd (ADDR_A (pa, DCF_CNT), DCF_CNT_LEN);   /* get count */
t = dp_cvt_bcd (ADDR_A (pa, DCF_ADR), DCF_ADR_LEN);     /* get address */
if ((t < 0) || (t & 1))                                 /* bad address? */
    return STOP_INVDBA;
dp_ba = t;                                              /* save addr */

if (f1 >= FNC_WRI)                                      /* invalid func? */
    return STOP_INVFNC;
if (op == OP_RN)                                        /* read? set wch */
    qwc = f1 & FNC_WCH;
else if (op == OP_WN) {                                 /* write? */
    if (op & FNC_WCH)                                   /* cant check */
        return STOP_INVFNC;
    f1 = f1 + FNC_WRI;                                  /* offset fnc */
    }
else return STOP_INVFNC;                                /* not R or W */
qnr = f1 & FNC_NRL;                                     /* no rec check? */

switch (f1 & ~(FNC_WCH | FNC_NRL)) {                    /* case on function */

    case FNC_SEC:                                       /* read sectors */
        if (cnt <= 0)                                   /* bad count? */
            return STOP_INVDCN;
        psec = dp_fndsec (uptr, sec, TRUE);             /* find sector */
        if (psec < 0)                                   /* error? */
            CRETIOE (dp_stop, STOP_DACERR);
        do {                                            /* loop on count */
            if ((r = dp_rdsec (uptr, psec, qnr, qwc)))  /* read sector */
                break;
            sec++; psec++;                              /* next sector */
            } while ((--cnt > 0) &&
              ((r = dp_nexsec (uptr, sec, psec, TRUE)) == SCPE_OK));
        break;                                          /* done, clean up */

    case FNC_TRK:                                       /* read track */
        psec = dp_trkop (drv, sec);                     /* start of track */
        for (cnt = 0; cnt < DP_NUMSC; cnt++) {          /* full track */
            if ((r = dp_rdadr (uptr, psec, qnr, qwc)))  /* read addr */
                break;                                  /* error? */
            if ((r = dp_rdsec (uptr, psec, qnr, qwc)))  /* read data */
                break;                                  /* error? */
            psec = dp_trkop (drv, sec) + ((psec + 1) % DP_NUMSC);
            }
        break;                                          /* done, clean up */        

    case FNC_SEC + FNC_WRI:                             /* write */
        if (cnt <= 0)                                   /* bad count? */
            return STOP_INVDCN;
        psec = dp_fndsec (uptr, sec, FALSE);            /* find sector */
        if (psec < 0)                                   /* error? */
            CRETIOE (dp_stop, STOP_DACERR);
        do {                                            /* loop on count */
            if ((r = dp_tstgm (M[dp_ba], qnr)))         /* start with gm? */
                break;
            if ((r = dp_wrsec (uptr, psec, qnr)))       /* write data */
                break;
            sec++; psec++;                              /* next sector */
            } while ((--cnt > 0) &&
              ((r = dp_nexsec (uptr, sec, psec, FALSE)) == SCPE_OK));
        break;                                          /* done, clean up */

    case FNC_TRK + FNC_WRI:                             /* write track */
        if ((uptr->flags & UNIT_WAE) == 0)              /* enabled? */
                return STOP_WRADIS;
        psec = dp_trkop (drv, sec);                     /* start of track */
        for (cnt = 0; cnt < DP_NUMSC; cnt++) {          /* full track */
            if ((r = dp_tstgm (M[dp_ba], qnr)))         /* start with gm? */
                break;
            if ((r = dp_wradr (uptr, psec, qnr)))       /* write addr */
                break;
            if ((r = dp_wrsec (uptr, psec, qnr)))       /* write data */
                break;
            psec = dp_trkop (drv, sec) + ((psec + 1) % DP_NUMSC);
            }
        break;                                          /* done, clean up */

    default:                                            /* unknown */
        return STOP_INVFNC;
        }

if ((r == SCPE_OK) && !qnr) {                           /* eor check? */
    if ((M[dp_ba] & DIGIT) != GRP_MARK) {               /* GM at end? */
        ind[IN_DWLR] = ind[IN_DERR] = 1;                /* no, error */
        r = STOP_WRLERR;
        }
    }
if ((r != SCPE_OK) &&                                   /* error? */
    (dp_stop || !ind[IN_DERR]))                         /* iochk or stop? */
    return r;
return SCPE_OK;                                         /* continue */
}

/* Read or compare address with memory */

t_stat dp_rdadr (UNIT *uptr, int32 sec, int32 qnr, int32 qwc)
{
int32 i;
uint8 ad;
int32 da = (sec % DP_TOTSC) * DP_NUMCH;                 /* char number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da;             /* buf ptr */
t_bool zad = dp_zeroad (ap);                            /* zero address */
static const int32 dec_tab[DP_ADDR] = {                 /* powers of 10 */
    10000, 1000, 100, 10, 1
    } ;

for (i = 0; i < DP_ADDR; i++) {                         /* copy/check addr */
    if (zad) {                                          /* addr zero? */
        ad = sec / dec_tab[i];                          /* get addr digit */
        sec = sec % dec_tab[i];                         /* get remainder */
        }
    else ad = *ap;                                      /* addr digit */
    if (qwc) {                                          /* write check? */
        if (dp_tstgm (M[dp_ba], qnr))                   /* grp mrk in mem? */
            return STOP_WRLERR;                         /* yes, error */
        if (!zad && (M[dp_ba] != ad)) {                 /* digits equal? */
            ind[IN_DACH] = ind[IN_DERR] = 1;            /* no, error */
            return STOP_DWCERR;
            }
        }
    else M[dp_ba] = ad & (FLAG | DIGIT);                /* store digit */
    if (dp_tstgm (*ap, qnr))                            /* grp mrk on disk? */
        return STOP_WRLERR;
    ap++; PP (dp_ba);                                   /* adv ptrs */
    }
return SCPE_OK;
}

/* Read or compare data with memory */

t_stat dp_rdsec (UNIT *uptr, int32 sec, int32 qnr, int32 qwc)
{
int32 i;
int32 da = (sec % DP_TOTSC) * DP_NUMCH;                 /* char number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da + DP_ADDR;   /* buf ptr */

for (i = 0; i < DP_DATA; i++) {                         /* copy data */
    if (qwc) {                                          /* write check? */
        if (dp_tstgm (M[dp_ba], qnr))                   /* grp mrk in mem? */
            return STOP_WRLERR;                         /* yes, error */
        if (M[dp_ba] != *ap) {                          /* dig+flags equal? */
            ind[IN_DACH] = ind[IN_DERR] = 1;            /* no, error */
            return STOP_DWCERR;
            }
        }
    else M[dp_ba] = *ap & (FLAG | DIGIT);               /* flag + digit */
    if (dp_tstgm (*ap, qnr))                            /* grp mrk on disk? */
        return STOP_WRLERR;
    ap++; PP (dp_ba);                                   /* adv ptrs */
    }
return SCPE_OK;
}

/* Write address to disk */

t_stat dp_wradr (UNIT *uptr, int32 sec, int32 qnr)
{
int32 i;
uint32 da = (sec % DP_TOTSC) * DP_NUMCH;                /* char number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da;             /* buf ptr */

for (i = 0; i < DP_ADDR; i++) {                         /* copy address */
    *ap = M[dp_ba] & (FLAG | DIGIT);                    /* flag + digit */
    if (da >= uptr->hwmark)
        uptr->hwmark = da + 1;
    if (dp_tstgm (*ap, qnr)) {                          /* grp mrk fm mem? */
        dp_fill (uptr, da + 1, DP_NUMCH - i - 1);       /* fill addr+data */
        return STOP_WRLERR;                             /* error */
        }
    da++; ap++; PP (dp_ba);                             /* adv ptrs */
    }
return SCPE_OK;
}

/* Write data to disk */

t_stat dp_wrsec (UNIT *uptr, int32 sec, int32 qnr)
{
int32 i;
uint32 da = ((sec % DP_TOTSC) * DP_NUMCH) + DP_ADDR;    /* char number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da;             /* buf ptr */

for (i = 0; i < DP_DATA; i++) {                         /* copy data */
    *ap = M[dp_ba] & (FLAG | DIGIT);                    /* get character */
    if (da >= uptr->hwmark)
        uptr->hwmark = da + 1;
    if (dp_tstgm (*ap, qnr)) {                          /* grp mrk fm mem? */
        dp_fill (uptr, da + 1, DP_DATA - i - 1);        /* fill data */ 
        return STOP_WRLERR;                             /* error */
        }
    da++; ap++; PP (dp_ba);                             /* adv ptrs */
    }
return SCPE_OK;
}

/* Find sector */

int32 dp_fndsec (UNIT *uptr, int32 sec, t_bool rd)
{
int32 ctrk = sec % (DP_NUMSF * DP_NUMSC);               /* curr trk-sec */
int32 psec = ((uptr->CYL) * (DP_NUMSF * DP_NUMSC)) + ctrk;
int32 da = psec * DP_NUMCH;                             /* char number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da;             /* buf ptr */
int32 dskad, i;

if (dp_zeroad (ap))                                     /* addr zero? ok */
    return psec;
dskad = dp_cvt_ad (ap);                                 /* cvt addr */
if (dskad == sec) {                                     /* match? */
    if (rd || ((*ap & FLAG) == 0))                      /* read or !wprot? */
        return psec;
    ind[IN_DACH] = ind[IN_DERR] = 1;                    /* no match */
    return -1;
    }           
psec = psec - (psec % DP_NUMSC);                        /* sector 0 */
for (i = 0; i < DP_NUMSC; i++, psec++) {                /* check track */
    da = psec * DP_NUMCH;                               /* char number */
    ap = ((uint8 *) uptr->filebuf) + da;                /* word pointer */
    if (dp_zeroad (ap))                                 /* no implicit match */
        continue;
    dskad = dp_cvt_ad (ap);                             /* cvt addr */
    if (dskad == sec) {                                 /* match? */
        if (rd || ((*ap & FLAG) == 0))                  /* read or !wprot? */
            return psec;
        ind[IN_DACH] = ind[IN_DERR] = 1;                /* no match */
        return -1;
        }
    }           
ind[IN_DACH] = ind[IN_DERR] = 1;                        /* no match */
return -1;
}

/* Find next sector - must be sequential, cannot cross cylinder boundary */

t_stat dp_nexsec (UNIT *uptr, int32 sec, int32 psec, t_bool rd)
{
int32 ctrk = psec % (DP_NUMSF * DP_NUMSC);              /* curr trk-sec */
int32 da = psec * DP_NUMCH;                             /* word number */
uint8 *ap = ((uint8 *) uptr->filebuf) + da;             /* buf ptr */
int32 dskad;

if (ctrk) {                                             /* not trk zero? */
    if (dp_zeroad (ap))                                 /* addr zero? ok */
        return SCPE_OK;
    dskad = dp_cvt_ad (ap);                             /* cvt addr */
    if ((dskad == sec) &&                               /* match? */
       (rd || ((*ap & FLAG) == 0)))                     /* read or !wprot? */
       return SCPE_OK;
    ind[IN_DACH] = ind[IN_DERR] = 1;                    /* no, error */
    return STOP_DACERR;
    }
ind[IN_DCYO] = ind[IN_DERR] = 1;                        /* cyl overflow */
return STOP_CYOERR;
}

/* Test for zero address */

t_bool dp_zeroad (uint8 *ap)
{
int32 i;

for (i = 0; i < DP_ADDR; i++, ap++) {                   /* loop thru addr */
    if (*ap & DIGIT)                                    /* nonzero? lose */
        return FALSE;
    }
return TRUE;                                            /* all zeroes */
}

/* Test for group mark when enabled */

t_stat dp_tstgm (uint32 c, int32 qnr)
{
if (!qnr && ((c & DIGIT) == GRP_MARK)) {                /* premature GM? */
    ind[IN_DWLR] = ind[IN_DERR] = 1;                    /* error */
    return STOP_WRLERR;
    }
return SCPE_OK;
}

/* Convert disk address to binary - invalid char force bad address */

int32 dp_cvt_ad (uint8 *ap)
{
int32 i, r;
uint8 c;

for (i = r = 0; i < DP_ADDR; i++, ap++) {               /* loop thru addr */
    c = *ap & DIGIT;                                    /* get digit */
    if (BAD_DIGIT (c))                                  /* bad digit? */
        return -1;
    r = (r * 10) + c;                                   /* bcd to binary */
    }
return r;
}

/* Track operation setup */

int32 dp_trkop (int32 drv, int32 sec)
{
int32 ctrk = (sec / DP_NUMSC) % DP_NUMSF;

return ((drv * DP_TOTSC) + (dp_unit[drv].CYL * DP_NUMSF * DP_NUMSC) +
    (ctrk * DP_NUMSC));
}

/* Convert DCF BCD field to binary */

int32 dp_cvt_bcd (uint32 ad, int32 len)
{
uint8 c;
int32 r;

for (r = 0; len > 0; len--) {                           /* loop thru char */
    c = M[ad] & DIGIT;                                  /* get digit */
    if (BAD_DIGIT (c))                                  /* invalid? */
        return -1;
    r = (r * 10) + c;                                   /* cvt to bin */
    PP (ad);                                            /* next digit */
    }
return r;
}

/* Fill sector buffer with zero */

void dp_fill (UNIT *uptr, uint32 da, int32 cnt)
{
while (cnt-- > 0) {                                     /* fill with zeroes*/
    *(((uint8 *) uptr->filebuf) + da) = 0;
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
ind[IN_DACH] = ind[IN_DWLR] = 0;                        /* clr indicators */
ind[IN_DERR] = ind[IN_DCYO] = 0;
return SCPE_OK;
}
