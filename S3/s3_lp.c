/* s3_lp.c: IBM 1403 line printer simulator

   Copyright (c) 2001-2012, Charles E. Owen

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

   Except as contained in this notice, the name of Charles E. Owen shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Charles E. Owen.

   lpt          1403 line printer

   19-Mar-12    RMS     Fixed declaration of conversion tables (Mark Pizzolato)
   25-Apr-03    RMS     Revised for extended file support
   08-Oct-02    RMS     Added impossible function catcher
*/

#include "s3_defs.h"

extern uint8 M[];
extern char bcd_to_ascii[64];
extern int32 iochk, ind[64];
int32 cct[CCT_LNT] = { 03 };
int32 cctlnt = 66, cctptr = 0, lines = 0, lflag = 0;
t_stat lpt_reset (DEVICE *dptr);
t_stat lpt_attach (UNIT *uptr, CONST char *cptr);
t_stat write_line (int32 ilnt, int32 mod);
t_stat space (int32 lines, int32 lflag);
t_stat carriage_control (int32 action, int32 mod);
extern unsigned char ebcdic_to_ascii[];

#define UNIT_V_PCHAIN   (UNIT_V_UF + 0)
#define UNIT_M_PCHAIN   03
#define M_UCS           00                              /* Universal */
#define M_PCF           00                              /* full */
#define M_PCA           01                              /* business */
#define M_PCH           02                              /* Fortran */
#define UNIT_PCHAIN     (UNIT_M_PCHAIN << UNIT_V_PCHAIN)
#define UCS             (M_UCS << UNIT_V_PCHAIN)
#define PCF             (M_PCF << UNIT_V_PCHAIN)
#define PCA             (M_PCA << UNIT_V_PCHAIN)
#define PCH             (M_PCH << UNIT_V_PCHAIN)
#define GET_PCHAIN(x)   (((x) >> UNIT_V_PCHAIN) & UNIT_M_PCHAIN)
#define CHP(ch,val)     ((val) & (1 << (ch)))

int32 LPDAR;                                            /* Data Address */
int32 LPFLR;                                            /* Forms Length */
int32 LPIAR;                                            /* Image address */
int32 linectr;                                          /* current line # */
int32 lpterror = 0;
int32 CC9 = 0;
int32 CC12 = 0;

/* LPT data structures

   lpt_dev      LPT device descriptor
   lpt_unit     LPT unit descriptor
   lpt_reg      LPT register list
*/

UNIT lpt_unit = { UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0) };

REG lpt_reg[] = {
    { FLDATA (ERR, lpterror, 0) },
    { HRDATA (LPDAR, LPDAR, 16) },
    { HRDATA (LPFLR, LPFLR, 8) },
    { HRDATA (LPIAR, LPIAR, 16) },
    { DRDATA (LINECT, linectr, 8) },
    { DRDATA (POS, lpt_unit.pos, T_ADDR_W), PV_LEFT },
    { BRDATA (CCT, cct, 8, 32, CCT_LNT) },
    { DRDATA (LINES, lines, 8), PV_LEFT },
    { DRDATA (CCTP, cctptr, 8), PV_LEFT },
    { DRDATA (CCTL, cctlnt, 8), REG_RO + PV_LEFT },
    { GRDATA (CHAIN, lpt_unit.flags, 10, 2, UNIT_V_PCHAIN), REG_HRO },
    { NULL }
};

MTAB lpt_mod[] = {
    { UNIT_PCHAIN, UCS, "UCS", "UCS", NULL },
    { UNIT_PCHAIN, PCA, "A chain", "PCA", NULL },
    { UNIT_PCHAIN, PCH, "H chain", "PCH", NULL },
    { 0 }
};

DEVICE lpt_dev = {
    "LPT", &lpt_unit, lpt_reg, lpt_mod,
    1, 10, 31, 1, 8, 7,
    NULL, NULL, &lpt_reset,
    NULL, NULL, NULL
};


/* -------------------------------------------------------------------- */

/* Printer: master routine */

int32 lpt (int32 op, int32 m, int32 n, int32 data)
{
    int32 iodata;
    switch (op) {
        case 0:                                         /* SIO 1403 */
            iodata = 0;
//            printf("\0");
            switch (n) {
                case 0x00:                              /* Spacing only */
                    if (data > 0 && data < 4)
                        iodata = carriage_control(2, data);
                    break;
                case 0x02:                              /* Print & space */
                    iodata = write_line(0, 0);
                    if (data > 3) data = 0;
                    if (iodata == SCPE_OK)
                        iodata = carriage_control(2, data);
                    break;
                case 0x04:                              /* Skip only */
                    iodata = carriage_control(4, data);
                    break;
                case 0x06:                              /* Print and skip */
                    iodata = write_line(0, 0);
                    if (iodata == SCPE_OK)
                        iodata = carriage_control(4, data);
                    break;
                default:        
                    return STOP_INVDEV;
            }                   
            return iodata;
        case 1:                                         /* LIO 1403 */
            switch (n) {
                case 0x00:                              /* LPFLR */
                    LPFLR = (data >> 8) & 0xff;
                    break;
                case 0x04:
                    LPIAR = data & 0xffff;
                    break;
                case 0x06:
                    LPDAR = data & 0xffff;
                    break;
                default:
                    return STOP_INVDEV;
            }                   
            return SCPE_OK;
        case 2:                                         /* TIO 1403 */
            iodata = 0;
            switch (n) {
                case 0x00:                              /* Not ready/check */
                    if (lpterror)
                        iodata = 1;
                    if ((lpt_unit.flags & UNIT_ATT) == 0) 
                        iodata = 1; 
                    break;
                case 0x02:                              /* Buffer Busy */
                    iodata = 0;
                    break;
                case 0x04:                              /* Carriage Busy */
                    iodata = 0;
                    break;
                case 0x06:                              /* Printer busy */
                    iodata = 0;
                    break;
                default:
                    return (STOP_INVDEV << 16);             
            }       
            return ((SCPE_OK << 16) | iodata);          
        case 3:                                         /* SNS 1403 */
            switch (n) {
                case 0x00:                              /* Line count */
                    iodata = (linectr << 8);
                    break;
                case 0x02:                              /* Timing data */
                    iodata = 0;
                    break;
                case 0x03:                              /* Check data */
                    iodata = 0;
                    break;
                case 0x04:                              /* LPIAR */
                    iodata = LPIAR;
                    break;
                case 0x06:                              /* LPDAR */
                    iodata = LPDAR; 
                    break;
                default:
                    return (STOP_INVDEV << 16);
            }           
            return ((SCPE_OK << 16) | iodata);          
        case 4:                                         /* APL 1403 */
            iodata = 0;
            return ((SCPE_OK << 16) | iodata);          
        default:
            break;
    }                       
    sim_printf (">>LPT non-existent function %d\n", op);
    return SCPE_OK;                     
}


/* Print routine

   Modifiers have been checked by the caller
        S       =       suppress automatic newline
*/

t_stat write_line (int32 ilnt, int32 mod)
{
int32 i, t, lc;
static char lbuf[LPT_WIDTH + 1];                        /* + null */

if ((lpt_unit.flags & UNIT_ATT) == 0)
     return SCPE_UNATT; 

lpterror = 0;
lc = LPDAR;                                             /* clear error */
for (i = 0; i < LPT_WIDTH; i++) {                       /* convert print buf */
    t = M[lc];
    lbuf[i] = ebcdic_to_ascii[t & 0xff];
    M[lc] = 0x40;                                       /* HJS MOD */
    lc++;
}
for (i = LPT_WIDTH - 1; (i >= 0) && (lbuf[i] == ' '); i--) lbuf[i] = 0;
fputs (lbuf, lpt_unit.fileref);                         /* write line */
if (lines) space (lines, lflag);                        /* cc action? do it */
else if (mod == 0) space (1, FALSE);                    /* default? 1 line */
else {
    fputc ('\r', lpt_unit.fileref);                     /* sup -> overprint */
    lpt_unit.pos = ftell (lpt_unit.fileref);            /* update position */
}
lines = lflag = 0;                                      /* clear cc action */
if (ferror (lpt_unit.fileref)) {                        /* error? */
    sim_perror ("Line printer I/O error");
    clearerr (lpt_unit.fileref);
    lpterror = 1;
}
return SCPE_OK;
}

/* Carriage control routine

   Parameters:
        action  =       00, skip to channel now
                =       01, space lines after
                =       02, space lines now
                =       03, skip to channel after
                =       04, skip to line number
        mod     =       number of lines or channel number or line number
*/

t_stat carriage_control (int32 action, int32 mod)
{
int32 i;

if ((lpt_unit.flags & UNIT_ATT) == 0)
     return SCPE_UNATT; 

switch (action) {
case 0:                                                 /* to channel now */
    if ((mod == 0) || (mod > 12) || CHP (mod, cct[cctptr])) return SCPE_OK;
    for (i = 1; i < cctlnt + 1; i++) {                  /* sweep thru cct */
        if (CHP (mod, cct[(cctptr + i) % cctlnt]))
            return space (i, TRUE);
    }
    return STOP_INVDEV;                                 /* runaway channel */
case 1:                                                 /* space after */
    if (mod <= 3) {
        lines = mod;                                    /* save # lines */
        lflag = FALSE;                                  /* flag spacing */
        CC9 = CC12 = 0;
    }
    return SCPE_OK;
case 2:                                                 /* space now */
    if (mod <= 3) return space (mod, FALSE);
    return SCPE_OK;
case 3:                                                 /* to channel after */
    if ((mod == 0) || (mod > 12)) return SCPE_OK;       /* check channel */
    CC9 = CC12 = 0;
    for (i = 1; i < cctlnt + 1; i++) {                  /* sweep thru cct */
        if (CHP (mod, cct[(cctptr + i) % cctlnt])) {
            lines = i;                                  /* save # lines */
            lflag = TRUE;                               /* flag skipping */
            return SCPE_OK;
        }
    }
    return STOP_INVDEV;     
case 4:                                                 /* To line # */
    if (mod < 2) {
        fputs ("\n\f", lpt_unit.fileref);               /* nl, ff */
        linectr = 1;
    } else {
        if (mod <= linectr) {
            fputs ("\n\f", lpt_unit.fileref);
            linectr = 1;
        }   
        while (1) {
            if (linectr == mod)
                break;
            space(1, 0);
        }       
    }   
    return SCPE_OK;
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

if ((lpt_unit.flags & UNIT_ATT) == 0) return SCPE_UNATT;
cctptr = (cctptr + count) % cctlnt;                     /* adv cct, mod lnt */
if (sflag && CHP (0, cct[cctptr])) {                    /* skip, top of form? */
    fputs ("\n\f", lpt_unit.fileref);                   /* nl, ff */
    linectr = 1;
} else {
    for (i = 0; i < count; i++) fputc ('\n', lpt_unit.fileref);
}
lpt_unit.pos = ftell (lpt_unit.fileref);                /* update position */
CC9 = CHP (9, cct[cctptr]) != 0;                        /* set indicators */
CC12 = CHP (12, cct[cctptr]) != 0;
linectr += count;
if (linectr > LPFLR) 
    linectr -= LPFLR;
return SCPE_OK;
}

/* Reset routine */

t_stat lpt_reset (DEVICE *dptr)
{
cctptr = 0;                                             /* clear cct ptr */
lines = linectr = lflag = 0;                            /* no cc action */
lpterror = 0;
return SCPE_OK;
}

/* Attach routine */

t_stat lpt_attach (UNIT *uptr, CONST char *cptr)
{
cctptr = 0;                                             /* clear cct ptr */
lines = 0;                                              /* no cc action */
lpterror = 0;
linectr = 0;
return attach_unit (uptr, cptr);
}
