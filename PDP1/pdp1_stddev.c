/* pdp1_stddev.c: PDP-1 standard devices

   Copyright (c) 1993-2020, Robert M. Supnik

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

   ptr          paper tape reader
   ptp          paper tape punch
   tti          keyboard
   tto          teleprinter

   21-Mar-20    RMS     Generalized PTR EOL and EOF handling
   13-Jul-16    RMS     Added Expensive Typewriter ribbon color support
   18-May-16    RMS     Added FIODEC-to-ASCII mode for paper tape punch
   28-Mar-15    RMS     Revised to use sim_printf
   21-Mar-12    RMS     Fixed unitialized variable in tto_svc (Michael Bloom)
   21-Dec-06    RMS     Added 16-channel sequence break support
   29-Oct-03    RMS     Added PTR FIODEC-to-ASCII translation (Phil Budne)
   07-Sep-03    RMS     Changed ioc to ios
   30-Aug-03    RMS     Revised PTR to conform to Maintenance Manual;
                        added deadlock prevention on errors
   23-Jul-03    RMS     Revised to detect I/O wait hang
   25-Apr-03    RMS     Revised for extended file support
   22-Dec-02    RMS     Added break support
   29-Nov-02    RMS     Fixed output flag initialization (Derek Peschel)
   21-Nov-02    RMS     Changed typewriter to half duplex (Derek Peschel)
   06-Oct-02    RMS     Revised for V2.10
   30-May-02    RMS     Widened POS to 32b
   29-Nov-01    RMS     Added read only unit support
   07-Sep-01    RMS     Moved function prototypes
   10-Jun-01    RMS     Fixed comment
   30-Oct-00    RMS     Standardized device naming

   Note: PTP timeout must be >10X faster that TTY output timeout for Macro
   to work correctly!
*/

#include "pdp1_defs.h"
#include "sim_tmxr.h"

#define FIODEC_SPACE    000                             /* space */
#define FIODEC_STOP     013                             /* stop code */
#define FIODEC_BLACK    034                             /* TTY black ribbon */
#define FIODEC_RED      035                             /* TTY red ribbon */
#define FIODEC_UC       074                             /* upper case */
#define FIODEC_LC       072                             /* lower case */
#define FIODEC_CR       077                             /* carriage return */

#define UC_V            6                               /* upper case */
#define UC              (1 << UC_V)
#define BOTH            (1 << (UC_V + 1))               /* both cases */
#define CW              (1 << (UC_V + 2))               /* char waiting */
#define TT_WIDTH        077
#define UNIT_V_ASCII    (UNIT_V_UF + 0)                 /* ASCII/binary mode */
#define UNIT_ASCII      (1 << UNIT_V_ASCII)
#define UNIT_V_ET       (UNIT_V_UF + 1)                 /* expensive typewriter mode */
#define UNIT_ET         (1 << UNIT_V_ET)
#define PTR_LEADER      20                              /* ASCII leader chars */

int32 ptr_state = 0;
int32 ptr_wait = 0;
int32 ptr_stopioe = 0;
int32 ptr_uc = 0;                                       /* upper/lower case */
int32 ptp_uc = 0;
int32 ptr_hold = 0;                                     /* holding buffer */
int32 ptr_leader = PTR_LEADER;                          /* leader count */
int32 ptr_last = 0;                                     /* prev character*/
int32 ptr_sbs = 0;                                      /* SBS level */
int32 ptp_stopioe = 0;
int32 ptp_sbs = 0;                                      /* SBS level */
int32 tti_hold = 0;                                     /* tti hold buf */
int32 tti_sbs = 0;                                      /* SBS level */
int32 tty_buf = 0;                                      /* tty buffer */
int32 tty_uc = 0;                                       /* tty uc/lc */
int32 tty_ribbon = FIODEC_BLACK;                        /* ribbon color */
int32 tto_sbs = 0;

extern int32 ios, ioh, cpls, iosta;
extern int32 PF, IO, PC, TA;
extern int32 M[];

int ptr_get_ascii (UNIT *uptr);
t_stat ptr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat tty_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);
t_stat ptr_attach (UNIT *uptr, CONST char *cptr);
t_stat ptp_attach (UNIT *uptr, CONST char *cptr);
t_stat ptr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
t_stat ptp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *ptr_description (DEVICE *dptr);
const char *ptp_description (DEVICE *dptr);

/* Character translation tables */

int32 fiodec_to_ascii[128] = {
    ' ', '1', '2', '3', '4', '5', '6', '7',             /* lower case */
    '8', '9', 0, '\f', 0, 0, 0, 0,
    '0', '/', 's', 't', 'u', 'v', 'w', 'x',
    'y', 'z', 0, ',', 0, 0, '\t', 0,
    '@', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
    'q', 'r', 0, 0, '-', ')', '\\', '(',
    0, 'a', 'b', 'c', 'd', 'e', 'f', 'g',
    'h', 'i', 0, '.', 0, '\b', 0, '\n',
    ' ', '"', '\'', '~', '#', '!', '&', '<',            /* upper case */
    '>', '^', 0, 0, 0, 0, 0, 0,
    '`', '?', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 0, '=', 0, 0, '\t', 0,
    '_', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 0, 0, '+', ']', '|', '[',
    0, 'A', 'B', 'C', 'D', 'E', 'F', 'G',
    'H', 'I', 0, '*', 0, '\b', 0, '\n'
    };

int32 ascii_to_fiodec[128] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    BOTH+075, BOTH+036, BOTH+FIODEC_CR, 0, BOTH+FIODEC_STOP, BOTH+FIODEC_CR, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    BOTH+FIODEC_SPACE, UC+005, UC+001, UC+004, 0, 0, UC+006, UC+002,
    057, 055, UC+073, UC+054, 033, 054, 073, 021,
    020, 001, 002, 003, 004, 005, 006, 007,
    010, 011, 0, 0, UC+007, UC+033, UC+010, UC+021,
    040, UC+061, UC+062, UC+063, UC+064, UC+065, UC+066, UC+067,
    UC+070, UC+071, UC+041, UC+042, UC+043, UC+044, UC+045, UC+046,
    UC+047, UC+050, UC+051, UC+022, UC+023, UC+024, UC+025, UC+026,
    UC+027, UC+030, UC+031, UC+057, 056, UC+055, UC+011, UC+040,
    UC+020, 061, 062, 063, 064, 065, 066, 067,
    070, 071, 041, 042, 043, 044, 045, 046,
    047, 050, 051, 022, 023, 024, 025, 026,
    027, 030, 031, 0, UC+056, 0, UC+003, BOTH+075
    };

/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit
   ptr_reg      PTR register list
*/

UNIT ptr_unit = {
    UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
           SERIAL_IN_WAIT
    };

REG ptr_reg[] = {
    { ORDATAD (BUF, ptr_unit.buf, 18, "last data item processed") },
    { FLDATAD (UC, ptr_uc, UC_V, "upper case/lower case state (shared)") },
    { FLDATAD (DONE, iosta, IOS_V_PTR, "device done flag") },
    { FLDATAD (RPLS, cpls, CPLS_V_PTR, "return restart pulse flag") },
    { ORDATA (HOLD, ptr_hold, 9), REG_HRO },
    { ORDATA (LAST, ptr_last, 8), REG_HRO },
    { ORDATA (STATE, ptr_state, 5), REG_HRO },
    { FLDATA (WAIT, ptr_wait, 0), REG_HRO },
    { DRDATAD (POS, ptr_unit.pos, T_ADDR_W, "position in the input file"), PV_LEFT },
    { DRDATAD (TIME, ptr_unit.wait, 24, "time from I/O initiation to interrupt"), PV_LEFT },
    { DRDATA (LEADER, ptr_leader, 6), REG_HRO },
    { FLDATAD (STOP_IOE, ptr_stopioe, 0, "stop on I/O error") },
    { DRDATA (SBSLVL, ptr_sbs, 4), REG_HRO },
    { NULL }
    };

MTAB ptr_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "SBSLVL", "SBSLVL",
      &dev_set_sbs, &dev_show_sbs, (void *) &ptr_sbs },
    { UNIT_ASCII, UNIT_ASCII, "ASCII", NULL, NULL },
    { UNIT_ASCII, 0,          "FIODEC", NULL, NULL },
    { 0 }
    };

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, ptr_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    &ptr_boot, &ptr_attach, NULL,
    NULL, 0, 0, NULL,
    NULL, NULL, &ptr_help, NULL, NULL, &ptr_description
    };

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit
   ptp_reg      PTP register list
*/

UNIT ptp_unit = {
    UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT
    };

REG ptp_reg[] = {
    { ORDATAD (BUF, ptp_unit.buf, 8, "last data item processed") },
    { FLDATAD (DONE, iosta, IOS_V_PTP, "device done flag") },
    { FLDATAD (RPLS, cpls, CPLS_V_PTP, "return restart pulse flag") },
    { DRDATAD (POS, ptp_unit.pos, T_ADDR_W, "position in the output file"), PV_LEFT },
    { DRDATAD (TIME, ptp_unit.wait, 24, "time from I/O initiation to interrupt"), PV_LEFT },
    { FLDATAD (STOP_IOE, ptp_stopioe, 0, "stop on I/O error") },
    { DRDATA (SBSLVL, ptp_sbs, 4), REG_HRO },
    { NULL }
    };

MTAB ptp_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "SBSLVL", "SBSLVL",
      &dev_set_sbs, &dev_show_sbs, (void *) &ptp_sbs },
    { UNIT_ASCII, UNIT_ASCII, "ASCII", NULL, NULL },
    { UNIT_ASCII, 0,          "FIODEC", NULL, NULL },
    { 0 }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, ptp_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, &ptp_attach, NULL,
    NULL, 0, 0, NULL,
    NULL, NULL, &ptp_help, NULL, NULL, &ptp_description
    };

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit
   tti_reg      TTI register list
*/

UNIT tti_unit = { UDATA (&tti_svc, 0, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
    { ORDATAD (BUF, tty_buf, 6, "typewriter buffer (shared)") },
    { FLDATAD (UC, tty_uc, UC_V, "upper case/lower case state (shared)") },
    { ORDATA (HOLD, tti_hold, 9), REG_HRO },
    { FLDATAD (DONE, iosta, IOS_V_TTI, "input ready flag") },
    { DRDATAD (POS, tti_unit.pos, T_ADDR_W, "number of characters input"), PV_LEFT },
    { DRDATAD (TIME, tti_unit.wait, 24, "keyboard polling interval"), REG_NZ + PV_LEFT },
    { DRDATA (SBSLVL, tti_sbs, 4), REG_HRO },
    { NULL }
    };

MTAB tti_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "SBSLVL", "SBSLVL",
      &dev_set_sbs, &dev_show_sbs, (void *) &tti_sbs },
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, tti_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tty_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit
   tto_reg      TTO register list
*/

UNIT tto_unit = { UDATA (&tto_svc, 0, 0), SERIAL_OUT_WAIT * 10 };

REG tto_reg[] = {
    { ORDATAD (BUF, tty_buf, 6, "typewriter buffer (shared)") },
    { FLDATAD (UC, tty_uc, UC_V, "upper case/lower case state (shared") },
    { FLDATAD (RPLS, cpls, CPLS_V_TTO, "return restart pulse flag") },
    { FLDATAD (DONE, iosta, IOS_V_TTO, "output done flag") },
    { DRDATAD (POS, tto_unit.pos, T_ADDR_W, "number of characters output"), PV_LEFT },
    { DRDATAD (TIME, tto_unit.wait, 24, "time from I/O initiation interrupt"), PV_LEFT },
    { DRDATA (SBSLVL, tto_sbs, 4), REG_HRO },
    { ORDATA (RIBBON, tty_ribbon, 6), REG_HRO },
    { NULL }
    };

MTAB tto_mod[] = {
    { MTAB_XTD|MTAB_VDV, 0, "SBSLVL", "SBSLVL",
      &dev_set_sbs, &dev_show_sbs, (void *) &tto_sbs },
    { UNIT_ET, UNIT_ET, "Expensive Typewriter mode", "ET", NULL },
    { UNIT_ET, UNIT_ET, "normal mode", "NOET", NULL },
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, tto_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tty_reset,
    NULL, NULL, NULL,
    NULL, 0
    };

/* Paper tape reader: IOT routine.  Points to note:

   - RPA (but not RPB) complements the reader clutch control.  Thus,
     if the reader is running, RPA will stop it.
   - The status bit indicates data in the reader buffer that has not
     been transfered to IO.  It is cleared by any RB->IO operation,
     including RRB and the completion pulse.
   - A reader error on a wait mode operation could hang the simulator.
     IOH is set; any retry (without RESET) will be NOP'd.  Accordingly,
     the PTR service routine clears IOH on any error during a rpa/rpb i.
*/

int32 ptr (int32 inst, int32 dev, int32 dat)
{
if (dev == 0030) {                                      /* RRB */
    iosta = iosta & ~IOS_PTR;                           /* clear status */
    return ptr_unit.buf;                                /* return data */
    }
if (dev == 0002)                                        /* RPB, mode = binary */
    ptr_state = 18;
else if (sim_is_active (&ptr_unit)) {                   /* RPA, running? */
    sim_cancel (&ptr_unit);                             /* stop reader */
    return dat;
    }
else ptr_state = 0;                                     /* mode = alpha */
ptr_unit.buf = 0;                                       /* clear buffer */
if (inst & IO_WAIT)                                     /* set ptr wait */
    ptr_wait = 1;
else ptr_wait = 0;                                      /* from IR<5> */
if (GEN_CPLS (inst)) {                                  /* comp pulse? */
    ios = 0;
    cpls = cpls | CPLS_PTR;
    }
else cpls = cpls & ~CPLS_PTR;
sim_activate (&ptr_unit, ptr_unit.wait);                /* start reader */
return dat;
}

/* Unit service */

t_stat ptr_svc (UNIT *uptr)
{
int32 temp;

if ((uptr->flags & UNIT_ATT) == 0) {                    /* attached? */
    if (ptr_wait)                                       /* if wait, clr ioh */
        ptr_wait = ioh = 0;
    if ((cpls & CPLS_PTR) || ptr_stopioe)
        return SCPE_UNATT;
    return SCPE_OK;
    }
if ((uptr->flags & UNIT_ASCII) && (ptr_state == 0))     /* ASCII mode, alpha read? */
    temp = ptr_get_ascii (uptr);                        /* get processed char */
else if ((temp = getc (uptr->fileref)) != EOF)          /* no, get raw char */
    uptr->pos = uptr->pos + 1;                          /* if not eof, count */
if (temp == EOF) {                                      /* end of file? */
    if (ptr_wait)                                       /* if wait, clr ioh */
        ptr_wait = ioh = 0;
    if (feof (uptr->fileref)) {
        if ((cpls & CPLS_PTR) || ptr_stopioe)
            sim_printf ("PTR end of file\n");
        else return SCPE_OK;
        }
    else sim_perror ("PTR I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
if (ptr_state == 0)                                     /* alpha */
    uptr->buf = temp & 0377;
else if (temp & 0200) {                                 /* binary */
    ptr_state = ptr_state - 6;
    uptr->buf = uptr->buf | ((temp & 077) << ptr_state);
    }
if (ptr_state == 0) {                                   /* done? */
    if (cpls & CPLS_PTR) {                              /* completion pulse? */
        iosta = iosta & ~IOS_PTR;                       /* clear flag */
        IO = uptr->buf;                                 /* fill IO */
        ios = 1;                                        /* restart */
        cpls = cpls & ~CPLS_PTR;
        }
    else {                                              /* no, interrupt */
        iosta = iosta | IOS_PTR;                        /* set flag */
        dev_req_int (ptr_sbs);                          /* req interrupt */
        }
    }
else sim_activate (uptr, uptr->wait);                   /* get next char */
return SCPE_OK;
}

/* Read next ASCII character

   This handles all three styles of end of line.
   1a. Old Mac style - only CRs. CRs are converted to FIODEC_CR.
   1b. Linux style - only LFs. LFs are converted to FIODEC_CR.
   1c. Windows syle - CR+LF. CRs are converted to FIODEC_CR; next LF is ignored.

   On end of file, the routine returns a FIODEC_STOP, unless the
   previous character was the ASCII equivalent, FF. On the next end of file,
   or if the previous character was FF, the routine returns EOF.
*/

int ptr_get_ascii (UNIT *uptr)
{
int c;
int32 in;

if (ptr_leader > 0) {                                   /* leader? */
    ptr_leader = ptr_leader - 1;                        /* count down */
    return 0;
    }
if (ptr_hold & CW) {                                    /* char waiting? */
    in = ptr_hold & TT_WIDTH;                           /* return char */
    ptr_hold = 0;                                       /* not waiting */
    }
else {
    for (;;) {                                          /* until valid char */
        if ((c = getc (uptr->fileref)) == EOF) {        /* get next char, EOF? */
            if (ptr_last == '\f')                       /* already returned FIO_STOP? */
                return EOF;                             /* then EOF */
            ptr_last = '\f';                            /* pretend read FIO_STOP */
            return FIODEC_STOP;                         /* return FIO_STOP */
            }
        uptr->pos = uptr->pos + 1;                      /* count char */
        c = c & 0177;                                   /* cut to 7b */
        if ((c == '\n') && (ptr_last == '\r')) {        /* LF after CR? */
            ptr_last = 0;                               /* defang test */
            continue;                                   /* ignore char */
            }
        ptr_last = c;                                   /* save char */
        if ((c == '\n') || (c == '\r'))                 /* CR, LF -> FIO_CR*/
            in = BOTH | FIODEC_CR;
        else if (c == ' ')                              /* space -> FIO_SPC */
            in = BOTH | FIODEC_SPACE;
        else {                                          /* other */
            in = ascii_to_fiodec[c];                    /* convert */
            if (in == 0)                                /* ignore invalid char */
                continue;
            }
        if ((in & BOTH) || ((in & UC) == ptr_uc))       /* case match? */
            in = in & TT_WIDTH;                         /* cut to 6b */
        else {                                          /* no, case shift */
            ptr_hold = in | CW;                         /* set char waiting */
            ptr_uc = in & UC;                           /* set case */
            in = ptr_uc? FIODEC_UC: FIODEC_LC;          /* return case */
            }                                           /* end else */
        break;
        }                                               /* end for */
    }                                                   /* end else */
in = in * 010040201;                                    /* even parity from */
in = in | 027555555400;                                 /* HACKMEM 167 */
in = in % (9 << 7);
return in & 0377;
}

/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
ptr_state = 0;                                          /* clear state */
ptr_wait = 0;
ptr_hold = 0;
ptr_last = 0;
ptr_uc = 0;
ptr_unit.buf = 0;
cpls = cpls & ~CPLS_PTR;
iosta = iosta & ~IOS_PTR;                               /* clear flag */
sim_cancel (&ptr_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Attach routine */

t_stat ptr_attach (UNIT *uptr, CONST char *cptr)
{
ptr_leader = PTR_LEADER;                                /* set up leader */
if (sim_switches & SWMASK ('A'))
    uptr->flags = uptr->flags | UNIT_ASCII;
else uptr->flags = uptr->flags & ~UNIT_ASCII;
sim_switches &= ~SWMASK ('A');      /* Turn off A switch to avoid Append mode ambiguity */
return attach_unit (uptr, cptr);
}

/* Bootstrap routine */

int32 ptr_getw (UNIT *uptr)
{
int32 i, tmp, word;

for (i = word = 0; i < 3;) {
    if ((tmp = getc (uptr->fileref)) == EOF)
        return -1;
    uptr->pos = uptr->pos + 1;
    if (tmp & 0200) {
        word = (word << 6) | (tmp & 077);
        i++;
        }
    }
return word;
}

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
int32 origin, val;
int32 fld = TA & EPCMASK;

for (;;) {
    if ((val = ptr_getw (&ptr_unit)) < 0)
        return SCPE_FMT;
    if (((val & 0760000) == OP_DIO) ||                  /* DIO? */
        ((val & 0760000) == OP_DAC)) {                  /* hack - Macro1 err */
        origin = val & DAMASK;
        if ((val = ptr_getw (&ptr_unit)) < 0)
            return SCPE_FMT;
        M[fld | origin] = val;
        }
    else if ((val & 0760000) == OP_JMP) {               /* JMP? */
        PC = fld | (val & DAMASK);
        break;
        }
    else return SCPE_FMT;                               /* bad instr */
    }
return SCPE_OK;                                         /* done */
}

t_stat ptr_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Paper Tape Reader (PTR)\n\n");
fprintf (st, "The paper tape reader (PTR) reads data from a disk file.  The POS register\n");
fprintf (st, "specifies the number of the next data item to be read.  Thus, by changing\n");
fprintf (st, "The paper tape reader supports the BOOT command.  BOOT PTR copies the RIM\n");
fprintf (st, "loader into memory and starts it running.  BOOT PTR loads into the field\n");
fprintf (st, "selected by TA<0:3> (the high order four bits of the address switches).\n\n");
fprintf (st, "The paper tape reader recognizes one switch at ATTACH time:\n\n");
fprintf (st, "    ATT -A PTP <file>       convert input characters from ASCII\n\n");
fprintf (st, "By default, the paper tape reader does no conversions on input characters.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *ptr_description (DEVICE *dptr)
{
return "Paper Tape Reader";
}


/* Paper tape punch: IOT routine */

int32 ptp (int32 inst, int32 dev, int32 dat)
{
iosta = iosta & ~IOS_PTP;                               /* clear flag */
ptp_unit.buf = (dev == 0006)? ((dat >> 12) | 0200): (dat & 0377);
if (GEN_CPLS (inst)) {                                  /* comp pulse? */
    ios = 0;
    cpls = cpls | CPLS_PTP;
    }
else cpls = cpls & ~CPLS_PTP;
sim_activate (&ptp_unit, ptp_unit.wait);                /* start unit */
return dat;
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
int32 c;

if (cpls & CPLS_PTP) {                                  /* completion pulse? */
    ios = 1;                                            /* restart */
    cpls = cpls & ~CPLS_PTP;
    }
iosta = iosta | IOS_PTP;                                /* set flag */
dev_req_int (ptp_sbs);                                  /* req interrupt */
if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return IORETURN (ptp_stopioe, SCPE_UNATT);
if ((uptr->flags & UNIT_ASCII) != 0) {                  /* ASCII mode? */
    int32 c1 = uptr->buf & 077;
    if (uptr->buf == 0)                                 /* ignore nulls */
        return SCPE_OK;
    if (c1 == FIODEC_UC) {                              /* UC? absorb */
        ptp_uc = UC;
        return SCPE_OK;
        }
    else if (c1 == FIODEC_LC) {                         /* LC? absorb */
        ptp_uc = 0;
        return SCPE_OK;
        }
    else c = fiodec_to_ascii[c1 | ptp_uc];
    if (c == 0)
        return SCPE_OK;
    if (c == '\n') {                                    /* new line? */
        putc ('\r', uptr->fileref);                     /* cr first */
        uptr->pos = uptr->pos + 1;
        }
    }
else c = uptr->buf;        
if (putc (c, uptr->fileref) == EOF) {                   /* I/O error? */
    sim_perror ("PTP I/O error");
    clearerr (uptr->fileref);
    return SCPE_IOERR;
    }
uptr->pos = uptr->pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
ptp_unit.buf = 0;                                       /* clear state */
ptp_uc = 0;
cpls = cpls & ~CPLS_PTP;
iosta = iosta & ~IOS_PTP;                               /* clear flag */
sim_cancel (&ptp_unit);                                 /* deactivate unit */
return SCPE_OK;
}

/* Attach routine */

t_stat ptp_attach (UNIT *uptr, CONST char *cptr)
{
if (sim_switches & SWMASK ('A'))
    uptr->flags = uptr->flags | UNIT_ASCII;
else uptr->flags = uptr->flags & ~UNIT_ASCII;
sim_switches |= SWMASK ('A');       /* Default to Append to existing file */
return attach_unit (uptr, cptr);
}

t_stat ptp_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "Paper Tape Punch (PTP)\n\n");
fprintf (st, "The paper tape punch (PTP) writes data to a disk file.  The POS register\n");
fprintf (st, "specifies the number of the next data item to be written.  Thus, by changing\n");
fprintf (st, "POS, the user can backspace or advance the punch.\n\n");
fprintf (st, "The paper tape punch recognizes two switches at ATTACH time:\n\n");
fprintf (st, "    ATT -A PTP <file>       output characters as ASCII text\n");
fprintf (st, "    ATT -N PTP <file>       create a new (empty) output file\n\n");
fprintf (st, "By default, the paper tape punch punches files with no conversions.\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprint_reg_help (st, dptr);
return SCPE_OK;
}

const char *ptp_description (DEVICE *dptr)
{
return "Paper Tape Punch";
}

/* Typewriter IOT routines */

int32 tti (int32 inst, int32 dev, int32 dat)
{
iosta = iosta & ~IOS_TTI;                               /* clear flag */
if (inst & (IO_WAIT | IO_CPLS))                         /* wait or sync? */
    return (STOP_RSRV << IOT_V_REASON) | (tty_buf & 077);
return tty_buf & 077;
}

int32 tto (int32 inst, int32 dev, int32 dat)
{
iosta = iosta & ~IOS_TTO;                               /* clear flag */
tty_buf = dat & TT_WIDTH;                               /* load buffer */
if (GEN_CPLS (inst)) {                                  /* comp pulse? */
    ios = 0;
    cpls = cpls | CPLS_TTO;
    }
else cpls = cpls & ~CPLS_TTO;
sim_activate (&tto_unit, tto_unit.wait);                /* activate unit */
return dat;
}

/* Unit service routines */

t_stat tti_svc (UNIT *uptr)
{
int32 in, temp;

sim_activate (uptr, uptr->wait);                        /* continue poll */
if (tti_hold & CW) {                                    /* char waiting? */
    tty_buf = tti_hold & TT_WIDTH;                      /* return char */
    tti_hold = 0;                                       /* not waiting */
    }
else {
    if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)
        return temp;
    if (temp & SCPE_BREAK)                              /* ignore break */
        return SCPE_OK;
    temp = temp & 0177;
    if (temp == 0177)                                   /* rubout? bs */
        temp = '\b';
    sim_putchar (temp);                                 /* echo */
    if (temp == '\r')                                   /* cr? add nl */
        sim_putchar ('\n');
    in = ascii_to_fiodec[temp];                         /* translate char */
    if (in == 0)                                        /* no xlation? */
        return SCPE_OK;
    if ((in & BOTH) || ((in & UC) == (tty_uc & UC)))
        tty_buf = in & TT_WIDTH;
    else {                                              /* must shift */
        tty_uc = in & UC;                               /* new case */
        tty_buf = tty_uc? FIODEC_UC: FIODEC_LC;
        tti_hold = in | CW;                             /* set 2nd waiting */
        }
    }
iosta = iosta | IOS_TTI;                                /* set flag */
dev_req_int (tti_sbs);                                  /* req interrupt */
PF = PF | PF_SS_1;                                      /* set prog flag 1 */
uptr->pos = uptr->pos + 1;
return SCPE_OK;
}

void tto_puts (const char *cptr)
{
int32 c;

while ((c = *cptr++) != 0)
    sim_putchar (c);
return;
}

t_stat tto_svc (UNIT *uptr)
{
t_stat r;
int32 c;
static const char *red_str = "[red]\r\n";
static const char *black_str = "[black]\r\n";

if (tty_buf == FIODEC_UC)                               /* upper case? */
    tty_uc = UC;
else if (tty_buf == FIODEC_LC)                          /* lower case? */
    tty_uc = 0;
else if (((uptr->flags & UNIT_ET) != 0) &&              /* ET ribbon chg? */
    ((tty_buf == FIODEC_BLACK) || (tty_buf == FIODEC_RED)) &&
    (tty_buf != tty_ribbon)) {
    tto_puts ((tty_buf == FIODEC_RED)? red_str: black_str);
    tty_ribbon = tty_buf;
    }
else if (tty_buf == FIODEC_CR)
    tto_puts ("\r\n");
else {
    c = fiodec_to_ascii[tty_buf | tty_uc];              /* translate */
    if ((c != 0) && ((r = sim_putchar_s (c)) != SCPE_OK)) { /* output; error? */
        sim_activate (uptr, uptr->wait);                /* retry */
        return ((r == SCPE_STALL)? SCPE_OK: r);
        }
    }
if (cpls & CPLS_TTO) {                                  /* completion pulse? */
    ios = 1;                                            /* restart */
    cpls = cpls & ~CPLS_TTO;
    }
iosta = iosta | IOS_TTO;                                /* set flag */
dev_req_int (tto_sbs);                                  /* req interrupt */
uptr->pos = uptr->pos + 1;
return SCPE_OK;
}

/* Reset routine */

t_stat tty_reset (DEVICE *dptr)
{
tmxr_set_console_units (&tti_unit, &tto_unit);
tty_buf = 0;                                            /* clear buffer */
tty_ribbon = FIODEC_BLACK;                              /* start black */
tty_uc = 0;                                             /* clear case */
tti_hold = 0;                                           /* clear hold buf */
cpls = cpls & ~CPLS_TTO;
iosta = (iosta & ~IOS_TTI) | IOS_TTO;                   /* clear flag */
sim_activate (&tti_unit, tti_unit.wait);                 /* activate keyboard */
sim_cancel (&tto_unit);                                 /* stop printer */
return SCPE_OK;
}
