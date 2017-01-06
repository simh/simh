/*************************************************************************
 *                                                                       *
 * $Id: tx0_stddev.c 2063 2009-02-25 07:37:57Z hharte $                  *
 *                                                                       *
 * Copyright (c) 2009-2012 Howard M. Harte.                              *
 * Based on pdp1_stddev.c, Copyright (c) 1993-2006, Robert M. Supnik     *
 *                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining *
 * a copy of this software and associated documentation files (the       *
 * "Software"), to deal in the Software without restriction, including   *
 * without limitation the rights to use, copy, modify, merge, publish,   *
 * distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to *
 * the following conditions:                                             *
 *                                                                       *
 * The above copyright notice and this permission notice shall be        *
 * included in all copies or substantial portions of the Software.       *
 *                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 * NONINFRINGEMENT. IN NO EVENT SHALL HOWARD M. HARTE BE LIABLE FOR ANY  *
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  *
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     *
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                *
 *                                                                       *
 * Except as contained in this notice, the name of Howard M. Harte shall *
 * not be used in advertising or otherwise to promote the sale, use or   *
 * other dealings in this Software without prior written authorization   *
 * of Howard M. Harte.                                                   *
 *                                                                       *
 * Module Description:                                                   *
 *     TX-0 Standard Devices                                             *
 *                                                                       *
 * Environment:                                                          *
 *     User mode only                                                    *
 *                                                                       *
 *************************************************************************/

/*
   petr         paper tape reader
   ptp          paper tape punch
   tti          keyboard
   tto          teleprinter

   Note: PTP timeout must be >10X faster than TTY output timeout for Macro
   to work correctly!
*/

#include "tx0_defs.h"
#include "sim_tmxr.h"

#define FLEXO_STOP     061                             /* stop code */
#define FLEXO_UC       071
#define FLEXO_LC       075
#define UC_V            6                               /* upper case */
#define UC              (1 << UC_V)
#define BOTH            (1 << (UC_V + 1))               /* both cases */
#define CW              (1 << (UC_V + 2))               /* char waiting */
#define TT_WIDTH        077
#define UNIT_V_ASCII    (UNIT_V_UF + 0)                 /* ASCII/binary mode */
#define UNIT_ASCII      (1 << UNIT_V_ASCII)
#define PETR_LEADER      20                              /* ASCII leader chars */

#define TRACE_PRINT(dev, level, args)   if(dev.dctrl & level) {   \
                                            printf args;          \
                                        }

int32 petr_state = 0;
int32 petr_wait = 0;
int32 petr_stopioe = 0;
int32 petr_uc = 0;                                       /* upper/lower case */
int32 petr_hold = 0;                                     /* holding buffer */
int32 petr_leader = PETR_LEADER;                          /* leader count */
int32 ptp_stopioe = 0;
int32 tti_hold = 0;                                     /* tti hold buf */
int32 tty_buf = 0;                                      /* tty buffer */
int32 tty_uc = 0;                                       /* tty uc/lc */
int32 tto_sbs = 0;

extern int32 ios, iosta;
extern int32 PF, IR, PC, TA;
extern int32 M[];

t_stat petr_svc (UNIT *uptr);
t_stat ptp_svc (UNIT *uptr);
t_stat tti_svc (UNIT *uptr);
t_stat tto_svc (UNIT *uptr);
t_stat petr_reset (DEVICE *dptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat tty_reset (DEVICE *dptr);
t_stat petr_boot (int32 unitno, DEVICE *dptr);
t_stat petr_attach (UNIT *uptr, CONST char *cptr);

/* Character translation tables */

int32 flexo_to_ascii[128] = {
/*00*/    0,   0,   'e', '8', 0,   '|', 'a', '3',             /* lower case */
/*10*/    ' ', '=', 's', '4', 'i', '+', 'u', '2',
/*20*/    0,   '.', 'd', '5', 'r', '1', 'j', '7',
/*30*/    'n', ',', 'f', '6', 'c', '-', 'k', 0,
/*40*/    't', 0,   'z', '\b','l', '\t','w', 0,
/*50*/    'h', '\r','y', 0,   'p', 0,   'q', 0,
/*60*/    'o', '*', 'b', 0,   'g', 0,   '9', 0,
/*70*/    'm', 0,   'x', 0,   'v', 0,   '0', 0,
/*00*/    0,   0,   'E', '8', 0,   '_', 'A', '3',             /* upper case */
/*10*/    ' ', ':', 'S', '4', 'I', '/', 'U', '2',
/*20*/    0,   ')', 'D', '5', 'R', '1', 'J', '7',
/*30*/    'N', '(', 'F', '6', 'C', '-', 'K', 0,
/*40*/    'T', 0,   'Z', '\b','L', '\t','W', 0,
/*50*/    'H', '\r','Y', 0,   'P', 0,   'Q', 0,
/*60*/    'O', '*', 'B', 0,   'G', 0,   '9', 0,
/*70*/    'M', 0,   'X', 0,   'V', 0,   '0', 0,
    };

int32 ascii_to_flexo[128] = {
/*00*/    0, 0, 0, BOTH+061, 0, 0, 0, 0,    /* STOP mapped to ^C */
/*10*/    BOTH+043, BOTH+045, 0, 0, 0, BOTH+051, 0, 0,
/*20*/    0, 0, 0, 0, 0, 0, 0, 0,
/*30*/    0, 0, 0, BOTH+020, 0, 0, 0, 0, /* Color Shift mapped to ESC */
/*40*/    BOTH+010, 0, 0, 0, 0, 0, 0, 0, /* " ", */
/*50*/    UC+021, UC+031, 021, 015, 031, UC+035, UC+011, UC+015, /* ()*+,-./ */
/*60*/    076, 025, 017, 007, 013, 023, 033, 027, /* 0-7 */
/*70*/    003, 066, 0, 0, 0, 011, 0, 0, /* 89:;<=>? */
/*00*/    040, UC+006, UC+062, UC+034, UC+022, UC+002, UC+032, UC+064,  /* A-G */
/*10*/    UC+050, UC+014, UC+026, UC+036, UC+044, UC+070, UC+030, UC+060, /* H-O */
/*20*/    UC+054, UC+056, UC+024, UC+012, 040, 016, 074, 046, /* P-W */
/*30*/    UC+072, UC+052, UC+042, 0, 0, 0, 0, UC+005, /* X-Z, */
/*40*/    00, 006, 062, 034, 022, 002, 032, 064,    /* a-g */
/*50*/    050, 014, 026, 036, 044, 070, 030, 060, /* h-o */
/*60*/    054, 056, 024, 012, 040, 016, 074, 046, /* p-w */
/*70*/    072, 052, 042, 0, 005, 0, UC+035, BOTH+077 /* x-z, */
    };

/* PETR data structures

   petr_dev     PETR device descriptor
   petr_unit     PETR unit
   petr_reg      PETR register list
*/

UNIT petr_unit = {
    UDATA (&petr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0),
           SERIAL_IN_WAIT
    };

REG petr_reg[] = {
    { ORDATAD (BUF, petr_unit.buf, 18, "18-bit buffer to store up to three lines of                                     paper tape input") },
    { FLDATAD (UC, petr_uc, UC_V, "upper case/lower case state") },
    { FLDATAD (DONE, iosta, IOS_V_PETR, "input ready flag") },
    { ORDATA (HOLD, petr_hold, 9), REG_HRO },
    { ORDATA (STATE, petr_state, 5), REG_HRO },
    { FLDATA (WAIT, petr_wait, 0), REG_HRO },
    { DRDATAD (POS, petr_unit.pos, T_ADDR_W, "position in input file"), PV_LEFT },
    { DRDATAD (TIME, petr_unit.wait, 24, "time from I/O initiation to interrupt"), PV_LEFT },
    { DRDATA (LEADER, petr_leader, 6), REG_HRO },
    { FLDATAD (STOP_IOE, petr_stopioe, 0, "stop on I/O error") },
    { NULL }
    };

MTAB petr_mod[] = {
    { UNIT_ASCII, UNIT_ASCII, "ASCII", "ASCII", NULL },
    { UNIT_ASCII, 0,          "FLEXO", "FLEXO", NULL },
    { 0 }
    };

/* Debug flags */
#define ERROR_MSG   (1 << 0)
#define TRACE_MSG   (1 << 1)
#define VERBOSE_MSG (1 << 2)

/* Debug Flags */
static DEBTAB petr_dt[] = {
    { "ERROR",  ERROR_MSG },
    { "TRACE",  TRACE_MSG },
    { "VERBOSE",VERBOSE_MSG },
    { NULL,     0 }
};

DEVICE petr_dev = {
    "PETR", &petr_unit, petr_reg, petr_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &petr_reset,
    &petr_boot, &petr_attach, NULL,
    NULL, DEV_DEBUG, (ERROR_MSG),
    petr_dt, NULL
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
    { DRDATAD (POS, ptp_unit.pos, T_ADDR_W, "position in the output file"), PV_LEFT },
    { DRDATAD (TIME, ptp_unit.wait, 24, "time from I/O initiation to interrupt"), PV_LEFT },
    { FLDATAD (STOP_IOE, ptp_stopioe, 0, "stop on I/O error") },
    { NULL }
    };

MTAB ptp_mod[] = {
    { 0 }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, ptp_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG, (ERROR_MSG|TRACE_MSG),
    petr_dt, NULL
    };

/* TTI data structures

   tti_dev      TTI device descriptor
   tti_unit     TTI unit
   tti_reg      TTI register list
*/

UNIT tti_unit = { UDATA (&tti_svc, 0, 0), KBD_POLL_WAIT };

REG tti_reg[] = {
    { ORDATAD (BUF, tty_buf, 6, "typewrite buffer (shared)") },
    { FLDATAD (UC, tty_uc, UC_V, "upper case/lower case state (shared)") },
    { ORDATA (HOLD, tti_hold, 9), REG_HRO },
    { FLDATAD (DONE, iosta, IOS_V_TTI, "input ready flag") },
    { DRDATAD (POS, tti_unit.pos, T_ADDR_W, "number of characters input"), PV_LEFT },
    { DRDATAD (TIME, tti_unit.wait, 24, "keyboard polling interval"), REG_NZ + PV_LEFT },
    { NULL }
    };

MTAB tti_mod[] = {
    { 0 }
    };

DEVICE tti_dev = {
    "TTI", &tti_unit, tti_reg, tti_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tty_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG, (ERROR_MSG|TRACE_MSG),
    petr_dt, NULL
    };

/* TTO data structures

   tto_dev      TTO device descriptor
   tto_unit     TTO unit
   tto_reg      TTO register list
*/

UNIT tto_unit = { UDATA (&tto_svc, 0, 0), SERIAL_OUT_WAIT * 10 };

REG tto_reg[] = {
    { ORDATAD (BUF, tty_buf, 6, "typewrite buffer (shared)") },
    { FLDATAD (UC, tty_uc, UC_V, "upper case/lower case state (shared)") },
    { FLDATAD (DONE, iosta, IOS_V_TTO, "output done flag") },
    { DRDATAD (POS, tto_unit.pos, T_ADDR_W, "number of characters output"), PV_LEFT },
    { DRDATAD (TIME, tto_unit.wait, 24, "time from I/O initiation to interrupt"), PV_LEFT },
    { NULL }
    };

MTAB tto_mod[] = {
    { 0 }
    };

DEVICE tto_dev = {
    "TTO", &tto_unit, tto_reg, tto_mod,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &tty_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEBUG, (ERROR_MSG|TRACE_MSG),
    petr_dt, NULL
    };

/* Photoelectric Tape Reader:

The PETR is a 250 line per minute Ferranti photoelectric paper tape reader
using standard seven-hole Flexowriter tape that was modified to solid state
circuitry. Lines without seventh hole punched are ignored by the PETR.
As each line of the tape is read in, the data is stored into an 18-bit BUF
register with bits mapped as follows:

Tape    BUF
0       0
1       3
2       6
3       9
4       12
5       15

Up to three lines of tape may be read into a single the single BUF register.
Before subsequent lines are read, the BUF register is cycled one bit right.

The PETR reads data from or a disk file.  The POS register specifies the
number of the next data item to be read. Thus, by changing POS, the user can
backspace or advance the reader.

The PETR supports the BOOT command.  BOOT PETR switches the CPU to Read-In
mode, and starts the processor running.
*/

int32 petr (int32 inst, int32 dev, int32 dat)
{
    int32 tmpAC = 0;
    int i = 0;
    t_stat result;
    ios = 1;

    for (i=0;i<inst;i++) {
        do {
            result = petr_svc(&petr_unit);
            if (result != SCPE_OK) {
                sim_printf("PETR: Read error\n");
                break;
            }
        } while ((petr_unit.buf & 0100) == 0);  /* NOTE: Lines without seventh hole are ignored by PETR. */
        petr_unit.buf &= 077;   /* Mask to 6 bits. */
        tmpAC |= ((petr_unit.buf & 001) >> 0) << 17;    /* bit 0 */
        tmpAC |= ((petr_unit.buf & 002) >> 1) << 14;    /* bit 3 */
        tmpAC |= ((petr_unit.buf & 004) >> 2) << 11;    /* bit 6 */
        tmpAC |= ((petr_unit.buf & 010) >> 3) << 8;     /* bit 9 */
        tmpAC |= ((petr_unit.buf & 020) >> 4) << 5;     /* bit 12 */
        tmpAC |= ((petr_unit.buf & 040) >> 5) << 2;     /* bit 15 */

        if (i < (inst-1)) {
            uint32 bit0 = (tmpAC & 1) << 17;
            TRACE_PRINT(petr_dev, TRACE_MSG, ("PETR read [%04x=0x%02x] %03o\n", petr_unit.pos-1, petr_unit.buf, petr_unit.buf));
            tmpAC >>= 1;
            tmpAC |= bit0;
        } else {
            TRACE_PRINT(petr_dev, TRACE_MSG, ("PETR read [%04x=0x%02x] %03o, tmpAC=%06o\n", petr_unit.pos-1, petr_unit.buf, petr_unit.buf, tmpAC));
        }
    }
    return tmpAC;

    /*  sim_activate (&petr_unit, petr_unit.wait); */             /* start reader */

}

/* Unit service */

t_stat petr_svc (UNIT *uptr)
{
int32 temp;

if ((uptr->flags & UNIT_ATT) == 0) {                    /* attached? */
    ios = 0;
    return SCPE_OK;
    }

    if ((temp = getc (uptr->fileref)) != EOF) {         /* no, get raw char */
        uptr->pos = uptr->pos + 1;                          /* if not eof, count */
    }
    if (temp == EOF) {                                      /* end of file? */
        if (feof (uptr->fileref)) {
                ios = 0;
                return SCPE_IOERR;
        }
        else sim_perror ("PETR I/O error");
        clearerr (uptr->fileref);
        ios = 0;
        return SCPE_IOERR;
    }

    uptr->buf = temp;

ios = 0;

return SCPE_OK;
}

/* Reset routine */

t_stat petr_reset (DEVICE *dptr)
{
    petr_state = 0;                                          /* clear state */
    petr_wait = 0;
    petr_hold = 0;
    petr_uc = 0;
    petr_unit.buf = 0;
    iosta = iosta & ~IOS_PETR;                               /* clear flag */
    sim_cancel (&petr_unit);                                 /* deactivate unit */
    return SCPE_OK;
}

/* Attach routine */

t_stat petr_attach (UNIT *uptr, CONST char *cptr)
{
    petr_leader = PETR_LEADER;                                /* set up leader */
    return attach_unit (uptr, cptr);
}

/* Bootstrap routine */
extern t_stat cpu_set_mode (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern UNIT cpu_unit;

//#define SANITY_CHECK_TAPE

/* Switches the CPU to READIN mode and starts execution. */
t_stat petr_boot (int32 unitno, DEVICE *dptr)
{
    t_stat reason = SCPE_OK;

#ifdef SANITY_CHECK_TAPE
    int32 AC, MBR, MAR, IR = 0;
    int32 blkcnt, chksum = 0, fa, la;
    int32 addr, tdata;
#endif /* SANITY_CHECK_TAPE */

    /* Switch to READIN mode. */
    cpu_set_mode(&cpu_unit, UNIT_MODE_READIN, NULL, NULL);
#ifdef SANITY_CHECK_TAPE
    for(;(IR != 2) && (IR != 1);) {
        AC = petr(3,0,0);   /* Read three chars from tape into AC */
        MAR = AC & AMASK;   /* Set memory address */
        IR = AC >> 16;

        if (!MEM_ADDR_OK(MAR)) {
            TRACE_PRINT(petr_dev, ERROR_MSG, ("READIN: Tape address out of range.\n"));
            reason = SCPE_FMT;
        }

        switch (IR) {
            case 00:    /* Storage (sto x) */
            case 03:    /* Storage (opr x) */
                MBR = petr(3,0,0);  /* Read three characters from tape. */
                TRACE_PRINT(petr_dev, ERROR_MSG, ("READIN: sto @%06o = %06o\n", MAR, MBR));
                sim_printf("[%06o] = %06o\n", MAR, MBR);
                break;
            case 02:    /* Transfer Control (trn x) Start Execution */
                PC = MAR;
                reason = SCPE_OK;   /* let SIMH start execution. */
                TRACE_PRINT(petr_dev, ERROR_MSG, ("READIN: trn %06o (Start Execution)\n", PC));
                reason = cpu_set_mode(&cpu_unit, 0, NULL, NULL);
                break;
            case 01:    /* Transfer (add x) - Halt */
                PC = MAR;
                reason = SCPE_STOP; /* let SIMH halt. */
                TRACE_PRINT(petr_dev, ERROR_MSG, ("READIN: add %06o (Halt)\n", PC));
                reason = cpu_set_mode(&cpu_unit, 0, NULL, NULL);
                break;
            default:
                reason = SCPE_IERR;
                break;
        }
    }

    blkcnt = 0;
    while (1) {
        chksum = 0;

        fa = petr(3,0,0);  /* Read three characters from tape. */

        if ((fa & 0400000) || (fa & 0200000)) {
            break;
        }

        chksum += fa;
        if (chksum > 0777777) {
            chksum +=1;
        }
        chksum &= 0777777;

        la = petr(3,0,0);  /* Read three characters from tape. */

        chksum += la;
        if (chksum > 0777777) {
            chksum +=1;
        }
        chksum &= 0777777;

        la = (~la) & 0177777;

        sim_printf("First Address=%06o, Last Address=%06o\n", fa, la);

        for(addr = fa; addr <= la; addr++) {
            tdata = petr(3,0,0);  /* Read three characters from tape. */
            chksum += tdata;
            if (chksum > 0777777) {
                chksum +=1;
            }
            chksum &= 0777777;
        }

        chksum = (~chksum) & 0777777;

        tdata = petr(3,0,0);

        if (chksum != tdata) {
            reason = SCPE_FMT;
        }

        sim_printf("Block %d: Calculated checksum=%06o, real checksum=%06o, %s\n", blkcnt, chksum, tdata, chksum == tdata ? "OK" : "BAD Checksum!");
        blkcnt++;
    }

    fseek (petr_dev.units[0].fileref, 0, SEEK_SET);
#endif /* SANITY_CHECK_TAPE */

    /* Start Execution */
    return (reason);

}

/* Paper tape punch: punches standard seven-hole Flexowriter tape. */

int32 ptp (int32 inst, int32 dev, int32 dat)
{
    iosta = iosta & ~IOS_PTP;                               /* clear flag */
    ptp_unit.buf = dat & 0177;
    ptp_svc (&ptp_unit);
    /* sim_activate (&ptp_unit, ptp_unit.wait); */               /* start unit */
    return dat;
}

/* Unit service */

t_stat ptp_svc (UNIT *uptr)
{
    ios = 1;                                            /* restart */
    iosta = iosta | IOS_PTP;                                /* set flag */
    if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
        return SCPE_UNATT;
    if (putc (uptr->buf, uptr->fileref) == EOF) {           /* I/O error? */
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
    iosta = iosta & ~IOS_PTP;                               /* clear flag */
    sim_cancel (&ptp_unit);                                 /* deactivate unit */
    return SCPE_OK;
}

/* Typewriter IOT routines */

int32 tti (int32 inst, int32 dev, int32 dat)
{
    iosta = iosta & ~IOS_TTI;                               /* clear flag */
    return tty_buf & 077;
}

int32 tto (int32 inst, int32 dev, int32 dat)
{
    tty_buf = dat & TT_WIDTH;                               /* load buffer */
    ios = 0;
    tto_svc(&tto_unit);
    /* sim_activate (&tto_unit, tto_unit.wait); */               /* activate unit */
    return dat;
}

/* Unit service routines */

t_stat tti_svc (UNIT *uptr)
{
    int32 in = 0, temp = 0;

    sim_activate (uptr, uptr->wait);                        /* continue poll */
    if (tti_hold & CW) {                                    /* char waiting? */
        tty_buf = tti_hold & TT_WIDTH;                      /* return char */
        tti_hold = 0;                                       /* not waiting */
    } else {
        if ((temp = sim_poll_kbd ()) < SCPE_KFLAG) return temp;
        if (temp & SCPE_BREAK) return SCPE_OK;              /* ignore break */
        temp = temp & 0177;
        if (temp == 0177) temp = '\b';                      /* rubout? bs */
        sim_putchar (temp);                                 /* echo */
        if (temp == '\r') sim_putchar ('\n');               /* cr? add nl */
        in = ascii_to_flexo[temp];                         /* translate char */

        if (in == 0) return SCPE_OK;                        /* no xlation? */
        if ((in & BOTH) || ((in & UC) == (tty_uc & UC))) {
            tty_buf = in & TT_WIDTH;
        } else {                                              /* must shift */
            tty_uc = in & UC;                               /* new case */
            tty_buf = tty_uc? FLEXO_UC: FLEXO_LC;
            tti_hold = in | CW;                             /* set 2nd waiting */
        }
    }
    iosta = iosta | IOS_TTI;                                /* set flag */
    TRACE_PRINT(tti_dev, TRACE_MSG, ("TTI read ASCII: %02x / FLEXO=%03o\n", temp, tty_buf));
    uptr->pos = uptr->pos + 1;
    return SCPE_OK;
}

t_stat tto_svc (UNIT *uptr)
{
    int32 c = 0;
    t_stat r;

    if (tty_buf == FLEXO_UC) tty_uc = UC;                  /* upper case? */
    else if (tty_buf == FLEXO_LC) tty_uc = 0;              /* lower case? */
    else {
        c = flexo_to_ascii[tty_buf | tty_uc];              /* translate */
        if (c && ((r = sim_putchar_s (c)) != SCPE_OK)) {    /* output; error? */
            sim_activate (uptr, uptr->wait);                /* retry */
            return ((r == SCPE_STALL)? SCPE_OK: r);
        }
    }
    iosta = iosta | IOS_TTO;                                /* set flag */
    uptr->pos = uptr->pos + 1;
    if (c == '\r') {                                        /* cr? add lf */
        sim_putchar ('\n');
        uptr->pos = uptr->pos + 1;
    }
    return SCPE_OK;
}

/* Reset routine */

t_stat tty_reset (DEVICE *dptr)
{
    tmxr_set_console_units (&tti_unit, &tto_unit);
    tty_buf = 0;                                            /* clear buffer */
    tty_uc = 0;                                             /* clear case */
    tti_hold = 0;                                           /* clear hold buf */
    iosta = (iosta & ~IOS_TTI) | IOS_TTO;                   /* clear flag */
    sim_activate (&tti_unit, tti_unit.wait);                /* activate keyboard */
    sim_cancel (&tto_unit);                                 /* stop printer */
    return SCPE_OK;
}
