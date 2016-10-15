/* hp2100_cpu7.c: HP 1000 VIS and SIGNAL/1000 microcode

   Copyright (c) 2008, Holger Veit
   Copyright (c) 2006-2016, J. David Bryan

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
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   CPU7         Vector Instruction Set and SIGNAL firmware

   05-Aug-16    JDB     Renamed the P register from "PC" to "PR"
   24-Dec-14    JDB     Added casts for explicit downward conversions
   18-Mar-13    JDB     Moved EMA helper declarations to hp2100_cpu1.h
   09-May-12    JDB     Separated assignments from conditional expressions
   06-Feb-12    JDB     Corrected "opsize" parameter type in vis_abs
   11-Sep-08    JDB     Moved microcode function prototypes to hp2100_cpu1.h
   05-Sep-08    JDB     Removed option-present tests (now in UIG dispatchers)
   30-Apr-08    JDB     Updated SIGNAL code from Holger
   24-Apr-08    HV      Implemented SIGNAL
   20-Apr-08    JDB     Updated comments
   26-Feb-08    HV      Implemented VIS

   Primary references:
   - HP 1000 M/E/F-Series Computers Technical Reference Handbook
        (5955-0282, Mar-1980)
   - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
        (92851-90001, Mar-1981)
   - Macro/1000 Reference Manual (92059-90001, Dec-1992)

   Additional references are listed with the associated firmware
   implementations, as are the HP option model numbers pertaining to the
   applicable CPUs.
*/

#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu1.h"

#if defined (HAVE_INT64)                                 /* int64 support available */

#include "hp2100_fp1.h"


static const OP zero   = { { 0, 0, 0, 0, 0 } };          /* DEC 0.0D0 */


/* Vector Instruction Set

   The VIS provides instructions that operate on one-dimensional arrays of
   floating-point values.  Both single- and double-precision operations are
   supported.  VIS uses the F-Series floating-point processor to handle the
   floating-point math.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A     N/A    12824A

   The routines are mapped to instruction codes as follows:

        Single-Precision        Double-Precision
     Instr.  Opcode  Subcod  Instr.  Opcode  Subcod  Description
     ------  ------  ------  ------  ------  ------  -----------------------------
     VADD    101460  000000  DVADD   105460  004002  Vector add
     VSUB    101460  000020  DVSUB   105460  004022  Vector subtract
     VMPY    101460  000040  DVMPY   105460  004042  Vector multiply
     VDIV    101460  000060  DVDIV   105460  004062  Vector divide
     VSAD    101460  000400  DVSAD   105460  004402  Scalar-vector add
     VSSB    101460  000420  DVSSB   105460  004422  Scalar-vector subtract
     VSMY    101460  000440  DVSMY   105460  004442  Scalar-vector multiply
     VSDV    101460  000460  DVSDV   105460  004462  Scalar-vector divide
     VPIV    101461  0xxxxx  DVPIV   105461  0xxxxx  Vector pivot
     VABS    101462  0xxxxx  DVABS   105462  0xxxxx  Vector absolute value
     VSUM    101463  0xxxxx  DVSUM   105463  0xxxxx  Vector sum
     VNRM    101464  0xxxxx  DVNRM   105464  0xxxxx  Vector norm
     VDOT    101465  0xxxxx  DVDOT   105465  0xxxxx  Vector dot product
     VMAX    101466  0xxxxx  DVMAX   105466  0xxxxx  Vector maximum value
     VMAB    101467  0xxxxx  DVMAB   105467  0xxxxx  Vector maximum absolute value
     VMIN    101470  0xxxxx  DVMIN   105470  0xxxxx  Vector minimum value
     VMIB    101471  0xxxxx  DVMIB   105471  0xxxxx  Vector minimum absolute value
     VMOV    101472  0xxxxx  DVMOV   105472  0xxxxx  Vector move
     VSWP    101473  0xxxxx  DVSWP   105473  0xxxxx  Vector swap
     .ERES   101474    --     --       --      --    Resolve array element address
     .ESEG   101475    --     --       --      --    Load MSEG maps
     .VSET   101476    --     --       --      --    Vector setup
     [test]    --      --     --     105477    --    [self test]

   Instructions use IR bit 11 to select single- or double-precision format.  The
   double-precision instruction names begin with "D" (e.g., DVADD vs. VADD).
   Most VIS instructions are two words in length, with a sub-opcode immediately
   following the primary opcode.

   Notes:

     1. The .VECT (101460) and .DVCT (105460) opcodes preface a single- or
        double-precision arithmetic operation that is determined by the
        sub-opcode value.  The remainder of the dual-precision sub-opcode values
        are "don't care," except for requiring a zero in bit 15.

     2. The VIS uses the hardware FPP of the F-Series.  FPP malfunctions are
        detected by the VIS firmware and are indicated by a memory-protect
        violation and setting the overflow flag.  Under simulation,
        malfunctions cannot occur.

   Additional references:
    - 12824A Vector Instruction Set User's Manual (12824-90001, Jun-1979).
    - VIS Microcode Source (12824-18059, revision 3).
*/

static const OP_PAT op_vis[16] = {
  OP_N,    OP_AAKAKAKK,OP_AKAKK, OP_AAKK,                /*  .VECT  VPIV   VABS   VSUM   */
  OP_AAKK, OP_AAKAKK,  OP_AAKK,  OP_AAKK,                /*  VNRM   VDOT   VMAX   VMAB   */
  OP_AAKK, OP_AAKK,    OP_AKAKK, OP_AKAKK,               /*  VMIN   VMIB   VMOV   VSWP   */
  OP_AA,   OP_A,       OP_AAACCC,OP_N                    /*  .ERES  .ESEG  .VSET  [test] */
  };

static const t_bool op_ftnret[16] = {
  FALSE, TRUE,  TRUE,  TRUE,
  TRUE,  TRUE,  TRUE,  TRUE,
  TRUE,  TRUE,  TRUE,  TRUE,
  FALSE, TRUE,  TRUE, FALSE,
  };


/* handle the scalar/vector base ops */
static void vis_svop(uint32 subcode, OPS op, OPSIZE opsize)
{
OP v1,v2;
int16 delta = opsize==fp_f ? 2 : 4;
OP s = ReadOp(op[0].word,opsize);
uint32 v1addr = op[1].word;
int16 ix1 = INT16(op[2].word) * delta;
uint32 v2addr = op[3].word;
int16 ix2 = INT16(op[4].word) * delta;
int16 i, n = INT16(op[5].word);
uint16 fpuop = (uint16) (subcode & 060) | (opsize==fp_f ? 0 : 2);

if (n <= 0) return;
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    (void)fp_exec(fpuop, &v2, s ,v1);
    WriteOp(v2addr, v2, opsize);
    v1addr += ix1;
    v2addr += ix2;
    }
}

/* handle the vector/vector base ops */
static void vis_vvop(uint32 subcode, OPS op,OPSIZE opsize)
{
OP v1,v2,v3;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 v1addr = op[0].word;
int32 ix1 = INT16(op[1].word) * delta;
uint32 v2addr = op[2].word;
int32 ix2 = INT16(op[3].word) * delta;
uint32 v3addr = op[4].word;
int32 ix3 = INT16(op[5].word) * delta;
int16 i, n = INT16(op[6].word);
uint16 fpuop = (uint16) (subcode & 060) | (opsize==fp_f ? 0 : 2);

if (n <= 0) return;
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    v2 = ReadOp(v2addr, opsize);
    (void)fp_exec(fpuop, &v3, v1 ,v2);
    WriteOp(v3addr, v3, opsize);
    v1addr += ix1;
    v2addr += ix2;
    v3addr += ix3;
    }
}

#define GET_MSIGN(op) ((op)->fpk[0] & 0100000)

static void vis_abs(OP* in, OPSIZE opsize)
{
uint32 sign = GET_MSIGN(in);                             /* get sign */
if (sign) (void)fp_pcom(in, opsize);                     /* if negative, make positive */
}

static void vis_minmax(OPS op,OPSIZE opsize,t_bool domax,t_bool doabs)
{
OP v1,vmxmn,res;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 mxmnaddr = op[0].word;
uint32 v1addr = op[1].word;
int16 ix1 = INT16(op[2].word) * delta;
int16 n = INT16(op[3].word);
int16 i,mxmn,sign;
uint16 subop = 020 | (opsize==fp_f ? 0 : 2);

if (n <= 0) return;
mxmn = 0;                                                /* index of maxmin element */
vmxmn = ReadOp(v1addr,opsize);                           /* initialize with first element */
if (doabs) vis_abs(&vmxmn,opsize);                       /* ABS(v[1]) if requested */

for (i = 0; i<n; i++) {
    v1 = ReadOp(v1addr,opsize);                          /* get v[i] */
    if (doabs) vis_abs(&v1,opsize);                      /* build ABS(v[i]) if requested */
    (void)fp_exec(subop,&res,vmxmn,v1);                  /* subtract vmxmn - v1[i] */
    sign = GET_MSIGN(&res);                              /* !=0 if vmxmn < v1[i] */
    if ((domax && sign) ||                               /* new max value found */
        (!domax && !sign)) {                             /* new min value found */
        mxmn = i;
       vmxmn = v1;                                       /* save the new max/min value */
       }
    v1addr += ix1;                                       /* point to next element */
    }
res.word = mxmn+1;                                       /* adjust to one-based FTN array */
WriteOp(mxmnaddr, res, in_s);                            /* save result */
}

static void vis_vpiv(OPS op, OPSIZE opsize)
{
OP s,v1,v2,v3;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 saddr = op[0].word;
uint32 v1addr = op[1].word;
int16 ix1 = INT16(op[2].word) * delta;
uint32 v2addr = op[3].word;
int16 ix2 = INT16(op[4].word) * delta;
uint32 v3addr = op[5].word;
int16 ix3 = INT16(op[6].word) * delta;
int16 i, n = INT16(op[7].word);
int16 oplen = opsize==fp_f ? 0 : 2;

if (n <= 0) return;
s = ReadOp(saddr,opsize);
/* calculates v3[k] = s * v1[i] + v2[j] for incrementing i,j,k */
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    (void)fp_exec(040+oplen, ACCUM, s ,v1);              /* ACCU := s*v1 */
    v2 = ReadOp(v2addr, opsize);
    (void)fp_exec(004+oplen,&v3,v2,NOP);                 /* v3 := v2 + s*v1 */
    WriteOp(v3addr, v3, opsize);                         /* write result */
    v1addr += ix1;                                       /* forward to next array elements */
    v2addr += ix2;
    v3addr += ix3;
    }
}

static void vis_vabs(OPS op, OPSIZE opsize)
{
OP v1;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 v1addr = op[0].word;
int16 ix1 = INT16(op[1].word) * delta;
uint32 v2addr = op[2].word;
int32 ix2 = INT16(op[3].word) * delta;
int16 i,n = INT16(op[4].word);

if (n <= 0) return;
/* calculates v2[j] = ABS(v1[i]) for incrementing i,j */
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    vis_abs(&v1,opsize);                                 /* make absolute value */
    WriteOp(v2addr, v1, opsize);                         /* write result */
    v1addr += ix1;                                       /* forward to next array elements */
    v2addr += ix2;
    }
}

static void vis_trunc(OP* out, OP in)
{
/* Note there is fp_trun(), but this doesn't seem to do the same conversion
 * as the original code does */
out->fpk[0] = in.fpk[0];
out->fpk[1] = (in.fpk[1] & 0177400) | (in.fpk[3] & 0377);
}

static void vis_vsmnm(OPS op,OPSIZE opsize,t_bool doabs)
{
uint16 fpuop;
OP v1,sumnrm = zero;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 saddr = op[0].word;
uint32 v1addr = op[1].word;
int16 ix1 = INT16(op[2].word) * delta;
int16 i,n = INT16(op[3].word);

if (n <= 0) return;
/* calculates sumnrm = sumnrm + DBLE(v1[i]) resp DBLE(ABS(v1[i])) for incrementing i */
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    if (opsize==fp_f) (void)fp_cvt(&v1,fp_f,fp_t);       /* cvt to DBLE(v1) */
    fpuop = (doabs && GET_MSIGN(&v1)) ? 022 : 002;       /* use subtract for NRM && V1<0 */
    (void)fp_exec(fpuop,&sumnrm, sumnrm, v1);            /* accumulate */
    v1addr += ix1;                                       /* forward to next array elements */
    }
if (opsize==fp_f)
    (void)vis_trunc(&sumnrm,sumnrm);                     /* truncate to SNGL(sumnrm) */
WriteOp(saddr, sumnrm, opsize);                          /* write result */
}

static void vis_vdot(OPS op,OPSIZE opsize)
{
OP v1,v2,dot = zero;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 daddr = op[0].word;
uint32 v1addr = op[1].word;
int16 ix1 = INT16(op[2].word) * delta;
uint32 v2addr = op[3].word;
int16 ix2 = INT16(op[4].word) * delta;
int16 i,n = INT16(op[5].word);

if (n <= 0) return;
/* calculates dot = dot + v1[i]*v2[j] for incrementing i,j */
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    if (opsize==fp_f) (void)fp_cvt(&v1,fp_f,fp_t);       /* cvt to DBLE(v1) */
    v2 = ReadOp(v2addr, opsize);
    if (opsize==fp_f) (void)fp_cvt(&v2,fp_f,fp_t);       /* cvt to DBLE(v2) */
    (void)fp_exec(042,ACCUM, v1, v2);                    /* ACCU := v1 * v2 */
    (void)fp_exec(006,&dot,dot,NOP);                     /* dot := dot + v1*v2 */
    v1addr += ix1;                                       /* forward to next array elements */
    v2addr += ix2;
    }
if (opsize==fp_f)
    (void)vis_trunc(&dot,dot);                           /* truncate to SNGL(sumnrm) */
WriteOp(daddr, dot, opsize);                             /* write result */
}

static void vis_movswp(OPS op, OPSIZE opsize, t_bool doswp)
{
OP v1,v2;
int16 delta = opsize==fp_f ? 2 : 4;
uint32 v1addr = op[0].word;
int16 ix1 = INT16(op[1].word) * delta;
uint32 v2addr = op[2].word;
int16 ix2 = INT16(op[3].word) * delta;
int16 i,n = INT16(op[4].word);

if (n <= 0) return;
for (i=0; i<n; i++) {
    v1 = ReadOp(v1addr, opsize);
    v2 = ReadOp(v2addr, opsize);
    WriteOp(v2addr, v1, opsize);                         /* v2 := v1 */
    if (doswp) WriteOp(v1addr, v2, opsize);              /* v1 := v2 */
    v1addr += ix1;                                       /* forward to next array elements */
    v2addr += ix2;
    }
}

t_stat cpu_vis (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
OP ret;
uint32 entry, rtn, rtn1 = 0, subcode = 0;
OP_PAT pattern;
OPSIZE opsize;
t_bool debug = DEBUG_PRI (cpu_dev, DEB_VIS);

opsize = (IR & 004000) ? fp_t : fp_f;                    /* double or single precision */
entry = IR & 017;                                        /* mask to entry point */
pattern = op_vis[entry];

if (entry==0) {                                          /* retrieve sub opcode */
    ret = ReadOp (PR, in_s);                             /* get it */
    subcode = ret.word;
    if (subcode & 0100000)                               /* special property of ucode */
        subcode = AR;                                    /*   for reentry */
    PR = (PR + 1) & VAMASK;                              /* bump to real argument list */
    pattern = (subcode & 0400) ? OP_AAKAKK : OP_AKAKAKK; /* scalar or vector operation */
    }

if (pattern != OP_N) {
    if (op_ftnret[entry]) {                              /* most VIS instrs ignore RTN addr */
        ret = ReadOp(PR, in_s);
        rtn = rtn1 = ret.word;                           /* but save it just in case */
        PR = (PR + 1) & VAMASK;                          /* move to next argument */
        }
    reason = cpu_ops (pattern, op, intrq);               /* get instruction operands */
    if (reason != SCPE_OK)                               /* evaluation failed? */
        return reason;                                   /* return reason for failure */
    }

if (debug) {                                             /* debugging? */
    fprintf (sim_deb, ">>CPU VIS: IR = %06o/%06o (",     /* print preamble and IR */
             IR, subcode);
    fprint_sym (sim_deb, err_PC, (t_value *) &IR,        /* print instruction mnemonic */
                NULL, SWMASK('M'));
    fprintf (sim_deb, "), P = %06o", err_PC);            /* print location */
    fprint_ops (pattern, op);                            /* print operands */
    fputc ('\n', sim_deb);                               /* terminate line */
    }

switch (entry) {                                         /* decode IR<3:0> */
   case 000:                                             /* .VECT (OP_special) */
       if (subcode & 0400)
           vis_svop(subcode,op,opsize);                  /* scalar/vector op */
       else
           vis_vvop(subcode,op,opsize);                  /* vector/vector op */
       break;
   case 001:                                             /* VPIV (OP_(A)AAKAKAKK) */
       vis_vpiv(op,opsize);
       break;
   case 002:                                             /* VABS (OP_(A)AKAKK) */
       vis_vabs(op,opsize);
       break;
   case 003:                                             /* VSUM (OP_(A)AAKK) */
       vis_vsmnm(op,opsize,FALSE);
       break;
   case 004:                                             /* VNRM (OP_(A)AAKK) */
       vis_vsmnm(op,opsize,TRUE);
       break;
   case 005:                                             /* VDOT (OP_(A)AAKAKK) */
       vis_vdot(op,opsize);
       break;
   case 006:                                             /* VMAX (OP_(A)AAKK) */
       vis_minmax(op,opsize,TRUE,FALSE);
       break;
   case 007:                                             /* VMAB (OP_(A)AAKK) */
       vis_minmax(op,opsize,TRUE,TRUE);
       break;
   case 010:                                             /* VMIN (OP_(A)AAKK) */
       vis_minmax(op,opsize,FALSE,FALSE);
       break;
   case 011:                                             /* VMIB (OP_(A)AAKK) */
       vis_minmax(op,opsize,FALSE,TRUE);
       break;
   case 012:                                             /* VMOV (OP_(A)AKAKK) */
       vis_movswp(op,opsize,FALSE);
       break;
   case 013:                                             /* VSWP (OP_(A)AKAKK) */
       vis_movswp(op,opsize,TRUE);
       break;
   case 014:                                             /* .ERES (OP_(A)AA) */
       reason = cpu_ema_eres(&rtn,op[2].word,PR,debug);  /* handle the ERES instruction */
       PR = rtn;
       if (debug)
           fprintf (sim_deb,
                    ">>CPU VIS: return .ERES: AR = %06o, BR = %06o, rtn=%s\n",
                    AR, BR, PR==op[0].word ? "error" : "good");
       break;

   case 015:                                             /* .ESEG (OP_(A)A) */
       reason = cpu_ema_eseg(&rtn,IR,op[0].word,debug);  /* handle the ESEG instruction */
       PR = rtn;
       if (debug)
           fprintf (sim_deb,
                    ">>CPU VIS: return .ESEG: AR = %06o , BR = %06o, rtn=%s\n",
                    AR, BR, rtn==rtn1 ? "error" : "good");
       break;

   case 016:                                             /* .VSET (OP_(A)AAACCC) */
       reason = cpu_ema_vset(&rtn,op,debug);
       PR = rtn;
       if (debug)
           fprintf (sim_deb, ">>CPU VIS: return .VSET: AR = %06o BR = %06o, rtn=%s\n",
                    AR, BR,
                    rtn==rtn1 ? "error" : (rtn==(rtn1+1) ? "hard" : "easy") );
       break;

   case 017:                                             /* [test] (OP_N) */
       XR = 3;                                           /* firmware revision */
       SR = 0102077;                                     /* test passed code */
       PR = (PR + 1) & VAMASK;                           /* P+2 return for firmware w/VIS */
       break;
   default:                                              /* others undefined */
        reason = stop_inst;
        }

return reason;
}


/* SIGNAL/1000 Instructions

   The SIGNAL/1000 instructions provide fast Fourier transforms and complex
   arithmetic.  They utilize the F-Series floating-point processor and the
   Vector Instruction Set.

   Option implementation by CPU was as follows:

      2114    2115    2116    2100   1000-M  1000-E  1000-F
     ------  ------  ------  ------  ------  ------  ------
      N/A     N/A     N/A     N/A     N/A     N/A    92835A

   The routines are mapped to instruction codes as follows:

     Instr.  1000-F  Description
     ------  ------  ----------------------------------------------
     BITRV   105600  Bit reversal
     BTRFY   105601  Butterfly algorithm
     UNSCR   105602  Unscramble for phasor MPY
     PRSCR   105603  Unscramble for phasor MPY
     BITR1   105604  Swap two elements in array (alternate format)
     BTRF1   105605  Butterfly algorithm (alternate format)
     .CADD   105606  Complex number addition
     .CSUB   105607  Complex number subtraction
     .CMPY   105610  Complex number multiplication
     .CDIV   105611  Complex number division
     CONJG   105612  Complex conjugate
     ..CCM   105613  Complex complement
     AIMAG   105614  Return imaginary part
     CMPLX   105615  Form complex number
     [nop]   105616  [no operation]
     [test]  105617  [self test]

   Notes:

     1. SIGNAL/1000 ROM data are available from Bitsavers.

   Additional references (documents unavailable):
    - HP Signal/1000 User Reference and Installation Manual (92835-90002).
    - SIGNAL/1000 Microcode Source (92835-18075, revision 2).
*/

#define RE(x) (x+0)
#define IM(x) (x+2)

static const OP_PAT op_signal[16] = {
  OP_AAKK,  OP_AAFFKK, OP_AAFFKK,OP_AAFFKK,             /*  BITRV  BTRFY  UNSCR  PRSCR */
  OP_AAAKK, OP_AAAFFKK,OP_AAA,   OP_AAA,                /*  BITR1  BTRF1  .CADD  .CSUB */
  OP_AAA,   OP_AAA,    OP_AAA,   OP_A,                  /*  .CMPY  .CDIV  CONJG  ..CCM */
  OP_AA,    OP_AAFF,   OP_N,     OP_N                   /*  AIMAG  CMPLX  ---    [test]*/
  };

/* complex addition helper */
static void sig_caddsub(uint16 addsub,OPS op)
{
OP a,b,c,d,p1,p2;

a = ReadOp(RE(op[1].word), fp_f);                  /* read 1st op */
b = ReadOp(IM(op[1].word), fp_f);
c = ReadOp(RE(op[2].word), fp_f);                  /* read 2nd op */
d = ReadOp(IM(op[2].word), fp_f);
(void)fp_exec(addsub,&p1, a, c);                   /* add real */
(void)fp_exec(addsub,&p2, b, d);                   /* add imag */
WriteOp(RE(op[0].word), p1, fp_f);                 /* write result */
WriteOp(IM(op[0].word), p2, fp_f);                 /* write result */
}

/* butterfly operation helper */
static void sig_btrfy(uint32 re,uint32 im,OP wr,OP wi,uint32 k, uint32 n2)
{
/*
 * v(k)-------->o-->o----> v(k)
 *               \ /
 *                x
 *               / \
 * v(k+N/2)---->o-->o----> v(k+N/2)
 *           Wn   -1
 *
 */

OP p1,p2,p3,p4;
OP v1r = ReadOp(re+k, fp_f);                       /* read v1 */
OP v1i = ReadOp(im+k, fp_f);
OP v2r = ReadOp(re+k+n2, fp_f);                    /* read v2 */
OP v2i = ReadOp(im+k+n2, fp_f);

/* (p1,p2) := cmul(w,v2) */
(void)fp_exec(040, &p1, wr, v2r);                  /* S7,8 p1 := wr*v2r */
(void)fp_exec(040, ACCUM, wi, v2i);                /* ACCUM := wi*v2i */
(void)fp_exec(024, &p1, p1, NOP);                  /* S7,S8 p1 := wr*v2r-wi*v2i ==real(w*v2) */
(void)fp_exec(040, &p2, wi, v2r);                  /* S9,10 p2 := wi*v2r */
(void)fp_exec(040, ACCUM, wr, v2i);                /* ACCUM := wr*v2i */
(void)fp_exec(004, &p2, p2, NOP);                  /* S9,10 p2 := wi*v2r+wr*v2i ==imag(w*v2) */
/* v2 := v1 - (p1,p2) */
(void)fp_exec(020, &p3, v1r, p1);                  /* v2r := v1r-real(w*v2) */
(void)fp_exec(020, &p4, v1i, p2);                  /* v2i := v1i-imag(w*v2) */
WriteOp(re+k+n2, p3, fp_f);                        /* write v2r */
WriteOp(im+k+n2, p4, fp_f);                        /* write v2i */
/* v1 := v1 + (p1,p2) */
(void)fp_exec(0, &p3, v1r, p1);                    /* v1r := v1r+real(w*v2) */
(void)fp_exec(0, &p4, v1i, p2);                    /* v1i := v1i+imag(w*v2) */
WriteOp(re+k, p3, fp_f);                           /* write v1r */
WriteOp(im+k, p4, fp_f);                           /* write v1i */
O = 0;
}

/* helper for bit reversal
 * idx is 0-based already */
static void sig_bitrev(uint32 re,uint32 im, uint32 idx, uint32 log2n, int sz)
{
uint32 i, org=idx, rev = 0;
OP v1r,v1i,v2r,v2i;

for (i=0; i<log2n; i++) {                          /* swap bits of idx */
    rev = (rev<<1) | (org & 1);                    /* into rev */
    org >>= 1;
}

if (rev < idx) return;                             /* avoid swapping same pair twice in loop */

idx *= sz;                                         /* adjust for element size */
rev *= sz;                                         /* (REAL*4 vs COMPLEX*8) */

v1r = ReadOp(re+idx, fp_f);                        /* read 1st element */
v1i = ReadOp(im+idx, fp_f);
v2r = ReadOp(re+rev, fp_f);                        /* read 2nd element */
v2i = ReadOp(im+rev, fp_f);
WriteOp(re+idx, v2r, fp_f);                        /* swap elements */
WriteOp(im+idx, v2i, fp_f);
WriteOp(re+rev, v1r, fp_f);
WriteOp(im+rev, v1i, fp_f);
}

/* helper for PRSCR/UNSCR */
static OP sig_scadd(uint16 oper,t_bool addh, OP a, OP b)
{
OP r;
static const OP plus_half = { { 0040000, 0000000 } };   /* DEC +0.5 */

(void)fp_exec(oper,&r,a,b);                         /* calculate r := a +/- b */
if (addh) (void)fp_exec(044,&r,plus_half,NOP);      /* if addh set, multiply by 0.5 */
return r;
}

/* complex multiply helper */
static void sig_cmul(OP *r, OP *i, OP a, OP b, OP c, OP d)
{
OP p;
(void)fp_exec(040, &p , a, c);                      /* p := ac */
(void)fp_exec(040, ACCUM, b, d);                    /* ACCUM := bd */
(void)fp_exec(024, r, p , NOP);                     /* real := ac-bd */
(void)fp_exec(040, &p, a, d);                       /* p := ad */
(void)fp_exec(040, ACCUM, b, c);                    /* ACCUM := bc */
(void)fp_exec(004, i, p, NOP);                      /* imag := ad+bc */
}

t_stat cpu_signal (uint32 IR, uint32 intrq)
{
t_stat reason = SCPE_OK;
OPS op;
OP a,b,c,d,p1,p2,p3,p4,m1,m2,wr,wi;
uint32 entry, v, idx1, idx2;
int32 exc, exd;

t_bool debug = DEBUG_PRI (cpu_dev, DEB_SIG);

entry = IR & 017;                                  /* mask to entry point */

if (op_signal [entry] != OP_N) {
    reason = cpu_ops (op_signal [entry], op, intrq);    /* get instruction operands */
    if (reason != SCPE_OK)                              /* evaluation failed? */
        return reason;                                  /* return reason for failure */
    }

if (debug) {                                             /* debugging? */
    fprintf (sim_deb, ">>CPU SIG: IR = %06o (", IR);     /* print preamble and IR */
    fprint_sym (sim_deb, err_PC, (t_value *) &IR,        /* print instruction mnemonic */
                NULL, SWMASK('M'));
    fprintf (sim_deb, "), P = %06o", err_PC);            /* print location */
    fprint_ops (op_signal[entry], op);                   /* print operands */
    fputc ('\n', sim_deb);                               /* terminate line */
    }

switch (entry) {                                   /* decode IR<3:0> */
    case 000:                                      /* BITRV (OP_AAKK) */
        /* BITRV
         * bit reversal for FFT
         *   JSB BITRV
         *   DEF ret(,I)   return address
         *   DEF vect,I    base address of array
         *   DEF idx,I     index bitmap to be reversed (one-based)
         *   DEF nbits,I   number of bits of index
         *
         * Given a complex*8 vector of nbits (power of 2), this calculates:
         * swap( vect[idx], vect[rev(idx)]) where rev(i) is the bitreversed value of i */
        sig_bitrev(op[1].word, op[1].word+2, op[2].word-1, op[3].word, 4);
        PR = op[0].word & VAMASK;
        break;

    case 001:                                      /* BTRFY (OP_AAFFKK) */
        /* BTRFY - butterfly operation
         *   JSB BTRFY
         *   DEF ret(,I)   return address
         *   DEF vect(,I)  complex*8 vector
         *   DEF wr,I      real part of W
         *   DEF wi,I      imag part of W
         *   DEF node,I    index of 1st op (1 based)
         *   DEF lmax,I    offset to 2nd op (0 based) */
        sig_btrfy(op[1].word, op[1].word+2,
                  op[2], op[3],
                  2*(op[4].word-1), 2*op[5].word);
        PR = op[0].word & VAMASK;
        break;

    case 002:                                      /* UNSCR (OP_AAFFKK) */
        /* UNSCR unscramble for phasor MPY
         *   JSB UNSCR
         *   DEF ret(,I)
         *   DEF vector,I
         *   DEF WR
         *   DEF WI
         *   DEF idx1,I
         *   DEF idx2,I */
        v = op[1].word;
        idx1 = 2 * (op[4].word - 1);
        idx2 = 2 * (op[5].word - 1);
        wr = op[2];                               /* read WR */
        wi = op[3];                               /* read WI */
        p1 = ReadOp(RE(v + idx1), fp_f);          /* S1 VR[idx1] */
        p2 = ReadOp(RE(v + idx2), fp_f);          /* S2 VR[idx2] */
        p3 = ReadOp(IM(v + idx1), fp_f);          /* S9 VI[idx1] */
        p4 = ReadOp(IM(v + idx2), fp_f);          /* S10 VI[idx2] */
        c = sig_scadd(000, TRUE, p3, p4);         /* S5,6 0.5*(p3+p4) */
        d = sig_scadd(020, TRUE, p2, p1);         /* S7,8 0.5*(p2-p1) */
        sig_cmul(&m1, &m2, wr, wi, c, d);         /* (WR,WI) * (c,d) */
        c = sig_scadd(000, TRUE, p1, p2);         /* 0.5*(p1+p2) */
        d = sig_scadd(020, TRUE, p3, p4);         /* 0.5*(p3-p4) */
        (void)fp_exec(000, &p1, c, m1);           /* VR[idx1] := 0.5*(p1+p2) + real(W*(c,d)) */
        WriteOp(RE(v + idx1), p1, fp_f);
        (void)fp_exec(000, &p2, d, m2);           /* VI[idx1] := 0.5*(p3-p4) + imag(W*(c,d)) */
        WriteOp(IM(v + idx1), p2, fp_f);
        (void)fp_exec(020, &p1, c, m1);           /* VR[idx2] := 0.5*(p1+p2) - imag(W*(c,d)) */
        WriteOp(RE(v + idx2), p1, fp_f);
        (void)fp_exec(020, &p2, d, m2);           /* VI[idx2] := 0.5*(p3-p4) - imag(W*(c,d)) */
        WriteOp(IM(v + idx2), p2, fp_f);
        PR = op[0].word & VAMASK;
        break;

    case 003:                                      /* PRSCR (OP_AAFFKK) */
        /* PRSCR unscramble for phasor MPY
         *   JSB PRSCR
         *   DEF ret(,I)
         *   DEF vector,I
         *   DEF WR
         *   DEF WI
         *   DEF idx1,I
         *   DEF idx2,I */
        v = op[1].word;
        idx1 = 2 * (op[4].word - 1);
        idx2 = 2 * (op[5].word - 1);
        wr = op[2];                               /* read WR */
        wi = op[3];                               /* read WI */
        p1 = ReadOp(RE(v + idx1), fp_f);          /* VR[idx1] */
        p2 = ReadOp(RE(v + idx2), fp_f);          /* VR[idx2] */
        p3 = ReadOp(IM(v + idx1), fp_f);          /* VI[idx1] */
        p4 = ReadOp(IM(v + idx2), fp_f);          /* VI[idx2] */
        c = sig_scadd(020, FALSE, p1, p2);        /* p1-p2 */
        d = sig_scadd(000, FALSE, p3, p4);        /* p3+p4 */
        sig_cmul(&m1,&m2, wr, wi, c, d);          /* (WR,WI) * (c,d) */
        c = sig_scadd(000, FALSE, p1, p2);        /* p1+p2 */
        d = sig_scadd(020, FALSE, p3,p4);         /* p3-p4 */
        (void)fp_exec(020, &p1, c, m2);           /* VR[idx1] := (p1-p2) - imag(W*(c,d)) */
        WriteOp(RE(v + idx1), p1, fp_f);
        (void)fp_exec(000, &p2, d, m1);           /* VI[idx1] := (p3-p4) + real(W*(c,d)) */
        WriteOp(IM(v + idx1), p2, fp_f);
        (void)fp_exec(000, &p1, c, m2);           /* VR[idx2] := (p1+p2) + imag(W*(c,d)) */
        WriteOp(RE(v + idx2), p1, fp_f);
        (void)fp_exec(020, &p2, m1, d);           /* VI[idx2] := imag(W*(c,d)) - (p3-p4) */
        WriteOp(IM(v + idx2), p2, fp_f);
        PR = op[0].word & VAMASK;
        break;

    case 004:                                      /* BITR1 (OP_AAAKK) */
        /* BITR1
         * bit reversal for FFT, alternative version
         *   JSB BITR1
         *   DEF ret(,I)   return address if already swapped
         *   DEF revect,I  base address of real vect
         *   DEF imvect,I  base address of imag vect
         *   DEF idx,I     index bitmap to be reversed (one-based)
         *   DEF nbits,I   number of bits of index
         *
         * Given a complex*8 vector of nbits (power of 2), this calculates:
         * swap( vect[idx], vect[rev(idx)]) where rev(i) is the bitreversed value of i
         *
         * difference to BITRV is that BITRV uses complex*8, and BITR1 uses separate real*4
         * vectors for Real and Imag parts */
        sig_bitrev(op[1].word, op[2].word, op[3].word-1, op[4].word, 2);
        PR = op[0].word & VAMASK;
        break;


    case 005:                                      /* BTRF1 (OP_AAAFFKK) */
        /* BTRF1 - butterfly operation with real*4 vectors
         *   JSB BTRF1
         *   DEF ret(,I)   return address
         *   DEF rvect,I   real part of vector
         *   DEF ivect,I   imag part of vector
         *   DEF wr,I      real part of W
         *   DEF wi,I      imag part of W
         *   DEF node,I    index (1 based)
         *   DEF lmax,I    index (0 based) */
        sig_btrfy(op[1].word, op[2].word,
                  op[3], op[4],
                  op[5].word-1, op[6].word);
        PR = op[0].word & VAMASK;
        break;

    case 006:                                      /* .CADD (OP_AAA) */
        /* .CADD Complex addition
         *   JSB .CADD
         *   DEF result,I
         *   DEF oprd1,I
         *   DEF oprd2,I
         * complex addition is: (a+bi) + (c+di) => (a+c) + (b+d)i */
        sig_caddsub(000,op);
        break;

    case 007:                                      /* .CSUB (OP_AAA) */
        /* .CSUB Complex subtraction
         *   JSB .CSUB
         *   DEF result,I
         *   DEF oprd1,I
         *   DEF oprd2,I
         * complex subtraction is: (a+bi) - (c+di) => (a - c) + (b - d)i */
        sig_caddsub(020,op);
        break;

    case 010:                                      /* .CMUL (OP_AAA) */
        /* .CMPY Complex multiplication
         * call:
         *   JSB .CMPY
         *   DEF result,I
         *   DEF oprd1,I
         *   DEF oprd2,I
         * complex multiply is: (a+bi)*(c+di) => (ac-bd) + (ad+bc)i */
        a = ReadOp(RE(op[1].word), fp_f);          /* read 1st op */
        b = ReadOp(IM(op[1].word), fp_f);
        c = ReadOp(RE(op[2].word), fp_f);          /* read 2nd op */
        d = ReadOp(IM(op[2].word), fp_f);
        sig_cmul(&p1, &p2, a, b, c, d);
        WriteOp(RE(op[0].word), p1, fp_f);         /* write real result */
        WriteOp(IM(op[0].word), p2, fp_f);         /* write imag result */
        break;

    case 011:                                      /* .CDIV (OP_AAA) */
        /* .CDIV Complex division
         * call:
         *   JSB .CDIV
         *   DEF result,I
         *   DEF oprd1,I
         *   DEF oprd2,I
         * complex division is: (a+bi)/(c+di) => ((ac+bd) + (bc-ad)i)/(c^2+d^2) */
        a = ReadOp(RE(op[1].word), fp_f);          /* read 1st op */
        b = ReadOp(IM(op[1].word), fp_f);
        c = ReadOp(RE(op[2].word), fp_f);          /* read 2nd op */
        d = ReadOp(IM(op[2].word), fp_f);
        (void)fp_unpack (NULL, &exc, c, fp_f);     /* get exponents */
        (void)fp_unpack (NULL, &exd, d, fp_f);
        if (exc < exd) {                           /* ensure c/d < 1 */
            p1 = a; a = c; c = p1;                 /* swap dividend and divisor */
            p1 = b; b = d; d = p1;
        }
        (void)fp_exec(060, &p1, d, c);             /* p1,accu := d/c */
        (void)fp_exec(044, ACCUM, d, NOP);         /* ACCUM := dd/c */
        (void)fp_exec(004, &p2, c, NOP);           /* p2 := c + dd/c */
        (void)fp_exec(040, ACCUM, b, p1);          /* ACCUM := bd/c */
        (void)fp_exec(004, ACCUM, a, NOP);         /* ACCUM := a + bd/c */
        (void)fp_exec(070, &p3, NOP, p2);          /* p3 := (a+bd/c)/(c+dd/c) == (ac+bd)/(cc+dd) */
        WriteOp(RE(op[0].word), p3, fp_f);         /* Write real result */
        (void)fp_exec(040, ACCUM, a, p1);          /* ACCUM := ad/c */
        (void)fp_exec(030, ACCUM, NOP, b);         /* ACCUM := ad/c - b */
        if (exd < exc) {                           /* was not swapped? */
            (void)fp_exec(024, ACCUM, zero, NOP);  /* ACCUM := -ACCUM */
        }
        (void)fp_exec(070, &p3, NOP, p2);          /* p3 := (b-ad/c)/(c+dd/c) == (bc-ad)/cc+dd) */
        WriteOp(IM(op[0].word), p3, fp_f);         /* Write imag result */
        break;

    case 012:                                      /* CONJG (OP_AAA) */
        /* CONJG build A-Bi from A+Bi
         * call:
         *   JSB CONJG
         *   DEF RTN
         *   DEF res,I    result
         *   DEF arg,I    input argument */
        a = ReadOp(RE(op[2].word), fp_f);          /* read real */
        b = ReadOp(IM(op[2].word), fp_f);          /* read imag */
        (void)fp_pcom(&b, fp_f);                   /* negate imag */
        WriteOp(RE(op[1].word), a, fp_f);          /* write real */
        WriteOp(IM(op[1].word), b, fp_f);          /* write imag */
        break;

    case 013:                                      /* ..CCM (OP_A) */
        /* ..CCM complement complex
         * call
         *   JSB ..CCM
         *   DEF arg
         * build (-RE,-IM)
         */
        v = op[0].word;
        a = ReadOp(RE(v), fp_f);                   /* read real */
        b = ReadOp(IM(v), fp_f);                   /* read imag */
        (void)fp_pcom(&a, fp_f);                   /* negate real */
        (void)fp_pcom(&b, fp_f);                   /* negate imag */
        WriteOp(RE(v), a, fp_f);                   /* write real */
        WriteOp(IM(v), b, fp_f);                   /* write imag */
        break;

    case 014:                                       /* AIMAG (OP_AA) */
        /* AIMAG return the imaginary part in AB
         *   JSB AIMAG
         *   DEF *+2
         *   DEF cplx(,I)
         * returns: AB imaginary part of complex number */
        a = ReadOp(IM(op[1].word), fp_f);           /* read imag */
        AR = a.fpk[0];                              /* move MSB to A */
        BR = a.fpk[1];                              /* move LSB to B */
        break;

    case 015:                                       /* CMPLX (OP_AFF) */
        /* CMPLX form a complex number
         *   JSB CMPLX
         *   DEF *+4
         *   DEF result,I  complex number
         *   DEF repart,I  real value
         *   DEF impart,I  imaginary value */
        WriteOp(RE(op[1].word), op[2], fp_f);       /* write real part */
        WriteOp(IM(op[1].word), op[3], fp_f);       /* write imag part */
        break;

    case 017:                                       /* [slftst] (OP_N) */
        XR = 2;                                     /* firmware revision */
        SR = 0102077;                               /* test passed code */
        PR = (PR + 1) & VAMASK;                     /* P+2 return for firmware w/SIGNAL1000 */
        break;

    case 016:                                       /* invalid */
    default:                                        /* others undefined */
        reason = stop_inst;
        }

return reason;
}

#endif                                                  /* end of int64 support */
