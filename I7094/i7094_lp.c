/* i7094_lp.c: IBM 716 line printer simulator

   Copyright (c) 2003-2017, Robert M. Supnik

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

   lpt          716 line printer

   13-Mar-17    RMS     Fixed GET_PCHAIN macro (COVERITY)
   19-Jan-07    RMS     Added UNIT_TEXT flag

   Internally, the 7094 works only with column binary and is limited to
   72 columns of data.  Each row of the printed line is represented by
   72b of data (two 36b words).  A complete print line consists of 12 rows
   (24 36b words).

   The printer can also echo part of what it prints, namely, the digit rows
   plus the 8+3 and 8+4 combinations.  This was intended for verification of
   check printing.  Echoed data is interspersed with output data in the
   following order:

        output  row 9 to row 1
        echo    row "8+4"
        output  row 0
        echo    row "8+3"
        output  row 11
        echo    row 9
        output  row 12
        echo    row 8 to row 1
*/

#include "i7094_defs.h"

#define UNIT_V_CONS             (UNIT_V_UF + 0)         /* print to console */
#define UNIT_CONS               (1u << UNIT_V_CONS)
#define UNIT_V_BZ               (UNIT_V_UF + 1)
#define UNIT_V_48               (UNIT_V_UF + 2)
#define UNIT_BZ                 (1 << UNIT_V_BZ)
#define UNIT_48                 (1 << UNIT_V_48)
#define GET_PCHAIN(x)           (((x) >> UNIT_V_BZ) & 03)

#define LPT_BINLNT              24                      /* bin buffer length */
#define LPT_ECHLNT              22                      /* echo buffer length */
#define LPT_CHRLNT              80                      /* char buffer length */

#define LPS_INIT                0                       /* init state */
#define LPS_DATA                1                       /* print data state */
#define ECS_DATA                2                       /* echo data state */
#define LPS_END                 3                       /* end state */

#define LPB_9ROW                0                       /* bin buf: 9 row */
#define LPB_8ROW                2                       /*  8 row */
#define LPB_4ROW                10                      /*  4 row */
#define LPB_3ROW                12                      /*  3 row */
#define LPB_1ROW                16                      /*  1 row */
#define LPB_12ROW               22                      /* 12 row */

#define ECB_84ROW               0                       /* echo buf: 8-4 row */
#define ECB_83ROW               2                       /*  8-3 row */
#define ECB_9ROW                4                       /*  9 row */

#define ECHO_F                  0100                    /* echo map: flag */
#define ECHO_MASK               0037                    /* mask */

#define CMD_BIN                 1                       /* cmd: bcd/bin */
#define CMD_ECHO                2                       /* cmd: wrs/rds */

uint32 lpt_sta = 0;                                     /* state */
uint32 lpt_bptr = 0;                                    /* buffer ptr */
uint32 lpt_cmd = 0;                                     /* modes */
uint32 lpt_tstart = 27500;                              /* timing */
uint32 lpt_tstop = 27500;
uint32 lpt_tleft = 150;
uint32 lpt_tright = 4000;
t_uint64 lpt_chob = 0;
uint32 lpt_chob_v = 0;
t_uint64 lpt_bbuf[LPT_BINLNT];                          /* binary buffer */
t_uint64 lpt_ebuf[LPT_ECHLNT];                          /* echo buffer */

/* Echo ordering map */

static const uint8 echo_map[LPT_BINLNT + LPT_ECHLNT] = {
  0,  1,  2,  3,  4,  5,  6,  7,                        /* write 9 to 1 */
  8,  9, 10, 11, 12, 13, 14, 15,
 16, 17,
  0+ECHO_F,  1+ECHO_F,                                  /* echo 8+4 */
 18, 19,                                                /* write 0 */
  2+ECHO_F,  3+ECHO_F,                                  /* echo 8+3 */
 20, 21,                                                /* write 11 */
  4+ECHO_F,  5+ECHO_F,                                  /* echo 9 */
 22, 23,                                                /* write 12 */
  6+ECHO_F,  7+ECHO_F,  8+ECHO_F,  9+ECHO_F,            /* echo 8 to 1 */
 10+ECHO_F, 11+ECHO_F, 12+ECHO_F, 13+ECHO_F,
 14+ECHO_F, 15+ECHO_F, 16+ECHO_F, 17+ECHO_F,
 18+ECHO_F, 19+ECHO_F, 20+ECHO_F, 21+ECHO_F
 };

const char *pch_table[4] = {
    bcd_to_ascii_h, bcd_to_ascii_a, bcd_to_pch, bcd_to_pca,
    };

t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_svc (UNIT *uptr);
t_stat lpt_chsel (uint32 ch, uint32 sel, uint32 unit);
t_stat lpt_chwr (uint32 ch, t_uint64 val, uint32 flags);
t_stat lpt_end_line (UNIT *uptr);

extern char colbin_to_bcd (uint32 colbin);

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

DIB lpt_dib = { &lpt_chsel, &lpt_chwr };

UNIT lpt_unit = {
    UDATA (&lpt_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_CONS+UNIT_TEXT, 0)
    };

REG lpt_reg[] = {
    { ORDATA (STATE, lpt_sta, 2) },
    { ORDATA (CMD, lpt_cmd, 2) },
    { ORDATA (CHOB, lpt_chob, 36) },
    { FLDATA (CHOBV, lpt_chob_v, 0) },
    { DRDATA (BPTR, lpt_bptr, 6), PV_LEFT },
    { BRDATA (BUF, lpt_bbuf, 8, 36, LPT_BINLNT) },
    { BRDATA (EBUF, lpt_ebuf, 8, 36, LPT_ECHLNT) },
    { DRDATA (POS, lpt_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TSTART, lpt_tstart, 24), PV_LEFT + REG_NZ },
    { DRDATA (TSTOP, lpt_tstop, 24), PV_LEFT + REG_NZ },
    { DRDATA (TLEFT, lpt_tleft, 24), PV_LEFT + REG_NZ },
    { DRDATA (TRIGHT, lpt_tright, 24), PV_LEFT + REG_NZ },
    { NULL }
    };

MTAB lpt_mod[] = {
    { UNIT_CONS, UNIT_CONS, "default to console", "DEFAULT" },
    { UNIT_CONS, 0        , "no default device", "NODEFAULT" },
    { UNIT_48, UNIT_48, "48 character chain", "48" },
    { UNIT_48, 0,       "64 character chain", "64" },
    { UNIT_BZ, UNIT_BZ, "business set", "BUSINESS" },
    { UNIT_BZ, 0,       "Fortran set", "FORTRAN" },
    { 0 }
    };

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &lpt_reset,
    NULL, NULL, NULL,
    &lpt_dib, DEV_DISABLE
    };

/* Channel select routine */

t_stat lpt_chsel (uint32 ch, uint32 sel, uint32 unit)
{
if (sel & CHSL_NDS)                                     /* nds? nop */
    return ch6_end_nds (ch);

switch (sel) {                                          /* case on cmd */

    case CHSL_RDS:                                      /* read */
    case CHSL_WRS:                                      /* write */
        if (!(lpt_unit.flags & (UNIT_ATT|UNIT_CONS)))   /* not attached? */
            return SCPE_UNATT;
        if (sim_is_active (&lpt_unit))                  /* busy? */
            return ERR_STALL;
        lpt_cmd = ((unit & 02)? CMD_BIN: 0) |           /* save modes */
            ((sel == CHSL_RDS)? CMD_ECHO: 0);
        lpt_sta = LPS_INIT;                             /* initial state */
        sim_activate (&lpt_unit, lpt_tstart);           /* start reader */
        break;

    default:                                            /* other */
        return STOP_ILLIOP;
        }

return SCPE_OK;
}

/* Channel write routine

   - Normal mode is processed here
   - Echo mode is processed in the service routine (like a read) */

t_stat lpt_chwr (uint32 ch, t_uint64 val, uint32 eorfl)
{
uint32 u = (lpt_cmd & CMD_BIN)? U_LPBIN: U_LPBCD;       /* reconstruct unit */

lpt_chob = val & DMASK;                                 /* store data */
lpt_chob_v = 1;                                         /* set valid */
if (lpt_sta == ECS_DATA)
    return SCPE_OK;
if (lpt_sta == LPS_DATA) {
    lpt_bbuf[lpt_bptr++] = lpt_chob;                    /* store data */
    if (eorfl ||                                        /* end record, or */
        ((lpt_cmd & CMD_BIN)?                           /* last word in buffer? */
         (lpt_bptr > (LPB_1ROW + 1)):                   /* (binary mode) */
         (lpt_bptr > (LPB_12ROW + 1)))) {               /* (normal mode) */
        ch6_set_flags (CH_A, u, CHF_EOR);               /* set eor */
        return lpt_end_line (&lpt_unit);
        }
    return SCPE_OK;
    }
return SCPE_IERR;
}

/* Unit timeout */

t_stat lpt_svc (UNIT *uptr)
{
uint32 u = (lpt_cmd & CMD_BIN)? U_LPBIN: U_LPBCD;       /* reconstruct unit */
uint32 i, map;

switch (lpt_sta) {                                      /* case on state */

    case LPS_INIT:                                      /* initial state */
        for (i = 0; i < LPT_BINLNT; i++)                /* clear data buffer */
            lpt_bbuf[i] = 0;
        for (i = 0; i < LPT_ECHLNT; i++)                /* clear echo buffer */
            lpt_ebuf[i] = 0;
        if (lpt_cmd & CMD_BIN)                          /* set buffer ptr */
            lpt_bptr = LPB_1ROW;
        else lpt_bptr = LPB_9ROW;
        if (lpt_cmd & CMD_ECHO)                         /* set data state */
            lpt_sta = ECS_DATA;
        else lpt_sta = LPS_DATA;
        ch6_req_wr (CH_A, u);                           /* request channel */
        lpt_chob = 0;                                   /* clr, inval buffer */
        lpt_chob_v = 0;
        sim_activate (uptr, lpt_tleft);                 /* go again */
        break;

    case LPS_DATA:                                      /* print data state */
        if (!ch6_qconn (CH_A, u))                       /* disconnect? */
            return lpt_end_line (uptr);                 /* line is done */
        if (lpt_chob_v)                                 /* valid? clear */
            lpt_chob_v = 0;
        else ind_ioc = 1;                               /* no, io check */
        ch6_req_wr (CH_A, u);                           /* request chan again */
        sim_activate (uptr, (lpt_bptr & 1)? lpt_tleft: lpt_tright);
        break;

    case ECS_DATA:                                      /* echo data state */
        map = echo_map[lpt_bptr++];                     /* map column */
        if (map == ECHO_F) {                            /* first echo? */
            lpt_ebuf[ECB_84ROW] = lpt_bbuf[LPB_8ROW] & lpt_bbuf[LPB_4ROW];
            lpt_ebuf[ECB_84ROW + 1] = lpt_bbuf[LPB_8ROW + 1] & lpt_bbuf[LPB_4ROW + 1];
            lpt_ebuf[ECB_83ROW] = lpt_bbuf[LPB_8ROW] & lpt_bbuf[LPB_3ROW];
            lpt_ebuf[ECB_83ROW + 1] = lpt_bbuf[LPB_8ROW + 1] & lpt_bbuf[LPB_3ROW + 1];
            for (i = 0; i < 18; i++)                    /* copy rows 9.. 1 */
                lpt_ebuf[ECB_9ROW + i] = lpt_bbuf[LPB_9ROW + i];
            }
        if (map & ECHO_F) {                             /* echo cycle */
            ch6_req_rd (CH_A, u, lpt_ebuf[map & ECHO_MASK], 0);
            if (lpt_bptr >= (LPT_BINLNT + LPT_ECHLNT))
                return lpt_end_line (uptr);             /* done? */
            sim_activate (uptr, lpt_tleft);             /* short timer */
            }
        else {                                          /* print cycle */
            if (lpt_chob_v)                             /* valid? clear */
                lpt_chob_v = 0;
            else ind_ioc = 1;                           /* no, io check */
            lpt_bbuf[map] = lpt_chob;                   /* store in buffer */
            sim_activate (uptr, (lpt_bptr & 1)? lpt_tleft: lpt_tright);
            }
        if (!(echo_map[lpt_bptr] & ECHO_F))             /* print word next? */
            ch6_req_wr (CH_A, u);                       /* req channel */
        break;

    case LPS_END:                                       /* end state */
        if (ch6_qconn (CH_A, u)) {                      /* lpt still conn? */
            lpt_sta = LPS_INIT;                         /* initial state */
            sim_activate (uptr, 1);                     /* next line */
            }
        break;
        }

return SCPE_OK;
}

/* End line routine */

t_stat lpt_end_line (UNIT *uptr)
{
uint32 i, col, row, bufw, colbin;
const char *pch;
char bcd, lpt_cbuf[LPT_CHRLNT + 1];
t_uint64 dat;

pch = pch_table[GET_PCHAIN (lpt_unit.flags)];           /* get print chain */
for (col = 0; col < (LPT_CHRLNT + 1); col++)            /* clear ascii buf */
    lpt_cbuf[col] = ' '; 
for (col = 0; col < 72; col++) {                        /* proc 72 columns */
    colbin = 0;
    dat = bit_masks[35 - (col % 36)];                   /* mask for column */
    for (row = 0; row < 12; row++) {                    /* proc 12 rows */
        bufw = (row * 2) + (col / 36);                  /* index to buffer */
        if (lpt_bbuf[bufw] & dat)
            colbin |= col_masks[row];
        }
    bcd = colbin_to_bcd (colbin);                       /* column bin -> BCD */
    lpt_cbuf[col] = pch[bcd & 077];                     /* -> ASCII */
    }
for (i = LPT_CHRLNT; (i > 0) &&
    (lpt_cbuf[i - 1] == ' '); --i) ;                    /* trim spaces */
lpt_cbuf[i] = 0;                                        /* append nul */
if (uptr->flags & UNIT_ATT) {                           /* file? */
    fputs (lpt_cbuf, uptr->fileref);                    /* write line */
    fputc ('\n', uptr->fileref);                        /* append nl */
    uptr->pos = ftell (uptr->fileref);                  /* update position */
    if (ferror (uptr->fileref)) {                       /* error? */
        sim_perror ("LPT I/O error");
        clearerr (uptr->fileref);
        return SCPE_IOERR;
        }
    }
else if (uptr->flags & UNIT_CONS) {                     /* print to console? */
    for (i = 0; lpt_cbuf[i] != 0; i++)
        sim_putchar (lpt_cbuf[i]);
    sim_putchar ('\r');
    sim_putchar ('\n');
    }
else return SCPE_UNATT;                                 /* otherwise error */
lpt_sta = LPS_END;                                      /* end line state */
sim_cancel (uptr);                                      /* cancel current */
sim_activate (uptr, lpt_tstop);                         /* long timer */
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
uint32 i;

for (i = 0; i < LPT_BINLNT; i++)                        /* clear bin buf */
    lpt_bbuf[i] = 0;
for (i = 0; i < LPT_ECHLNT; i++)                        /* clear echo buf */
    lpt_ebuf[i] = 0;
lpt_sta = 0;                                            /* clear state */
lpt_cmd = 0;                                            /* clear modes */
lpt_bptr = 0;                                           /* clear buf ptr */
lpt_chob = 0;
lpt_chob_v = 0;
sim_cancel (&lpt_unit);                                 /* stop printer */
return SCPE_OK;
}
