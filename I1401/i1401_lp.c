/* i1401_lp.c: IBM 1403 line printer simulator

   Copyright (c) 1993-2015, Robert M. Supnik

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

   lpt          1403 line printer

   08-Mar-15    RMS     Added print to console option
   16-Apr-13    RMS     Fixed printer chain selection
   19-Jan-07    RMS     Added UNIT_TEXT flag
   07-Mar-05    RMS     Fixed bug in write_line (Van Snyder)
   25-Apr-03    RMS     Revised for extended file support
   30-May-02    RMS     Widened POS to 32b
   13-Apr-01    RMS     Revised for register arrays
*/

#include "i1401_defs.h"

extern uint8 M[];
extern int32 iochk, ind[64];
extern t_bool conv_old;

int32 cct[CCT_LNT] = { 03 };
int32 cctlnt = 66, cctptr = 0, lines = 0, lflag = 0;

t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_attach (UNIT *uptr, CONST char *cptr);
t_stat space (int32 lines, int32 lflag);
t_stat lpt_puts (const char *buf);

extern void inq_puts (const char *buf);

const char *pch_table_old[4] = {
    bcd_to_ascii_old, bcd_to_ascii_old, bcd_to_pca, bcd_to_pch 
    };
const char *pch_table[4] = {
    bcd_to_ascii_a, bcd_to_ascii_h, bcd_to_pca, bcd_to_pch
    };

#define UNIT_V_FT       (UNIT_V_UF + 0)
#define UNIT_V_48       (UNIT_V_UF + 1)
#define UNIT_V_CONS     (UNIT_V_UF + 2)
#define UNIT_FT         (1 << UNIT_V_FT)
#define UNIT_48         (1 << UNIT_V_48)
#define UNIT_CONS       (1 << UNIT_V_CONS)
#define GET_PCHAIN(x)   (((x) >> UNIT_V_FT) & 03)
#define CHP(ch,val)     ((val) & (1 << (ch)))

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

UNIT lpt_unit = {
    UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE+UNIT_TEXT, 0)
    };

REG lpt_reg[] = {
    { FLDATA (ERR, ind[IN_LPT], 0) },
    { DRDATA (POS, lpt_unit.pos, T_ADDR_W), PV_LEFT },
    { BRDATA (CCT, cct, 8, 32, CCT_LNT) },
    { DRDATA (LINES, lines, 8), PV_LEFT },
    { DRDATA (CCTP, cctptr, 8), PV_LEFT },
    { DRDATA (CCTL, cctlnt, 8), REG_RO + PV_LEFT },
    { NULL }
    };

MTAB lpt_mod[] = {
    { UNIT_48, UNIT_48, "48 character chain", "48" },
    { UNIT_48, 0,       "64 character chain", "64" },
    { UNIT_FT, UNIT_FT, "Fortran set", "FORTRAN" },
    { UNIT_FT, 0,       "business set", "BUSINESS" },
    { UNIT_CONS, UNIT_CONS, "default to console", "DEFAULT" },
    { UNIT_CONS, 0        , "no default device", "NODEFAULT" },
    { UNIT_FT|UNIT_48, 0,               NULL, "PCF" },  /* obsolete */
    { UNIT_FT|UNIT_48, UNIT_48,         NULL, "PCA" },
    { UNIT_FT|UNIT_48, UNIT_FT|UNIT_48, NULL, "PCH" },
    { 0 }
    };

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &lpt_reset,
    NULL, &lpt_attach, NULL
    };

/* Print routine

   Modifiers have been checked by the caller
        SQUARE  =       word mark mode
        S       =       suppress automatic newline
*/

t_stat write_line (int32 ilnt, int32 mod)
{
int32 i, t, wm, sup;
const char *bcd2asc;
t_stat r;
static char lbuf[LPT_WIDTH + 1];                        /* + null */

wm = ((ilnt == 2) || (ilnt == 5)) && (mod == BCD_SQUARE);
sup = ((ilnt == 2) || (ilnt == 5)) && (mod == BCD_S);
ind[IN_LPT] = 0;                                        /* clear error */
t = GET_PCHAIN (lpt_unit.flags);
if (conv_old)                                           /* get print chain */
    bcd2asc = pch_table_old[t];
else bcd2asc = pch_table[t];
for (i = 0; i < LPT_WIDTH; i++) {                       /* convert print buf */
    t = M[LPT_BUF + i];
    if (wm)                                             /* wmarks -> 1 or sp */
        lbuf[i] = (t & WM)? '1': ' ';
    else lbuf[i] = bcd2asc[t & CHAR];                   /* normal */
    }
lbuf[LPT_WIDTH] = 0;                                    /* trailing null */
for (i = LPT_WIDTH - 1; (i >= 0) && (lbuf[i] == ' '); i--)
    lbuf[i] = 0;
if ((r = lpt_puts (lbuf)) != SCPE_OK)                   /* write line */
    return r;                                           /* error? */
if (lines)                                              /* cc action? do it */
    r = space (lines, lflag); 
else if (sup == 0)                                      /* default? 1 line */
    r = space (1, FALSE);
else r = lpt_puts ("\r");                               /* sup -> overprint */
lines = lflag = 0;                                      /* clear cc action */
return r;
}

/* Carriage control routine

   The modifier has not been checked, its format is
        <5:4>   =       00, skip to channel now
                =       01, space lines after
                =       10, space lines now
                =       11, skip to channel after
        <3:0>   =       number of lines or channel number
*/

t_stat carriage_control (int32 mod)
{
int32 i, action;

action = (mod & ZONE) >> V_ZONE;                        /* get mod type */
mod = mod & DIGIT;                                      /* isolate value */

switch (action) {

    case 0:                                             /* to channel now */
        if ((mod == 0) || (mod > 12) || CHP (mod, cct[cctptr]))
            return SCPE_OK;
        for (i = 1; i < cctlnt + 1; i++) {              /* sweep thru cct */
            if (CHP (mod, cct[(cctptr + i) % cctlnt]))
                return space (i, TRUE);
            }
        return STOP_CCT;                                /* runaway channel */

    case 1:                                             /* space after */
        if (mod <= 3) {
            lines = mod;                                /* save # lines */
            lflag = FALSE;                              /* flag spacing */
            ind[IN_CC9] = ind[IN_CC12] = 0;
            }
        return SCPE_OK;

    case 2:                                             /* space now */
        if (mod <= 3)
            return space (mod, FALSE);
        return SCPE_OK;

    case 3:                                             /* to channel after */
        if ((mod == 0) || (mod > 12))                   /* check channel */
            return SCPE_OK;
        ind[IN_CC9] = ind[IN_CC12] = 0;
        for (i = 1; i < cctlnt + 1; i++) {              /* sweep thru cct */
            if (CHP (mod, cct[(cctptr + i) % cctlnt])) {
                lines = i;                              /* save # lines */
                lflag = TRUE;                           /* flag skipping */
                return SCPE_OK;
                }
            }
        return STOP_CCT;                                /* runaway channel */
        }

return SCPE_OK;
}

/* Space routine - space or skip n lines
   
   Inputs:
        count   =       number of lines to space or skip
        sflag   =       skip (TRUE) or space (FALSE)
*/

t_stat space (int32 count, int32 sflag)
{
int32 i;
t_stat r;

cctptr = (cctptr + count) % cctlnt;                     /* adv cct, mod lnt */
if (sflag && CHP (0, cct[cctptr]))                      /* skip, top of form? */
    r = lpt_puts ("\n\f");                              /* nl, ff */
else {
    for (i = 0; (i < count); i++)
        if ((r = lpt_puts ("\n")) != SCPE_OK)
            break;
    }
ind[IN_CC9] = CHP (9, cct[cctptr]) != 0;                /* set indicators */
ind[IN_CC12] = CHP (12, cct[cctptr]) != 0;
return r;
}


/* Centralized string print routine
   Prints to either a file or the console

   Note that if printing to the console, newline must be converted to crlf */

t_stat lpt_puts (const char *buf)
{
if ((lpt_unit.flags & UNIT_ATT) != 0) {                 /* attached? */
    fputs (buf, lpt_unit.fileref);                      /* print string */
    if (ferror (lpt_unit.fileref)) {                    /* error? */
        ind[IN_LPT] = 1;
        sim_perror ("Line printer I/O error");
        clearerr (lpt_unit.fileref);
        if (iochk)
            return SCPE_IOERR;
        }
    lpt_unit.pos = ftell (lpt_unit.fileref);            /* update position */
    return SCPE_OK;
    }
if ((lpt_unit.flags & UNIT_CONS) != 0) {               /* default to cons? */
    if (buf[0] == '\n') {                              /* bare lf? */
        inq_puts ("\r");                               /* cvt to crlf */
        lpt_unit.pos = lpt_unit.pos + 1;
        }
    inq_puts (buf);
    lpt_unit.pos = lpt_unit.pos + strlen (buf);
    return SCPE_OK;
    }
return SCPE_UNATT;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
cctptr = 0;                                             /* clear cct ptr */
lines = lflag = 0;                                      /* no cc action */
ind[IN_LPT] = 0;
return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, CONST char *cptr)
{
cctptr = 0;                                             /* clear cct ptr */
lines = 0;                                              /* no cc action */
ind[IN_LPT] = 0;
return attach_unit (uptr, cptr);
}
