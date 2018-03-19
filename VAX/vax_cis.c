/* vax_cis.c: VAX CIS instructions

   Copyright (c) 2004-2016, Robert M Supnik

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

   On a full VAX, this module simulates the VAX commercial instruction set (CIS).
   On a subset VAX, this module implements the emulated instruction fault.

   30-May-16    RMS     Fixed WordLshift FOR REAL
   16-Oct-08    RMS     Fixed bug in ASHP left overflow calc (Word/NibbleLshift)
                        Fixed bug in DIVx (LntDstr calculation)
   28-May-08    RMS     Inlined physical memory routines
   16-May-06    RMS     Fixed bug in length calculation (Tim Stark)
   03-May-06    RMS     Fixed MOVTC, MOVTUC to preserve cc's through page faults
                        Fixed MOVTUC to stop on translated == escape
                        Fixed CVTPL to set registers before destination reg write
                        Fixed CVTPL to set correct cc bit on overflow
                        Fixed EDITPC to preserve cc's through page faults
                        Fixed EDITPC EO$BLANK_ZERO count, cc test
                        Fixed EDITPC EO$INSERT to insert fill instead of blank
                        Fixed EDITPC EO$LOAD_PLUS/MINUS to skip character
                        (Tim Stark)
   12-Apr-04    RMS     Cloned from pdp11_cis.c and vax_cpu1.c

   Zero length decimal strings require either zero bytes (trailing) or one byte
   (separate, packed).

   CIS instructions can run for a very long time, so they are interruptible
   and restartable.  In the simulator, string instructions (and EDITPC) are
   interruptible by faults, but decimal instructions run to completion.
   The code is unoptimized.
*/

#include "vax_defs.h"

#if defined (FULL_VAX)

/* Decimal string structure */

#define DSTRLNT         4
#define DSTRMAX         (DSTRLNT - 1)
#define MAXDVAL         429496730                       /* 2^32 / 10 */

#define C_SPACE         0x20                            /* ASCII chars */
#define C_PLUS          0x2B
#define C_MINUS         0x2D
#define C_ZERO          0x30
#define C_NINE          0x39

typedef struct {
    uint32              sign;
    uint32              val[DSTRLNT];
    } DSTR;

static DSTR Dstr_zero = { 0, {0, 0, 0, 0} };
static DSTR Dstr_one = { 0, {0x10, 0, 0, 0} };

int32 ReadDstr (int32 lnt, int32 addr, DSTR *dec, int32 acc);
int32 WriteDstr (int32 lnt, int32 addr, DSTR *dec, int32 v, int32 acc);
int32 SetCCDstr (int32 lnt, DSTR *src, int32 pslv);
int32 AddDstr (DSTR *src1, DSTR *src2, DSTR *dst, int32 cin);
void SubDstr (DSTR *src1, DSTR *src2, DSTR *dst);
int32 CmpDstr (DSTR *src1, DSTR *src2);
int32 TestDstr (DSTR *dsrc);
void ProbeDstr (int32 lnt, int32 addr, int32 acc);
int32 LntDstr (DSTR *dsrc, int32 nz);
uint32 NibbleLshift (DSTR *dsrc, int32 sc, uint32 cin);
uint32 NibbleRshift (DSTR *dsrc, int32 sc, uint32 cin);
int32 WordLshift (DSTR *dsrc, int32 sc);
void WordRshift (DSTR *dsrc, int32 sc);
void CreateTable (DSTR *dsrc, DSTR mtable[10]);
int32 do_crc_4b (int32 crc, int32 tbl, int32 acc);
int32 edit_read_src (int32 inc, int32 acc);
void edit_adv_src (int32 inc);
int32 edit_read_sign (int32 acc);

extern int32 eval_int (void);

/* CIS emulator */

int32 op_cis (int32 *op, int32 cc, int32 opc, int32 acc)
{
int32 i, j, c, t, pop, rpt, V;
int32 match, fill, sign, shift;
int32 ldivd, ldivr;
int32 lenl, lenp;
uint32 nc, d, result;
t_stat r;
DSTR accum, src1, src2, dst;
DSTR mptable[10];

switch (opc) {                                          /* case on opcode */

/* MOVTC

   Operands if PSL<fpd> = 0:
        op[0:1]         =       source string descriptor
        op[2]           =       fill character
        op[3]           =       table address
        op[4:5]         =       destination string descriptor

   Registers if PSL<fpd> = 1:
        R[0]            =       delta-PC/fill/source string length
        R[1]            =       source string address
        R[2]            =       number of bytes remaining to move
        R[3]            =       table address
        R[4]            =       saved cc's/destination string length
        R[5]            =       destination string address

   Condition codes:
        NZC             =       set from op[0]:op[4]
        V               =       0

   Registers:
        R0              =       src length remaining, or 0
        R1              =       addr of end of source string + 1
        R2              =       0
        R3              =       table address
        R4              =       0
        R5              =       addr of end of dest string + 1
*/

    case MOVTC:
        if (PSL & PSL_FPD) {                            /* FPD set? */
            SETPC (fault_PC + STR_GETDPC (R[0]));       /* reset PC */
            fill = STR_GETCHR (R[0]);                   /* get fill */
            R[2] = R[2] & STR_LNMASK;                   /* remaining move */
            cc = (R[4] >> 16) & CC_MASK;                /* restore cc's */
            }
        else {
            CC_CMP_W (op[0], op[4]);                    /* set cc's */
            R[0] = STR_PACK (op[2], op[0]);             /* src len, fill */
            R[1] = op[1];                               /* src addr */
            fill = op[2];                               /* set fill */
            R[3] = op[3];                               /* table addr */
            R[4] = op[4] | ((cc & CC_MASK) << 16);      /* dst len + cc's */
            R[5] = op[5];                               /* dst addr */
            R[2] = (op[0] < op[4])? op[0]: op[4];       /* remaining move */
            PSL = PSL | PSL_FPD;                        /* set FPD */
            }
        if (R[2]) {                                     /* move to do? */
            int32 mvl;
            mvl = R[0] & STR_LNMASK;                    /* orig move len */
            if (mvl >= (R[4] & STR_LNMASK))
                mvl = R[4] & STR_LNMASK;
            if (((uint32) R[1]) < ((uint32) R[5])) {    /* backward? */
                while (R[2]) {                          /* loop thru char */
                    t = Read ((R[1] + R[2] - 1) & LMASK, L_BYTE, RA);
                    c = Read ((R[3] + t) & LMASK, L_BYTE, RA);
                    Write ((R[5] + R[2] - 1) & LMASK, c, L_BYTE, WA);
                    R[2] = (R[2] - 1) & STR_LNMASK;
                    }
                R[1] = (R[1] + mvl) & LMASK;            /* adv src, dst */
                R[5] = (R[5] + mvl) & LMASK;
                }
            else {                                      /* forward */
                while (R[2]) {                          /* loop thru char */
                    t = Read (R[1], L_BYTE, RA);        /* read src */
                    c = Read ((R[3] + t) & LMASK, L_BYTE, RA);
                    Write (R[5], c, L_BYTE, WA);        /* write dst */
                    R[1] = (R[1] + 1) & LMASK;          /* adv src, dst */
                    R[2] = (R[2] - 1) & STR_LNMASK;
                    R[5] = (R[5] + 1) & LMASK;
                    }
                }                                       /* update lengths */
            R[0] = (R[0] & ~STR_LNMASK) | ((R[0] - mvl) & STR_LNMASK);
            R[4] = (R[4] & ~STR_LNMASK) | ((R[4] - mvl) & STR_LNMASK);
            }
        while (R[4] & STR_LNMASK) {                     /* fill if needed */
            Write (R[5], fill, L_BYTE, WA);
            R[4] = (R[4] & ~STR_LNMASK) | ((R[4] - 1) & STR_LNMASK);
            R[5] = (R[5] + 1) & LMASK;                  /* adv dst */
            }
        R[0] = R[0] & STR_LNMASK;                       /* mask off state */
        R[4] = 0;
        PSL = PSL & ~PSL_FPD;
        return cc;

/* MOVTUC

   Operands:
        op[0:1]         =       source string descriptor
        op[2]           =       escape character
        op[3]           =       table address
        op[4:5]         =       destination string descriptor

   Registers if PSL<fpd> = 1:
        R[0]            =       delta-PC/match/source string length
        R[1]            =       source string address
        R[2]            =       saved condition codes
        R[3]            =       table address
        R[4]            =       destination string length
        R[5]            =       destination string address

   Condition codes:
        NZC             =       set from op[0]:op[4]
        V               =       1 if match to escape character

   Registers:
        R0              =       src length remaining, or 0
        R1              =       addr of end of source string + 1
        R2              =       0
        R3              =       table address
        R4              =       dst length remaining, or 0
        R5              =       addr of end of dest string + 1
*/

    case MOVTUC:
        if (PSL & PSL_FPD) {                            /* FPD set? */
            SETPC (fault_PC + STR_GETDPC (R[0]));       /* reset PC */
            fill = STR_GETCHR (R[0]);                   /* get match */
            R[4] = R[4] & STR_LNMASK;
            cc = R[2] & CC_MASK;                        /* restore cc's */
            }
        else {
            CC_CMP_W (op[0], op[4]);                    /* set cc's */
            R[0] = STR_PACK (op[2], op[0]);             /* src len, fill */
            R[1] = op[1];                               /* src addr */
            fill = op[2];                               /* set match */
            R[3] = op[3];                               /* table addr */
            R[4] = op[4];                               /* dst len */
            R[5] = op[5];                               /* dst addr */
            R[2] = cc;                                  /* save cc's */
            PSL = PSL | PSL_FPD;                        /* set FPD */
            }
        while ((R[0] & STR_LNMASK) && R[4]) {           /* while src & dst */
            t = Read (R[1], L_BYTE, RA);                /* read src */
            c = Read ((R[3] + t) & LMASK, L_BYTE, RA);  /* translate */
            if (c == fill) {                            /* stop char? */
                cc = cc | CC_V;                         /* set V, done */
                break;
                }
            Write (R[5], c, L_BYTE, WA);                /* write dst */
            R[0] = (R[0] & ~STR_LNMASK) | ((R[0] - 1) & STR_LNMASK);
            R[1] = (R[1] + 1) & LMASK;
            R[4] = (R[4] - 1) & STR_LNMASK;             /* adv src, dst */
            R[5] = (R[5] + 1) & LMASK;
            }
        R[0] = R[0] & STR_LNMASK;                       /* mask off state */
        R[2] = 0;
        PSL = PSL & ~PSL_FPD;
        return cc;

/* MATCHC

   Operands:
        op[0:1]         =       substring descriptor
        op[2:3]         =       string descriptor

   Registers if PSL<fpd> = 1:
        R[0]            =       delta-PC/match/substring length
        R[1]            =       substring address
        R[2]            =       source string length
        R[3]            =       source string address

   Condition codes:
        NZ              =       set from R0
        VC              =       0

   Registers:
        R0              =       if match, 0, else, op[0]
        R1              =       if match, op[0] + op[1], else, op[1]
        R2              =       if match, src bytes remaining, else, 0
        R3              =       if match, end of substr, else, op[2] + op[3]

   Notes:
        - If the string is zero length, and the substring is not,
          the outer loop exits immediately, and the result is
          "no match"
        - If the substring is zero length, the inner loop always
          exits immediately, and the result is a "match"
        - If the string is zero length, and the substring is as
          well, the outer loop executes, the inner loop exits
          immediately, and the result is a match, but the result
          is the length of the string (zero)
        - This instruction can potentially run a very long time - worst
          case execution on a real VAX-11/780 was more than 30 minutes.
          Accordingly, the instruction tests for interrupts and stops
          if one is found.
*/

    case MATCHC:
        if (PSL & PSL_FPD) {                            /* FPD? */
            SETPC (fault_PC + STR_GETDPC (R[0]));       /* reset PC */
            R[2] = R[2] & STR_LNMASK;
            }
        else {
            R[0] = STR_PACK (0, op[0]);                 /* srclen + FPD data */
            R[1] = op[1];                               /* save operands */
            R[2] = op[2];
            R[3] = op[3];
            PSL = PSL | PSL_FPD;                        /* set FPD */
            }
        for (match = 0; R[2] >= (R[0] & STR_LNMASK); ) {
            for (i = 0, match = 1; match && (i < (R[0] & STR_LNMASK)); i++) {
                c = Read ((R[1] + i) & LMASK, L_BYTE, RA);
                t = Read ((R[3] + i) & LMASK, L_BYTE, RA);
                match = (c == t);                       /* continue if match */
                }                                       /* end for substring */
            if (match)                                  /* exit if match */
                break; 
            R[2] = (R[2] - 1) & STR_LNMASK;             /* decr src length */
            R[3] = (R[3] + 1) & LMASK;                  /* next string char */
            if (i >= sim_interval) {                    /* done with interval? */
                sim_interval = 0;
                if ((r = sim_process_event ())) {       /* presumably WRU */
                    PC = fault_PC;                      /* backup up PC */
                    ABORT (r);                          /* abort flushes IB */
                    }
                SET_IRQL;                               /* update interrupts */
                if (trpirq)                             /* pending? stop */
                    ABORT (ABORT_INTR);
                }
            else sim_interval = sim_interval - i;
            }                                           /* end for string */
        R[0] = R[0] & STR_LNMASK;
        if (match) {                                    /* if match */
            R[1] = (R[1] + R[0]) & LMASK;
            R[2] = (R[2] - R[0]) & STR_LNMASK;
            R[3] = (R[3] + R[0]) & LMASK;
            R[0] = 0;
            }
        else {                                          /* if no match */
            R[3] = (R[3] + R[2]) & LMASK;
            R[2] = 0;
            }
        PSL = PSL & ~PSL_FPD;
        CC_IIZZ_L (R[0]);                               /* set cc's */
        return cc;

/* CRC

   Operands:
        op[0]           =       table address
        op[1]           =       initial CRC
        op[2:3]         =       source string descriptor

   Registers if PSL<fpd> = 1:
        R[0]            =       result
        R[1]            =       table address
        R[2]            =       delta-PC/0/source string length
        R[3]            =       source string address

   Condition codes:
        NZ              =       set from result
        VC              =       0

   Registers:
        R0              =       result
        R1              =       0
        R2              =       0
        R3              =       addr + 1 of last byte in source string
*/

    case CRC:
        if (PSL & PSL_FPD) {                            /* FPD? */
            SETPC (fault_PC + STR_GETDPC (R[2]));       /* reset PC */
            }
        else {
            R[2] = STR_PACK (0, op[2]);                 /* srclen + FPD data */
            R[0] = op[1];                               /* save operands */
            R[1] = op[0];
            R[3] = op[3];
            PSL = PSL | PSL_FPD;                        /* set FPD */
            }
        while ((R[2] & STR_LNMASK) != 0) {              /* loop thru chars */
            c = Read (R[3], L_BYTE, RA);                /* get char */
            t = R[0] ^ c;                               /* XOR to CRC */
            t = do_crc_4b (t, R[1], acc);               /* do 4b shift */
            R[0] = do_crc_4b (t, R[1], acc);            /* do 4b shift */
            R[3] = (R[3] + 1) & LMASK;
            R[2] = R[2] - 1;
            }
        R[1] = 0;
        R[2] = 0;
        PSL = PSL & ~PSL_FPD;
        CC_IIZZ_L (R[0]);                               /* set cc's */
        return cc;

/* MOVP

   Operands:
        op[0]           =       length
        op[1]           =       source string address
        op[2]           =       dest string address

   Condition codes:
        NZ              =       set from result
        V               =       0
        C               =       unchanged

   Registers:
        R0              =       0
        R1              =       addr of source string
        R2              =       0
        R3              =       addr of dest string
*/

    case MOVP:
        if ((PSL & PSL_FPD) || (op[0] > 31))
            RSVD_OPND_FAULT;
        ReadDstr (op[0], op[1], &dst, acc);             /* read source */
        cc = WriteDstr (op[0], op[2], &dst, 0, acc) |   /* write dest */
            (cc & CC_C);                                /* preserve C */
        R[0] = 0;
        R[1] = op[1];
        R[2] = 0;
        R[3] = op[2];
        return cc;

/* ADDP4, ADDP6, SUBP4, SUBP6

   Operands:
        op[0:1]         =       src1 string descriptor
        op[2:3]         =       src2 string descriptor
        (ADDP6, SUBP6 only)
        op[4:5]         =       dest string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers:
        R0              =       0
        R1              =       addr of src1 string
        R2              =       0
        R3              =       addr of src2 string
        (ADDP6, SUBP6 only)
        R4              =       0
        R5              =       addr of dest string     
*/

    case ADDP4: case SUBP4:
        op[4] = op[2];                                  /* copy dst */
        op[5] = op[3];
    case ADDP6: case SUBP6:
        if ((PSL & PSL_FPD) || (op[0] > 31) ||
            (op[2] > 31) || (op[4] > 31))
            RSVD_OPND_FAULT;
        ReadDstr (op[0], op[1], &src1, acc);            /* get src1 */
        ReadDstr (op[2], op[3], &src2, acc);            /* get src2 */
        if (opc & 2)                                    /* sub? invert sign */
            src1.sign = src1.sign ^ 1;
        if (src1.sign ^ src2.sign) {                    /* opp signs?  sub */
            if (CmpDstr (&src1, &src2) < 0) {           /* src1 < src2? */
                SubDstr (&src1, &src2, &dst);           /* src2 - src1 */
                dst.sign = src2.sign;                   /* sign = src2 */
                }
            else {
                SubDstr (&src2, &src1, &dst);           /* src1 - src2 */
                dst.sign = src1.sign;                   /* sign = src1 */
                }
            V = 0;                                      /* can't carry */
            }
        else {                                          /* addition */
            V = AddDstr (&src1, &src2, &dst, 0);        /* add magnitudes */
            dst.sign = src1.sign;                       /* set result sign */
            }
        cc = WriteDstr (op[4], op[5], &dst, V, acc);    /* store result */
        R[0] = 0;
        R[1] = op[1];
        R[2] = 0;
        R[3] = op[3];
        if (opc & 1) {                                  /* ADDP6, SUBP6? */
            R[4] = 0;
            R[5] = op[5];
            }
        return cc;

/* MULP

   Operands:
        op[0:1]         =       src1 string descriptor
        op[2:3]         =       src2 string descriptor
        op[4:5]         =       dest string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers:
        R0              =       0
        R1              =       addr of src1 string
        R2              =       0
        R3              =       addr of src2 string
        R4              =       0
        R5              =       addr of dest string     
*/

    case MULP:
        if ((PSL & PSL_FPD) || (op[0] > 31) ||
            (op[2] > 31) || (op[4] > 31))
            RSVD_OPND_FAULT;
        dst = Dstr_zero;                                /* clear result */
        if (ReadDstr (op[0], op[1], &src1, acc) &&      /* read src1, src2 */
            ReadDstr (op[2], op[3], &src2, acc)) {      /* if both > 0 */
            dst.sign = src1.sign ^ src2.sign;           /* sign of result */
            accum = Dstr_zero;                          /* clear accum */
            NibbleRshift (&src1, 1, 0);                 /* shift out sign */
            CreateTable (&src1, mptable);               /* create *1, *2, ... */
            for (i = 1; i < (DSTRLNT * 8); i++) {       /* 31 iterations */
                d = (src2.val[i / 8] >> ((i % 8) * 4)) & 0xF;
                if (d > 0)                              /* add in digit*mpcnd */
                    AddDstr (&mptable[d], &accum, &accum, 0);
                nc = NibbleRshift (&accum, 1, 0);       /* ac right 4 */
                NibbleRshift (&dst, 1, nc);             /* result right 4 */
                }
            V = TestDstr (&accum) != 0;                 /* if ovflo, set V */
            }
        else V = 0;                                     /* result = 0 */
        cc = WriteDstr (op[4], op[5], &dst, V, acc);    /* store result */
        R[0] = 0;
        R[1] = op[1];
        R[2] = 0;
        R[3] = op[3];
        R[4] = 0;
        R[5] = op[5];
        return cc;

/* DIVP

   Operands:
        op[0:1]         =       src1 string descriptor
        op[2:3]         =       src2 string descriptor
        op[4:5]         =       dest string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers:
        R0              =       0
        R1              =       addr of src1 string
        R2              =       0
        R3              =       addr of src2 string
        R4              =       0
        R5              =       addr of dest string     
*/

    case DIVP:
        if ((PSL & PSL_FPD) || (op[0] > 31) ||
            (op[2] > 31) || (op[4] > 31))
            RSVD_OPND_FAULT;
        ldivr = ReadDstr (op[0], op[1], &src1, acc);    /* get divisor */
        if (ldivr == 0) {                               /* divisor = 0? */
            SET_TRAP (TRAP_FLTDIV);                     /* dec div trap */
            return cc;
            }
        ldivr = LntDstr (&src1, ldivr);                 /* get exact length */
        ldivd = ReadDstr (op[2], op[3], &src2, acc);    /* get dividend */
        ldivd = LntDstr (&src2, ldivd);                 /* get exact length */
        dst = Dstr_zero;                                /* clear dest */
        NibbleRshift (&src1, 1, 0);                     /* right justify ops */
        NibbleRshift (&src2, 1, 0);
        if ((t = ldivd - ldivr) >= 0) {                 /* any divide to do? */
            dst.sign = src1.sign ^ src2.sign;           /* calculate sign */
            WordLshift (&src1, t / 8);                  /* align divr to divd */
            NibbleLshift (&src1, t % 8, 0);
            CreateTable (&src1, mptable);               /* create *1, *2, ... */
            for (i = 0; i <= t; i++) {                  /* divide loop */
                for (d = 9; d > 0; d--) {               /* find digit */
                    if (CmpDstr (&src2, &mptable[d]) >= 0) {
                        SubDstr (&mptable[d], &src2, &src2);
                        dst.val[0] = dst.val[0] | d;
                        break;
                        }                               /* end if */
                    }                                   /* end for */
                NibbleLshift (&src2, 1, 0);             /* shift dividend */
                NibbleLshift (&dst, 1, 0);              /* shift quotient */
                }                                       /* end divide loop */
            }                                           /* end if */
        cc = WriteDstr (op[4], op[5], &dst, 0, acc);    /* store result */
        R[0] = 0;
        R[1] = op[1];
        R[2] = 0;
        R[3] = op[3];
        R[4] = 0;
        R[5] = op[5];
        return cc;

/* CMPP3, CMPP4

   Operands (CMPP3):
        op[0]           =       string length
        op[1], op[2]    =       string lengths

   Operands (CMPP4):
        op[0:1]         =       string1 descriptor
        op[2:3]         =       string2 descriptor

   Condition codes:
        NZ              =       set from comparison
        VC              =       0

   Registers:
        R0              =       0
        R1              =       addr of src1 string
        R2              =       0
        R3              =       addr of src2 string
*/

    case CMPP3:
        op[3] = op[2];                                  /* reposition ops */
        op[2] = op[0];
    case CMPP4:
        if ((PSL & PSL_FPD) || (op[0] > 31) || (op[2] > 31))
            RSVD_OPND_FAULT;
        ReadDstr (op[0], op[1], &src1, acc);            /* get src1 */
        ReadDstr (op[2], op[3], &src2, acc);            /* get src2 */
        cc = 0;
        if (src1.sign != src2.sign) cc = (src1.sign)? CC_N: 0;
        else {
            t = CmpDstr (&src1, &src2);                 /* compare strings */
            if (t < 0)
                cc = (src1.sign? 0: CC_N);
            else if (t > 0)
                cc = (src1.sign? CC_N: 0);
            else cc = CC_Z;
            }
        R[0] = 0;
        R[1] = op[1];
        R[2] = 0;
        R[3] = op[3];
        return cc ;

/* ASHP

   Operands:
        op[0]           =       shift count
        op[1:2]         =       source string descriptor
        op[3]           =       round digit
        op[4:5]         =       dest string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers:
        R0              =       0
        R1              =       addr of src1 string
        R2              =       0
        R3              =       addr of src2 string
*/

    case ASHP:
        if ((PSL & PSL_FPD) || (op[1] > 31) || (op[4] > 31))
            RSVD_OPND_FAULT;
        ReadDstr (op[1], op[2], &src1, acc);            /* get source */
        V = 0;                                          /* init V */
        shift = op[0];                                  /* get shift count */
        if (shift & BSIGN) {                            /* right shift? */
            shift = BMASK + 1 - shift;                  /* !shift! */
            WordRshift (&src1, shift / 8);              /* do word shifts */    
            NibbleRshift (&src1, shift % 8, 0);         /* do nibble shifts */
            t = op[3] & 0xF;                            /* get round nibble */
            if ((t + (src1.val[0] & 0xF)) > 9)          /* rounding needed? */
                AddDstr (&src1, &Dstr_one, &src1, 0);   /* round */
            src1.val[0] = src1.val[0] & ~0xF;           /* clear sign */
            }                                           /* end right shift */
        else if (shift) {                               /* left shift? */
            if (WordLshift (&src1, shift / 8))          /* do word shifts */
                V = 1;
            if (NibbleLshift (&src1, shift % 8, 0))
                V = 1;
            }                                           /* end left shift */
        cc = WriteDstr (op[4], op[5], &src1, V, acc);   /* store result */
        R[0] = 0;
        R[1] = op[2];
        R[2] = 0;
        R[3] = op[5];
        return cc ;

/* CVTPL

   Operands:
        op[0:1]         =       source string descriptor
        op[2]           =       memory flag/register number
        op[3]           =       memory address

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers:
        R0              =       0
        R1              =       addr of source string
        R2              =       0
        R3              =       0
*/

    case CVTPL:
        if ((PSL & PSL_FPD) || (op[0] > 31))
            RSVD_OPND_FAULT;
        ReadDstr (op[0], op[1], &src1, acc);            /* get source */
        V = result = 0;                                 /* clear V, result */
        for (i = (DSTRLNT * 8) - 1; i > 0; i--) {       /* loop thru digits */
            d = (src1.val[i / 8] >> ((i % 8) * 4)) & 0xF;
            if (d || result || V) {                     /* skip initial 0's */
                if (result >= MAXDVAL)
                    V = 1;
                result = ((result * 10) + d) & LMASK;
                if (result < d)
                    V = 1;
                }                                       /* end if */
            }                                           /* end for */
        if (src1.sign)                                  /* negative? */
            result = (~result + 1) & LMASK;
        if (src1.sign ^ ((result & LSIGN) != 0))        /* test for overflow */
            V = 1;
        if (op[2] < 0)                                  /* if mem, store result */
            Write (op[3], result, L_LONG, WA);          /* before reg update */
        R[0] = 0;                                       /* update registers */
        R[1] = op[1];
        R[2] = 0;
        R[3] = 0;
        if (op[2] >= 0)                                 /* if reg, store result */
            R[op[2]] = result;                          /* after reg update */
        if (V && (PSL & PSW_IV))                        /* ovflo and IV? trap */
            SET_TRAP (TRAP_INTOV);
        CC_IIZZ_L (result);
        return cc | (V? CC_V: 0);

/* CVTLP

   Operands:
        op[0]           =       source long
        op[1:2]         =       dest string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers:
        R0              =       0
        R1              =       0
        R2              =       0
        R3              =       addr of dest string
*/

    case CVTLP:
        if ((PSL & PSL_FPD) || (op[1] > 31))
            RSVD_OPND_FAULT;
        dst = Dstr_zero;                                /* clear result */
        result = op[0];
        if ((result & LSIGN) != 0) {
            dst.sign = 1;
            result = (~result + 1) & LMASK;
            }
        for (i = 1; (i < (DSTRLNT * 8)) && result; i++) {
            d = result % 10;
            result = result / 10;
            dst.val[i / 8] = dst.val[i / 8] | (d << ((i % 8) * 4));
            }
        cc = WriteDstr (op[1], op[2], &dst, 0, acc);    /* write result */
        R[0] = 0;
        R[1] = 0;
        R[2] = 0;
        R[3] = op[2];
        return cc;

/* CVTSP

   Operands:
        op[0:1]         =       source string descriptor
        op[2:3]         =       dest string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers:
        R0              =       0
        R1              =       address of sign byte of source string
        R2              =       0
        R3              =       addr of dest string
*/

    case CVTSP:
        if ((PSL & PSL_FPD) || (op[0] > 31) || (op[2] > 31))
            RSVD_OPND_FAULT;
        dst = Dstr_zero;                                /* clear result */
        t = Read (op[1], L_BYTE, RA);                   /* read source sign */
        if (t == C_MINUS)                               /* sign -, */
            dst.sign = 1;
        else if ((t != C_PLUS) && (t != C_SPACE))       /* + or blank? */
            RSVD_OPND_FAULT;
        for (i = 1; i <= op[0]; i++) {                  /* loop thru chars */
            c = Read ((op[1] + op[0] + 1 - i) & LMASK, L_BYTE, RA);
            if ((c < C_ZERO) || (c > C_NINE))           /* [0:9]? */
                RSVD_OPND_FAULT;
            d = c & 0xF;
            dst.val[i / 8] = dst.val[i / 8] | (d << ((i % 8) * 4));
            }
        TestDstr (&dst);                                /* correct -0 */
        cc = WriteDstr (op[2], op[3], &dst, 0, acc);    /* write result */
        R[0] = 0;
        R[1] = op[1];
        R[2] = 0;
        R[3] = op[3];
        return cc;

/* CVTPS 

   Operands:
        op[0:1]         =       source string descriptor
        op[2:3]         =       dest string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers:
        R0              =       0
        R1              =       addr of source string
        R2              =       0
        R3              =       addr of dest string
*/

    case CVTPS:
        if ((PSL & PSL_FPD) || (op[0] > 31) || (op[2] > 31))
            RSVD_OPND_FAULT;
        lenl = ReadDstr (op[0], op[1], &dst, acc);      /* get source, lw len */
        lenp = LntDstr (&dst, lenl);                    /* get exact nz src len */
        ProbeDstr (op[2], op[3], WA);                   /* test dst write */
        Write (op[3], dst.sign? C_MINUS: C_PLUS, L_BYTE, WA);
        for (i = 1; i <= op[2]; i++) {                  /* loop thru chars */
            d = (dst.val[i / 8] >> ((i % 8) * 4)) & 0xF;/* get digit */
            c = d | C_ZERO;                             /* cvt to ASCII */
            Write ((op[3] + op[2] + 1 - i) & LMASK, c, L_BYTE, WA);
            }
        cc = SetCCDstr (op[0], &dst, 0);                /* set cc's */
        if (lenp > op[2]) {                             /* src fit in dst? */
            cc = cc | CC_V;                             /* set ovflo */
            if (PSL & PSW_DV) SET_TRAP (TRAP_DECOVF);   /* if enabled, trap */
            }
        R[0] = 0;
        R[1] = op[1];
        R[2] = 0;
        R[3] = op[3];
        return cc;

/* CVTTP

   Operands:
        op[0:1]         =       source string descriptor
        op[2]           =       table address
        op[3:4]         =       dest string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers:
        R0              =       0
        R1              =       addr of source string
        R2              =       0
        R3              =       addr of dest string
*/

    case CVTTP:
        if ((PSL & PSL_FPD) || (op[0] > 31) || (op[3] > 31))
            RSVD_OPND_FAULT;
        dst = Dstr_zero;                                /* clear result */
        for (i = 1; i <= op[0]; i++) {                  /* loop thru char */
            c = Read ((op[1] + op[0] - i) & LMASK, L_BYTE, RA); /* read char */
            if (i != 1) {                               /* normal byte? */
                if ((c < C_ZERO) || (c > C_NINE))       /* valid digit? */
                    RSVD_OPND_FAULT;
                d = c & 0xF;
                }
            else {                                      /* highest byte */
                t = Read ((op[2] + c) & LMASK, L_BYTE, RA); /* xlate */
                d = (t >> 4) & 0xF;                     /* digit */
                t = t & 0xF;                            /* sign */
                if ((d > 0x9) || (t < 0xA))
                    RSVD_OPND_FAULT;
                if ((t == 0xB) || (t == 0xD))
                    dst.sign = 1;
                }
            dst.val[i / 8] = dst.val[i / 8] | (d << ((i % 8) * 4));
            }
        TestDstr (&dst);                                /* correct -0 */
        cc = WriteDstr (op[3], op[4], &dst, 0, acc);    /* write result */
        R[0] = 0;
        R[1] = op[1];
        R[2] = 0;
        R[3] = op[4];
        return cc;

/* CVTPT

   Operands:
        op[0:1]         =       source string descriptor
        op[2]           =       table address
        op[3:4]         =       dest string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers:
        R0              =       0
        R1              =       addr of source string
        R2              =       0
        R3              =       addr of dest string
*/

    case CVTPT:
        if ((PSL & PSL_FPD) || (op[0] > 31) || (op[3] > 31))
            RSVD_OPND_FAULT;
        lenl = ReadDstr (op[0], op[1], &dst, acc);      /* get source, lw len */
        lenp = LntDstr (&dst, lenl);                    /* get exact src len */
        ProbeDstr (op[3], op[4], WA);                   /* test writeability */
        for (i = 1; i <= op[3]; i++) {                  /* loop thru chars */
            if (i != 1) {                               /* not last? */
                d = (dst.val[i / 8] >> ((i % 8) * 4)) & 0xF; /* get digit */
                c = d + C_ZERO;                         /* convert */
                }
            else {                                      /* translate last */
                t = Read ((op[1] + (op[0] / 2)) & LMASK, L_BYTE, RA);
                c = Read ((op[2] + t) & LMASK, L_BYTE, RA);
                }
            Write ((op[4] + op[3] - i) & LMASK, c, L_BYTE, WA);
            }
        cc = SetCCDstr (op[0], &dst, 0);                /* set cc's from src */
        if (lenp > op[3]) {                             /* src fit in dst? */
            cc = cc | CC_V;                             /* set ovflo */
            if (PSL & PSW_DV) SET_TRAP (TRAP_DECOVF);   /* if enabled, trap */
            }
        R[0] = 0;
        R[1] = op[1];
        R[2] = 0;
        R[3] = op[4];
        return cc;

/* EDITPC

   Operands:
        op[0:1]         =       source string descriptor
        op[2]           =       pattern string address
        op[3]           =       dest string address

   Condition codes:
        N               =       source is negative
        Z               =       source is zero
        V               =       significant digits lost
        C               =       significant digits seen

   Registers at packup:
        R0<31:16>       =       -count of source zeroes to supply
        R0<15:0>        =       remaining source length
        R1              =       source address
        R2<31:24>       =       delta PC
        R2<19:16>       =       condition codes
        R2<15:8>        =       sign char
        R2<7:0>         =       fill char
        R3              =       pattern string address
        R4              =       original source length
        R5              =       dest string addr

   Registers at end:
        R0              =       source length
        R1              =       source addr
        R2              =       0
        R3              =       addr of byte containing EO$END
        R4              =       0
        R5              =       addr of end of dst string + 1

   Fault and abort conditions for EDITPC are complicated.  In general:
   - It is safe to take a memory management fault on the read of
     any pattern byte.  After correction of the fault, the pattern
     operator is fetched and executed again.
   - It is safe to take a memory management fault on a write-only
     operation, like fill.  After correction of the fault, the
     pattern operator is fetched and executed again.
   - The move operators do not alter visible state (registers or saved cc)
     until all memory operations are complete.
*/

    case EDITPC:
        if (PSL & PSL_FPD) {                            /* FPD set? */
            SETPC (fault_PC + STR_GETDPC (R[2]));       /* reset PC */
            fill = ED_GETFILL (R[2]);                   /* get fill */
            sign = ED_GETSIGN (R[2]);                   /* get sign */
            cc = ED_GETCC (R[2]);                       /* get cc's */
            R[0] = R[0] & ~0xFFE0;                      /* src len <= 31 */
            }
        else {                                          /* new instr */
            if (op[0] > 31)                             /* lnt > 31? */
                RSVD_OPND_FAULT;
            t = Read ((op[1] + (op[0] / 2)) & LMASK, L_BYTE, RA) & 0xF;
            if ((t == 0xB) || (t == 0xD)) {
                cc = CC_N | CC_Z;
                sign = C_MINUS;
                }
            else {
                cc = CC_Z;
                sign = C_SPACE;
                }
            fill = C_SPACE;
            R[0] = R[4] = op[0];                        /* src len */
            R[1] = op[1];                               /* src addr */
            R[2] = STR_PACK (cc, (sign << ED_V_SIGN) | (fill << ED_V_FILL));
                                                        /* delta PC, cc, sign, fill */
            R[3] = op[2];                               /* pattern */
            R[5] = op[3];                               /* dst addr */
            PSL = PSL | PSL_FPD;                        /* set FPD */
            }

        for ( ;; ) {                                    /* loop thru pattern */
            pop = Read (R[3], L_BYTE, RA);              /* rd pattern op */
            if (pop == EO_END)                          /* end? */
                break;
            if (pop & EO_RPT_FLAG) {                    /* repeat class? */
                rpt = pop & EO_RPT_MASK;                /* isolate count */
                if (rpt == 0)                           /* can't be zero */
                    RSVD_OPND_FAULT;
                pop = pop & ~EO_RPT_MASK;               /* isolate op */
                }
            switch (pop) {                              /* case on op */

            case EO_END_FLOAT:                          /* end float */
                if (!(cc & CC_C)) {                     /* not signif? */
                    Write (R[5], sign, L_BYTE, WA);     /* write sign */
                    R[5] = (R[5] + 1) & LMASK;          /* now fault safe */
                    cc = cc | CC_C;                     /* set signif */
                    }
                break;

            case EO_CLR_SIGNIF:                         /* clear signif */
                cc = cc & ~CC_C;                        /* clr C */
                break;

            case EO_SET_SIGNIF:                         /* set signif */
                cc = cc | CC_C;                         /* set C */
                break;

            case EO_STORE_SIGN:                         /* store sign */
                Write (R[5], sign, L_BYTE, WA);         /* write sign */
                R[5] = (R[5] + 1) & LMASK;              /* now fault safe */
                break;

            case EO_LOAD_FILL:                          /* load fill */
                fill = Read ((R[3] + 1) & LMASK, L_BYTE, RA);
                R[2] = ED_PUTFILL (R[2], fill);         /* now fault safe */
                R[3]++;
                break;

            case EO_LOAD_SIGN:                          /* load sign */
                sign = edit_read_sign (acc);
                R[3]++;
                break;

            case EO_LOAD_PLUS:                          /* load sign if + */
                if (!(cc & CC_N))
                    sign = edit_read_sign (acc);
                R[3]++;
                break;

            case EO_LOAD_MINUS:                         /* load sign if - */
                if (cc & CC_N)
                    sign = edit_read_sign (acc);
                R[3]++;
                break;

            case EO_INSERT:                             /* insert char */
                c = Read ((R[3] + 1) & LMASK, L_BYTE, RA);
                Write (R[5], ((cc & CC_C)? c: fill), L_BYTE, WA);
                R[5] = (R[5] + 1) & LMASK;              /* now fault safe */
                R[3]++;
                break;

            case EO_BLANK_ZERO:                         /* blank zero */
                t = Read ((R[3] + 1) & LMASK, L_BYTE, RA);
                if (t == 0)
                    RSVD_OPND_FAULT;
                if (cc & CC_Z) {                        /* zero? */
                    do {                                /* repeat and blank */
                        Write ((R[5] - t) & LMASK, fill, L_BYTE, WA);
                        } while (--t);
                    }
                R[3]++;                                 /* now fault safe */
                break;

            case EO_REPL_SIGN:                          /* replace sign */
                t = Read ((R[3] + 1) & LMASK, L_BYTE, RA);
                if (t == 0)
                    RSVD_OPND_FAULT;
                if (cc & CC_Z)
                    Write ((R[5] - t) & LMASK, fill, L_BYTE, WA);
                R[3]++;                                 /* now fault safe */
                break;

            case EO_ADJUST_LNT:                         /* adjust length */
                t = Read ((R[3] + 1) & LMASK, L_BYTE, RA);
                if ((t == 0) || (t > 31))
                    RSVD_OPND_FAULT;
                R[0] = R[0] & WMASK;                    /* clr old ld zero */
                if (R[0] > t) {                         /* decrease */
                    for (i = 0; i < (R[0] - t); i++) {  /* loop thru src */
                        d = edit_read_src (i, acc);     /* get nibble */
                        if (d)
                            cc = (cc | CC_V | CC_C) & ~CC_Z;
                        }                               /* end for */
                    edit_adv_src (R[0] - t);            /* adv src ptr */
                    }                                   /* end else */      
                else R[0] = R[0] | (((R[0] - t) & WMASK) << 16);
                R[3]++;
                break;

            case EO_FILL:                               /* fill */
                for (i = 0; i < rpt; i++)               /* fill string */
                    Write ((R[5] + i) & LMASK, fill, L_BYTE, WA);
                R[5] = (R[5] + rpt) & LMASK;            /* now fault safe */
                break;

            case EO_MOVE:
                for (i = 0; i < rpt; i++) {             /* for repeat */
                    d = edit_read_src (i, acc);         /* get nibble */
                    if (d)                              /* test for non-zero */
                        cc = (cc | CC_C) & ~CC_Z;
                    c = (cc & CC_C)? (d | 0x30): fill;  /* test for signif */
                    Write ((R[5] + i) & LMASK, c, L_BYTE, WA);
                    }                                   /* end for */
                edit_adv_src (rpt);                     /* advance src */
                R[5] = (R[5] + rpt) & LMASK;            /* advance dst */
                break;

            case EO_FLOAT:
                for (i = j = 0; i < rpt; i++, j++) {    /* for repeat */
                    d = edit_read_src (i, acc);         /* get nibble */
                    if (d && !(cc & CC_C)) {            /* nz, signif clear? */
                        Write ((R[5] + j) & LMASK, sign, L_BYTE, WA);
                        cc = (cc | CC_C) & ~CC_Z;       /* set signif */
                        j++;                            /* extra dst char */
                        }                               /* end if */
                    c = (cc & CC_C)? (d | 0x30): fill;  /* test for signif */
                    Write ((R[5] + j) & LMASK, c, L_BYTE, WA);
                    }                                   /* end for */
                edit_adv_src (rpt);                     /* advance src */
                R[5] = (R[5] + j) & LMASK;              /* advance dst */
                break;

            default:                                    /* undefined */
                RSVD_OPND_FAULT;
                }                                       /* end case pattern */

            R[3] = (R[3] + 1) & LMASK;                  /* next pattern byte */
            R[2] = ED_PUTCC (R[2], cc);                 /* update cc's */
            }                                           /* end for pattern */

        if (R[0])                                       /* pattern too short */
            RSVD_OPND_FAULT;
        PSL = PSL & ~PSL_FPD;                           /* clear FPD */
        if (cc & CC_Z)                                  /* zero? clear n */
            cc = cc & ~CC_N;
        if ((cc & CC_V) && (PSL & PSW_DV))              /* overflow & trap enabled? */
            SET_TRAP (TRAP_DECOVF);
        R[0] = R[4];                                    /* restore src len */
        R[1] = R[1] - (R[0] >> 1);                      /* restore src addr */
        R[2] = R[4] = 0;
        return cc;

    default:
        RSVD_INST_FAULT;
        }
                                                        /* end case op */
return cc;
}

/* Get packed decimal string

   Arguments:
        lnt     =       decimal string length
        adr     =       decimal string address
        src     =       decimal string structure
        acc     =       access mode

   The routine returns the length in int32's of the non-zero part of
   the string.

   To simplify the code elsewhere, digits are range checked,
   and bad digits cause a fault.
*/

int32 ReadDstr (int32 lnt, int32 adr, DSTR *src, int32 acc)
{
int32 c, i, end, t;

*src = Dstr_zero;                                       /* clear result */
end = lnt / 2;                                          /* last byte */
for (i = 0; i <= end; i++) {                            /* loop thru string */
    c = Read ((adr + end - i) & LMASK, L_BYTE, RA);     /* get byte */
    if (i == 0) {                                       /* sign char? */
        t = c & 0xF;                                    /* save sign */
        c = c & 0xF0;                                   /* erase sign */
        }
    if ((i == end) && ((lnt & 1) == 0))
        c = c & 0xF;
/*    if (((c & 0xF0) > 0x90) ||                        *//* check hi digit */
/*        ((c & 0x0F) > 0x09))                          *//* check lo digit */    
/*        RSVD_OPND_FAULT; */
    src->val[i / 4] = src->val[i / 4] | (c << ((i % 4) * 8));
    }                                                   /* end for */
if ((t == 0xB) || (t == 0xD))                           /* if -, set sign */
    src->sign = 1;
return TestDstr (src);                                  /* clean -0 */
}
       
/* Store decimal string

   Arguments:
        lnt     =       decimal string length
        adr     =       decimal string address
        dst     =       decimal string structure
        V       =       initial overflow flag
        acc     =       access mode

   Returns condition codes.

   PSL.NZ are also set to their proper values
   PSL.V will be set on overflow; it must be initialized elsewhere
        (to allow for external overflow calculations)

   The rules for the stored sign and the PSW sign are:

   - Stored sign is negative if input is negative, and the result
     is non-zero or there was overflow
   - PSL sign is negative if input is negative, and the result is
     non-zero

   Thus, the stored sign and the PSL sign will differ in one case:
   a negative zero generated by overflow is stored with a negative
   sign, but PSL.N is clear
*/

int32 WriteDstr (int32 lnt, int32 adr, DSTR *dst, int32 pslv, int32 acc)
{
int32 c, i, cc, end;

end = lnt / 2;                                          /* end of string */
ProbeDstr (end, adr, WA);                               /* test writeability */
cc = SetCCDstr (lnt, dst, pslv);                        /* set cond codes */
dst->val[0] = dst->val[0] | 0xC | dst->sign;            /* set sign */
for (i = 0; i <= end; i++) {                            /* store string */
    c = (dst->val[i / 4] >> ((i % 4) * 8)) & 0xFF;
    Write ((adr + end - i) & LMASK, c, L_BYTE, WA);
    }                                                   /* end for */
return cc;
}

/* Set CC for decimal string

   Arguments:
        lnt     =       string length
        dst     =       decimal string structure
        pslv    =       initial V

   Output:
        cc      =       condition codes
*/

int32 SetCCDstr (int32 lnt, DSTR *dst, int32 pslv)
{
int32 psln, pslz, i, limit;
uint32 mask;
static uint32 masktab[8] = {
    0xFFFFFFF0, 0xFFFFFF00, 0xFFFFF000, 0xFFFF0000,
    0xFFF00000, 0xFF000000, 0xF0000000, 0x00000000
    };

mask = 0;                                               /* can't ovflo */
pslz = 1;                                               /* assume all 0's */
limit = lnt / 8;                                        /* limit for test */
for (i = 0; i < DSTRLNT; i++) {                         /* loop thru value */
    if (i == limit)                                     /* at limit, get mask */
        mask = masktab[lnt % 8];
    else if (i > limit)                                 /* beyond, all ovflo */
        mask = 0xFFFFFFFF;
    if (dst->val[i] & mask)                             /* test for ovflo */
        pslv = 1;
    dst->val[i] = dst->val[i] & ~mask;                  /* clr digits past end */
    if (dst->val[i])                                    /* test nz */
        pslz = 0;
    }
dst->sign = dst->sign & ~(pslz & ~pslv);
psln = dst->sign & ~pslz;                               /* N = sign, if ~zero */
if (pslv && (PSL & PSW_DV))
    SET_TRAP (TRAP_DECOVF);
return (psln? CC_N: 0) | (pslz? CC_Z: 0) | (pslv? CC_V: 0);
}

/* Probe decimal string for accessibility */

void ProbeDstr (int32 lnt, int32 addr, int32 acc)
{
Read (addr, L_BYTE, acc);
Read ((addr + lnt) & LMASK, L_BYTE, acc);
return;
}

/* Add decimal string magnitudes

   Arguments:
        s1      =       src1 decimal string
        s2      =       src2 decimal string
        ds      =       dest decimal string
        cy      =       carry in
   Output       =       1 if carry, 0 if no carry

   This algorithm courtesy Anton Chernoff, circa 1992 or even earlier.

   We trace the history of a pair of adjacent digits to see how the
   carry is fixed; each parenthesized item is a 4b digit.

   Assume we are adding:

        (a)(b)  I
   +    (x)(y)  J

   First compute I^J:

        (a^x)(b^y)      TMP

   Note that the low bit of each digit is the same as the low bit of
   the sum of the digits, ignoring the carry, since the low bit of the
   sum is the xor of the bits.

   Now compute I+J+66 to get decimal addition with carry forced left
   one digit:

        (a+x+6+carry mod 16)(b+y+6 mod 16)      SUM

   Note that if there was a carry from b+y+6, then the low bit of the
   left digit is different from the expected low bit from the xor.
   If we xor this SUM into TMP, then the low bit of each digit is 1
   if there was a carry, and 0 if not.  We need to subtract 6 from each
   digit that did not have a carry, so take ~(SUM ^ TMP) & 0x11, shift
   it right 4 to the digits that are affected, and subtract 6*adjustment
   (actually, shift it right 3 and subtract 3*adjustment).
*/

int32 AddDstr (DSTR *s1, DSTR *s2, DSTR *ds, int32 cy)
{
int32 i;
uint32 sm1, sm2, tm1, tm2, tm3, tm4;

for (i = 0; i < DSTRLNT; i++) {                         /* loop low to high */
    tm1 = s1->val[i] ^ (s2->val[i] + cy);               /* xor operands */
    sm1 = s1->val[i] + (s2->val[i] + cy);               /* sum operands */
    sm2 = sm1 + 0x66666666;                             /* force carry out */
    cy = ((sm1 < s1->val[i]) || (sm2 < sm1));           /* check for overflow */
    tm2 = tm1 ^ sm2;                                    /* get carry flags */
    tm3 = (tm2 >> 3) | (cy << 29);                      /* compute adjustment */
    tm4 = 0x22222222 & ~tm3;                            /* clear where carry */
    ds->val[i] = (sm2 - (3 * tm4)) & LMASK;             /* final result */
    }
return cy;
}

/* Subtract decimal string magnitudes

   Arguments:
        s1      =       src1 decimal string
        s2      =       src2 decimal string
        ds      =       dest decimal string
   Outputs:             s2 - s1 in ds

   Note: the routine assumes that s1 <= s2

*/

void SubDstr (DSTR *s1, DSTR *s2, DSTR *ds)
{
int32 i;
DSTR complX;

for (i = 0; i < DSTRLNT; i++)                           /* 10's comp s2 */
    complX.val[i] = 0x99999999 - s1->val[i];
AddDstr (&complX, s2, ds, 1);                            /* s1 + ~s2 + 1 */
return;
}

/* Compare decimal string magnitudes

   Arguments:
        s1      =       src1 decimal string
        s2      =       src2 decimal string
   Output       =       1 if >, 0 if =, -1 if <
*/

int32 CmpDstr (DSTR *s1, DSTR *s2)
{
int32 i;

for (i = DSTRMAX; i >=0; i--) {
    if (s1->val[i] > s2->val[i])
        return 1;
    if (s1->val[i] < s2->val[i])
        return -1;
    }
return 0;
}

/* Test decimal string for zero

   Arguments:
        dsrc    =       decimal string structure

   Returns the non-zero length of the string, in int32 units
   If the string is zero, the sign is cleared
*/

int32 TestDstr (DSTR *dsrc)
{
int32 i;

for (i = DSTRMAX; i >= 0; i--) {
    if (dsrc->val[i])
        return (i + 1);
    }
dsrc->sign = 0;
return 0;
}

/* Get exact length of decimal string

   Arguments:
        dsrc    =       decimal string structure
        nz      =       result from TestDstr
*/

int32 LntDstr (DSTR *dsrc, int32 nz)
{
int32 i;

if (nz == 0)
    return 0;
for (i = 7; i >= 0; i--) {
    if ((dsrc->val[nz - 1] >> (i * 4)) & 0xF)
        break;
    }
return ((nz - 1) * 8) + i;
}

/* Create table of multiples

   Arguments:
        dsrc    =       base decimal string structure
        mtable[10] =    array of decimal string structures

   Note that dsrc has a high order zero nibble; this
   guarantees that the largest multiple won't overflow
   Also note that mtable[0] is not filled in
*/

void CreateTable (DSTR *dsrc, DSTR mtable[10])
{
int32 (i);

mtable[1] = *dsrc;
for (i = 2; i < 10; i++)
    AddDstr (&mtable[1], &mtable[i-1], &mtable[i], 0);
return;
}

/* Word shift right

   Arguments:
        dsrc    =       decimal string structure
        sc      =       shift count
*/

void WordRshift (DSTR *dsrc, int32 sc)
{
int32 i;

if (sc != 0) {
    for (i = 0; i < DSTRLNT; i++) {
        if ((i + sc) < DSTRLNT)
            dsrc->val[i] = dsrc->val[i + sc];
        else dsrc->val[i] = 0;
        }
    }
return;
}

/* Word shift left

   Arguments:
        dsrc    =       decimal string structure
        sc      =       shift count
*/

int32 WordLshift (DSTR *dsrc, int32 sc)
{
int32 i, c, zc;

c = 0;
if (sc != 0) {
    for (i = DSTRMAX; i >= 0; i--) {                    /* work hi to low */
        if ((i + sc) <= DSTRMAX)                        /* move in range? */
            dsrc->val[i + sc] = dsrc->val[i];
        else c |= dsrc->val[i];                         /* no, count as ovflo */
        }
    zc = (sc >= DSTRLNT)? DSTRLNT: sc;                  /* cap fill */
    for (i = 0; i < zc; i++)                            /* fill with 0s */
        dsrc->val[i] = 0;
    }
return c;
}               

/* Nibble shift decimal string right

   Arguments:
        dsrc    =       decimal string structure
        sc      =       shift count
        cin     =       carry in
*/

uint32 NibbleRshift (DSTR *dsrc, int32 sc, uint32 cin)
{
int32 i, s, nc;

if ((s = sc * 4) != 0) {
    for (i = DSTRMAX; i >= 0; i--) {
        nc = (dsrc->val[i] << (32 - s)) & LMASK;
        dsrc->val[i] = ((dsrc->val[i] >> s) |
            cin) & LMASK;
        cin = nc;
        }
    return cin;
    }
return 0;
}

/* Nibble shift decimal string left

   Arguments:
        dsrc    =       decimal string structure
        sc      =       shift count
        cin     =       carry in
*/

uint32 NibbleLshift (DSTR *dsrc, int32 sc, uint32 cin)
{
int32 i, s, nc;

if ((s = sc * 4) != 0) {
    for (i = 0; i < DSTRLNT; i++) {
        nc = dsrc->val[i] >> (32 - s);
        dsrc->val[i] = ((dsrc->val[i] << s) |
            cin) & LMASK;
        cin = nc;
        }
    return cin;
    }
return 0;
}

/* Do 4b of CRC calculation

   Arguments:
        crc     =       current CRC ^ char
        tbl     =       16 lw table base

   Output:
        new CRC
*/

int32 do_crc_4b (int32 crc, int32 tbl, int32 acc)
{
int32 idx = (crc & 0xF) << 2;
int32 t;

crc = (crc >> 4) & 0x0FFFFFFF;
t = Read ((tbl + idx) & LMASK, L_LONG, RA);
return crc ^ t;
}

/* Edit routines */

int32 edit_read_src (int32 inc, int32 acc)
{
int32 c, r0, r1;

if (R[0] & LSIGN) {                                     /* ld zeroes? */
    r0 = (R[0] + (inc << 16)) & LMASK;                  /* retire increment */
    if (r0 & LSIGN)                                     /* more? return 0 */
        return 0;
    inc = (r0 >> 16) & 0x1F;                            /* effective inc */
    }
r1 = (R[1] + (inc / 2) + ((~R[0] & inc) & 1)) & LMASK;  /* eff addr */
r0 = (R[0] - inc) & 0x1F;                               /* eff lnt left */
if (r0 == 0) {                                          /* nothing left? */
    R[0] = -1;                                          /* out of input */
    RSVD_OPND_FAULT;
    }
c = Read (r1, L_BYTE, RA);
return (((r0 & 1)? (c >> 4): c) & 0xF);
}

void edit_adv_src (int32 inc)
{
if (R[0] & LSIGN) {                                     /* ld zeroes? */
    R[0] = (R[0] + (inc << 16)) & LMASK;                /* retire 0's */
    if (R[0] & LSIGN)                                   /* more to do? */
        return;
    inc = (R[0] >> 16) & 0x1F;                          /* get excess */
    if (inc == 0)                                       /* more to do? */
        return;
    }
R[1] = (R[1] + (inc / 2) + ((~R[0] & inc) & 1)) & LMASK;/* retire src */
R[0] = (R[0] - inc) & 0x1F;
return;
}

int32 edit_read_sign (int32 acc)
{
int32 sign;

sign = Read ((R[3] + 1) & LMASK, L_BYTE, RA);           /* read */
R[2] = ED_PUTSIGN (R[2], sign);                         /* now fault safe */
return sign;
}

#else

/* CIS instructions - invoke emulator interface

        opnd[0:5] =     six operands to be pushed (if PSL<fpd> = 0)
        cc      =       condition codes
        opc     =       opcode

   If FPD is set, push old PC and PSL on stack, vector thru SCB.
   If FPD is clear, push opcode, old PC, operands, new PC, and PSL
        on stack, vector thru SCB.
   In both cases, the exception occurs in the current mode.
*/

int32 op_cis (int32 *opnd, int32 cc, int32 opc, int32 acc)
{
int32 vec;

if (PSL & PSL_FPD) {                                    /* FPD set? */
    Read (SP - 1, L_BYTE, WA);                          /* wchk stack */
    Write (SP - 8, fault_PC, L_LONG, WA);               /* push old PC */       
    Write (SP - 4, PSL | cc, L_LONG, WA);               /* push PSL */
    SP = SP - 8;                                        /* decr stk ptr */
    vec = ReadLP ((SCBB + SCB_EMULFPD) & PAMASK);
    }
else {
    if (opc == CVTPL)                                   /* CVTPL? .wl */
        opnd[2] = (opnd[2] >= 0)? ~opnd[2]: opnd[3];
    Read (SP - 1, L_BYTE, WA);                          /* wchk stack */
    Write (SP - 48, opc, L_LONG, WA);                   /* push opcode */
    Write (SP - 44, fault_PC, L_LONG, WA);              /* push old PC */
    Write (SP - 40, opnd[0], L_LONG, WA);               /* push operands */
    Write (SP - 36, opnd[1], L_LONG, WA);
    Write (SP - 32, opnd[2], L_LONG, WA);
    Write (SP - 28, opnd[3], L_LONG, WA);
    Write (SP - 24, opnd[4], L_LONG, WA);
    Write (SP - 20, opnd[5], L_LONG, WA);
    Write (SP - 8, PC, L_LONG, WA);                     /* push cur PC */
    Write (SP - 4, PSL | cc, L_LONG, WA);               /* push PSL */
    SP = SP - 48;                                       /* decr stk ptr */
    vec = ReadLP ((SCBB + SCB_EMULATE) & PAMASK);
    }
PSL = PSL & ~(PSL_TP | PSL_FPD | PSW_DV | PSW_FU | PSW_IV | PSW_T);
JUMP (vec & ~03);                                       /* set new PC */
return 0;                                               /* set new cc's */
}

#endif
