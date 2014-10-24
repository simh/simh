/* i1620_pt.c: IBM 1621/1624 paper tape reader/punch simulator

   Copyright (c) 2002-2013, Robert M Supnik

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

extern uint8 M[MAXMEMSIZE];
extern uint8 ind[NUM_IND];
extern UNIT cpu_unit;
extern uint32 io_stop;

t_stat ptr_reset (DEVICE *dptr);
t_stat ptr_boot (int32 unitno, DEVICE *dptr);
t_stat ptr_read (uint8 *c, t_bool ignfeed);
t_stat ptp_reset (DEVICE *dptr);
t_stat ptp_write (uint32 c);
t_stat ptp_num (uint32 pa, uint32 len, t_bool dump);

/* PTR data structures

   ptr_dev      PTR device descriptor
   ptr_unit     PTR unit descriptor
   ptr_reg      PTR register list
*/

UNIT ptr_unit = {
    UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE+UNIT_ROABLE, 0)
    };

REG ptr_reg[] = {
    { DRDATA (POS, ptr_unit.pos, T_ADDR_W), PV_LEFT },
    { NULL }
    };

DEVICE ptr_dev = {
    "PTR", &ptr_unit, ptr_reg, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptr_reset,
    &ptr_boot, NULL, NULL
    };

/* PTP data structures

   ptp_dev      PTP device descriptor
   ptp_unit     PTP unit descriptor
   ptp_reg      PTP register list
*/

UNIT ptp_unit = {
    UDATA (NULL, UNIT_SEQ+UNIT_ATTABLE, 0)
    };

REG ptp_reg[] = {
    { DRDATA (POS, ptp_unit.pos, T_ADDR_W), PV_LEFT },
    { NULL }
    };

DEVICE ptp_dev = {
    "PTP", &ptp_unit, ptp_reg, NULL,
    1, 10, 31, 1, 8, 8,
    NULL, NULL, &ptp_reset,
    NULL, NULL, NULL
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
 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,        /* - */
 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x00, 0x0E, 0x0F,
 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,        /* C */
 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x00, 0x0E, 0x0F,
 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,        /* O */
 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x00, 0x0E, 0x0F,
 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,        /* OC */
 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x00, 0x0E, 0x0F,
 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,        /* X */
 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x10, 0x1E, 0x1F,
 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,        /* XC */
 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x10, 0x1E, 0x1F,
 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,        /* XO */
 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x00, 0x0E, 0x0F,
 0x10, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,        /* XOC */
 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x00, 0x0E, 0x0F
 };

/* Paper tape read (7b) to alphameric (two digits)
   Codes XO82, 82, XO842, 842 do not have consistent translations
*/

const int8 ptr_to_alp[128] = {
 0x00, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,        /* - */
 0x78, 0x79,   -1, 0x33, 0x34, 0x70,   -1, 0x0F,
 0x00, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,        /* C */
 0x78, 0x79,   -1, 0x33, 0x34, 0x70,   -1, 0x0F,
 0x70, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,        /* O */
 0x68, 0x69, 0x0A, 0x23, 0x24, 0x60, 0x0E, 0x0F,
 0x70, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,        /* OC */
 0x68, 0x69, 0x0A, 0x23, 0x24, 0x60, 0x0E, 0x0F,
 0x20, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,        /* X */
 0x58, 0x59, 0x5A, 0x13, 0x14, 0x50, 0x5E, 0x5F,
 0x20, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,        /* XC */
 0x58, 0x59, 0x5A, 0x13, 0x14, 0x50, 0x5E, 0x5F,
 0x10, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,        /* XO */
 0x48, 0x49,   -1, 0x03, 0x04, 0x40,   -1, 0x7F,
 0x10, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,        /* XOC */
 0x48, 0x49,   -1, 0x03, 0x04, 0x40,   -1, 0x7F
 };

/* Numeric (flag + digit) to paper tape punch */

const int8 num_to_ptp[32] = {
 0x20, 0x01, 0x02, 0x13, 0x04, 0x15, 0x16, 0x07,        /* 0 */
 0x08, 0x19, 0x2A, 0x3B, 0x1C, 0x0D, 0x3E, 0x3F,
 0x40, 0x51, 0x52, 0x43, 0x54, 0x45, 0x46, 0x57,        /* F + 0 */
 0x58, 0x49, 0x4A, 0x5B, 0x4C, 0x5D, 0x5E, 0x4F
 };

/* Alphameric (two digits) to paper tape punch */

const int8 alp_to_ptp[256] = {
 0x10,   -1, 0x7A, 0x6B, 0x7C,   -1,   -1, 0x7F,        /* 00 */
   -1,   -1, 0x2A,   -1,   -1,   -1,   -1, 0x1F,
 0x70,   -1, 0x4A, 0x5B, 0x4C,   -1,   -1,   -1,        /* 10 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
 0x40, 0x31, 0x2A, 0x3B, 0x2C,   -1,   -1,   -1,        /* 20 */ 
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1,   -1, 0x1A, 0x0B, 0x1C, 0x0D, 0x0E,   -1,        /* 30 */
   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
   -1, 0x61, 0x62, 0x73, 0x64, 0x75, 0x76, 0x67,        /* 40 */
 0x68, 0x79,   -1,   -1,   -1,   -1,   -1,   -1,
 0x40, 0x51, 0x52, 0x43, 0x54, 0x45, 0x46, 0x57,        /* 50 */
 0x58, 0x49, 0x4A,   -1,   -1,   -1,   -1, 0x4F,
   -1, 0x31, 0x32, 0x23, 0x34, 0x25, 0x26, 0x37,        /* 60 */
 0x38, 0x29,   -1,   -1,   -1,   -1,   -1,   -1,
 0x20, 0x01, 0x02, 0x13, 0x04, 0x15, 0x16, 0x07,        /* 70 */
 0x08, 0x19, 0x7A,   -1,   -1,   -1,   -1, 0x7F,
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

/* Paper tape reader IO routine

   - Hard errors halt the operation and the system.
   - Parity errors place an invalid character in memory and set
     RDCHK, but the read continues until end of record.  If IO
     stop is set, the system then halts.
*/

t_stat ptr (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
uint32 i, q = 0;
int8 mc;
uint8 ptc;
t_stat r, sta;

sta = SCPE_OK;
switch (op) {                                           /* case on op */

    case OP_RN:                                         /* read numeric */
        for (i = 0; i < MEMSIZE; i++) {                 /* (stop runaway) */
            r = ptr_read (&ptc, TRUE);                  /* read frame */
            if (r != SCPE_OK)                           /* error? */
                return r;
            if (ptc & PT_EL) {                          /* end record? */
                M[pa] = REC_MARK;                       /* store rec mark */
                return sta;                             /* done */
                }
            if (pa == 18976)
                q++;
            if (bad_par[ptc]) {                         /* bad parity? */
                ind[IN_RDCHK] = 1;                      /* set read check */
                if (io_stop)                            /* set return status */
                    sta = STOP_INVCHR;
                M[pa] = 0;                              /* store zero */
                }
            else M[pa] = ptr_to_num[ptc];               /* translate, store */
            PP (pa);                                    /* incr mem addr */
            }
        break;

    case OP_RA:                                         /* read alphameric */
        for (i = 0; i < MEMSIZE; i = i + 2) {           /* (stop runaway) */
            r = ptr_read (&ptc, TRUE);                  /* read frame */
            if (r != SCPE_OK)                           /* error? */
                return r;
            if (ptc & PT_EL) {                          /* end record? */
                M[pa] = REC_MARK;                       /* store rec mark */
                M[pa - 1] = 0;
                return sta;                             /* done */
                }
            mc = ptr_to_alp[ptc];                       /* translate */
            if (bad_par[ptc] || (mc < 0)) {             /* bad par or char? */
                ind[IN_RDCHK] = 1;                      /* set read check */
                if (io_stop)                            /* set return status */
                    sta = STOP_INVCHR;
                mc = 0;                                 /* store blank */
                }
            M[pa] = (M[pa] & FLAG) | (mc & DIGIT);      /* store 2 digits */
            M[pa - 1] = (M[pa - 1] & FLAG) | ((mc >> 4) & DIGIT);
            pa = ADDR_A (pa, 2);                        /* incr mem addr */
            }
        break;  

    default:                                            /* invalid function */
        return STOP_INVFNC;
        }

return STOP_RWRAP;
}

/* Binary paper tape reader IO routine - see above for error handling */

t_stat btr (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
uint32 i;
uint8 ptc;
t_stat r, sta;

if ((cpu_unit.flags & IF_BIN) == 0)
    return STOP_INVIO;

sta = SCPE_OK;
switch (op) {                                           /* case on op */

    case OP_RA:                                         /* read alphameric */
        for (i = 0; i < MEMSIZE; i = i + 2) {           /* (stop runaway) */
            r = ptr_read (&ptc, FALSE);                 /* read frame */
            if (r != SCPE_OK)                           /* error? */
                return r;
            if (ptc & PT_EL) {                          /* end record? */
                M[pa] = REC_MARK;                       /* store rec mark */
                M[pa - 1] = 0;
                return sta;                             /* done */
                }
            if (bad_par[ptc]) {                         /* bad parity? */
                ind[IN_RDCHK] = 1;                      /* set read check */
                if (io_stop)                            /* set return status */
                    sta = STOP_INVCHR;
                }
            M[pa] = (M[pa] & FLAG) | (ptc & 07);        /* store 2 digits */
            M[pa - 1] = (M[pa - 1] & FLAG) |
                (((ptc >> 5) & 06) | ((ptc >> 3) & 1));
            pa = ADDR_A (pa, 2);                        /* incr mem addr */
            }
        break;  

    default:                                            /* invalid function */
        return STOP_INVFNC;
        }

return STOP_RWRAP;
}

/* Read ptr frame - all errors are 'hard' errors and halt the system */

t_stat ptr_read (uint8 *c, t_bool ignfeed)
{
int32 temp;

if ((ptr_unit.flags & UNIT_ATT) == 0) {                 /* attached? */
    ind[IN_RDCHK] = 1;                                  /* no, error */
    return SCPE_UNATT;
    }

do {
    if ((temp = getc (ptr_unit.fileref)) == EOF) {      /* read char */
        ind[IN_RDCHK] = 1;                              /* err, rd chk */
        if (feof (ptr_unit.fileref))
            sim_printf ("PTR end of file\n");
        else
            sim_printf ("PTR I/O error: %d\n", errno);
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
return SCPE_OK;
}

/* Bootstrap routine */

static const uint8 boot_rom[] = {
 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                    /* NOP */
 3, 6, 0, 0, 0, 3, 1, 0, 0, 3, 0, 0,                    /* RNPT 31 */
 2, 5, 0, 0, 0, 7, 1, 0, 0, 0, 0, 0,                    /* TD 71,loc */
 3, 6, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0,                    /* RNPT loc1 */
 2, 6, 0, 0, 0, 6, 6, 0, 0, 0, 3, 5,                    /* TF 66,35 */
 1, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                    /* TDM loc2,loc3 */
 4, 9, 0, 0, 0, 1, 2, 0, 0, 0, 0, 0                     /* BR 12 */
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

/* Paper tape punch IO routine

   - Hard errors halt the operation and the system.
   - Parity errors stop the operation and set WRCHK.
     If IO stop is set, the system then halts.
*/

t_stat ptp (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
uint32 i;
int8 ptc;
uint8 z, d;
t_stat r;

switch (op) {                                           /* decode op */

    case OP_DN:
        return ptp_num (pa, 20000 - (pa % 20000), TRUE);/* dump numeric */

    case OP_WN:
        return ptp_num (pa, 0, FALSE);                  /* punch numeric */

    case OP_WA:
        for (i = 0; i < MEMSIZE; i = i + 2) {           /* stop runaway */
            d = M[pa] & DIGIT;                          /* get digit */
            z = M[pa - 1] & DIGIT;                      /* get zone */
            if ((d & REC_MARK) == REC_MARK)             /* 8-2 char? */
                return ptp_write (PT_EL);               /* end record */
            ptc = alp_to_ptp[(z << 4) | d];             /* translate pair */
            if (ptc < 0) {                              /* bad char? */
                ind[IN_WRCHK] = 1;                      /* write check */
                CRETIOE (io_stop, STOP_INVCHR);
                }
            r = ptp_write (ptc);                        /* write char */
            if (r != SCPE_OK)                           /* error? */
                return r;
            pa = ADDR_A (pa, 2);                        /* incr mem addr */
            }
        break;          

    default:                                            /* invalid function */
        return STOP_INVFNC;
        }

return STOP_RWRAP;
}

/* Binary paper tape punch IO routine - see above for error handling */

t_stat btp (uint32 op, uint32 pa, uint32 f0, uint32 f1)
{
uint32 i;
uint8 ptc, z, d;
t_stat r;

if ((cpu_unit.flags & IF_BIN) == 0) return STOP_INVIO;

switch (op) {                                           /* decode op */

    case OP_WA:
        for (i = 0; i < MEMSIZE; i = i + 2) {           /* stop runaway */
            d = M[pa] & DIGIT;                          /* get digit */
            z = M[pa - 1] & DIGIT;                      /* get zone */
            if ((d & REC_MARK) == REC_MARK)             /* 8-2 char? */
                return ptp_write (PT_EL);               /* end record */
            ptc = ((z & 06) << 5) | ((z & 01) << 3) | (d & 07);
            if (bad_par[ptc])                           /* set parity */
                ptc = ptc | PT_C;
            r = ptp_write (ptc);                        /* write char */
            if (r != SCPE_OK)                           /* error? */
                return r;
            pa = ADDR_A (pa, 2);                        /* incr mem addr */
            }
        break;          

    default:                                            /* invalid function */
        return STOP_INVFNC;
        }

return STOP_RWRAP;
}

/* Punch tape numeric - cannot generate parity errors */

t_stat ptp_num (uint32 pa, uint32 len, t_bool dump)
{
t_stat r;
uint8 d;
uint32 i;

for (i = 0; i < MEMSIZE; i++) {                         /* stop runaway */
    d = M[pa] & (FLAG | DIGIT);                         /* get char */
    if (dump? (len-- == 0):                             /* dump: end reached? */
       ((d & REC_MARK) == REC_MARK))                    /* write: rec mark? */
        return ptp_write (PT_EL);                       /* end record */
    r = ptp_write (num_to_ptp[d]);                      /* write */
    if (r != SCPE_OK)                                   /* error? */
        return r;
    PP (pa);                                            /* incr mem addr */
    }
return STOP_RWRAP;
}

/* Write ptp frame - all errors are hard errors */

t_stat ptp_write (uint32 c)
{
if ((ptp_unit.flags & UNIT_ATT) == 0) {                 /* attached? */
    ind[IN_WRCHK] = 1;                                  /* no, error */
    return SCPE_UNATT;
    }
if (putc (c, ptp_unit.fileref) == EOF) {                /* write char */
    ind[IN_WRCHK] = 1;                                  /* error? */
    perror ("PTP I/O error");
    clearerr (ptp_unit.fileref);
    return SCPE_IOERR;
    }
ptp_unit.pos = ptp_unit.pos + 1;                        /* count char */
return SCPE_OK;
}

/* Reset routine */

t_stat ptp_reset (DEVICE *dptr)
{
return SCPE_OK;
}
