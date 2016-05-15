/* pdp18b_mt.c: 18b PDP magnetic tape simulator

   Copyright (c) 1993-2016, Robert M Supnik

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

   mt           (PDP-9) TC59 magtape
                (PDP-15) TC59D magtape

   10-Mar-16    RMS     Added 3-cycle databreak set/show entries
   07-Mar-16    RMS     Revised for dynamically allocated memory
   13-Sep-15    RMS     Added APIVEC register
   14-Nov-08    RMS     Replaced mt_log with standard debug facility
   16-Feb-06    RMS     Added tape capacity checking
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   18-Mar-05    RMS     Added attached test to detach routine
   14-Jan-04    RMS     Revised IO device call interface
   25-Apr-03    RMS     Revised for extended file support
   28-Mar-03    RMS     Added multiformat support
   04-Mar-03    RMS     Fixed bug in MTTR
   01-Mar-03    RMS     Fixed bug in interrupt handling
                        Revised for magtape library
   02-Feb-03    RMS     Revised IOT decoding
   30-Oct-02    RMS     Revised BOT handling, added error record handling
   05-Oct-02    RMS     Added DIB, device number support
                        Revamped error recovery
   28-Aug-02    RMS     Added end of medium support
   30-May-02    RMS     Widened POS to 32b
   22-Apr-02    RMS     Added maximum record length test
   06-Jan-02    RMS     Revised enabled/disable support
   29-Nov-01    RMS     Added read only unit support
   25-Nov-01    RMS     Revised interrupt structure
                        Changed UST, POS, FLG to arrays
   26-Apr-01    RMS     Added device enable/disable support
   15-Feb-01    RMS     Fixed 3-cycle data break sequence
   04-Oct-98    RMS     V2.4 magtape format
   22-Jan-97    RMS     V2.3 magtape format
   29-Jun-96    RMS     Added unit enable/disable support

   Magnetic tapes are represented as a series of variable records
   of the form:

        32b byte count
        byte 0
        byte 1
        :
        byte n-2
        byte n-1
        32 byte count

   If the byte count is odd, the record is padded with an extra byte
   of junk.  File marks are represented by a byte count of 0.
*/

#include "pdp18b_defs.h"
#include "sim_tape.h"

#define MT_NUMDR        8                               /* #drives */
#define USTAT           u3                              /* unit status */
#define MT_MAXFR        (1 << 16)                       /* max record length */
#define MT_WC           032                             /* word count */
#define MT_CA           033                             /* current addr */
#define WC_SIZE         (1 << 12)                       /* max word count */
#define WC_MASK         (WC_SIZE - 1)

/* Command/unit - mt_cu */

#define CU_V_UNIT       15                              /* unit */
#define CU_M_UNIT       07
#define CU_PARITY       0040000                         /* parity select */
#define CU_DUMP         0020000                         /* dump mode */
#define CU_ERASE        0010000                         /* ext rec gap */
#define CU_V_CMD        9                               /* command */
#define CU_M_CMD        07
#define  FN_NOP          00
#define  FN_REWIND       01
#define  FN_READ         02
#define  FN_CMPARE       03
#define  FN_WRITE        04
#define  FN_WREOF        05
#define  FN_SPACEF       06
#define  FN_SPACER       07
#define CU_IE           0000400                         /* interrupt enable */
#define CU_V_TYPE       6                               /* drive type */
#define CU_M_TYPE       03
#define  TY_9TK         3
#define GET_UNIT(x)     (((x) >> CU_V_UNIT) & CU_M_UNIT)
#define GET_CMD(x)      (((x) >> CU_V_CMD) & CU_M_CMD)
#define GET_TYPE(x)     (((x) >> CU_V_TYPE) & CU_M_TYPE)
#define PACKED(x)       (((x) & CU_DUMP) || (GET_TYPE (x) != TY_9TK))

/* Status - stored in mt_sta or (*) uptr->USTAT */

#define STA_ERR         0400000                         /* error */
#define STA_REW         0200000                         /* *rewinding */
#define STA_BOT         0100000                         /* *start of tape */
#define STA_ILL         0040000                         /* illegal cmd */
#define STA_PAR         0020000                         /* parity error */
#define STA_EOF         0010000                         /* *end of file */
#define STA_EOT         0004000                         /* *end of tape */
#define STA_CPE         0002000                         /* compare error */
#define STA_RLE         0001000                         /* rec lnt error */
#define STA_DLT         0000400                         /* data late */
#define STA_BAD         0000200                         /* bad tape */
#define STA_DON         0000100                         /* done */

#define STA_CLR         0000077                         /* always clear */
#define STA_DYN         (STA_REW | STA_BOT | STA_EOF | STA_EOT)
                                                        /* kept in USTAT */

extern int32 *M;
extern int32 int_hwre[API_HLVL+1];
extern int32 api_vec[API_HLVL][32];
extern UNIT cpu_unit;

int32 mt_cu = 0;                                        /* command/unit */
int32 mt_sta = 0;                                       /* status register */
int32 mt_time = 10;                                     /* record latency */
int32 mt_stopioe = 1;                                   /* stop on error */
uint8 *mtxb = NULL;                                     /* transfer buffer */

int32 mt (int32 dev, int32 pulse, int32 dat);
int32 mt_iors (void);
t_stat mt_svc (UNIT *uptr);
t_stat mt_reset (DEVICE *dptr);
t_stat mt_attach (UNIT *uptr, CONST char *cptr);
t_stat mt_detach (UNIT *uptr);
int32 mt_updcsta (UNIT *uptr, int32 val);
t_stat mt_map_err (UNIT *uptr, t_stat st);
UNIT *mt_busy (void);

/* MT data structures

   mt_dev       MT device descriptor
   mt_unit      MT unit list
   mt_reg       MT register list
   mt_mod       MT modifier list
*/

DIB mt_dib = { DEV_MT, 1, &mt_iors, { &mt } };

UNIT mt_unit[] = {
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) },
    { UDATA (&mt_svc, UNIT_ATTABLE+UNIT_DISABLE+UNIT_ROABLE, 0) }
    };

REG mt_reg[] = {
    { ORDATAD (STA, mt_sta, 18, "main status") },
    { ORDATAD (CMD, mt_cu, 18, "command") },
    { FLDATAD (INT, int_hwre[API_MTA], INT_V_MTA, "interrupt pending flag") },
    { FLDATAD (STOP_IOE, mt_stopioe, 0, "stop on I/O error") },
    { DRDATAD (TIME, mt_time, 24, "record delay"), PV_LEFT },
    { URDATAD (UST, mt_unit[0].USTAT, 8, 16, 0, MT_NUMDR, 0, "unit status, units 0 to 7") },
    { URDATAD (POS, mt_unit[0].pos, 10, T_ADDR_W, 0,
              MT_NUMDR, PV_LEFT | REG_RO, "position units 0 to 7") },
    { ORDATA (DEVNO, mt_dib.dev, 6), REG_HRO },
    { ORDATA (APIVEC, api_vec[API_MTA][INT_V_MTA], 6), REG_HRO },
    { NULL }
    };

MTAB mt_mod[] = {
    { MTUF_WLK, 0, "write enabled", "WRITEENABLED", NULL },
    { MTUF_WLK, MTUF_WLK, "write locked", "LOCKED", NULL }, 
    { MTAB_XTD|MTAB_VUN, 0, "FORMAT", "FORMAT",
      &sim_tape_set_fmt, &sim_tape_show_fmt, NULL },
    { MTAB_XTD|MTAB_VUN, 0, "TCAPACITY", "TCAPACITY",
      &sim_tape_set_capac, &sim_tape_show_capac, NULL },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, MT_WC, "WC", "WC", &set_3cyc_reg, &show_3cyc_reg, (void *)"WC" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, MT_CA, "CA", "CA", &set_3cyc_reg, &show_3cyc_reg, (void *)"CA" },
    { MTAB_XTD|MTAB_VDV, 0, "DEVNO", "DEVNO", &set_devno, &show_devno, NULL },
    { 0 }
    };

DEVICE mt_dev = {
    "MT", mt_unit, mt_reg, mt_mod,
    MT_NUMDR, 10, 31, 1, 8, 8,
    NULL, NULL, &mt_reset,
    NULL, &mt_attach, &mt_detach,
    &mt_dib, DEV_DISABLE | DEV_DEBUG | DEV_TAPE
    };

/* IOT routine */

int32 mt (int32 dev, int32 pulse, int32 dat)
{
int32 f, sb;
UNIT *uptr;

uptr = mt_dev.units + GET_UNIT (mt_cu);                 /* get unit */
mt_updcsta (uptr, 0);                                   /* update status */
sb = pulse & 060;                                       /* subop */
if (pulse & 01) {
    if ((sb == 000) && (uptr->flags & UNIT_ATT) &&      /* MTTR */
        !sim_is_active (uptr))
        dat = IOT_SKP | dat;
    else if ((sb == 020) && !mt_busy ())                /* MTCR */
        dat = IOT_SKP | dat;
    else if ((sb == 040) && (mt_sta & (STA_ERR | STA_DON))) /* MTSF */
        dat = IOT_SKP | dat;
    }
if ((pulse & 06) && DEBUG_PRS (mt_dev))
    fprintf (sim_deb, "[MT%d: IOT=%o, AC=%o, sta=%o]\n",
             GET_UNIT (mt_cu), 0707300 + pulse, dat, mt_sta);
if (pulse & 02) {
    if (sb == 000)                                      /* MTRC */
        dat = dat | (mt_cu & 0777700);
    else if (sb == 020) {                               /* MTAF, MTLC */
        if (!mt_busy ())                                /* if not busy, clr */
            mt_cu = mt_sta = 0;
        mt_sta = mt_sta & ~(STA_ERR | STA_DON);         /* clear flags */
        }
    else if (sb == 040) dat = dat | mt_sta;             /* MTRS */
    }
if (pulse & 04) {
    if (sb == 000) {                                    /* MTGO */
        f = GET_CMD (mt_cu);                            /* get function */
        if (mt_busy () ||
            sim_is_active (uptr) ||
            (f == FN_NOP) ||
            (((f == FN_SPACER) || (f == FN_REWIND)) && (uptr->USTAT & STA_BOT)) ||
            (((f == FN_WRITE) || (f == FN_WREOF)) && sim_tape_wrp (uptr)) ||
            ((uptr->flags & UNIT_ATT) == 0))
            mt_sta = mt_sta | STA_ILL | STA_ERR;        /* set illegal op */
        else {
            if (f == FN_REWIND)                         /* rewind? */
                uptr->USTAT = STA_REW;
            else mt_sta = uptr->USTAT = 0;              /* no, clear status */
            sim_activate (uptr, mt_time);               /* start io */
            }
        }
    if (sb == 020)                                      /* MTCM, MTLC  */
        mt_cu = (mt_cu & 0770700) | (dat & 0777700);    /* load status */
    }
mt_updcsta (mt_dev.units + GET_UNIT (mt_cu), 0);        /* update status */
return dat;
}

/* Unit service

   If rewind done, reposition to start of tape, set status
   else, do operation, set done, interrupt
*/

t_stat mt_svc (UNIT *uptr)
{
int32 c, c1, c2, c3, f, i, p, u;
int32 wc, xma;
t_mtrlnt tbc, cbc;
t_bool passed_eot;
t_stat st, r = SCPE_OK;

u = (int32) (uptr - mt_dev.units);                      /* get unit number */
f = GET_CMD (mt_cu);                                    /* get command */
wc = WC_SIZE - (M[MT_WC] & WC_MASK);                    /* word count is 12b */

if (uptr->USTAT & STA_REW) {                            /* rewind? */
    sim_tape_rewind (uptr);                             /* rewind tape */
    if (uptr->flags & UNIT_ATT)
        uptr->USTAT = STA_BOT;
    else uptr->USTAT = 0;
    if (u == GET_UNIT (mt_cu))
        mt_updcsta (uptr, STA_DON);
    if (DEBUG_PRS (mt_dev))
        fprintf (sim_deb, "[MT%d: rewind complete, sta=%o]\n", u, mt_sta);
    return SCPE_OK;
    }

if ((uptr->flags & UNIT_ATT) == 0) {                    /* if not attached */
    mt_updcsta (uptr, STA_ILL);                         /* illegal operation */
    return IORETURN (mt_stopioe, SCPE_UNATT);
    }

passed_eot = sim_tape_eot (uptr);                       /* passed EOT? */
switch (f) {                                            /* case on function */

    case FN_READ:                                       /* read */
    case FN_CMPARE:                                     /* read/compare */
        st = sim_tape_rdrecf (uptr, mtxb, &tbc, MT_MAXFR);      /* read rec */
        if (st == MTSE_RECE)                            /* rec in err? */
            mt_sta = mt_sta | STA_PAR | STA_ERR;
        else if (st != MTSE_OK) {                       /* other error? */
            mt_sta = mt_sta | STA_RLE | STA_ERR;        /* set RLE flag */
            r = mt_map_err (uptr, st);                  /* map error */
            break;
            }
        cbc = PACKED (mt_cu)? wc * 3: wc * 2;           /* expected bc */
        if (tbc != cbc)                                 /* wrong size? */
            mt_sta = mt_sta | STA_RLE | STA_ERR;
        if (tbc < cbc) {                                /* record small? */
            cbc = tbc;                                  /* use smaller */
            wc = PACKED (mt_cu)? ((tbc + 2) / 3): ((tbc + 1) / 2);
            }
        for (i = p = 0; i < wc; i++) {                  /* copy buffer */
            M[MT_WC] = (M[MT_WC] + 1) & DMASK;          /* inc WC, CA */
            M[MT_CA] = (M[MT_CA] + 1) & DMASK;
            xma = M[MT_CA] & AMASK;
            if (PACKED (mt_cu)) {                       /* packed? */
                c1 = mtxb[p++] & 077;
                c2 = mtxb[p++] & 077;
                c3 = mtxb[p++] & 077;
                c = (c1 << 12) | (c2 << 6) | c3;
                }
            else {
                c1 = mtxb[p++];
                c2 = mtxb[p++];
                c = (c1 << 8) | c2;
                }
            if ((f == FN_READ) && MEM_ADDR_OK (xma))
                M[xma] = c;
            else if ((f == FN_CMPARE) && (c != (M[xma] &
                (PACKED (mt_cu)? DMASK: 0177777)))) {
                mt_updcsta (uptr, STA_CPE);
                break;
                }
            }                                           /* end for */
        break;

    case FN_WRITE:                                      /* write */
        tbc = PACKED (mt_cu)? wc * 3: wc * 2;
        xma = M[MT_CA] & AMASK;                         /* get mem addr */
        for (i = p = 0; i < wc; i++) {                  /* copy buf to tape */
            xma = (xma + 1) & AMASK;                    /* incr mem addr */
            if (PACKED (mt_cu)) {                       /* packed? */
                mtxb[p++] = (M[xma] >> 12) & 077;
                mtxb[p++] = (M[xma] >> 6) & 077;
                mtxb[p++] = M[xma] & 077;
                }
            else {
                mtxb[p++] = (M[xma] >> 8) & 0377;
                mtxb[p++] = M[xma] & 0377;
                }
            }                                           /* end for */
        if ((st = sim_tape_wrrecf (uptr, mtxb, tbc)))   /* write rec, err? */
            r = mt_map_err (uptr, st);                  /* map error */
        else {
            M[MT_CA] = (M[MT_CA] + wc) & DMASK;         /* advance mem addr */
            M[MT_WC] = 0;                               /* clear word cnt */
            }
        mt_cu = mt_cu & ~CU_ERASE;                      /* clear erase flag */
        break;

    case FN_WREOF:
        if ((st = sim_tape_wrtmk (uptr)))               /* write tmk, err? */
            r = mt_map_err (uptr, st);                  /* map error */
        else uptr->USTAT = STA_EOF;
        mt_cu = mt_cu & ~CU_ERASE;                      /* clear erase flag */
        break;

    case FN_SPACEF:                                     /* space forward */
        do {
            M[MT_WC] = (M[MT_WC] + 1) & DMASK;          /* inc WC */
            if ((st = sim_tape_sprecf (uptr, &tbc))) {  /* space rec fwd, err? */
                r = mt_map_err (uptr, st);              /* map error */
                break;
                }
            } while ((M[MT_WC] != 0) && (passed_eot || !sim_tape_eot (uptr)));
        break;

    case FN_SPACER:                                     /* space reverse */
        do {
            M[MT_WC] = (M[MT_WC] + 1) & DMASK;          /* inc WC */
            if ((st = sim_tape_sprecr (uptr, &tbc))) {  /* space rec rev, err? */
                r = mt_map_err (uptr, st);              /* map error */
                break;
                }
            } while (M[MT_WC] != 0);
        break;
        }                                               /* end case */

if (!passed_eot && sim_tape_eot (uptr))                 /* just passed EOT? */
    uptr->USTAT = uptr->USTAT | STA_EOT;
mt_updcsta (uptr, STA_DON);                             /* set done */
if (DEBUG_PRS (mt_dev))
    fprintf (sim_deb, "MT%d: fnc=%d done, ma=%o, wc=%o, sta=%o]\n",
             u, f, M[MT_CA], M[MT_WC], mt_sta);
return r;
}

/* Update controller status */

int32 mt_updcsta (UNIT *uptr, int32 news)
{
mt_sta = (mt_sta & ~(STA_DYN | STA_CLR)) | (uptr->USTAT & STA_DYN) | news;
if ((mt_sta & (STA_ERR | STA_DON)) && (mt_cu & CU_IE))
    SET_INT (MTA);
else CLR_INT (MTA);                                     /* int request */
return mt_sta;
}

/* Test if controller busy */

UNIT *mt_busy (void)
{
int32 u;
UNIT *uptr;

for (u = 0; u < MT_NUMDR; u++) {                        /* loop thru units */
    uptr = mt_dev.units + u;
    if (sim_is_active (uptr) && ((uptr->USTAT & STA_REW) == 0))
        return uptr;
    }
return NULL;
}

/* Map tape error status */

t_stat mt_map_err (UNIT *uptr, t_stat st)
{
switch (st) {

    case MTSE_FMT:                                      /* illegal fmt */
    case MTSE_UNATT:                                    /* not attached */
        mt_sta = mt_sta | STA_ILL | STA_ERR;
    case MTSE_OK:                                       /* no error */
        return SCPE_IERR;

    case MTSE_TMK:                                      /* end of file */
        uptr->USTAT = uptr->USTAT | STA_EOF;            /* set EOF */
        mt_sta = mt_sta | STA_ERR;
        break;

    case MTSE_IOERR:                                    /* IO error */
        mt_sta = mt_sta | STA_PAR | STA_ERR;            /* set par err */
        if (mt_stopioe)
            return SCPE_IOERR;
        break;

    case MTSE_INVRL:                                    /* invalid rec lnt */
        mt_sta = mt_sta | STA_PAR | STA_ERR;            /* set par err */
        return SCPE_MTRLNT;

    case MTSE_RECE:                                     /* record in error */
        mt_sta = mt_sta | STA_PAR | STA_ERR;            /* set par err */
        break;

    case MTSE_EOM:                                      /* end of medium */
        mt_sta = mt_sta | STA_BAD | STA_ERR;            /* set end tape */
        break;

    case MTSE_BOT:                                      /* reverse into BOT */
        uptr->USTAT = uptr->USTAT | STA_BOT;            /* set status */
        mt_sta = mt_sta | STA_ERR;
        break;

    case MTSE_WRP:                                      /* write protect */
        mt_sta = mt_sta | STA_ILL | STA_ERR;            /* illegal operation */
        break;
        }

return SCPE_OK;
}

/* Reset routine */

t_stat mt_reset (DEVICE *dptr)
{
int32 u;
UNIT *uptr;

mt_cu = mt_sta = 0;
for (u = 0; u < MT_NUMDR; u++) {                        /* loop thru units */
    uptr = mt_dev.units + u;
    sim_tape_reset (uptr);                              /* reset tape */
    sim_cancel (uptr);                                  /* cancel activity */
    if (uptr->flags & UNIT_ATT)
        uptr->USTAT = STA_BOT;
    else uptr->USTAT = 0;
    }
mt_updcsta (&mt_unit[0], 0);                            /* update status */
if (mtxb == NULL)
    mtxb = (uint8 *) calloc (MT_MAXFR, sizeof (uint8));
if (mtxb == NULL)
    return SCPE_MEM;
return SCPE_OK;
}

/* IORS routine */

int32 mt_iors (void)
{
return (mt_sta & (STA_ERR | STA_DON))? IOS_MTA: 0;
}

/* Attach routine */

t_stat mt_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = sim_tape_attach (uptr, cptr);
if (r != SCPE_OK)
    return r;
uptr->USTAT = STA_BOT;
mt_updcsta (mt_dev.units + GET_UNIT (mt_cu), 0);        /* update status */
return r;
}

/* Detach routine */

t_stat mt_detach (UNIT* uptr)
{
if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
if (!sim_is_active (uptr))
    uptr->USTAT = 0;
mt_updcsta (mt_dev.units + GET_UNIT (mt_cu), 0);        /* update status */
return sim_tape_detach (uptr);
}
