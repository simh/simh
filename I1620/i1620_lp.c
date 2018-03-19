/* i1620_lp.c: IBM 1443 line printer simulator

   Copyright (c) 2002-2017, Robert M. Supnik

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

   lpt          1443 line printer

   15-Jun-17    RMS     Fixed K constants and print-no-spacing (Tom McBride)
                        Added option to emulate form feed with newlines
   31-Jan-15    TFM     Fixed various problems ... see comments in code
   10-Dec-13    RMS     Fixed DN wraparound (Bob Armstrong)
                        Fixed test on VFU 10 (Bob Armstrong)
   19-Jan-07    RMS     Added UNIT_TEXT flag
   21-Sep-05    RMS     Revised translation tables for 7094/1401 compatibility
   29-Dec-03    RMS     Fixed bug in scheduling
   25-Apr-03    RMS     Revised for extended file support
*/

#include "i1620_defs.h"

#define LPT_BSIZE       197                             /* buffer size */
#define UNIT_V_FF       (UNIT_V_UF + 0)
#define UNIT_FF         (1 << UNIT_V_FF)

/* decoded print control */

#define K_IMM           0x10                            /* before print */
#define K_LIN           0x20                            /* spc lines */
#define K_LCNT          0x03                            /* line count */
#define K_CHAN          0x0F                            /* channel */

extern uint8 M[MAXMEMSIZE];
extern uint8 ind[NUM_IND];
extern UNIT cpu_unit;
extern uint32 io_stop;

uint32 cct[CCT_LNT] = { 03 };                           /* car ctrl tape */
int32 cct_lnt = 66, cct_ptr = 0;                        /* cct len, ptr */
int32 lpt_bptr = 0;                                     /* lpt buf ptr */
char lpt_buf[LPT_BSIZE + 1];                            /* lpt buf */
int32 lpt_savctrl = 0;                                  /* saved spc ctrl */

t_stat lpt_svc (UNIT *uptr);
t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_attach (UNIT *uptr, CONST char *cptr);
void lpt_buf_init (void);
t_stat lpt_num(uint32 pa, uint32 f1, t_bool dump);
t_stat lpt_print (uint32 flag);
t_stat lpt_spcop (int32 ctrl);
t_stat lpt_space (int32 lines, int32 lflag);

#define CHP(ch,val)     ((val) & (1 << (ch)))

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

UNIT lpt_unit = {
    UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 50)
    };

REG lpt_reg[] = {
    { BRDATAD (LBUF, lpt_buf, 8, 8, LPT_BSIZE + 1, "line buffer") },
    { DRDATAD (BPTR, lpt_bptr, 8, "buffer pointer") },
    { HRDATAD (PCTL, lpt_savctrl, 6, "saved print control directive") },
    { FLDATAD (PRCHK, ind[IN_PRCHK], 0, "print check indicator") },
    { FLDATAD (PRCH9, ind[IN_PRCH9], 0, "channel 9 indicator") },
    { FLDATAD (PRCH12, ind[IN_PRCH12], 0, "channel 12 indicator") },
    { FLDATAD (PRBSY, ind[IN_PRBSY], 0, "busy indicator") },
    { DRDATAD (POS, lpt_unit.pos, T_ADDR_W, "position in the output file"), PV_LEFT },
    { BRDATAD (CCT, cct, 8, 32, CCT_LNT, "carriage control tape array") },
    { DRDATAD (CCTP, cct_ptr, 8, "carriage control tape pointer"), PV_LEFT },
    { DRDATAD (CCTL, cct_lnt, 8, "carriage control tape length (read only)"), REG_RO + PV_LEFT },
    { NULL }
    };

MTAB lp_mod[] = {
    { UNIT_FF, 0, "no form feeds", "NOFF", NULL },
    { UNIT_FF, UNIT_FF, "form feeds", "FF", NULL }, 
    { 0 }
    };

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lp_mod,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, NULL
    };

/* Data tables */

/* Numeric (flag plus digit) to lineprinter (ASCII) */

const int8 num_to_lpt[32] = {
 '0', '1', '2', '3', '4', '5', '6', '7',                /* All invalid char treated as errors */
 '8', '9', '|', -1,  '@',  -1,  -1, 'G',                /* @, G only print on DN; else NB is blank */
 '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
 'Q', 'R', 'W',  -1, '*',  -1,  -1, 'X'                 /* W, *, X only print on DN */
 };

/* Alphameric (digit pair) to lineprinter (ASCII) */

const int8 alp_to_lpt[256] = {                          /* tfm: invalid codes 02, 12, 15, 32, 35, 61 removed */
 ' ',  -1,  -1, '.', ')',  -1,  -1,  -1,                /* 00 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
 '+',  -1,  -1, '$', '*',  -1,  -1,  -1,                /* 10 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
 '-', '/', '|', ',', '(',  -1,  -1,  -1,                /* 20 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1, '=', '@',  -1,  -1,  -1,                /* 30 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1, 'A', 'B', 'C', 'D', 'E', 'F', 'G',                /* 40 */
 'H', 'I',  -1,  -1,  -1,  -1,  -1,  -1,
 '-', 'J', 'K', 'L', 'M', 'N', 'O', 'P',                /* 50 */
 'Q', 'R',  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1, 'S', 'T', 'U', 'V', 'W', 'X',                /* 60 */
 'Y', 'Z',  -1,  -1,  -1,  -1,  -1,  -1,
 '0', '1', '2', '3', '4', '5', '6', '7',                /* 70 */
 '8', '9',  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* 80 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* 90 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* A0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* B0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* C0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* D0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* E0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,                /* F0 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1
 };

/* K validation and translation table - entryies 80:FF always 0 */

static const int8 lpt_ktbl[128] = {
  0, 0, 0, 11, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     /* 00 */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,       /* 10 */
  0, K_LIN|1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 20 */
  0, 0, 0, K_IMM|11, K_IMM|12, 0, 0, 0, 0, 0,           /* 30 */
         0, 0, 0, 0, 0, 0,
  10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,      /* 40 */
  0, K_IMM|K_LIN|1, K_IMM|K_LIN|2, K_IMM|K_LIN|3, 0,    /* 50 */
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, K_LIN|2, K_LIN|3, 0, 0, 0, 0, 0, 0,             /* 60 */
         0, 0, 0, 0, 0, 0,
  K_IMM|10, K_IMM|1, K_IMM|2, K_IMM|3, K_IMM|4,         /* 70 */
         K_IMM|5, K_IMM|6, K_IMM|7, K_IMM|8, K_IMM|9,
         0, 0, 0, 0, 0, 0,
  };

/* Line printer IO routine
 
   - Hard errors halt the system.
   - Invalid characters print a blank, set the WRCHK and PRCHK
     flags, and halt the system if IO stop is set.
*/

t_stat lpt (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
int8 lpc;
uint8 z, d;
int32 ctrl;
t_stat r, sta;

sta = SCPE_OK;
sim_cancel (&lpt_unit);                                 /* "stall" until */
ind[IN_PRBSY] = 0;                                      /* printer free */

switch (op) {                                           /* decode op */

    case OP_K:                                          /* control */
        ctrl = lpt_ktbl[((f0 & 0x7) << 4) | (f1 & 0xF)]; /* xlate ctrl */
        if ((f0 > 7) || (ctrl == 0)) {                  /* invalid? */
            ind[IN_PRCHK] = 1;                          /* print chk */
            if (io_stop)                                /* set return status */
                sta = STOP_INVFNC;
            }
        else {
            if ((ctrl & K_IMM) != 0)                    /* immediate? */
                return lpt_spcop (ctrl);                /* execute */
            else lpt_savctrl = ctrl;                    /* otherwise, save */
            }
        return sta;

    case OP_DN:
        return lpt_num (pa, f1, TRUE);                  /* dump numeric */

    case OP_WN:
        return lpt_num (pa, f1, FALSE);                 /* write numeric */

    case OP_WA:
        for ( ; lpt_bptr < LPT_BSIZE; lpt_bptr++) {     /* only fill buf */
            d = M[pa] & DIGIT;                          /* get digit */
            z = M[pa - 1] & DIGIT;                      /* get zone */
            if ((d & REC_MARK) == REC_MARK)             /* 8-2 char? */
                break;
            lpc = alp_to_lpt[(z << 4) | d];             /* translate pair */
            if (lpc < 0) {                              /* bad char? */
                ind[IN_WRCHK] = ind[IN_PRCHK] = 1;      /* wr chk */
                if (io_stop)                            /* set return status */
                    sta = STOP_INVCHR;
                }
            lpt_buf[lpt_bptr] = lpc & 0x7F;             /* fill buffer */
            pa = ADDR_A (pa, 2);                        /* incr mem addr */
            }
        r = lpt_print (f1);                             /* print line */
        if (r != SCPE_OK)
            return r;
        return sta;

    default:                                            /* invalid function */
        return STOP_INVFNC;
        }

return SCPE_OK;
}

/* Print numeric */

t_stat lpt_num (uint32 pa, uint32 f1, t_bool dump)
{
uint8 d;
int8 lpc;
t_stat r, sta;

sta = SCPE_OK;
for ( ; lpt_bptr < LPT_BSIZE; lpt_bptr++) {             /* only fill buf */
    d = M[pa];                                          /* get data char */
    if (!dump &&                                        /* not dumping? */
        ((d & REC_MARK) == REC_MARK))                   /* quit on RM or GM */
        break;
    lpc = num_to_lpt[d];                                /* translate digit */
    if (!dump &&                                        /* if not dumping */
        ((d & DIGIT) == NUM_BLANK))                     /* translate numeric blank */
        lpc = ' ';                                      /* to normal space */
    if (lpc < 0) {                                      /* bad char? */
        ind[IN_WRCHK] = ind[IN_PRCHK] = 1;              /* wr chk */
        if (io_stop)                                    /* set return status */
            sta = STOP_INVCHR;
        }
    lpt_buf[lpt_bptr] = lpc & 0x7F;                     /* put char into buffer (tfm: correct increment)*/
    PP (pa);                                            /* incr mem addr */
    }
r = lpt_print (f1);                                     /* print line */
if (r != SCPE_OK)
    return r;
return sta;
}

/* Print and possibly space - any spacing operation is non-immediate */

t_stat lpt_print (uint32 flag)
{
int32 i;

if ((lpt_unit.flags & UNIT_ATT) == 0) {                 /* not attached? */
    ind[IN_PRCHK] = 1;                                  /* pri check */
    return SCPE_UNATT;
    }

for (i = LPT_WIDTH; i <= LPT_BSIZE; i++)                /* clear unprintable */
    lpt_buf[i] = ' ';
while ((lpt_bptr > 0) && (lpt_buf[lpt_bptr - 1] == ' '))
    lpt_buf[--lpt_bptr] = 0;                            /* trim buffer */
if (lpt_bptr != 0) {                                    /* any line? */
    fputs (lpt_buf, lpt_unit.fileref);                  /* print */
    if ((flag & 1) != 0)                                /* no space? */
        fputc ('\r', lpt_unit.fileref);                 /* bare return */
    lpt_unit.pos = ftell (lpt_unit.fileref);            /* update pos */
    lpt_buf_init ();                                    /* reinit buf */
    if (ferror (lpt_unit.fileref)) {                    /* error? */
        ind[IN_PRCHK] = 1;                              /* pri check */
        sim_perror ("LPT I/O error");
        clearerr (lpt_unit.fileref);
        return SCPE_IOERR;
        }
    }
if ((flag & 1) == 0)                                    /* spacing? */
    return lpt_spcop (lpt_savctrl);                     /* execute */
return SCPE_OK;                                         /* done */
}

/* Space operation - direct (K) or deferred (WA, WN, DN) */

t_stat lpt_spcop (int32 ctrl)
{
int32 chan, i;

lpt_savctrl = K_LIN|1;                                  /* reset saved control */
if ((ctrl & K_LIN) != 0)                                /* space lines? */
    return lpt_space (ctrl & K_LCNT, FALSE);            /* execute spacing op */
chan = lpt_savctrl & K_CHAN;                            /* get chan */
if ((chan == 0) || (chan > 12))
    return STOP_INVFNC;
for (i = 1; i < cct_lnt + 1; i++) {                     /* sweep thru cct */
    if (CHP (chan, cct[(cct_ptr + i) % cct_lnt]))
        return lpt_space (i, TRUE);
    }
return STOP_CCT;                                        /* runaway channel */
}

/* Space routine - space or skip n lines
   
   Inputs:
        count   =       number of lines to space or skip
        sflag   =       skip (TRUE) or space (FALSE)
*/

t_stat lpt_space (int32 count, int32 sflag)
{
int32 i;

cct_ptr = (cct_ptr + count) % cct_lnt;                  /* adv cct, mod lnt */
if (sflag && CHP (0, cct[cct_ptr]) &&                   /* skip, top of form, */
    ((lpt_unit.flags & UNIT_FF) != 0))                  /* and use form feeds? */
    fputs ("\n\f", lpt_unit.fileref);                   /* nl, ff */
else {
    for (i = 0; i < count; i++)                         /* count lines */
        fputc ('\n', lpt_unit.fileref);
    }
lpt_unit.pos = ftell (lpt_unit.fileref);                /* update position */
ind[IN_PRCH9] = CHP (9, cct[cct_ptr]) != 0;             /* set indicators */
ind[IN_PRCH12] = CHP (12, cct[cct_ptr]) != 0;
if (ferror (lpt_unit.fileref)) {                        /* error? */
    ind[IN_PRCHK] = ind[IN_WRCHK] = 1;                  /* wr, pri check */
    sim_perror ("LPT I/O error");
    clearerr (lpt_unit.fileref);
    return SCPE_IOERR;
    }
ind[IN_PRBSY] = 1;                                      /* print busy */
sim_activate (&lpt_unit, lpt_unit.wait);                /* start timer */
return SCPE_OK;
}

/* Unit service - clear printer busy */

t_stat lpt_svc (UNIT *uptr)
{
ind[IN_PRBSY] = 0;
return SCPE_OK;
}

/* Initialize lpt buffer */

void lpt_buf_init (void)
{
int32 i;

lpt_bptr = 0;
for (i = 0; i < LPT_WIDTH + 1; i++)
    lpt_buf[i] = 0;
return;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
lpt_buf_init ();                                        /* clear buffer */
cct_ptr = 0;                                            /* clear cct ptr */
lpt_savctrl = K_LIN|1;                                  /* reset cct action */
ind[IN_PRCHK] = ind[IN_PRBSY] = 0;                      /* clear indicators */
ind[IN_PRCH9] = ind[IN_PRCH12] = 0;
return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, CONST char *cptr)
{
lpt_reset (&lpt_dev);
return attach_unit (uptr, cptr);
}
