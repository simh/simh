/* i1620_tty.c: IBM 1620 typewriter

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

   tty          console typewriter

   26-May-17    RMS     Added deferred IO
   18-May-17    RMS     Fixed keyboard interrupt problem for Linux
                        Added input backspace for Model II
   04-May-17    DW      Revised tab calculation algorithm
   13-Mar-17    RMS     Fixed tab stop array overrun at right margin (COVERITY)
   21-Feb-15    TFM     Option to provide single digit numeric output
   05-Feb-15    TFM     Changes to translate tables and valid input char.
   02-Jan-14    RMS     Added variable tab stops
   10-Dec-13    RMS     Fixed DN wraparound (Bob Armstrong)
   21-Sep-05    RMS     Revised translation tables for 7094/1401 compatibility
   22-Dec-02    RMS     Added break test
*/

#include "i1620_defs.h"

#define NUM_1_DIGIT TRUE /* indicate numeric output will use single digit format (tfm) */

/* Maximum number of characters per line, or one-based column number of last char cell */

#define TTO_COLMAX      80
#define UF_V_1DIG       (UNIT_V_UF)
#define UF_1DIG         (1 << UF_V_1DIG)
#define UTTI            1
#define UTTO            0

uint32 tti_unlock = 0;                                  /* expecting input */
uint32 tti_flag = 0;                                    /* flag typed */
int32 tto_col = 1;                                      /* one-based, char loc to print next */

uint8 tto_tabs[TTO_COLMAX + 1] = {  /* Zero-based access, one-based UI */
 0,0,0,0,0,0,0,0,
 1,0,0,0,0,0,0,0,
 1,0,0,0,0,0,0,0,
 1,0,0,0,0,0,0,0,
 1,0,0,0,0,0,0,0,
 1,0,0,0,0,0,0,0,
 1,0,0,0,0,0,0,0,
 1,0,0,0,0,0,0,0,
 1,0,0,0,0,0,0,0,
 1,0,0,0,0,0,0,0,
 1
};

extern uint8 M[MAXMEMSIZE];
extern uint8 ind[NUM_IND];
extern UNIT cpu_unit;
extern uint32 io_stop;
extern uint32 cpuio_inp, cpuio_opc, cpuio_cnt, PAR;

t_stat tto_num (void);
t_stat tto_write (uint32 c);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat tty_reset (DEVICE *dptr);
t_stat tty_set_fixtabs (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat tty_set_12digit (UNIT *uptr, int32 val, CONST char *cptr, void *desc);

/* TTY data structures

   tty_dev      TTY device descriptor
   tty_unit     TTY unit descriptor
   tty_reg      TTY register list
*/

UNIT tty_unit[] = {
    { UDATA (&tto_svc, 0, 0), SERIAL_OUT_WAIT },
    { UDATA (&tti_svc, 0, 0), KBD_POLL_WAIT }
    };

REG tty_reg[] = {
    { FLDATAD (UNLOCK, tti_unlock, 0, "keyboard unlocked flag") },
    { FLDATAD (FLAG, tti_flag, 0, "set flag on next input digit"), REG_HRO },
    { DRDATAD (COL, tto_col, 7, "current column") },
#if (SIM_MAJOR >= 4)
    { DRDATAD (CPS, tty_unit[UTTO].DEFIO_CPS, 24, "Character Output Rate"), PV_LEFT },
    { DRDATAD (ICPS, tty_unit[UTTI].DEFIO_CPS, 24, "Character Input Rate"), PV_LEFT },
#else
    { DRDATAD (KTIME, tty_unit[UTTI].wait, 24, "keyboard polling interval"), REG_NZ + PV_LEFT },
    { DRDATAD (TTIME, tty_unit[UTTO].wait, 24, "typewriter character delay"), REG_NZ + PV_LEFT },
#endif
    { NULL }
    };

MTAB tty_mod[] = {
    { MTAB_XTD|MTAB_VDV, TTO_COLMAX, NULL, "TABS=col;col;col...",
      &sim_tt_settabs, NULL, (void *) tto_tabs, "set tab stops at the specified columns" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, TTO_COLMAX, "TABS", NULL,
      NULL, &sim_tt_showtabs, (void *) tto_tabs, "display current tab stops" },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "NOTABS",
      &tty_set_fixtabs, NULL, NULL, "remove all tab stops" },
    { MTAB_XTD|MTAB_VDV, 8, NULL, "DEFAULTTABS",
      &tty_set_fixtabs, NULL, NULL, "set tab stops every eight columns" },
    { UF_1DIG, UF_1DIG, "combined digits and flags", "1DIGIT", 
      &tty_set_12digit, NULL, NULL, "type flagged digits as letters" },
    { UF_1DIG, 0      , "separate digits and flags", "2DIGIT", 
      &tty_set_12digit, NULL, NULL, "type flagged digits as ~digit" },
    { 0 }
    };

DEVICE tty_dev = {
    "TTY", tty_unit, tty_reg, tty_mod,
    2, 10, 31, 1, 8, 7,
    NULL, NULL, &tty_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEFIO
    };

/* Data tables */

/* Keyboard to numeric */

/* The following constant is a list of valid 1620 numeric characters 
   that can be entered from the keyboard. They are the digits 0-9, 
   record mark(|), numeric blank(@) and group mark(}). All others
   are considered invalid. When entering data, these characters may
   all be preceeded by tilde(~) or accent(`) to indicate that the
   following character should be entered into storage with a flag.

   Alternatively, ] can be entered for flagged 0,
                  J-R or j-r can be entered for flagged 1-9,
                  ! for flagged RM, * for flagged numeric blank,
                  " for flagged GM.

   These different methods of entering numeric data represent 
   compromises since there is no practical way to exactly emulate
   the 1620 typewriter capability of entering a flag but not 
   spacing the carriage. Entering a flag symbol in front of a
   character is easier and sometimes more readable; using the
   letters j-r is useful if column alignment is important on
   the screen or when copying data that has printed letters in
   place of flagged digits. This also matches the output of WN
   or DN to the line printer.

   *tti_to_num is the string of valid characters
    tti_position_to_internal[] are the matching internal codes
                                                     (Tom McBride)*/

const char *tti_to_num = "0123456789|@}]jklmnopqr!*\"JKLMNOPQR";  
const char tti_position_to_internal[35] = { 
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, REC_MARK, NUM_BLANK, GRP_MARK,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    FLG_REC_MARK, FLG_NUM_BLANK, FLG_GRP_MARK, 
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19
};

/* Keyboard to alphameric (digit pair) - translates LC to UC */

const int8 tti_to_alp[128] = {
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* 00 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* 10 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
 0x00, 0x5A, 0x5F,   -1, 0x13,   -1,   -1,   -1,        /*  !"#$%&' */
 0x24, 0x04, 0x14, 0x10, 0x23, 0x20, 0x03, 0x21,        /* ()*+,-./ */
 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,        /* 01234567 */
 0x78, 0x79,   -1,   -1,   -1, 0x33,   -1,   -1,        /* 89:;<=>? */
 0x34, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,        /* @ABCDEFG */
 0x48, 0x49, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56,        /* HIJKLMNO */
 0x57, 0x58, 0x59, 0x62, 0x63, 0x64, 0x65, 0x66,        /* PQRSTUVW */
 0x67, 0x68, 0x69,   -1,   -1, 0x50,   -1,   -1,        /* XYZ[\]^_ */
   -1, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,        /* `abcdefg */
 0x48, 0x49, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56,        /* hijklmno */
 0x57, 0x58, 0x59, 0x62, 0x63, 0x64, 0x65, 0x66,        /* pqrstuvw */
 0x67, 0x68, 0x69,   -1, 0x0A, 0x0F,   -1,   -1         /* xyz{|}~  */
 };

/* Numeric (digit) to typewriter */

/* Digits with values of 11, 13 and 14 should never occur and will be typed as :'s 
   if they ever do. These are really errors.            (Tom McBride)  */

/* If flagged digits are being printed with preceeding ` characters only the first
   half of this table is actually used. If digits are being printed one char per
   digit the whole table is used.                        (Tom McBride)  */

const char num_to_tto[32] = {
 '0', '1', '2', '3', '4', '5', '6', '7',
 '8', '9', '|', ':', '@', ':', ':', '}',

 ']', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
 'Q', 'R', '!', ':', '*', ':', ':', '"'
 };

/* Alphameric (digit pair) to typewriter */

/* Characters not in 1620 set have been removed from table (tfm) */

const char alp_to_tto[256] = {
 ' ',  -1,  -1, '.', ')',  -1,  -1,  -1,                /* 00 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
 '+',  -1,  -1, '$', '*',  -1,  -1,  -1,                /* 10 */
  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
 '-', '/',  -1, ',', '(',  -1,  -1,  -1,                /* 20 */
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

/* Terminal IO
 
   - On input, parity errors cannot occur.
   - On input, release-start does NOT cause a record mark to be stored.
   - On output, invalid characters type an invalid character and set WRCHK.
     If IO stop is set, the system halts at the end of the operation.
*/

t_stat tty (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
switch (op) {                                           /* case on op */

    case OP_K:                                          /* control */
        switch (f1) {                                   /* case on control */
        case 1:                                         /* space */
            tto_write (' ');
            break;
        case 2:                                         /* return */
            tto_write ('\r');
            break;
        case 3:                                         /* backspace */
            if ((cpu_unit.flags & IF_MII) == 0)
                return STOP_INVFNC;
            tto_write ('\b');
            break;
        case 4:                                         /* index */
            if ((cpu_unit.flags & IF_MII) == 0)
                return STOP_INVFNC;
            tto_write ('\n');
            break;
        case 8:                                         /* tab */
            tto_write ('\t');
            break;
        default:
            return STOP_INVFNC;
            }
        break;

    case OP_WN:
    case OP_DN:
    case OP_WA:
        cpuio_set_inp (op, IO_TTY, &tty_unit[UTTO]);    /* set IO in progress */
        break;

    case OP_RN:
    case OP_RA:
        tti_unlock = 1;                                 /* unlock keyboard */
        tti_flag = 0;                                   /* init flag */
        tto_write ('>');                                /* prompt user */
        cpuio_set_inp (op, IO_TTY, NULL);               /* set IO in progress */
        break;

    default:                                            /* invalid function */
        return STOP_INVFNC;
        }

return SCPE_OK;
}

/* Input unit service - OP can be RA or RN */

t_stat tti_svc (UNIT *uptr)
{
int32 temp;
int8 raw, c;
const char *cp;

DEFIO_ACTIVATE (uptr);                                  /* continue poll */
if ((temp = sim_poll_kbd ()) < SCPE_KFLAG)              /* no char or error? */
    return temp;
if (tti_unlock == 0)                                    /* expecting input? */
    return SCPE_OK;                                     /* no, ignore */
raw = (int8) (temp & 0x7F);

if (raw == '\r') {                                      /* return? */
    tto_write (raw);                                    /* echo */
    tti_unlock = 0;                                     /* no more input */
    cpuio_clr_inp (NULL);
    return SCPE_OK;
    }
if (cpuio_opc == OP_RN) {                               /* RN? */
    if (((raw == '\b') || (raw == 0x7F)) &&             /* backspace or del? */
        ((cpu_unit.flags & IF_MII) != 0)) {             /* on model 2? */
        tto_write ('-');                                /* print minus */
        MM (PAR);                                       /* decr mem addr */
        return SCPE_OK;
        }
    else if ((raw == '~') || (raw == '`')) {            /* flag? mark */
        tto_write (raw);
        tti_flag = FLAG;
        return SCPE_OK;
        }
    else if ((cp = strchr (tti_to_num, raw)) == 0) {    /* invalid? */
        tto_write ('\a');                               /* beep! */
        return SCPE_OK;
        }
    tto_write (raw);                                    /* echo */
    if (cpuio_cnt >= MEMSIZE) {                         /* wrap around? */
        tti_unlock = 0;                                 /* no more input */
        cpuio_clr_inp (NULL);
        return STOP_RWRAP;
        }
    c = tti_position_to_internal[(cp - tti_to_num)] | tti_flag; /* assemble char */
    M[PAR] = c & (FLAG | DIGIT);                        /* store char */
    tti_flag = 0;                                       /* re-init flag */
    PP (PAR);                                           /* incr mem addr */
    cpuio_cnt++;
    }

else {                                                  /* RA */
    if (((raw == '\b') || (raw == 0x7F)) &&             /* backspace or del? */
        ((cpu_unit.flags & IF_MII) != 0)) {             /* on model 2? */
        tto_write ('-');                                /* print minus */
        PAR = ADDR_A (PAR, -2);                         /* decr mem addr*/
        return SCPE_OK;
        }
    else if ((raw >= sizeof(tti_to_alp)) ||             /* illegal char? */
             (tti_to_alp[raw] < 0)) {
        tto_write ('\a');                               /* beep! */
        return SCPE_OK;
        }
    tto_write (raw);                                    /* echo */
    if (cpuio_cnt >= MEMSIZE) {                         /* wrap around? */
        tti_unlock = 0;                                 /* no more input */
        cpuio_clr_inp (NULL);
        return STOP_RWRAP;
        }
    c = tti_to_alp[raw];                                /* xlate */
    M[PAR] = (M[PAR] & FLAG) | (c & DIGIT);             /* store 2 digits */
    M[PAR - 1] = (M[PAR - 1] & FLAG) | ((c >> 4) & DIGIT);
    PAR = ADDR_A (PAR, 2);                              /* incr mem addr */
    cpuio_cnt = cpuio_cnt + 2;
    }

return SCPE_OK;
}

/* Output unit service */

t_stat tto_svc (UNIT *uptr) {

uint8 d;
int8 ttc;
t_stat sta = SCPE_OK;

if ((cpuio_opc != OP_DN) && (cpuio_cnt >= MEMSIZE)) {   /* wrap, ~dump? */
    cpuio_clr_inp (uptr);                               /* done */
    return STOP_RWRAP;
    }
DEFIO_ACTIVATE (uptr);                                  /* sched another xfer */

switch (cpuio_opc) {                                    /* decode op */

    case OP_DN:
        if ((cpuio_cnt != 0) && ((PAR % 20000) == 0))   /* done? */
            break;
        return tto_num ();                              /* write numeric */

    case OP_WN:
        if ((M[PAR] & REC_MARK) == REC_MARK)            /* end record? */
            break;
        return tto_num ();                              /* write numeric */

    case OP_WA:
        d = M[PAR] & DIGIT;                             /* get digit */
        if ((d & REC_MARK) == REC_MARK)                 /* 8-2 char? done */
            break;
        d = ((M[PAR - 1] & DIGIT) << 4) | d;            /* get digit pair */
        ttc = alp_to_tto[d];                            /* translate */
        if (ttc < 0) {                                  /* bad char? */
            ind[IN_WRCHK] = 1;                          /* set write check */
            if (io_stop)                                /* set return status */
                sta = STOP_INVCHR;
            break;
            }
        tto_write (ttc & 0x7F);                         /* write */
        PAR = ADDR_A (PAR, 2);                          /* incr mem addr */
        cpuio_cnt = cpuio_cnt + 2;
        return SCPE_OK;

    default:
        return SCPE_IERR;
    }        

cpuio_clr_inp (uptr);                                   /* clear IO in progress */
return sta;
}

/* Write numerically - cannot generate parity errors */

t_stat tto_num (void)
{
t_stat r;
uint8 d;

d = M[PAR];                                             /* get char */
if (tty_unit[UTTO].flags & UF_1DIG)                     /* how display flagged digits? */
    r = tto_write (num_to_tto[d & (DIGIT|FLAG)]);       /* single digit display */
else {
    if (d & FLAG)                                       /* flag? */
        tto_write ('`');                                /* write flag indicator */
    r = tto_write (num_to_tto[d & DIGIT]);              /* write the digit */
    }                                                      
if (r != SCPE_OK)                                       /* write error? */
    return r;
PP (PAR);                                               /* incr mem addr */
cpuio_cnt++;
return SCPE_OK;
}

/* Wrap line, if needed, prior to character output */

void tto_wrap(void)
{
if (tto_col > TTO_COLMAX) {                             /* line wrap? */
    sim_putchar('\r');
    sim_putchar('\n');
    tto_col = 1;
    }
}

/* Write, maintaining position */

t_stat tto_write (uint32 c)
{
if (c == '\t') {                                        /* tab? */
    tto_wrap();
    do {
        sim_putchar(' ');                               /* use spaces */
        tto_col++;
        } while (!(tto_col > TTO_COLMAX || tto_tabs[tto_col-1] == 1));
    }
else if (c == '\r') {                                   /* return? */
    sim_putchar ('\r');                                 /* crlf */
    sim_putchar ('\n');
    tto_col = 1;                                        /* back to LMargin */
    }
else if ((c == '\n') || (c == '\a')) {                  /* non-spacing? */
    sim_putchar (c);
    }
else if (c == '\b') {                                   /* backspace? */
    if (tto_col > 1) {
        sim_putchar(c);
        tto_col--;
        }
    }
else {                                                  /* normal */
    tto_wrap();
    sim_putchar(c);
    tto_col++;
    }
return SCPE_OK;
}

/* Reset routine */

t_stat tty_reset (DEVICE *dptr)
{
DEFIO_ACTIVATE(&tty_unit[UTTI]);                        /* activate poll */
sim_cancel (&tty_unit[UTTO]);                           /* cancel output */
tti_unlock = tti_flag = 0;                              /* tty locked */
tto_col = 1;
return SCPE_OK;
}

/* Set tab stops at fixed modulus */

t_stat tty_set_fixtabs (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 i;

for (i = 0; i < TTO_COLMAX; i++) {
    if ((val != 0) && (i != 0) && ((i % val) == 0))
        tto_tabs[i] = 1;
    else tto_tabs[i] = 0;
    }
return SCPE_OK;
}

/* Assure consistency of 1DIG/2DIG setting */

t_stat tty_set_12digit (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
tty_unit[UTTI].flags = (tty_unit[UTTI].flags & ~UF_1DIG) | val;
tty_unit[UTTO].flags = (tty_unit[UTTO].flags & ~UF_1DIG) | val;
return SCPE_OK;
}
