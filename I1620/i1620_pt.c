/* i1620_pt.c: IBM 1621/1624 paper tape reader/punch simulator

   Copyright (c) 2002-2017, Robert M Supnik

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

   ptr          1621 paper tape reader
   ptp          1624 paper tape punch

   23-Jun-17    RMS     PTR/PTP errors does not set read check
   10-Jun-17    RMS     Fixed typo in PTP unit (Dave Wise)
   26-May-17    RMS     Added deferred IO
   25-May-17    RMS     Fixed treatment of X0C82 on RN (Tom McBride)
   18-May-17    RMS     Separated EOF error from other IO errors (Dave Wise)
   23-Feb-15    TFM     Fixed RA, RBPT to preserve flags on RM at end (Tom McBride)
   09-Feb-15    TFM     Fixed numerous translation problems (Tom McBride)
   09-Feb-15    TFM     Fixed pack/unpack errors in binary r/w (Tom McBride)
   21-Dec-13    RMS     Fixed translation of paper tape code X0C
   10-Dec-13    RMS     Fixed DN wraparound (Bob Armstrong)
   19-Mar-12    RMS     Fixed declaration of io_stop (Mark Pizzolato)
   21-Sep-05    RMS     Revised translation tables for 7094/1401 compatibility
   25-Apr-03    RMS     Revised for extended file support
*/

#include "i1620_defs.h"

#define PT_EL   0x80                                    /* end record */
#define PT_X    0x40                                    /* X */
#define PT_O    0x20                                    /* O */
#define PT_C    0x10                                    /* C */
#define PT_FD   0x7F                                    /* deleted */

static uint32 ptr_mode = 0;                             /* normal/binary */
static uint32 ptp_mode = 0;

extern uint8 M[MAXMEMSIZE];
extern uint8 ind[NUM_IND];
extern UNIT cpu_unit;
extern uint32 io_stop;
extern uint32 PAR, cpuio_opc, cpuio_cnt;

t_stat ptr_svc (UNIT *uptr);
t_stat ptr_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);
t_stat ptr_read (uint8 *c, t_bool ignfeed);
t_stat ptp_svc (UNIT *uptr);
t_stat ptp_reset (DEVICE *dptr);
t_stat ptp_write (uint32 c);
t_stat ptp_num (void);

/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit descriptor
   ptr_reg      PTR register list
*/

UNIT ptr_unit = {
    UDATA (&ptr_svc, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0), SERIAL_OUT_WAIT
    };

REG ptr_reg[] = {
    { FLDATAD (BIN, ptr_mode, 0, "binary mode flag") },
    { DRDATAD (POS, ptr_unit.pos, T_ADDR_W, "position in the input file"), PV_LEFT },
    { DRDATAD (TIME, ptr_unit.wait, 24, "reader character delay"), PV_LEFT },
    { DRDATAD (CPS, ptr_unit.DEFIO_CPS, 24, "Character Input Rate"), PV_LEFT },
    { NULL }
    };

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    &ptr_boot, NULL, NULL,
    NULL, DEV_DEFIO
    };

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit descriptor
   ptp_reg      PTP register list
*/

UNIT ptp_unit = {
    UDATA (&ptp_svc, UNIT_SEQ+UNIT_ATTABLE, 0), SERIAL_OUT_WAIT
    };

REG ptp_reg[] = {
    { FLDATAD (BIN, ptp_mode, 0, "binary mode flag") },
    { DRDATAD (POS, ptp_unit.pos, T_ADDR_W, "position in the output file"), PV_LEFT },
    { DRDATAD (TIME, ptp_unit.wait, 24, "punch character delay"), PV_LEFT },
    { DRDATAD (CPS, ptp_unit.DEFIO_CPS, 24, "Character output rate"), PV_LEFT },
    { NULL }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, NULL, NULL,
    NULL, DEV_DEFIO
    };

/* Data tables */

/* Paper tape reader odd parity chart: 1 = bad, 0 = ok */

const int8 bad_par[128] = {
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,        /* 00 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,        /* 10 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,        /* 20 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,        /* 30 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,        /* 40 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,        /* 50 */
 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,        /* 60 */
 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0         /* 70 */
 };

/* Paper tape read (7b) to numeric (one digit) */

const int8 ptr_to_num[128] = {
   -1, 0x01, 0x02,   -1, 0x04,   -1,   -1, 0x07,        /* - */
 0x08,   -1,   -1, 0x0B,   -1,   -1,   -1,   -1,
 0x00,   -1,   -1, 0x03,   -1, 0x05, 0x06,   -1,        /* C */
   -1, 0x09,   -1,   -1, 0x0C,   -1,   -1,   -1,
 0x00,   -1,   -1, 0x03,   -1, 0x05, 0x06,   -1,        /* O */
   -1, 0x09, 0x0A,   -1, 0x0C,   -1,   -1, 0x0F,
   -1, 0x01, 0x02,   -1, 0x04,   -1,   -1, 0x07,        /* OC */
 0x08,   -1,   -1, 0x0B,   -1,   -1,   -1,   -1,
 0x10,   -1,   -1, 0x13,   -1, 0x15, 0x16,   -1,        /* X */
   -1, 0x19, 0x1A,   -1, 0x1C,   -1,   -1, 0x1F,
   -1, 0x11, 0x12,   -1, 0x14,   -1,   -1, 0x17,        /* XC */
 0x18,   -1,   -1, 0x1B,   -1,   -1,   -1,   -1,
   -1, 0x01, 0x02,   -1, 0x04,   -1,   -1, 0x07,        /* XO */
 0x08,   -1,   -1, 0x0B,   -1,   -1,   -1,   -1,
 0x10,   -1,   -1, 0x03,   -1, 0x05, 0x06,   -1,        /* XOC */
   -1, 0x09, 0x1A,   -1, 0x0C,    -1,  -1,   -1         /* X0C82 treated as flagged RM, RN only (tfm) */
 };

/* Paper tape read (7b) to alphameric (two digits) */

const int8 ptr_to_alp[128] = {
 0x00, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,        /* - */
 0x78, 0x79,   -1, 0x33, 0x34,   -1,   -1,   -1,
 0x00, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,        /* C */
 0x78, 0x79,   -1, 0x33, 0x34,   -1,   -1,   -1,
 0x70, 0x21, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,        /* O */
 0x68, 0x69, 0x0A, 0x23, 0x24,   -1,   -1, 0x0F,
 0x70, 0x21, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,        /* OC */
 0x68, 0x69, 0x0A, 0x23, 0x24,   -1,   -1, 0x0F,
 0x20, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,        /* X */
 0x58, 0x59, 0x5A, 0x13, 0x14,   -1,   -1, 0x5F,
 0x20, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,        /* XC */
 0x58, 0x59, 0x5A, 0x13, 0x14,   -1,   -1, 0x5F,
 0x10, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,        /* XO */
 0x48, 0x49,   -1, 0x03, 0x04,   -1,   -1,   -1,
 0x10, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,        /* XOC */
 0x48, 0x49,   -1, 0x03, 0x04,   -1,   -1,   -1
 };

/* Numeric (flag + digit) to paper tape punch */

const int8 num_to_ptp[32] = {
 0x20, 0x01, 0x02, 0x13, 0x04, 0x15, 0x16, 0x07,        /* 0 */
 0x08, 0x19, 0x2A,   -1, 0x1C,   -1,   -1, 0x2F,
 0x40, 0x51, 0x52, 0x43, 0x54, 0x45, 0x46, 0x57,        /* F + 0 */
 0x58, 0x49, 0x4A,   -1, 0x4C,   -1,   -1, 0x4F
 };

/* Alphameric (two digits) to paper tape punch */

const int8 alp_to_ptp[256] = {
 0x10,   -1,   -1, 0x6B, 0x7C,   -1,   -1,   -1,        /* 00 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
 0x70,   -1,   -1, 0x5B, 0x4C,   -1,   -1,   -1,        /* 10 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
 0x40, 0x31,   -1, 0x3B, 0x2C,   -1,   -1,   -1,        /* 20 */ 
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1, 0x0B, 0x1C,   -1,   -1,   -1,        /* 30 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1, 0x61, 0x62, 0x73, 0x64, 0x75, 0x76, 0x67,        /* 40 */
 0x68, 0x79,   -1,   -1,   -1,   -1,   -1,   -1,
 0x40, 0x51, 0x52, 0x43, 0x54, 0x45, 0x46, 0x57,        /* 50 */
 0x58, 0x49,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1, 0x32, 0x23, 0x34, 0x25, 0x26, 0x37,        /* 60 */
 0x38, 0x29,   -1,   -1,   -1,   -1,   -1,   -1,
 0x20, 0x01, 0x02, 0x13, 0x04, 0x15, 0x16, 0x07,        /* 70 */
 0x08, 0x19,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* 80 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* 90 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* A0 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* B0 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* C0 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* D0 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* E0 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,        /* F0 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1
 }; 

/* Paper tape reader IO init routine */

t_stat ptr (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
if ((op != OP_RN) && (op != OP_RA))                     /* RN & RA only */
    return STOP_INVFNC;
if ((ptr_unit.flags & UNIT_ATT) == 0)                   /* catch unattached */
    return SCPE_UNATT;
ptr_mode = 0;
cpuio_set_inp (op, IO_PTR, &ptr_unit);
return SCPE_OK;
}

/* Binary paper tape reader IO init routine */

t_stat btr (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
if (op != OP_RA)                                        /* RA only */
    return STOP_INVFNC;
if ((ptr_unit.flags & UNIT_ATT) == 0)                   /* catch unattached */
    return SCPE_UNATT;
ptr_mode = 1;
cpuio_set_inp (op, IO_BTR, &ptr_unit);
return SCPE_OK;
}

/* Paper tape unit service

   - If over the limit, cancel IO and return error.
   - If unattached, reschedule and return error.
   - Transfer a digit/character.
   - Hard errors halt the operation and the system.
   - Parity errors place an invalid character in memory and set
     RDCHK, but the read continues until end of record.  If IO
     stop is set, the system then halts.
*/

t_stat ptr_svc (UNIT *uptr)
{
t_stat r;
uint8 ptc;
int8 mc;

if (cpuio_cnt >= MEMSIZE) {                             /* over the limit? */
    cpuio_clr_inp (uptr);                               /* done */
    return STOP_RWRAP;
    }
DEFIO_ACTIVATE (uptr);                                  /* sched another xfer */
if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return SCPE_UNATT;

switch (cpuio_opc) {

    case OP_RN:                                         /* read numeric */
        r = ptr_read (&ptc, TRUE);                      /* read frame */
        if (r != SCPE_OK)                               /* error? */
            return r;
        if (ptc & PT_EL) {                              /* end record? */
            M[PAR] = REC_MARK;                          /* store rec mark */
            break;
            }
        mc = ptr_to_num[ptc];                           /* translate char */
        if ((bad_par[ptc]) || (mc < 0)) {               /* bad par. or char? */
            ind[IN_RDCHK] = 1;                          /* set read check */
            mc = 0;                                     /* store zero */
            }
        M[PAR] = mc;                                    /* store translated char */
        PP (PAR);                                       /* incr mem addr */
        cpuio_cnt++;
        return SCPE_OK;

    case OP_RA:                                         /* read alphameric */
        r = ptr_read (&ptc, TRUE);                      /* read frame */
        if (r != SCPE_OK)                               /* error? */
        return r;
        if (ptc & PT_EL) {                              /* end record? */
            M[PAR] = (M[PAR] & FLAG) | REC_MARK;        /* store alpha RM */
            M[PAR - 1] = M[PAR - 1] & FLAG;             /* and preserve flags */
            break;
            }
        if (ptr_mode == 0) {                            /* normal mode? */
            mc = ptr_to_alp[ptc];                       /* translate */
            if (bad_par[ptc] || (mc < 0)) {             /* bad par or char? */
                ind[IN_RDCHK] = 1;                      /* set read check */
                mc = 0;                                 /* store blank */
                }
            M[PAR] = (M[PAR] & FLAG) | (mc & DIGIT);    /* store 2 digits */
            M[PAR - 1] = (M[PAR - 1] & FLAG) | ((mc >> 4) & DIGIT);
            }
        else {                                          /* binary mode */
            if (bad_par[ptc])                           /* bad parity? */
                ind[IN_RDCHK] = 1;                      /* set read check */
            M[PAR] = (M[PAR] & FLAG) | (ptc & 07);      /* store 2 digits */
            M[PAR - 1] = (M[PAR - 1] & FLAG) |
               (((ptc >> 4) & 06) | ((ptc >> 3) & 1));
            }
       PAR = ADDR_A (PAR, 2);                           /* incr mem addr */
       cpuio_cnt = cpuio_cnt + 2;
       return SCPE_OK;       

    default:                                            /* invalid function */
        break;
        }

/* IO is complete */

cpuio_clr_inp (uptr);                                   /* clear IO in progress */
if ((ind[IN_RDCHK] != 0) && (io_stop != 0))             /* parity error? */
    return STOP_INVCHR;
return SCPE_OK;
}

/* Read ptr frame - all errors are 'hard' errors and halt the system */

t_stat ptr_read (uint8 *c, t_bool ignfeed)
{
int32 temp;

do {
    if ((temp = getc (ptr_unit.fileref)) == EOF) {      /* read char */
        if (feof (ptr_unit.fileref)) {                  /* EOF? */
            sim_printf ("PTR end of file\n");
            clearerr (ptr_unit.fileref);
            return SCPE_EOF;
            }
        else
            sim_perror ("PTR I/O error");               /* no, io err */
        clearerr (ptr_unit.fileref);
        return SCPE_IOERR;
        }
    *c = temp & 0377;                                   /* save char */
    ptr_unit.pos = ptr_unit.pos + 1;                    /* incr file addr */
    } while (ignfeed && (*c == PT_FD));                 /* until not feed */
return SCPE_OK;
}

/* Reset routine */

t_stat ptr_reset (DEVICE *dptr)
{
sim_cancel (&ptr_unit);
ptr_mode = 0;
return SCPE_OK;
}

/* Bootstrap routine */

static const uint8 boot_rom[] = {
 3, 6, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0,                    /* RNPT 0 */
 };

#define BOOT_START      0
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (uint8))

t_stat ptr_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern uint32 saved_PC;

for (i = 0; i < BOOT_LEN; i++)
    M[BOOT_START + i] = boot_rom[i];
saved_PC = BOOT_START;
return SCPE_OK;
}

/* Paper tape punch IO init routine */

t_stat ptp (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
if ((op != OP_WN) && (op != OP_WA) && (op != OP_DN))
    return STOP_INVFNC;
if ((ptp_unit.flags & UNIT_ATT) == 0)                   /* catch unattached */
    return SCPE_UNATT;
ptp_mode = 0;
cpuio_set_inp (op, IO_PTP, &ptp_unit);
return SCPE_OK;
}

/* Binary paper tape punch IO init routine */

t_stat btp (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
if (op != OP_WA)                                        /* WA only */
    return STOP_INVFNC;
if ((ptp_unit.flags & UNIT_ATT) == 0)                   /* catch unattached */
    return SCPE_UNATT;
ptp_mode = 1;
cpuio_set_inp (op, IO_BTP, &ptp_unit);
return SCPE_OK;
}

/* Paper tape punch unit service routine */

t_stat ptp_svc (UNIT *uptr)
{
int8 ptc;
uint8 z, d;
t_stat r;

if ((cpuio_opc != OP_DN) && (cpuio_cnt >= MEMSIZE)) {   /* wrap, ~dump? */
    cpuio_clr_inp (uptr);                               /* done */
    return STOP_RWRAP;
    }
DEFIO_ACTIVATE (uptr);                                  /* sched another xfer */
if ((uptr->flags & UNIT_ATT) == 0)                      /* not attached? */
    return SCPE_UNATT;

switch (cpuio_opc) {                                    /* decode op */

    case OP_DN:
        if ((cpuio_cnt != 0) && ((PAR % 20000) == 0))   /* done? */
            break;
        return ptp_num ();                              /* write numeric */

    case OP_WN:
        if ((M[PAR] & REC_MARK) == REC_MARK)            /* end record? */
            break;
        return ptp_num ();                              /* write numeric */

    case OP_WA:
        d = M[PAR] & DIGIT;                             /* get digit */
        z = M[PAR - 1] & DIGIT;                         /* get zone */
        if ((d & REC_MARK) == REC_MARK)                 /* 8-2 char? */
            break;                                      /* end record */
        if (ptp_mode == 0) {                            /* normal mode */
            ptc = alp_to_ptp[(z << 4) | d];             /* translate pair */
            if (ptc < 0) {                              /* bad char? */
                ind[IN_WRCHK] = 1;                      /* write check */
                CRETIOE (io_stop, STOP_INVCHR);
                }
            }
        else {                                          /* binary mode */
            ptc = ((z & 06) << 4) | ((z & 01) << 3) | (d & 07);
            if (bad_par[ptc])                           /* set parity */
                ptc = ptc | PT_C;
            }
        r = ptp_write (ptc);                            /* write char */
        if (r != SCPE_OK)                               /* error? */
            return r;
        PAR = ADDR_A (PAR, 2);                          /* incr mem addr */
        cpuio_cnt = cpuio_cnt + 2;
        return SCPE_OK;

    default:                                            /* invalid function */
        break;
        }

/* IO is complete */

ptp_write (PT_EL);                                      /* write record mark */
cpuio_clr_inp (uptr);                                   /* IO complete */
return SCPE_OK;
}

/* Punch tape numeric - cannot generate parity errors */

t_stat ptp_num (void)
{
t_stat r;
uint8 d;
int8 ptc;

d = M[PAR] & (FLAG | DIGIT);                            /* get char */
ptc = num_to_ptp[d];                                    /* translate digit */
if (ptc < 0) {                                          /* bad char? */
    ind[IN_WRCHK] = 1;                                  /* write check */
    CRETIOE(io_stop, STOP_INVCHR);                                  
    }
r = ptp_write (ptc);                                    /* write char */
if (r != SCPE_OK)                                       /* error? */
    return r;
PP (PAR);                                               /* incr mem addr */
cpuio_cnt++;
return SCPE_OK;
}

/* Write ptp frame - all errors are hard errors */

t_stat ptp_write (uint32 c)
{
if (putc (c, ptp_unit.fileref) == EOF) {                /* write char */
    sim_perror ("PTP I/O error");
    clearerr (ptp_unit.fileref);
    return SCPE_IOERR;
    }
ptp_unit.pos = ptp_unit.pos + 1;                        /* count char */
return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
sim_cancel (&ptp_unit);
ptp_mode = 0;
return SCPE_OK;
}
