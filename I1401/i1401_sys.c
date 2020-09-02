/* i1401_sys.c: IBM 1401 simulator interface

   Copyright (c) 1993-2017, Robert M. Supnik

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

   13-Mar-17    RMS     Fixed possible dull dereference (COVERITY)
   25-Mar-14    RMS     Fixed d character printout (Van Snyder)
   25-Mar-12    RMS     Fixed && -> & in test (Peter Schorn)
   20-Sep-05    RMS     Revised for new code tables
   04-Jan-05    WVS     Added address argument support
   14-Nov-04    WVS     Added data printout support
   16-Mar-03    RMS     Fixed mnemonic for MCS
   03-Jun-02    RMS     Added 1311 support
   18-May-02    RMS     Added -D feature (Van Snyder)
   26-Jan-02    RMS     Fixed H, NOP with no trailing wm (Van Snyder)
   17-Sep-01    RMS     Removed multiconsole support
   13-Jul-01    RMS     Fixed bug in symbolic output (Peter Schorn)
   27-May-01    RMS     Added multiconsole support
   14-Mar-01    RMS     Revised load/dump interface (again)
   30-Oct-00    RMS     Added support for examine to file
   27-Oct-98    RMS     V2.4 load interface
*/

#include "i1401_defs.h"
#include <ctype.h>

#define LINE_LNT        80
extern DEVICE cpu_dev, inq_dev, lpt_dev;
extern DEVICE cdr_dev, cdp_dev, stack_dev;
extern DEVICE dp_dev, mt_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern uint8 M[];
extern int32 store_addr_h (int32 addr);
extern int32 store_addr_t (int32 addr);
extern int32 store_addr_u (int32 addr);
extern t_bool conv_old;

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             maximum number of words for examine/deposit
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "IBM 1401";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = LINE_LNT;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &inq_dev,
    &cdr_dev,
    &cdp_dev,
    &stack_dev,
    &lpt_dev,
    &mt_dev,
    &dp_dev,
    NULL
    };

const char *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "Unimplemented instruction",
    "Non-existent memory",
    "Non-existent device",
    "No WM at instruction start",
    "Invalid A address",
    "Invalid B address",
    "Invalid instruction length",
    "Invalid modifer",
    "Invalid branch address",
    "Breakpoint",
    "HALT instruction",
    "Invalid MT unit number",
    "Invalid MT record length",
    "Write to locked MT unit",  
    "Skip to unpunched CCT channel",
    "Card reader empty",
    "Address register wrap",
    "I/O check",
    "Invalid disk sector address",
    "Invalid disk sector count",
    "Invalid disk unit",
    "Invalid disk function",
    "Invalid disk record length",
    "Write track while disabled",
    "Write check error",
    "Disk address miscompare",
    "Direct seek cylinder exceeds maximum"
    };

/* Binary loader -- load carriage control tape

   A carriage control tape consists of entries of the form

        (repeat count) column number,column number,column number,...

   The CCT entries are stored in cct[0:lnt-1], cctlnt contains the
   number of entries
*/

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
int32 col, rpt, ptr, mask, cctbuf[CCT_LNT];
t_stat r;
extern int32 cctlnt, cctptr, cct[CCT_LNT];
char cbuf[CBUFSIZE], gbuf[CBUFSIZE];

if ((*cptr != 0) || (flag != 0))
    return SCPE_ARG;
ptr = 0;
for ( ; (cptr = fgets (cbuf, CBUFSIZE, fileref)) != NULL; ) { /* until eof */
    mask = 0;
    if (*cptr == '(') {                                 /* repeat count? */
        cptr = get_glyph (cptr + 1, gbuf, ')');         /* get 1st field */
        rpt = get_uint (gbuf, 10, CCT_LNT, &r);         /* repeat count */
        if (r != SCPE_OK)
            return SCPE_FMT;
        }
    else rpt = 1;
    while (*cptr != 0) {                                /* get col no's */
        cptr = get_glyph (cptr, gbuf, ',');             /* get next field */
        col = get_uint (gbuf, 10, 12, &r);              /* column number */
        if (r != SCPE_OK)
            return SCPE_FMT;
        mask = mask | (1 << col);                       /* set bit */
        }
    for ( ; rpt > 0; rpt--) {                           /* store vals */
        if (ptr >= CCT_LNT)
            return SCPE_FMT;
        cctbuf[ptr++] = mask;
        }
    }
if (ptr == 0)
    return SCPE_FMT;
cctlnt = ptr;
cctptr = 0;
for (rpt = 0; rpt < cctlnt; rpt++)
    cct[rpt] = cctbuf[rpt];
return SCPE_OK;
}

/* Symbol table */

const char *opcode[64] = {
 NULL,  "R",   "W",  "WR",  "P",   "RP",  "WP",  "WRP",
 "SRF", "SPF", NULL, "MA",  "MUL", NULL,  NULL,  NULL,
 NULL,  "CS",  "S",  NULL,  "MTF", "BWZ", "BBE", NULL,
 "MZ",  "MCS", NULL, "SWM", "DIV", NULL,  NULL,  NULL,
 NULL,  NULL,  "SS", "LCA", "MCW", "NOP", NULL,  "MCM",
 "SAR", NULL,  "ZS", NULL,  NULL,  NULL,  NULL,  NULL,
 NULL,  "A",   "B",  "C",   "MN",  "MCE", "CC",  NULL,
 "SBR", NULL,  "ZA", "H",   "CWM", NULL,  NULL,  NULL
 };

/* Print an address from three characters */

void fprint_addr (FILE *of, t_value *dig)
{
int32 addr, xa;

addr = hun_table[dig[0] & CHAR] + ten_table[dig[1]] + one_table[dig[2]];
xa = (addr >> V_INDEX) & M_INDEX;
if (xa)
    fprintf (of, " %d,%d", addr & ADDRMASK, ((xa - (X1 >> V_INDEX)) / 5) + 1);
else if (addr >= MAXMEMSIZE)
    fprintf (of, " %d*", addr & ADDRMASK);
else fprintf (of, " %d", addr);
return;
}

/* Print unknown opcode as data */

t_stat dcw (FILE *of, int32 op, t_value *val, int32 sw)
{
int32 i;
t_bool use_h = sw & SWMASK ('F');

fprintf (of, "DCW @%c", bcd2ascii (op, use_h));         /* assume it's data */
for (i = 1; i < sim_emax; i++) {
    if (val[i] & WM)
        break;
    fprintf (of, "%c", bcd2ascii (val[i], use_h));
    }
fprintf (of, "@");
return -(i - 1);                                        /* return # chars */
}

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current address
        *val    =       values to decode
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        return  =       if >= 0, error code
                        if < 0, number of extra words retired
*/

#define FMTASC(x) ((x) < 040)? "<%03o>": "%c", (x)

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 op, flags, ilnt, i, t;
int32 wmch = conv_old? '~': '`';
t_bool use_h = sw & SWMASK ('F');

if (sw & SWMASK ('C')) {                                /* character? */
    t = val[0];
    if (uptr->flags & UNIT_BCD) {
        if (t & WM)
            fputc (wmch, of);
        fputc (bcd2ascii (t & CHAR, use_h), of);
        }
    else fprintf (of, FMTASC (t & 0177));
    return SCPE_OK;
    }
if ((uptr != NULL) && (uptr != &cpu_unit))              /* CPU? */
     return SCPE_ARG;
if (sw & SWMASK ('D')) {                                /* dump? */
    for (i = 0; i < 50; i++)
        fprintf (of, "%c", bcd2ascii (val[i] & CHAR, use_h)) ;
    fprintf (of, "\n\t");
    for (i = 0; i < 50; i++)
        fprintf (of, (val[i] & WM)? "1": " ") ;
    return -(i - 1);
    }
if (sw & SWMASK ('S')) {                                /* string? */
    i = 0;
    do {
        t = val[i++];
        if (t & WM)
            fputc (wmch, of);
        fputc (bcd2ascii (t & CHAR, use_h), of);
        } while ((i < LINE_LNT) && ((val[i] & WM) == 0));
    return -(i - 1);
    }
if ((sw & SWMASK ('M')) == 0)
    return SCPE_ARG;

if ((val[0] & WM) == 0)                                 /* WM under op? */
    return STOP_NOWM;
op = val[0]& CHAR;                                      /* isolate op */
if (opcode[op] == NULL)                                 /* invalid op */
    return dcw (of, op, val, sw);
flags = op_table[op];                                   /* get flags */
for (ilnt = 1; ilnt < sim_emax; ilnt++) {               /* find inst lnt */
    if (val[ilnt] & WM)
        break;
    }
if ((flags & (NOWM | HNOP)) && (ilnt > 7))              /* cs, swm, h, nop? */
    ilnt = 7;
else if ((op == OP_B) && (ilnt > 4) && (val[4] == BCD_BLANK))
    ilnt = 4;
if (ilnt == 3) {                                        /* lnt = 3? */
    fprintf (of, "DSA");                                /* assume DSA */
    fprint_addr (of, val);                              /* print addr */
    return -(ilnt - 1);
    }
if ((((flags & len_table[(ilnt > 8)? 8: ilnt]) == 0) && /* invalid lnt, */
    (op != OP_NOP)) ||                                  /* not nop? */
    (opcode[op] == NULL))                               /* or undef? */
    return dcw (of, op, val, sw);
fprintf (of, "%s",opcode[op]);                          /* print opcode */
if (ilnt > 2) {                                         /* A address? */
    if (((flags & IO) || (op == OP_NOP)) && (val[1] == BCD_PERCNT))
        fprintf (of, " %%%c%c", bcd2ascii (val[2], use_h),
            bcd2ascii (val[3], sw));
    else fprint_addr (of, &val[1]);
    }
if (ilnt > 5)                                           /* B address? */
    fprint_addr (of, &val[4]);
if ((ilnt == 2) || (ilnt == 5) || (ilnt >= 8))          /* d character? */
    fprintf (of, " '%c", bcd2ascii (val[ilnt - 1], use_h));
return -(ilnt - 1);                                     /* return # chars */
}

/* get_addr - get address + index pair */

t_stat get_addr (const char *cptr, t_value *val)
{
int32 addr, index;
t_stat r;
char gbuf[CBUFSIZE];

cptr = get_glyph (cptr, gbuf, ',');                     /* get address */
addr = get_uint (gbuf, 10, MAXMEMSIZE, &r);
if (r != SCPE_OK)
    return SCPE_ARG;
if (*cptr != 0) {                                       /* more? */
    cptr = get_glyph (cptr, gbuf, ' ');
    index = get_uint (gbuf, 10, 3, &r);
    if ((r != SCPE_OK) || (index == 0))
        return SCPE_ARG;
    }
else index = 0;
if (*cptr != 0)
    return SCPE_ARG;
val[0] = store_addr_h (addr);
val[1] = store_addr_t (addr) | (index << V_ZONE);
val[2] = store_addr_u (addr);
return SCPE_OK;
}

/* get_io - get I/O address */

t_stat get_io (char *cptr, t_value *val)
{
if ((cptr[0] != '%') || (cptr[3] != 0) ||
    !isalnum (cptr[1]) || !isalnum (cptr[2]))
    return SCPE_ARG;
val[0] = BCD_PERCNT;
val[1] = ascii2bcd (cptr[1]);
val[2] = ascii2bcd (cptr[2]);
return SCPE_OK;
}

/* Symbolic input

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        *uptr   =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       > 0   error code
                        <= 0  -number of extra words
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 i, op, ilnt, t, cflag, wm_seen;
int32 wmch = conv_old? '~': '`';
char gbuf[CBUFSIZE];

if (uptr == NULL)
    uptr = &cpu_unit;
cflag = (uptr == &cpu_unit);                            /* CPU flag */
while (isspace (*cptr))                                 /* absorb spaces */
    cptr++;
if ((sw & SWMASK ('C')) || (sw & SWMASK ('S')) || (*cptr == wmch) ||
    ((*cptr == '\'') && cptr++) || ((*cptr == '"') && cptr++)) {
        wm_seen = 0;
        for (i = 0; (i < sim_emax) && (*cptr != 0); ) {
            t = *cptr++;                                /* get character */
            if (cflag && (wm_seen == 0) && (t == wmch))
                wm_seen = WM;
            else if (uptr->flags & UNIT_BCD) {
                if (t < 040)
                    return SCPE_ARG;
                val[i++] = ascii2bcd (t) | wm_seen;
                wm_seen = 0;
                }
            else val[i++] = t;
            }
        if ((i == 0) || wm_seen)
            return SCPE_ARG;
        return -(i - 1);
        }

if (cflag == 0)                                         /* CPU only */
    return SCPE_ARG;
cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
for (op = 0; op < 64; op++) {                           /* look it up */
    if (opcode[op] && strcmp (gbuf, opcode[op]) == 0)
        break;
    }
if (op >= 64)                                           /* successful? */
    return SCPE_ARG;
val[0] = op | WM;                                       /* store opcode */
cptr = get_glyph (cptr, gbuf, 0);                       /* get addr or d */
if (((op_table[op] & IO) && (get_io (gbuf, &val[1]) == SCPE_OK)) ||
     (get_addr (gbuf, &val[1]) == SCPE_OK)) {
        cptr = get_glyph (cptr, gbuf, 0);               /* get addr or d */
        if (get_addr (gbuf, &val[4]) == SCPE_OK) {
            cptr = get_glyph (cptr, gbuf, ',');         /* get d */
            ilnt = 7;                                   /* a and b addresses */
            }
        else ilnt = 4;                                  /* a address */
        }
else ilnt = 1;                                          /* no addresses */
if ((gbuf[0] == '\'') || (gbuf[0] == '"')) {            /* d character? */
    t = gbuf[1];
    if ((gbuf[2] != 0) || (*cptr != 0) || (t < 040))
        return SCPE_ARG;                                /* end and legal? */
    val[ilnt] = ascii2bcd (t);                          /* save D char */
    ilnt = ilnt + 1;
    }
else if (gbuf[0] != 0)                                  /* not done? */
    return SCPE_ARG;
if ((op_table[op] & len_table[ilnt]) == 0)
    return STOP_INVL;
return -(ilnt - 1);
}

/* Convert BCD to ASCII */

int32 bcd2ascii (int32 c, t_bool use_h)
{
if (conv_old)
    return bcd_to_ascii_old[c];
else if (use_h)
    return bcd_to_ascii_h[c];
else return bcd_to_ascii_a[c];
}

/* Convert ASCII to BCD */

int32 ascii2bcd (int32 c)
{
if (conv_old)
    return ascii_to_bcd_old[c];
else return ascii_to_bcd[c];
}
