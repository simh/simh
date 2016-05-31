/* i7094_cd.c: IBM 711/721 card reader/punch

   Copyright (c) 2003-2012, Robert M. Supnik

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

   cdr          711 card reader
   cdp          721 card punch

   19-Mar-12    RMS     Fixed declaration of sim_switches (Mark Pizzolato)
   19-Jan-07    RMS     Added UNIT_TEXT
   13-Jul-06    RMS     Fixed problem with 80 column full cards

   Cards are represented as ASCII text streams terminated by newlines.
   This allows cards to be created and edited as normal files.  Two
   formats are supported:

   column binary        each character represents 6b of a 12b column
   text                 each character represents all 12b of a column

   Internally, the 7094 works only with column binary and is limited
   to 72 columns of data.  Each row of the card is represented by 72b
   of data (two 36b words).  A complete card image consists of 12 rows
   (24 36b words).
*/

#include "i7094_defs.h"

#define CD_BINLNT               24                      /* bin buf length */
#define CD_CHRLNT               80                      /* char buf length */

#define CDS_INIT                0                       /* card in motion */
#define CDS_DATA                1                       /* data transfer */
#define CDS_END                 2                       /* card complete */

#define UNIT_V_CBN              (UNIT_V_UF + 0)         /* column binary file */
#define UNIT_V_PCA              (UNIT_V_UF + 1)         /* A vs H punch flag */
#define UNIT_CBN                (1 << UNIT_V_CBN)
#define UNIT_PCA                (1 << UNIT_V_PCA)

uint32 cdr_sta = 0;                                     /* state */
uint32 cdr_bptr = 0;                                    /* buffer ptr */
uint32 cdr_tstart = 27500;                              /* timing */
uint32 cdr_tstop = 27500;
uint32 cdr_tleft = 150;
uint32 cdr_tright = 4000;
t_uint64 cdr_bbuf[CD_BINLNT];                           /* col binary buf */

uint32 cdp_sta = 0;                                     /* state */
uint32 cdp_bptr = 0;                                    /* buffer ptr */
uint32 cdp_tstart = 35000;                              /* timing */
uint32 cdp_tstop = 35000;
uint32 cdp_tleft = 150;
uint32 cdp_tright = 15500;
t_uint64 cdp_chob = 0;
uint32 cdp_chob_v = 0;
t_uint64 cdp_bbuf[CD_BINLNT];                           /* col binary buf */

t_stat cdr_chsel (uint32 ch, uint32 sel, uint32 unit);
t_stat cdr_reset (DEVICE *dptr);
t_stat cdr_svc (UNIT *uptr);
t_stat cdr_boot (int32 unitno, DEVICE *dptr);
t_stat cdp_chsel (uint32 ch, uint32 sel, uint32 unit);
t_stat cdp_chwr (uint32 ch, t_uint64 val, uint32 flags);
t_stat cdp_reset (DEVICE *dptr);
t_stat cdp_svc (UNIT *uptr);
t_stat cdp_card_end (UNIT *uptr);
t_stat cd_attach (UNIT *uptr, CONST char *cptr);
t_stat cd_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
char colbin_to_bcd (uint32 cb);


/* Card reader data structures

   cdr_dev      CDR descriptor
   cdr_unit     CDR unit descriptor
   cdr_reg      CDR register list
*/

DIB cdr_dib = { &cdr_chsel, NULL };

UNIT cdr_unit = {
    UDATA (&cdr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE+UNIT_TEXT, 0)
    };

REG cdr_reg[] = {
    { ORDATA (STATE, cdr_sta, 2) },
    { DRDATA (BPTR, cdr_bptr, 5), PV_LEFT },
    { BRDATA (BUF, cdr_bbuf, 8, 36, CD_BINLNT) },
    { DRDATA (POS, cdr_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TSTART, cdr_tstart, 24), PV_LEFT + REG_NZ },
    { DRDATA (TSTOP, cdr_tstop, 24), PV_LEFT + REG_NZ },
    { DRDATA (TLEFT, cdr_tleft, 24), PV_LEFT + REG_NZ },
    { DRDATA (TRIGHT, cdr_tright, 24), PV_LEFT + REG_NZ },
    { NULL }  };

MTAB cdr_mod[] = {
    { UNIT_CBN, UNIT_CBN, "column binary", "BINARY", &cd_set_mode },
    { UNIT_CBN, UNIT_CBN, "text", "TEXT", &cd_set_mode },
    { 0 }
    };

DEVICE cdr_dev = {
    "CDR", &cdr_unit, cdr_reg, cdr_mod,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &cdr_reset,
    &cdr_boot, &cd_attach, NULL,
    &cdr_dib, DEV_DISABLE
    };

/* CDP data structures

   cdp_dev      CDP device descriptor
   cdp_unit     CDP unit descriptor
   cdp_reg      CDP register list
*/

DIB cdp_dib = { &cdp_chsel, &cdp_chwr };

UNIT cdp_unit = {
    UDATA (&cdp_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0)
    };

REG cdp_reg[] = {
    { ORDATA (STATE, cdp_sta, 2) },
    { ORDATA (CHOB, cdp_chob, 36) },
    { FLDATA (CHOBV, cdp_chob_v, 0) },
    { DRDATA (BPTR, cdp_bptr, 5), PV_LEFT },
    { BRDATA (BUF, cdp_bbuf, 8, 36, CD_BINLNT) },
    { DRDATA (POS, cdp_unit.pos, T_ADDR_W), PV_LEFT },
    { DRDATA (TSTART, cdp_tstart, 24), PV_LEFT + REG_NZ },
    { DRDATA (TSTOP, cdp_tstop, 24), PV_LEFT + REG_NZ },
    { DRDATA (TLEFT, cdp_tleft, 24), PV_LEFT + REG_NZ },
    { DRDATA (TRIGHT, cdp_tright, 24), PV_LEFT + REG_NZ },
    { NULL }
    };

MTAB cdp_mod[] = {
    { UNIT_CBN, UNIT_CBN, "column binary", "BINARY", &cd_set_mode },
    { UNIT_CBN, UNIT_CBN, "text", "TEXT", &cd_set_mode },
    { UNIT_PCA, UNIT_PCA, "business set", "BUSINESS", NULL },
    { UNIT_PCA, 0,        "Fortran set", "FORTRAN", NULL },
    { 0 }
    };

DEVICE cdp_dev = {
    "CDP", &cdp_unit, cdp_reg, cdp_mod,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &cdp_reset,
    NULL, &cd_attach, NULL,
    &cdp_dib, DEV_DISABLE
    };

/* Card reader select */

t_stat cdr_chsel (uint32 ch, uint32 sel, uint32 unit)
{
if (sel & CHSL_NDS)                                     /* nds? nop */
    return ch6_end_nds (ch);

switch (sel) {                                          /* case on data sel */

    case CHSL_RDS:                                      /* read */
        if ((cdr_unit.flags & UNIT_ATT) == 0)           /* not attached? */
            return SCPE_UNATT;
        if (sim_is_active (&cdr_unit))                  /* busy? */
            return ERR_STALL;
        cdr_sta = CDS_INIT;                             /* initial state */
        sim_activate (&cdr_unit, cdp_tstart);           /* start reader */
        break;

    default:                                            /* other */
        return STOP_ILLIOP;                             /* not allowed */
        }

return SCPE_OK;
}

/* Unit timeout */

t_stat cdr_svc (UNIT *uptr)
{
uint32 i, col, row, bufw, colbin;
char cdr_cbuf[(2 * CD_CHRLNT) + 2];
t_uint64 dat = 0;

if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return SCPE_UNATT;
switch (cdr_sta) {                                      /* case on state */

    case CDS_INIT:                                      /* initial state */
        for (i = 0; i < CD_BINLNT; i++)                 /* clear bin buf */ 
             cdr_bbuf[i] = 0;
        for (i = 0; i < ((2 * CD_CHRLNT) + 2); i++)     /* clear char buf */
            cdr_cbuf[i] = ' ';
        cdr_sta = CDS_DATA;                             /* data state */
        cdr_bptr = 0;                                   /* init buf ptr */
        fgets (cdr_cbuf, (uptr->flags & UNIT_CBN)? (2 * CD_CHRLNT) + 2: CD_CHRLNT + 2,
            uptr->fileref);                             /* read card */
        if (feof (uptr->fileref))                       /* eof? */
            return ch6_err_disc (CH_A, U_CDR, CHF_EOF); /* set EOF, disc */
        if (ferror (uptr->fileref)) {                   /* error? */
            sim_perror ("CDR I/O error");
            clearerr (uptr->fileref);
            return SCPE_IOERR;                          /* stop */
            }
        uptr->pos = ftell (uptr->fileref);              /* update position */
        for (i = 0; i < (2 * CD_CHRLNT); i++)           /* convert to BCD */
            cdr_cbuf[i] = ascii_to_bcd[cdr_cbuf[i] & 0177] & 077;
        for (col = 0; col < 72; col++) {                /* process 72 columns */
            if (uptr->flags & UNIT_CBN)                 /* column binary? */
                colbin = (((uint32) cdr_cbuf[2 * col]) << 6) |
                ((uint32) cdr_cbuf[(2 * col) + 1]);     /* 2 chars -> col bin */
            else colbin = bcd_to_colbin[cdr_cbuf[col]]; /* cvt to col binary */
            dat = bit_masks[35 - (col % 36)];           /* mask for column */
            for (row = 0; row < 12; row++) {            /* rows 9..0, 11, 12 */
                bufw = (row * 2) + (col / 36);          /* index to buffer */
                if (colbin & col_masks[row])            /* row bit set? */
                    cdr_bbuf[bufw] |= dat;
                }
            }

    case CDS_DATA:                                      /* data state */
        dat = cdr_bbuf[cdr_bptr++];                     /* get next word */
        if (cdr_bptr >= CD_BINLNT) {                    /* last word? */
            cdr_sta = CDS_END;                          /* end state */
            ch6_req_rd (CH_A, U_CDR, dat, CH6DF_EOR);   /* req chan, dat, EOR */
            sim_activate (uptr, cdr_tstop);
            }
        else {
            ch6_req_rd (CH_A, U_CDR, dat, 0);           /* req chan, dat */
            sim_activate (uptr, (cdr_bptr & 1)? cdr_tleft: cdr_tright);
            }
        break;

    case CDS_END:                                       /* end state */
        if (ch6_qconn (CH_A, U_CDR)) {                  /* if cdr still conn */
            cdr_sta = CDS_INIT;                         /* return to init */
            sim_activate (uptr, 1);                     /* next card */
            }
        break;
        }

return SCPE_OK;
}

/* Card reader reset */

t_stat cdr_reset (DEVICE *dptr)
{
uint32 i;

for (i = 0; i < CD_BINLNT; i++)                         /* clear buffer */
    cdr_bbuf[i] = 0;
cdr_sta = 0;                                            /* clear state */
cdr_bptr = 0;                                           /* clear buf ptr */
sim_cancel (&cdr_unit);                                 /* stop reader */
return SCPE_OK;
}

/* Card reader bootstrap */

#define BOOT_START      01000
#define BOOT_SIZE       (sizeof (boot_rom) / sizeof (t_uint64))

static const t_uint64 boot_rom[] = {
    INT64_C(00762000001000) + U_CDR,                    /* RDSA CDR */
    INT64_C(00544000000000) + BOOT_START + 4,           /* LCHA *+3 */
    INT64_C(00544000000000),                            /* LCHA 0 */
    INT64_C(00021000000001),                            /* TTR 1 */
    INT64_C(05000030000000),                            /* IOCT 3,,0 */
    };

t_stat cdr_boot (int32 unitno, DEVICE *dptr)
{
uint32 i;
extern t_uint64 *M;

for (i = 0; i < BOOT_SIZE; i++)
    WriteP (BOOT_START + i, boot_rom[i]);
PC = BOOT_START;
return SCPE_OK;
}

/* Reader/punch attach */

t_stat cd_attach (UNIT *uptr, CONST char *cptr)
{
t_stat r;

r = attach_unit (uptr, cptr);
if (r != SCPE_OK)                                       /* attach */
    return r;
if (sim_switches & SWMASK ('T'))                        /* text? */
    uptr->flags = uptr->flags & ~UNIT_CBN;
else if (sim_switches & SWMASK ('C'))                   /* column binary? */
    uptr->flags = uptr->flags | UNIT_CBN;
else if (match_ext (cptr, "TXT"))                       /* .txt? */
    uptr->flags = uptr->flags & ~UNIT_CBN;
else if (match_ext (cptr, "CBN"))                       /* .cbn? */
    uptr->flags = uptr->flags | UNIT_CBN;
return SCPE_OK;
}

/* Reader/punch set mode - valid only if not attached */

t_stat cd_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
return (uptr->flags & UNIT_ATT)? SCPE_NOFNC: SCPE_OK;
}

/* Card punch select */

t_stat cdp_chsel (uint32 ch, uint32 sel, uint32 unit)
{
if (sel & CHSL_NDS)                                     /* nds? nop */
    return ch6_end_nds (ch);

switch (sel) {                                          /* case on cmd */

    case CHSL_WRS:                                      /* write */
        if ((cdp_unit.flags & UNIT_ATT) == 0)           /* not attached? */
            return SCPE_UNATT;
        if (sim_is_active (&cdp_unit))                  /* busy? */
            return ERR_STALL;
        cdp_sta = CDS_INIT;                             /* initial state */
        sim_activate (&cdp_unit, cdp_tstart);           /* start punch */
        break;

    default:                                            /* other */
        return STOP_ILLIOP;                             /* not allowed */
        }
return SCPE_OK;
}

/* Channel write routine - write word to buffer, write card when full */

t_stat cdp_chwr (uint32 ch, t_uint64 val, uint32 eorfl)
{
cdp_chob = val & DMASK;                                 /* store data */
cdp_chob_v = 1;                                         /* buffer valid */
if (cdp_sta == CDS_DATA) {
    cdp_bbuf[cdp_bptr++] = cdp_chob;                    /* store data */
    if ((cdp_bptr >= CD_BINLNT) || eorfl) {             /* end card or end rec? */
        ch6_set_flags (CH_A, U_CDP, CHF_EOR);           /* set eor */
        return cdp_card_end (&cdp_unit);                /* write card */
        }
    return SCPE_OK;
    }
return SCPE_IERR;
}

/* Unit timeout */

t_stat cdp_svc (UNIT *uptr)
{
uint32 i;

switch (cdp_sta) {                                      /* case on state */

    case CDS_INIT:                                      /* initial state */
        for (i = 0; i < CD_BINLNT; i++)                 /* clear bin buffer */
            cdp_bbuf[i] = 0;
        cdp_sta = CDS_DATA;                             /* data state */
        cdp_bptr = 0;                                   /* init pointer */
        ch6_req_wr (CH_A, U_CDP);                       /* request channel */
        cdp_chob = 0;                                   /* clr, inval buffer */
        cdp_chob_v = 0;
        sim_activate (uptr, cdp_tleft);                 /* go again */
        break;

    case CDS_DATA:                                      /* data state */
        if (!ch6_qconn (CH_A, U_CDP))                   /* chan disconnect? */
            return cdp_card_end (uptr);                 /* write card */
        if (cdp_chob_v)                                 /* valid? clear */
            cdp_chob_v = 0;
        else ind_ioc = 1;                               /* no, io check */
        ch6_req_wr (CH_A, U_CDP);                       /* req channel */
        sim_activate (uptr, (cdp_bptr & 1)? cdp_tleft: cdp_tright);
        break;

    case CDS_END:                                       /* end state */
        if (ch6_qconn (CH_A, U_CDP)) {                  /* if cdp still conn */
            cdp_sta = CDS_INIT;                         /* return to init */
            sim_activate (uptr, 1);                     /* next card */
            }
        break;
        }

return SCPE_OK;
}

/* Card end - write card image to file, transition to end state */

t_stat cdp_card_end (UNIT *uptr)
{
uint32 i, col, row, bufw, colbin;
const char *pch;
char bcd, cdp_cbuf[(2 * CD_CHRLNT) + 2];
t_uint64 dat;

if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return SCPE_UNATT;
if (uptr->flags & UNIT_PCA)
    pch = bcd_to_ascii_a;
else pch = bcd_to_ascii_h;
for (col = 0; col < ((2 * CD_CHRLNT) + 1); col++)
    cdp_cbuf[col] = ' ';                                /* clear char buf */
for (col = 0; col < 72; col++) {                        /* process 72 columns */
    colbin = 0;
    dat = bit_masks[35 - (col % 36)];                   /* mask for column */
    for (row = 0; row < 12; row++) {                    /* proc 12 rows */
        bufw = (row * 2) + (col / 36);                  /* index to buffer */
        if (cdp_bbuf[bufw] & dat)
            colbin |= col_masks[row];
        }
    if (cdp_unit.flags & UNIT_CBN) {                    /* column binary? */
        cdp_cbuf[2 * col] = pch[(colbin >> 6) & 077];
        cdp_cbuf[(2 * col) + 1] = pch[colbin & 077];
        }
    else {                                              /* text */
        bcd = colbin_to_bcd (colbin);                   /* column bin -> BCD */
        cdp_cbuf[col] = pch[bcd];                       /* -> ASCII */
        }
    }
for (i = ((2 * CD_CHRLNT) + 1); (i > 0) &&
    (cdp_cbuf[i - 1] == ' '); --i) ;                    /* trim spaces */
cdp_cbuf[i++] = '\n';                                   /* append nl */
cdp_cbuf[i++] = 0;                                      /* append nul */
fputs (cdp_cbuf, uptr->fileref);                        /* write card */
uptr->pos = ftell (uptr->fileref);                      /* update position */
if (ferror (uptr->fileref)) {                           /* error? */
    sim_perror ("CDP I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
cdp_sta = CDS_END;                                      /* end state */
sim_cancel (uptr);                                      /* cancel current */
sim_activate (uptr, cdp_tstop);                         /* long timer */
return SCPE_OK;
}

/* Card punch reset */

t_stat cdp_reset (DEVICE *dptr)
{
uint32 i;

for (i = 0; i < 24; i++)                                /* clear buffer */
    cdp_bbuf[i] = 0;
cdp_sta = 0;                                            /* clear state */
cdp_bptr = 0;                                           /* clear buf ptr */
cdp_chob = 0;
cdp_chob_v = 0;
sim_cancel (&cdp_unit);                                 /* stop punch */
return SCPE_OK;
}

/* Column binary to BCD

   This is based on documentation in the IBM 1620 manual and may not be
   accurate for the 7094.  Each row (12,11,0,1..9) is interpreted as a bit
   pattern, and the appropriate bits are set.  (Double punches inclusive
   OR, eg, 1,8,9 is 9.)  On the 1620, double punch errors are detected;
   since the 7094 only reads column binary, double punches are ignored.

   Bit order, left to right, is 12, 11, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9.
   The for loop works right to left, so the table is reversed. */

static const char row_val[12] = {
    011, 010, 007, 006, 005, 004,
    003, 002, 001, 020, 040, 060
    };

char colbin_to_bcd (uint32 cb)
{
uint32 i;
char bcd;

for (i = 0, bcd = 0; i < 12; i++) {                     /* 'sum' rows */
    if (cb & (1 << i))
        bcd |= row_val[i];
    }
return bcd;
}
