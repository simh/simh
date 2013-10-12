/* pdp11_cis.c: PDP-11 CIS optional instruction set simulator

   Copyright (c) 1993-2008, Robert M Supnik

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

   This module simulates the PDP-11 commercial instruction set (CIS).

   16-Oct-08    RMS     Fixed overflow bug in ASHx (Word/NibbleLShift)
                        Fixed bug in DIVx (LntDstr calculation)
   30-May-06    RMS     Added interrupt tests to character instructions
                        Added 11/44 stack probe test to MOVCx (only)
   22-May-06    RMS     Fixed bug in decode table (John Dundas)
                        Fixed bug in ASHP (John Dundas)
                        Fixed bug in write decimal string with mmgt enabled
                        Fixed bug in 0-length strings in multiply/divide
   16-Sep-04    RMS     Fixed bug in CMPP/N of negative strings
   17-Oct-02    RMS     Fixed compiler warning (Hans Pufal)
   08-Oct-02    RMS     Fixed macro definitions

   The commercial instruction set consists of three instruction formats:

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    register operands
   | 0  1  1  1  1  1| 0  0  0  0|      opcode     |    076030:076057
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    076070:076077

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    inline operands
   | 0  1  1  1  1  1| 0  0  0  1|      opcode     |    076130:076157
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    076170:076177

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    load descriptors
   | 0  1  1  1  1  1| 0  0  0  0|op| 1  0|  reg   |    076020:076027
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    076060:076067

   The CIS instructions operate on character strings, packed (decimal)
   strings, and numeric (decimal) strings.  Strings are described by
   a two word descriptor:

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |                 length in bytes               |    char string
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    descriptor
   |             starting byte address             |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   |  |str type|                    |   length     |    decimal string
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+    descriptor
   |             starting byte address             |
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   Decimal string types are:

     <14:12>            data type               bytes occupied by n digits
        0               signed zoned                    n
        1               unsigned zone                   n
        2               trailing overpunch              n
        3               leading overpunch               n
        4               trailing separate               n+1
        5               leading separate                n+1
        6               signed packed                   n/2 +1
        7               unsigned packed                 n/2 +1

   Zero length character strings occupy no memory; zero length decimal strings
   require either zero bytes (zoned, overpunch) or one byte (separate, packed).

   CIS instructions can run for a very long time, so they are interruptible
   and restartable.  In the simulator, all instructions run to completion.
   The code is unoptimized.
*/

#include "pdp11_defs.h"

/* Opcode bits */

#define INLINE          0100                            /* inline */
#define PACKED          0020                            /* packed */
#define NUMERIC         0000                            /* numeric */

/* Interrupt test latency */

#define INT_TEST        100

/* Operand type definitions */

#define R0_DESC         1                               /* descr in R0:R1 */
#define R2_DESC         2                               /* descr in R2:R3 */
#define R4_DESC         3                               /* descr in R4:R5 */
#define R4_ARG          4                               /* argument in R4 */
#define IN_DESC         5                               /* inline descriptor */
#define IN_ARG          6                               /* inline argument */
#define MAXOPN          4                               /* max # operands */

/* Decimal data type definitions */

#define XZ              0                               /* signed zoned */
#define UZ              1                               /* unsigned zoned */
#define TO              2                               /* trailing overpunch */
#define LO              3                               /* leading overpunch */
#define TS              4                               /* trailing separate */
#define LS              5                               /* leading separate */
#define XP              6                               /* signed packed */
#define UP              7                               /* unsigned packed */

/* Decimal descriptor definitions */

#define DTYP_M          07                              /* type mask */
#define DTYP_V          12                              /* type position */
#define DLNT_M          037                             /* length mask */
#define DLNT_V          0                               /* length position */
#define GET_DTYP(x)     (((x) >> DTYP_V) & DTYP_M)
#define GET_DLNT(x)     (((x) >> DLNT_V) & DLNT_M)

/* Shift operand definitions */

#define ASHRND_M        017                             /* round digit mask */
#define ASHRND_V        8                               /* round digit pos */
#define ASHLNT_M        0377                            /* shift count mask */
#define ASHLNT_V        0                               /* shift length pos */
#define ASHSGN          0200                            /* shift sign */
#define GET_ASHRND(x)   (((x) >> ASHRND_V) & ASHRND_M)
#define GET_ASHLNT(x)   (((x) >> ASHLNT_V) & ASHLNT_M)

/* Operand array aliases */

#define A1LNT           arg[0]
#define A1ADR           arg[1]
#define A2LNT           arg[2]
#define A2ADR           arg[3]
#define A3LNT           arg[4]
#define A3ADR           arg[5]
#define A1              &arg[0]
#define A2              &arg[2]
#define A3              &arg[4]

/* Condition code macros */

#define GET_BIT(ir,n)   (((ir) >> (n)) & 1)
#define GET_SIGN_L(ir)  GET_BIT((ir), 31)
#define GET_SIGN_W(ir)  GET_BIT((ir), 15)
#define GET_SIGN_B(ir)  GET_BIT((ir), 7)
#define GET_Z(ir)       ((ir) == 0)

/* Decimal string structure */

#define DSTRLNT         4
#define DSTRMAX         (DSTRLNT - 1)
#define MAXDVAL         429496730                       /* 2^32 / 10 */

typedef struct {
    uint32              sign;
    uint32              val[DSTRLNT];
    } DSTR;

static DSTR Dstr0 = { 0, {0, 0, 0, 0} };

extern int32 isenable, dsenable;
extern int32 N, Z, V, C, fpd, ipl;
extern int32 R[8], trap_req;
extern uint32 cpu_type;

int32 ReadDstr (int32 *dscr, DSTR *dec, int32 flag);
void WriteDstr (int32 *dscr, DSTR *dec, int32 flag);
int32 AddDstr (DSTR *src1, DSTR *src2, DSTR *dst, int32 cin);
void SubDstr (DSTR *src1, DSTR *src2, DSTR *dst);
int32 CmpDstr (DSTR *src1, DSTR *src2);
int32 TestDstr (DSTR *dsrc);
int32 LntDstr (DSTR *dsrc, int32 nz);
uint32 NibbleLshift (DSTR *dsrc, int32 sc);
uint32 NibbleRshift (DSTR *dsrc, int32 sc, uint32 cin);
int32 WordLshift (DSTR *dsrc, int32 sc);
void WordRshift (DSTR *dsrc, int32 sc);
void CreateTable (DSTR *dsrc, DSTR mtable[10]);
t_bool cis_int_test (int32 cycles, int32 oldpc, t_stat *st);
int32 movx_setup (int32 op, int32 *arg);
void movx_cleanup (int32 op);

extern int32 ReadW (int32 addr);
extern void WriteW (int32 data, int32 addr);
extern int32 ReadB (int32 addr);
extern int32 ReadMB (int32 addr);
extern void WriteB (int32 data, int32 addr);
extern int32 calc_ints (int32 nipl, int32 trq);

/* Table of instruction operands */

static int32 opntab[128][MAXOPN] = {
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 000 - 007 */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 010 - 017 */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,                             /* LD2R */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0,                                         /* MOVC */
    0, 0, 0, 0,                                         /* MOVRC */
    0, 0, 0, 0,                                         /* MOVTC */
                0, 0, 0, 0,                             /* 033 */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 034 - 037 */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0,                                         /* LOCC */
    0, 0, 0, 0,                                         /* SKPC */
    0, 0, 0, 0,                                         /* SCANC */
    0, 0, 0, 0,                                         /* SPANC */
    0, 0, 0, 0,                                         /* CMPC */
    0, 0, 0, 0,                                         /* MATC */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 046 - 047 */
    R0_DESC, R2_DESC, R4_DESC, 0,                       /* ADDN */
    R0_DESC, R2_DESC, R4_DESC, 0,                       /* SUBN */
    R0_DESC, R2_DESC, 0, 0,                             /* CMPN */
    R0_DESC, 0, 0, 0,                                   /* CVTNL */
    R0_DESC, R2_DESC, 0, 0,                             /* CVTPN */
    R0_DESC, R2_DESC, 0, 0,                             /* CVTNP */
    R0_DESC, R2_DESC, R4_ARG, 0,                        /* ASHN */
    R0_DESC, 0, 0, 0,                                   /* CVTLN */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* LD3R */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    R0_DESC, R2_DESC, R4_DESC, 0,                       /* ADDP */
    R0_DESC, R2_DESC, R4_DESC, 0,                       /* SUBP */
    R0_DESC, R2_DESC, 0, 0,                             /* CMPP */
    R0_DESC, 0, 0, 0,                                   /* CVTPL */
    R0_DESC, R2_DESC, R4_DESC, 0,                       /* MULP */
    R0_DESC, R2_DESC, R4_DESC, 0,                       /* DIVP */
    R0_DESC, R2_DESC, R4_ARG, 0,                        /* ASHP */
    R0_DESC, 0, 0, 0,                                   /* CVTLP */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 100 - 107 */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 110 - 117 */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 120 - 127 */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    IN_DESC, IN_DESC, IN_ARG, 0,                        /* MOVCI */
    IN_DESC, IN_DESC, IN_ARG, 0,                        /* MOVRCI */
    IN_DESC, IN_DESC, IN_ARG, IN_ARG,                   /* MOVTCI */
                0, 0, 0, 0,                             /* 133 */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 134 - 137 */
    0, 0, 0, 0, 0, 0, 0, 0,
    IN_DESC, IN_ARG, 0, 0,                              /* LOCCI */
    IN_DESC, IN_ARG, 0, 0,                              /* SKPCI */
    IN_DESC, IN_DESC, 0, 0,                             /* SCANCI */
    IN_DESC, IN_DESC, 0, 0,                             /* SPANCI */
    IN_DESC, IN_DESC, IN_ARG, 0,                        /* CMPCI */
    IN_DESC, IN_DESC, 0, 0,                             /* MATCI */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 146 - 147 */
    IN_DESC, IN_DESC, IN_DESC, 0,                       /* ADDNI */
    IN_DESC, IN_DESC, IN_DESC, 0,                       /* SUBNI */
    IN_DESC, IN_DESC, 0, 0,                             /* CMPNI */
    IN_DESC, IN_ARG, 0, 0,                              /* CVTNLI */
    IN_DESC, IN_DESC, 0, 0,                             /* CVTPNI */
    IN_DESC, IN_DESC, 0, 0,                             /* CVTNPI */
    IN_DESC, IN_DESC, IN_ARG, 0,                        /* ASHNI */
    IN_DESC, IN_DESC, 0, 0,                             /* CVTLNI */
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 160 - 167 */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    IN_DESC, IN_DESC, IN_DESC, 0,                       /* ADDPI */
    IN_DESC, IN_DESC, IN_DESC, 0,                       /* SUBPI */
    IN_DESC, IN_DESC, 0, 0,                             /* CMPPI */
    IN_DESC, IN_ARG, 0, 0,                              /* CVTPLI */
    IN_DESC, IN_DESC, IN_DESC, 0,                       /* MULPI */
    IN_DESC, IN_DESC, IN_DESC, 0,                       /* DIVPI */
    IN_DESC, IN_DESC, IN_ARG, 0,                        /* ASHPI */
    IN_DESC, IN_DESC, 0, 0                              /* CVTLPI */
    };

/* ASCII to overpunch table: sign is <7>, digit is <4:0> */

static int32 overbin[128] = {
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 000 - 037 */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0x80, 0, 0, 0, 0, 0, 0,                          /* 040 - 077 */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 0x80, 0, 0, 0, 0, 0,
    0, 1, 2, 3, 4, 5, 6, 7,                             /* 100 - 137 */
    8, 9, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86,
    0x87, 0x88, 0x89, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0x80, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,                             /* 140 - 177 */
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0x80, 0, 0
    };

/* Overpunch to ASCII table: indexed by sign and digit */

static int32 binover[2][16] = {
    {'{', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',
     '0', '0', '0', '0', '0', '0'},
    {'}', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',
     '0', '0', '0', '0', '0', '0'}
    };

/* CIS emulator */

t_stat cis11 (int32 IR)
{
int32 c, i, j, t, op, rn, addr;
int32 match, limit, mvlnt, shift;
int32 spc, ldivd, ldivr;
int32 arg[6];                                           /* operands */
int32 old_PC;
uint32 nc, digit, result;
t_stat st;
static DSTR accum, src1, src2, dst;
static DSTR mptable[10];
static DSTR Dstr1 = { 0, {0x10, 0, 0, 0} };

old_PC = (PC - 2) & 0177777;                            /* original PC */
op = IR & 0177;                                         /* IR <6:0> */
for (i = j = 0; (i < MAXOPN) && opntab[op][i]; i++) {   /* parse operands */
    switch (opntab[op][i]) {                            /* case on op type */

        case R0_DESC:
            arg[j++] = R[0];
            arg[j++] = R[1];
            break;

        case R2_DESC:
            arg[j++] = R[2];
            arg[j++] = R[3];
            break;

        case R4_DESC:
            arg[j++] = R[4];
            arg[j++] = R[5];
            break;

        case R4_ARG:
            arg[j++] = R[4];
            break;

        case IN_DESC:
            addr = ReadW (PC | isenable);
            PC = (PC + 2) & 0177777;
            arg[j++] = ReadW (addr | dsenable);
            arg[j++] = ReadW (((addr + 2) & 0177777) | dsenable);
            break;

        case IN_ARG:
            arg[j++] = ReadW (PC | isenable);
            PC = (PC + 2) & 0177777;
            break;

        default:
            return SCPE_IERR;
            }                                           /* end case */
    }                                                   /* end for */
switch (op) {                                           /* case on opcode */

/* MOVC, MOVTC, MOVCI, MOVTCI

   Operands (MOVC, MOVTC):
        R0, R1          =       source string descriptor
        R2, R3          =       dest string descriptor
        R4<7:0>         =       fill character
        R5              =       translation table address (MOVTC only)
   Operands (MOVCI, MOVTCI):
        A1LNT, A1ADR    =       source string descriptor
        A2LNT, A2ADR    =       dest string descriptor
        A3LNT<7:0>      =       fill character
        A3ADR           =       translation table address (MOVTCI only)

   Condition codes:
        NZVC            =       set from src.lnt - dst.lnt

   Registers (MOVC, MOVTC only)
        R0              =       max (0, src.len - dst.len)
        R1:R3           =       0
        R4:R5           =       unchanged

        Notes:   
        - If either the source or destination lengths are zero,
          the move loops exit immediately.
        - If the source length does not exceed the destination
          length, the fill loop exits immediately.
*/

    case 030: case 032: case 0130: case 0132:
        if (!fpd) {                                     /* first time? */
            mvlnt = movx_setup (op, arg);               /* set up reg */
            if (R[1] < R[3]) {                          /* move backwards? */
                R[1] = (R[1] + mvlnt) & 0177777;        /* bias addresses */
                R[3] = (R[3] + mvlnt) & 0177777;
                }
            }

/* At this point,

   R0-R5    =   arguments
   M[SP]    =   move length */

        if (R[0] && R[2]) {                             /* move to do? */
            if (R[1] < R[3]) {                          /* backwards? */
                for (i = 0; R[0] && R[2]; ) {           /* move loop */
                    t = ReadB (((R[1] - 1) & 0177777) | dsenable);
                    if (op & 2)
                        t = ReadB (((R[5] + t) & 0177777) | dsenable);
                    WriteB (t, ((R[3] - 1) & 0177777) | dsenable);
                    R[0]--;
                    R[1] = (R[1] - 1) & 0177777;
                    R[2]--;
                    R[3] = (R[3] - 1) & 0177777;
                    if ((++i >= INT_TEST) && R[0] && R[2]) {
                        if (cis_int_test (i, old_PC, &st))
                            return st;
                        i = 0;
                        }
                    }                                   /* end for lnts */
                mvlnt = ReadW (SP | dsenable);          /* recover mvlnt */
                R[3] = (R[3] + mvlnt) & 0177777;        /* end of dst str */
                }                                       /* end if bkwd */
            else {                                      /* forward */
                for (i = 0; R[0] && R[2]; ) {           /* move loop */
                    t = ReadB ((R[1] & 0177777) | dsenable);
                    if (op & 2)
                        t = ReadB (((R[5] + t) & 0177777) | dsenable);
                    WriteB (t, (R[3] & 0177777) | dsenable);
                    R[0]--;
                    R[1] = (R[1] + 1) & 0177777;
                    R[2]--;
                    R[3] = (R[3] + 1) & 0177777;
                    if ((++i >= INT_TEST) && R[0] && R[2]) {
                        if (cis_int_test (i, old_PC, &st))
                            return st;
                        i = 0;
                        }
                    }                                   /* end for lnts */
                }                                       /* end else fwd */
            }                                           /* end if move */
        for (i = 0; i < R[2]; i++) {
            WriteB (R[4], ((R[3] + i) & 0177777) | dsenable);
            }
        movx_cleanup (op);                              /* cleanup */
        return SCPE_OK;

/* MOVRC, MOVRCI

   Operands (MOVC, MOVTC):
        R0, R1          =       source string descriptor
        R2, R3          =       dest string descriptor
        R4<7:0>         =       fill character
   Operands (MOVCI, MOVTCI):
        A1LNT, A1ADR    =       source string descriptor
        A2LNT, A2ADR    =       dest string descriptor
        A3LNT<7:0>      =       fill character

   Condition codes:
        NZVC            =       set from src.lnt - dst.lnt

   Registers (MOVRC only)
        R0              =       max (0, src.len - dst.len)
        R1:R3           =       0
        R4:R5           =       unchanged

   Notes: see MOVC, MOVCI
*/

    case 031: case 0131:
        if (!fpd) {                                     /* first time? */
            mvlnt = movx_setup (op, arg);               /* set up reg */
            R[1] = (R[1] + R[0] - mvlnt) & 0177777;     /* eff move start */
            R[3] = (R[3] + R[2] - mvlnt) & 0177777;
            if (R[1] < R[3]) {                          /* move backwards? */
                R[1] = (R[1] + mvlnt) & 0177777;        /* bias addresses */
                R[3] = (R[3] + mvlnt) & 0177777;
                }
            }

/* At this point,

   R0-R5    =   arguments
   M[SP]    =   move length */

        if (R[0] && R[2]) {                             /* move to do? */
            if (R[1] < R[3]) {                          /* backwards? */
                for (i = 0; R[0] && R[2]; ) {           /* move loop */
                    t = ReadB (((R[1] - 1) & 0177777) | dsenable);
                    WriteB (t, ((R[3] - 1) & 0177777) | dsenable);
                    R[0]--;
                    R[1] = (R[1] - 1) & 0177777;
                    R[2]--;
                    R[3] = (R[3] - 1) & 0177777;
                    if ((++i >= INT_TEST) && R[0] && R[2]) {
                        if (cis_int_test (i, old_PC, &st))
                            return st;
                        i = 0;
                        }
                    }                                   /* end for lnts */
                }                                       /* end if bkwd */
            else {                                      /* forward */
                for (i = 0; R[0] && R[2]; ) {           /* move loop */
                    t = ReadB ((R[1] & 0177777) | dsenable);
                    WriteB (t, (R[3] & 0177777) | dsenable);
                    R[0]--;
                    R[1] = (R[1] + 1) & 0177777;
                    R[2]--;
                    R[3] = (R[3] + 1) & 0177777;
                    if ((++i >= INT_TEST) && R[0] && R[2]) {
                        if (cis_int_test (i, old_PC, &st))
                            return st;
                        i = 0;
                        }
                    }                                   /* end for lnts */
                mvlnt = ReadW (SP | dsenable);          /* recover mvlnt */
                R[3] = (R[3] - mvlnt) & 0177777;        /* start of dst str */
                }                                       /* end else fwd */
            }                                           /* end if move */
        for (i = 0; i < R[2]; i++) {
            WriteB (R[4], ((R[3] - R[2] + i) & 0177777) | dsenable);
            }
        movx_cleanup (op);                              /* cleanup */
        return SCPE_OK;

/* Load descriptors - no operands */

    case 020: case 021: case 022: case 023:
    case 024: case 025: case 026: case 027:
    case 060: case 061: case 062: case 063:
    case 064: case 065: case 066: case 067:
        limit = (op & 040)? 6: 4;
        rn = IR & 07;                                   /* get register */
        t = R[rn];
        spc = (rn == 7)? isenable: dsenable;
        for (j = 0; j < limit; j = j + 2) {             /* loop for 2,3 dscr */
            addr = ReadW (((t + j) & 0177777) | spc);
            R[j] = ReadW (addr | dsenable);
            R[j + 1] = ReadW (((addr + 2) & 0177777) | dsenable);
            }
        if (rn >= limit)
            R[rn] = (R[rn] + limit) & 0177777;
        return SCPE_OK;

/* LOCC, SKPC, LOCCI, SKPCI 

   Operands (LOCC, SKPC):
        R0, R1          =       source string descriptor
        R4<7:0>         =       match character
   Operands (LOCCI, SKPCI):
        A1LNT, A1ADR    =       source string descriptor
        A2LNT<7:0>      =       match character

   Condition codes:
        NZ              =       set from R0
        VC              =       0

   Registers:
        R0:R1           =       substring descriptor where operation terminated
*/

    case 0140: case 0141:                               /* inline */
        if (!fpd) {                                     /* FPD clear? */
            WriteW (R[4], ((SP - 2) & 0177777) | dsenable); 
            SP = (SP - 2) & 0177777;                    /* push R4 */
            R[0] = A1LNT;                               /* args to registers */
            R[1] = A1ADR;
            R[4] = A2LNT;
            }                                           /* fall through */
    case 040: case 041:                                 /* register */
        fpd = 1;                                        /* set FPD */
        R[4] = R[4] & 0377;                             /* match character */
        for (i = 0; R[0] != 0;) {                       /* loop */
            c = ReadB (R[1] | dsenable);                /* get char */
            if ((c == R[4]) ^ (op & 1))                 /* = + LOC, != + SKP? */
                break;
            R[0]--;                                     /* decr count, */
            R[1] = (R[1] + 1) & 0177777;                /* incr addr */
            if ((++i >= INT_TEST) && R[0]) {            /* test for intr? */
                if (cis_int_test (i, old_PC, &st))
                    return st;
                i = 0;
                }
            }
        N = GET_SIGN_W (R[0]);
        Z = GET_Z (R[0]);
        V = C = 0;
        fpd = 0;                                        /* instr done */
        if (op & INLINE) {                              /* inline? */
            R[4] = ReadW (SP | dsenable);               /* restore R4 */
            SP = (SP + 2) & 0177777;
            }
        return SCPE_OK;

/* SCANC, SPANC, SCANCI, SPANCI

   Operands (SCANC, SPANC):
        R0, R1          =       source string descriptor
        R4<7:0>         =       mask
        R5              =       table address
   Operands (SCANCI, SPANCI):
        A1LNT, A1ADR    =       source string descriptor
        A2LNT<7:0>      =       match character
        A2ADR           =       table address

   Condition codes:
        NZ              =       set from R0
        VC              =       0

   Registers:
        R0:R1           =       substring descriptor where operation terminated
*/

    case 0142: case 0143:                               /* inline */
        if (!fpd) {                                     /* FPD clear? */
            WriteW (R[4], ((SP - 4) & 0177777) | dsenable);
            WriteW (R[5], ((SP - 2) & 0177777) | dsenable);
            SP = (SP - 4) & 0177777;                    /* push R4, R5 */
            R[0] = A1LNT;                               /* args to registers */
            R[1] = A1ADR;
            R[4] = A2LNT;
            R[5] = A2ADR;
            }                                           /* fall through */
    case 042: case 043:                                 /* register */
        fpd = 1;                                        /* set FPD */
        R[4] = R[4] & 0377;                             /* match character */
        for (i = 0; R[0] != 0;) {                       /* loop */
            t = ReadB (R[1] | dsenable);                /* get char as index */
            c = ReadB (((R[5] + t) & 0177777) | dsenable);
            if (((c & R[4]) != 0) ^ (op & 1))           /* != + SCN, = + SPN? */
                break;
            R[0]--;                                     /* decr count, */
            R[1] = (R[1] + 1) & 0177777;                /* incr addr */
            if ((++i >= INT_TEST) && R[0]) {            /* test for intr? */
                if (cis_int_test (i, old_PC, &st))
                    return st;
                i = 0;
                }
            }
        N = GET_SIGN_W (R[0]);
        Z = GET_Z (R[0]);
        V = C = 0;
        fpd = 0;                                        /* instr done */
        if (op & INLINE) {                              /* inline? */
            R[4] = ReadW (SP | dsenable);               /* restore R4, R5 */
            R[5] = ReadW (((SP + 2) & 0177777) | dsenable);
            SP = (SP + 4) & 0177777;
            }
        return SCPE_OK;

/* CMPC, CMPCI

   Operands (CMPC):
        R0, R1          =       source1 string descriptor
        R2, R3          =       source2 string descriptor
        R4<7:0>         =       fill character
   Operands (CMPCI):
        A1LNT, A1ADR    =       source1 string descriptor
        A2LNT, A2ADR    =       source2 string descriptor
        A3LNT<7:0>      =       fill character

   Condition codes:
        NZVC            =       set from src1 - src2 at mismatch, or
                        =       0100 if equal

   Registers (CMPC only):
        R0:R1           =       unmatched source1 substring descriptor
        R2:R3           =       unmatched source2 substring descriptor
*/

   case 0144:                                           /* inline */
        if (!fpd) {                                     /* FPD clear? */
            WriteW (R[0], ((SP - 10) & 0177777) | dsenable);
            WriteW (R[1], ((SP - 8) & 0177777) | dsenable);
            WriteW (R[2], ((SP - 6) & 0177777) | dsenable);
            WriteW (R[3], ((SP - 4) & 0177777) | dsenable);
            WriteW (R[4], ((SP - 2) & 0177777) | dsenable);
            SP = (SP - 10) & 0177777;                   /* push R0 - R4 */
            R[0] = A1LNT;                               /* args to registers */
            R[1] = A1ADR;
            R[2] = A2LNT;
            R[3] = A2ADR;
            R[4] = A3LNT;
            }                                           /* fall through */
   case 044:                                            /* register */
        fpd = 1;                                        /* set FPD */
        R[4] = R[4] & 0377;                             /* mask fill */
        c = t = 0;
        for (i = 0; (R[0] || R[2]); ) {                 /* until cnts == 0 */
            if (R[0])                                   /* get src1 or fill */
                c = ReadB (R[1] | dsenable);
            else c = R[4];
            if (R[2])                                   /* get src2 or fill */
                t = ReadB (R[3] | dsenable);
            else t = R[4];
            if (c != t)                                 /* if diff, done */
                break;
            if (R[0]) {                                 /* if more src1 */
                R[0]--;                                 /* decr count, */
                R[1] = (R[1] + 1) & 0177777;            /* incr addr */
                }
            if (R[2]) {                                 /* if more src2 */
                R[2]--;                                 /* decr count, */
                R[3] = (R[3] + 1) & 0177777;            /* incr addr */
                }
            if ((++i >= INT_TEST) && (R[0] || R[2])) {  /* test for intr? */
                if (cis_int_test (i, old_PC, &st))
                    return st;
                i = 0;
                }
            }
        j = c - t;                                      /* last chars read */
        N = GET_SIGN_B (j);                             /* set cc's */
        Z = GET_Z (j);
        V = GET_SIGN_B ((c ^ t) & (~t ^ j));
        C = (c < t);
        fpd = 0;                                        /* instr done */
        if (op & INLINE) {                              /* inline? */
            R[0] = ReadW (SP | dsenable);               /* restore R0 - R4 */
            R[1] = ReadW (((SP + 2) & 0177777) | dsenable);
            R[2] = ReadW (((SP + 4) & 0177777) | dsenable);
            R[3] = ReadW (((SP + 6) & 0177777) | dsenable);
            R[4] = ReadW (((SP + 8) & 0177777) | dsenable);
            SP = (SP + 10) & 0177777;
            }
        return SCPE_OK;

/* MATC, MATCI

   Operands (MATC):
        R0, R1          =       source string descriptor
        R2, R3          =       substring descriptor
   Operands (MATCI):
        A1LNT, A1ADR    =       source1 string descriptor
        A2LNT, A2ADR    =       source2 string descriptor

   Condition codes:
        NZ              =       set from R0
        VC              =       0

   Registers:
        R0:R1           =       source substring descriptor for match

   Notes:
        - If the string is zero length, and the substring is not,
          the outer loop exits immediately, and the result is
          "no match"
        - If the substring is zero length, the inner loop always
          exits immediately, and the result is a "match"
        - If the string is zero length, and the substring is as
          well, the outer loop executes, the inner loop exits
          immediately, and the result is a match, but the result
          is the length of the string (zero), or "no match"
*/

    case 0145:                                          /* inline */
        if (!fpd) {                                     /* FPD clear? */
            WriteW (R[2], ((SP - 4) & 0177777) | dsenable);
            WriteW (R[3], ((SP - 2) & 0177777) | dsenable);
            SP = (SP - 4) & 0177777;                    /* push R2, R3 */
            R[0] = A1LNT;                               /* args to registers */
            R[1] = A1ADR;
            R[2] = A2LNT;
            R[3] = A2ADR;
            }                                           /* fall through */
    case 0045:                                          /* register */
        fpd = 1;
        for (match = 0; R[0] >= R[2]; ) {               /* loop thru string */
            for (i = 0, match = 1; match && (i < R[2]); i++) {
                c = ReadB (((R[1] + i) & 0177777) | dsenable);
                t = ReadB (((R[3] + i) & 0177777) | dsenable);
                match = (c == t);                       /* end for substring */
                }
            if (match)                                  /* exit if match */
                break;
            R[0]--;                                     /* on to next char */
            R[1] = (R[1] + 1) & 0177777;
            if (cis_int_test (i, old_PC, &st))
                return st;
            }
        if (!match) {                                   /* if no match */
            R[1] = (R[1] + R[0]) & 0177777;
            R[0] = 0;
            }
        N = GET_SIGN_W (R[0]);
        Z = GET_Z (R[0]);
        V = C = 0;
        fpd = 0;                                        /* instr done */
        if (op & INLINE) {                              /* inline? */
            R[2] = ReadW (SP | dsenable);               /* restore R2, R3 */
            R[3] = ReadW (((SP + 2) & 0177777) | dsenable);
            SP = (SP + 4) & 0177777;
            }
        return SCPE_OK;

/* ADDN, SUBN, ADDP, SUBP, ADDNI, SUBNI, ADDPI, SUBPI

   Operands:
        A1LNT, A1ADR    =       source1 string descriptor
        A2LNT, A2ADR    =       source2 string descriptor
        A3LNT, A3ADR    =       destination string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers (ADDN, ADDP, SUBN, SUBP only):
        R0:R3           =       0       
*/

    case 050: case 051: case 070: case 071:
    case 0150: case 0151: case 0170: case 0171:
        ReadDstr (A1, &src1, op);                       /* get source1 */
        ReadDstr (A2, &src2, op);                       /* get source2 */
        if (op & 1)                                     /* sub? invert sign */
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
        C = 0;
        WriteDstr (A3, &dst, op);                       /* store result */
        if ((op & INLINE) == 0)                         /* if reg, clr reg */
            R[0] = R[1] = R[2] = R[3] = 0;
        return SCPE_OK;

/* MULP, MULPI

   Operands:
        A1LNT, A1ADR    =       source1 string descriptor
        A2LNT, A2ADR    =       source2 string descriptor
        A3LNT, A3ADR    =       destination string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers (MULP only):
        R0:R3           =       0       
*/

    case 074: case 0174:
        dst = Dstr0;                                    /* clear result */
        if (ReadDstr (A1, &src1, op) && ReadDstr (A2, &src2, op)) {
            dst.sign = src1.sign ^ src2.sign;           /* sign of result */
            accum = Dstr0;                              /* clear accum */
            NibbleRshift (&src1, 1, 0);                 /* shift out sign */
            CreateTable (&src1, mptable);               /* create *1, *2, ... */
            for (i = 1; i < (DSTRLNT * 8); i++) {       /* 31 iterations */
                digit = (src2.val[i / 8] >> ((i % 8) * 4)) & 0xF;
                if (digit > 0)                          /* add in digit*mpcnd */
                    AddDstr (&mptable[digit], &accum, &accum, 0);
                nc = NibbleRshift (&accum, 1, 0);       /* ac right 4 */
                NibbleRshift (&dst, 1, nc);             /* result right 4 */
                }
            V = TestDstr (&accum) != 0;                 /* if ovflo, set V */
            }
        else V = 0;                                     /* result = 0 */
        C = 0;                                          /* C = 0 */
        WriteDstr (A3, &dst, op);                       /* store result */
        if ((op & INLINE) == 0)                         /* if reg, clr reg */
                R[0] = R[1] = R[2] = R[3] = 0;
        return SCPE_OK;

/* DIVP, DIVPI

   Operands:
        A1LNT, A1ADR    =       divisor string descriptor
        A2LNT, A2ADR    =       dividend string descriptor
        A3LNT, A3ADR    =       destination string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       set if divide by zero

   Registers (DIVP only):
        R0:R3           =       0       
*/

    case 075: case 0175:
        ldivr = ReadDstr (A1, &src1, op);               /* get divisor */
        if (ldivr == 0) {                               /* divisor = 0? */
            V = C = 1;                                  /* set cc's */
            return SCPE_OK;
            }
        ldivr = LntDstr (&src1, ldivr);                 /* get exact length */
        ldivd = ReadDstr (A2, &src2, op);               /* get dividend */
        ldivd = LntDstr (&src2, ldivd);                 /* get exact length */
        dst = Dstr0;                                    /* clear dest */
        NibbleRshift (&src1, 1, 0);                     /* right justify ops */
        NibbleRshift (&src2, 1, 0);
        if ((t = ldivd - ldivr) >= 0) {                 /* any divide to do? */
            WordLshift (&src1, t / 8);                  /* align divr to divd */
            NibbleLshift (&src1, t % 8);
            CreateTable (&src1, mptable);               /* create *1, *2, ... */
            for (i = 0; i <= t; i++) {                  /* divide loop */
                for (digit = 9; digit > 0; digit--) {   /* find digit */
                    if (CmpDstr (&src2, &mptable[digit]) >= 0) {
                        SubDstr (&mptable[digit], &src2, &src2);
                        dst.val[0] = dst.val[0] | digit;
                        break;
                        }                               /* end if */
                    }                                   /* end for */
                NibbleLshift (&src2, 1);                /* shift dividend */
                NibbleLshift (&dst, 1);                 /* shift quotient */
                }                                       /* end divide loop */
            dst.sign = src1.sign ^ src2.sign;           /* calculate sign */
            }                                           /* end if */
        V = C = 0;
        WriteDstr (A3, &dst, op);                       /* store result */
        if ((op & INLINE) == 0)                         /* if reg, clr reg */
            R[0] = R[1] = R[2] = R[3] = 0;
        return SCPE_OK;

/* CMPN, CMPP, CMPNI, CMPPI

   Operands:
        A1LNT, A1ADR    =       source1 string descriptor
        A2LNT, A2ADR    =       source2 string descriptor

   Condition codes:
        NZ              =       set from comparison
        VC              =       0

   Registers (CMPN, CMPP only):
        R0:R3           =       0
*/

    case 052: case 072: case 0152: case 0172:
        ReadDstr (A1, &src1, op);                       /* get source1 */
        ReadDstr (A2, &src2, op);                       /* get source2 */
        N = Z = V = C = 0;
        if (src1.sign != src2.sign) N = src1.sign;
        else {
            t = CmpDstr (&src1, &src2);                 /* compare strings */
            if (t < 0)
                N = (src1.sign? 0: 1);
            else if (t > 0)
                N = (src1.sign? 1: 0);
            else Z = 1;
            }
        if ((op & INLINE) == 0)                         /* if reg, clr reg */
            R[0] = R[1] = R[2] = R[3] = 0;
        return SCPE_OK;

/* ASHN, ASHP, ASHNI, ASHPI

   Operands:
        A1LNT, A1ADR    =       source string descriptor
        A2LNT, A2ADR    =       destination string descriptor
        A3LNT<11:8>     =       rounding digit
        A3LNT<7:0>      =       shift count

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers (ASHN, ASHP only):
        R0:R1, R4       =       0 
*/

    case 056: case 076: case 0156: case 0176:
        ReadDstr (A1, &src1, op);                       /* get source */
        V = C = 0;                                      /* init cc's */
        shift = GET_ASHLNT (A3LNT);                     /* get shift count */
        if (shift & ASHSGN) {                           /* right shift? */
            shift = (ASHLNT_M + 1 - shift);             /* !shift! */
            WordRshift (&src1, shift / 8);              /* do word shifts */    
            NibbleRshift (&src1, shift % 8, 0);         /* do nibble shifts */
            t = GET_ASHRND (A3LNT);                     /* get rounding digit */
            if ((t + (src1.val[0] & 0xF)) > 9)          /* rounding needed? */
                AddDstr (&src1, &Dstr1, &src1, 0);      /* round */
            src1.val[0] = src1.val[0] & ~0xF;           /* clear sign */
            }                                           /* end right shift */
        else if (shift) {                               /* left shift? */
            if (WordLshift (&src1, shift / 8))          /* do word shifts */
                V = 1;
            if (NibbleLshift (&src1, shift % 8))        /* do nibble shifts */
                V = 1;
            }                                           /* end left shift */
        WriteDstr (A2, &src1, op);                      /* store result */
        if ((op & INLINE) == 0)                         /* if reg, clr reg */
            R[0] = R[1] = R[4] = 0;
        return SCPE_OK;

/* CVTPN, CVTPNI

   Operands:
        A1LNT, A1ADR    =       source string descriptor
        A2LNT, A2ADR    =       destination string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers (CVTPN only):
        R0:R1           =       0
*/

    case 054: case 0154:
        ReadDstr (A1, &src1, PACKED);                   /* get source */
        V = C = 0;                                      /* init cc's */
        WriteDstr (A2, &src1, NUMERIC);                 /* write dest */
        if ((op & INLINE) == 0)                         /* if reg, clr reg */
            R[0] = R[1] = 0;
        return SCPE_OK;

/* CVTNP, CVTNPI

   Operands:
        A1LNT, A1ADR    =       source string descriptor
        A2LNT, A2ADR    =       destination string descriptor

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers (CVTNP only):
        R0:R1           =       0
*/

    case 055: case 0155:
        ReadDstr (A1, &src1, NUMERIC);                  /* get source */
        V = C = 0;                                      /* init cc's */
        WriteDstr (A2, &src1, PACKED);                  /* write dest */
        if ((op & INLINE) == 0)                         /* if reg, clr reg */
            R[0] = R[1] = 0;
        return SCPE_OK;

/* CVTNL, CVTPL, CVTNLI, CVTPLI

   Operands:
        A1LNT, A1ADR    =       source string descriptor
        A2LNT           =       destination address (inline only)

   Condition codes:
        NZV             =       set from result
        C               =       source < 0 and result != 0

   Registers (CVTNL, CVTPL only):
        R0:R1           =       0
        R2:R3           =       result
*/

    case 053: case 073: case 0153: case 0173:
        ReadDstr (A1, &src1, op);                       /* get source */
        V = result = 0;                                 /* clear V, result */
        for (i = (DSTRLNT * 8) - 1; i > 0; i--) {       /* loop thru digits */
            digit = (src1.val[i / 8] >> ((i % 8) * 4)) & 0xF;
            if (digit || result || V) {                 /* skip initial 0's */
                if (result >= MAXDVAL)
                    V = 1;
                result = (result * 10) + digit;
                if (result < digit)
                    V = 1;
                }                                       /* end if */
            }                                           /* end for */
        if (src1.sign)
            result = (~result + 1) & 0xFFFFFFFF;
        N = GET_SIGN_L (result);
        Z = GET_Z (result);
        V = V | (N ^ src1.sign);                        /* overflow if +2**31 */
        C = src1.sign && (Z == 0);                      /* set C based on std */
        if (op & INLINE) {                              /* inline? */
            WriteW (result & 0177777, A2LNT | dsenable);
            WriteW ((result >> 16) & 0177777,
                ((A2LNT + 2) & 0177777) | dsenable);
            }
        else {
            R[0] = R[1] = 0;
            R[2] = (result >> 16) & 0177777;
            R[3] = result & 0177777;
            }
        return SCPE_OK;

/* CVTLN, CVTLP, CVTLNI, CVTLPI

   Operands:
        A1LNT, A1ADR    =       destination string descriptor
        A2LNT, A2ADR    =       source long (CVTLNI, CVTLPI) - VAX format
        R2:R3           =       source long (CVTLN, CVTLP) - EIS format

   Condition codes:
        NZV             =       set from result
        C               =       0

   Registers (CVTLN, CVTLP only)
        R2:R3           =       0       
*/

    case 057: case 077:
        result = (R[2] << 16) | R[3];                   /* op in EIS format */
        R[2] = R[3] = 0;                                /* clear registers */
        goto CVTLx;                                     /* join common code */
    case 0157: case 0177:
        result = (A2ADR << 16) | A2LNT;                 /* op in VAX format */
    CVTLx:
        dst = Dstr0;                                    /* clear result */
        if ((dst.sign = GET_SIGN_L (result)))
            result = (~result + 1) & 0xFFFFFFFF;
        for (i = 1; (i < (DSTRLNT * 8)) && result; i++) {
            digit = result % 10;
            result = result / 10;
            dst.val[i / 8] = dst.val[i / 8] | (digit << ((i % 8) * 4));
            }
        V = C = 0;
        WriteDstr (A1, &dst, op);                       /* write result */
        return SCPE_OK;

    default:
        setTRAP (TRAP_ILL);
        break;
        }                                               /* end case */
return SCPE_OK;
}                                                       /* end cis */

/* Get decimal string

   Arguments:
        dscr    =       decimal string descriptor
        src     =       decimal string structure
        flag    =       numeric/packed flag

   The routine returns the length in int32's of the non-zero part of
   the string.

   This routine plays fast and loose with operand checking, as did the
   original 11/23 microcode (half of which I wrote).  In particular,

   - If the flag specifies packed, the type is not checked at all.
     The sign of an unsigned string is assumed to be 0xF (an
     alternative for +).
   - If the flag specifies numeric, packed types will be treated
     as unsigned zoned.
   - For separate, only the '-' sign is checked, not the '+'.

   However, to simplify the code elsewhere, digits are range checked,
   and bad digits are replaced with 0's.
*/

int32 ReadDstr (int32 *dscr, DSTR *src, int32 flag)
{
int32 c, i, end, lnt, type, t;

*src = Dstr0;                                           /* clear result */
type = GET_DTYP (dscr[0]);                              /* get type */
lnt = GET_DLNT (dscr[0]);                               /* get string length */
if (flag & PACKED) {                                    /* packed? */
    end = lnt / 2;                                      /* last byte */
    for (i = 0; i <= end; i++) {                        /* loop thru string */
        c = ReadB (((dscr[1] + end - i) & 0177777) | dsenable);
        if (i == 0)                                     /* save sign */
            t = c & 0xF;
        if ((i == end) && ((lnt & 1) == 0))
            c = c & 0xF;
        if (c >= 0xA0)                                  /* check hi digit */
            c = c & 0xF;
        if ((c & 0xF) >= 0xA)                           /* check lo digit */   
            c = c & 0xF0; 
        src->val[i / 4] = src->val[i / 4] | (c << ((i % 4) * 8));
        }                                               /* end for */
    if ((t == 0xB) || (t == 0xD))                       /* if -, set sign */
        src->sign = 1;
    src->val[0] = src->val[0] & ~0xF;                   /* clear sign */
    }                                                   /* end packed */
else {                                                  /* numeric */
    if (type >= TS) src->sign = (ReadB ((((type == TS)?
         dscr[1] + lnt: dscr[1] - 1) & 0177777) | dsenable) == '-');
    for (i = 1; i <= lnt; i++) {                        /* loop thru string */
        c = ReadB (((dscr[1] + lnt - i) & 0177777) | dsenable);
        if ((i == 1) && (type == XZ) && ((c & 0xF0) == 0x70))
            src->sign = 1;                              /* signed zoned */
        else if (((i == 1) && (type == TO)) ||
            ((i == lnt) && (type == LO))) {
            c = overbin[c & 0177];                      /* get sign and digit */
            src->sign = c >> 7;                         /* set sign */
            }
        c = c & 0xF;                                    /* get digit */
        if (c > 9)                                      /* range check */
            c = 0;
        src->val[i / 8] = src->val[i / 8] | (c << ((i % 8) * 4));
        }                                               /* end for */
    }                                                   /* end numeric */
return TestDstr (src);                                  /* clean -0 */
}
       
/* Store decimal string

   Arguments:
        dsrc    =       decimal string descriptor
        src     =       decimal string structure
        flag    =       numeric/packed flag

   PSW.NZ are also set to their proper values
   PSW.V will be set on overflow; it must be initialized elsewhere
   (to allow for external overflow calculations)

   The rules for the stored sign and the PSW sign are:

   - Stored sign is negative if input is negative, string type
     is signed, and the result is non-zero or there was overflow
   - PSW sign is negative if input is negative, string type is
     signed, and the result is non-zero

   Thus, the stored sign and the PSW sign will differ in one case:
   a negative zero generated by overflow is stored with a negative
   sign, but PSW.N is clear
*/

void WriteDstr (int32 *dscr, DSTR *dst, int32 flag)
{
int32 c, i, limit, end, type, lnt;
uint32 mask;
static uint32 masktab[8] = {
    0xFFFFFFF0, 0xFFFFFF00, 0xFFFFF000, 0xFFFF0000,
    0xFFF00000, 0xFF000000, 0xF0000000, 0x00000000
    };
static int32 unsignedtab[8] = { 0, 1, 0, 0, 0, 0, 0, 1 };

type = GET_DTYP (dscr[0]);                              /* get type */
lnt = GET_DLNT (dscr[0]);                               /* get string length */
mask = 0;                                               /* can't ovflo */
Z = 1;                                                  /* assume all 0's */
limit = lnt / 8;                                        /* limit for test */
for (i = 0; i < DSTRLNT; i++) {                         /* loop thru value */
    if (i == limit)                                     /* at limit, get mask */
        mask = masktab[lnt % 8];
    else if (i > limit)                                 /* beyond, all ovflo */
        mask = 0xFFFFFFFF;
    if (dst->val[i] & mask)                             /* test for ovflo */
        V = 1;
    if ((dst->val[i] = dst->val[i] & ~mask))            /* test nz */
        Z = 0;
    }
dst->sign = dst->sign & ~unsignedtab[type] & ~(Z & ~V);
N = dst->sign & ~Z;                                     /* N = sign, if ~zero */

if (flag & PACKED) {                                    /* packed? */
    end = lnt / 2;                                      /* end of string */
    if (type == UP)
        dst->val[0] = dst->val[0] | 0xF;
    else dst->val[0] = dst->val[0] | 0xC | dst->sign;
    for (i = 0; i <= end; i++) {                        /* store string */
        c = (dst->val[i / 4] >> ((i % 4) * 8)) & 0xFF;
        WriteB (c, ((dscr[1] + end - i) & 0177777) | dsenable);
        }                                               /* end for */
    }                                                   /* end packed */
else {
    if (type >= TS) WriteB (dst->sign? '-': '+', (((type == TS)?
         dscr[1] + lnt: dscr[1] - 1) & 0177777) | dsenable);
    for (i = 1; i <= lnt; i++) {                        /* store string */
        c = (dst->val[i / 8] >> ((i % 8) * 4)) & 0xF;   /* get digit */
        if ((i == 1) && (type == XZ) && dst->sign)
            c = c | 0x70;                               /* signed zoned */
        else if (((i == 1) && (type == TO)) ||
            ((i == lnt) && (type == LO)))
            c = binover[dst->sign][c];                  /* get sign and digit */
        else c = c | 0x30;                              /* default */
        WriteB (c, ((dscr[1] + lnt - i) & 0177777) |dsenable );    
        }                                               /* end for */
    }                                                   /* end numeric */
return;
}

/* Add decimal string magnitudes

   Arguments:
        s1      =       source1 decimal string
        s2      =       source2 decimal string
        ds      =       destination decimal string
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
    ds->val[i] = sm2 - (3 * tm4);                       /* final result */
    }
return cy;
}

/* Subtract decimal string magnitudes

   Arguments:
        s1      =       source1 decimal string
        s2      =       source2 decimal string
        ds      =       destination decimal string
   Outputs:             s2 - s1 in ds

   Note: the routine assumes that s1 <= s2

*/

void SubDstr (DSTR *s1, DSTR *s2, DSTR *ds)
{
int32 i;
DSTR compl;

for (i = 0; i < DSTRLNT; i++)
    compl.val[i] = 0x99999999 - s1->val[i];
AddDstr (&compl, s2, ds, 1);                            /* s1 + ~s2 + 1 */
return;
}

/* Compare decimal string magnitudes

   Arguments:
        s1      =       source1 decimal string
        s2      =       source2 decimal string
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
   guarantees that the largest multiple won't overflow.
   Also note that mtable[0] is not filled in.
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

if (sc) {
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
int32 i, c;

c = 0;
if (sc) {
    for (i = DSTRMAX; i >= 0; i--) {
        if (i >= sc)
            dsrc->val[i] = dsrc->val[i - sc];
        else {
            c |= dsrc->val[i];
            dsrc->val[i] = 0;
            }
        }
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

if ((s = sc * 4)) {
    for (i = DSTRMAX; i >= 0; i--) {
        nc = (dsrc->val[i] << (32 - s)) & 0xFFFFFFFF;
        dsrc->val[i] = ((dsrc->val[i] >> s) |
            cin) & 0xFFFFFFFF;
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
*/

uint32 NibbleLshift (DSTR *dsrc, int32 sc)
{
int32 i, s;
uint32 nc, cin;

cin = 0;
if ((s = sc * 4)) {
    for (i = 0; i < DSTRLNT; i++) {
        nc = dsrc->val[i] >> (32 - s);
        dsrc->val[i] = ((dsrc->val[i] << s) | cin) & 0xFFFFFFFF;
        cin = nc;
        }
    return cin;
    }
return 0;
}

/* Common setup routine for MOVC class instructions */

int32 movx_setup (int32 op, int32 *arg)
{
int32 mvlnt, t;

if (CPUT (CPUT_44)) {                                   /* 11/44? */
    ReadMB (((SP - 0200) & 0177777) | dsenable);        /* probe both blocks */
    ReadMB (((SP - 0100) & 0177777) | dsenable);        /* in 64W stack area */
    }
if (op & INLINE) {                                      /* inline */
    mvlnt = (A1LNT < A2LNT)? A1LNT: A2LNT;
    WriteW (mvlnt, ((SP - 14) & 0177777) | dsenable);   /* push move length */
    WriteW (R[0], ((SP - 12) & 0177777) | dsenable);    /* push R0 - R5 */
    WriteW (R[1], ((SP - 10) & 0177777) | dsenable);
    WriteW (R[2], ((SP - 8) & 0177777) | dsenable);
    WriteW (R[3], ((SP - 6) & 0177777) | dsenable);
    WriteW (R[4], ((SP - 4) & 0177777) | dsenable);
    WriteW (R[5], ((SP - 2) & 0177777) | dsenable);
    SP = (SP - 14) & 0177777;
    R[0] = A1LNT;                                       /* args to registers */
    R[1] = A1ADR;
    R[2] = A2LNT;
    R[3] = A2ADR;
    R[4] = A3LNT;
    R[5] = A3ADR & 0177777;
    }
else {                                                  /* register */
    mvlnt = (R[0] < R[2])? R[0]: R[2];
    WriteW (mvlnt, ((SP - 2) & 0177777) | dsenable);    /* push move length */
    SP = (SP - 2) & 0177777;
    }
fpd = 1;
t = R[0] - R[2];                                        /* src.lnt - dst.lnt */
N = GET_SIGN_W (t);                                     /* set cc's from diff */
Z = GET_Z (t);
V = GET_SIGN_W ((R[0] ^ R[2]) & (~R[2] ^ t));
C = (R[0] < R[2]);
return mvlnt;
}

/* Common cleanup routine for MOVC class instructions */

void movx_cleanup (int32 op)
{
SP = (SP + 2) & 0177777;                                /* discard mvlnt */
if (op & INLINE) {                                      /* inline? */
    R[0] = ReadW (SP | dsenable);                       /* restore R0 - R5 */
    R[1] = ReadW (((SP + 2) & 0177777) | dsenable);
    R[2] = ReadW (((SP + 4) & 0177777) | dsenable);
    R[3] = ReadW (((SP + 6) & 0177777) | dsenable);
    R[4] = ReadW (((SP + 8) & 0177777) | dsenable);
    R[5] = ReadW (((SP + 10) & 0177777) | dsenable);
    SP = (SP + 12) & 0177777;
    }
else R[1] = R[2] = R[3] = 0;                            /* reg, clear R1 - R3 */
fpd = 0;                                                /* instr done */
return;
}
    
/* Test for CIS mid-instruction interrupt */

t_bool cis_int_test (int32 cycles, int32 oldpc, t_stat *st)
{
while (cycles >= 0) {                                   /* until delay done */
    if (sim_interval > cycles) {                        /* event > delay */
        sim_interval = sim_interval - cycles;
        break;
        }
    else {                                              /* event <= delay */
        cycles = cycles - sim_interval;                 /* decr delay */
        sim_interval = 0;                               /* process event */
        *st = sim_process_event ();
        trap_req = calc_ints (ipl, trap_req);           /* recalc int req */
        if ((*st != SCPE_OK) ||                         /* bad status or */
            trap_req & TRAP_INT) {                      /* interrupt? */
            PC = oldpc;                                 /* back out */
            return TRUE;
            }                                           /* end if stop */
        }                                               /* end else event */
    }                                                   /* end while delay */
return FALSE;
}
