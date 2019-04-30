/* vax_cpu1.c: VAX complex instructions

   Copyright (c) 1998-2017, Robert M Supnik

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

   13-Mar-17    RMS     Annotated fall through in switch
   14-Jul-16    RMS     Corrected REI rule 9
   21-Jun-16    RMS     Removed reserved check on SIRR (Mark Pizzolato)
   18-Feb-16    RMS     Changed variables in MxPR to unsigned
   29-Mar-15    RMS     Added model-specific IPR max
   15-Mar-12    RMS     Fixed potential integer overflow in LDPCTX (Mark Pizzolato)
   25-Nov-11    RMS     Added VEC_QBUS test in interrupt handler
   23-Mar-11    RMS     Revised idle design (Mark Pizzolato)
   28-May-08    RMS     Inlined physical memory routines
   29-Apr-07    RMS     Separated base register access checks for 11/780
   10-May-06    RMS     Added access check on system PTE for 11/780
                        Added mbz check in LDPCTX for 11/780
   22-Sep-06    RMS     Fixed declarations (Sterling Garwood)
   30-Sep-04    RMS     Added conditionals for full VAX
                        Moved emulation to vax_cis.c
                        Moved model-specific IPRs to system module
   27-Jan-04    RMS     Added device logging support
                        Fixed EXTxV, INSV double register PC reference fault
   30-Apr-02    RMS     Fixed interrupt/exception handler to clear traps
   17-Apr-02    RMS     Fixed pos > 31 test in bit fields (should be unsigned)
   14-Apr-02    RMS     Fixed prv_mode handling for interrupts (Tim Stark)
                        Fixed PROBEx to mask mode to 2b (Kevin Handy)

   This module contains the instruction simulators for

   Field instructions:
        - BBS, BBC, BBSSI, BBCCI
        - BBSC, BBCC, BBCS, BBSS
        - EXTV, EXTZV, CMPV, CMPZV
        - FFS, FFC, INSV

   Call/return and push/pop instructions:
        - CALLS, CALLG, RET
        - PUSHR, POPR

   Queue instructions:
        - INSQUE, REMQUE
        - INSQHI, INSQTI, REMQHI, REMQTI

   String instructions:
        - MOVC3, MOVC5, CMPC3, CMPC5
        - LOCC, SKPC, SCANC, SPANC

   Operating system interface instructions:
        - CHMK, CHME, CHMS, CHMU
        - PROBER, PROBEW, REI
        - MTPR, MFPR
        - LDPCTX, SVPCTX
        - (interrupt and exception routine) 
*/

#include "vax_defs.h"

static const uint8 rcnt[128] = {
 0, 4, 4, 8, 4, 8, 8,12, 4, 8, 8,12, 8,12,12,16,        /* 00 - 0F */
 4, 8, 8,12, 8,12,12,16, 8,12,12,16,12,16,16,20,        /* 10 - 1F */
 4, 8, 8,12, 8,12,12,16, 8,12,12,16,12,16,16,20,        /* 20 - 2F */
 8,12,12,16,12,16,16,20,12,16,16,20,16,20,20,24,        /* 30 - 3F */
 4, 8, 8,12, 8,12,12,16, 8,12,12,16,12,16,16,20,        /* 40 - 4F */
 8,12,12,16,12,16,16,20,12,16,16,20,16,20,20,24,        /* 50 - 5F */
 8,12,12,16,12,16,16,20,12,16,16,20,16,20,20,24,        /* 60 - 6F */
12,16,16,20,16,20,20,24,16,20,20,24,20,24,24,28         /* 70 - 7F */
};

extern int32 ReadIPR (int32 rg);
extern void WriteIPR (int32 rg, int32 val);
extern t_bool BadCmPSL (int32 newpsl);

/* Branch on bit and no modify
   Branch on bit and modify

        opnd[0] =       position (pos.rl)
        opnd[1] =       register number/memory flag
        opnd[2] =       memory address, if memory
   Returns bit to be tested
*/

int32 op_bb_n (int32 *opnd, int32 acc)
{
int32 pos = opnd[0];
int32 rn = opnd[1];
int32 ea;
int32 by;

if (rn != OP_MEM) {                                     /* register? */
    if (((uint32) pos) > 31)                            /* pos > 31? fault */
        RSVD_OPND_FAULT(op_bb_n);
    return (R[rn] >> pos) & 1;                          /* get bit */
    }
ea = opnd[2] + (pos >> 3);                              /* base byte addr */
pos = pos & 07;                                         /* pos in byte */
by = Read (ea, L_BYTE, RA);                             /* read byte */
return ((by >> pos) & 1);                               /* get bit */
}

int32 op_bb_x (int32 *opnd, int32 newb, int32 acc)
{
int32 pos = opnd[0];
int32 rn = opnd[1];
int32 ea;
int32 by, bit;

if (rn != OP_MEM) {                                     /* register? */
    if (((uint32) pos) > 31)                            /* pos > 31? fault */
        RSVD_OPND_FAULT(op_bb_x);
    bit = (R[rn] >> pos) & 1;                           /* get bit */
    R[rn] = newb? (R[rn] | (1u << pos)): (R[rn] & ~(1u << pos));
    return bit;
    }
ea = opnd[2] + (pos >> 3);                              /* base byte addr */
pos = pos & 07;                                         /* pos in byte */
by = Read (ea, L_BYTE, WA);                             /* read byte */
bit = (by >> pos) & 1;                                  /* get bit */
by = newb? (by | (1u << pos)): (by & ~(1u << pos));     /* change bit */
Write (ea, by, L_BYTE, WA);                             /* rewrite byte */
return bit;
}

/* Extract field

        opnd[0] =       position (pos.rl)
        opnd[1] =       size (size.rb)
        opnd[2] =       register number/memory flag
        opnd[3] =       register content/memory address

   If the field is in a register, rn + 1 is in vfldrp1
*/

int32 op_extv (int32 *opnd, int32 vfldrp1, int32 acc)
{
int32 pos = opnd[0];
int32 size = opnd[1];
int32 rn = opnd[2];
uint32 wd = opnd[3];
int32 ba, wd1 = 0;

if (size == 0)                                          /* size 0? field = 0 */
    return 0;
if (size > 32)                                          /* size > 32? fault */
    RSVD_OPND_FAULT(op_extv);
if (rn != OP_MEM) {                                     /* register? */
    if (((uint32) pos) > 31)                            /* pos > 31? fault */
        RSVD_OPND_FAULT(op_extv);
    if (((pos + size) > 32) && (rn >= nSP))             /* span 2 reg, PC? */
        RSVD_ADDR_FAULT;                                /* fault */
    if (pos)
        wd = (wd >> pos) | (((uint32) vfldrp1) << (32 - pos));
    }
else {
    ba = wd + (pos >> 3);                               /* base byte addr */
    pos = (pos & 07) | ((ba & 03) << 3);                /* bit offset */
    ba = ba & ~03;                                      /* lw align base */
    wd = Read (ba, L_LONG, RA);                         /* read field */
    if ((size + pos) > 32)
        wd1 = Read (ba + 4, L_LONG, RA);
    if (pos)
        wd = (wd >> pos) | (((uint32) wd1) << (32 - pos));
    }
return wd & byte_mask[size];
}

/* Insert field

        opnd[0] =       field (src.rl)
        opnd[1] =       position (pos.rl)
        opnd[2] =       size (size.rb)
        opnd[3] =       register number/memory flag
        opnd[4] =       register content/memory address

   If the field is in a register, rn + 1 is in vfldrp1
*/

void op_insv (int32 *opnd, int32 vfldrp1, int32 acc)
{
uint32 ins = opnd[0];
int32 pos = opnd[1];
int32 size = opnd[2];
int32 rn = opnd[3];
int32 val, mask, ba, wd, wd1;

if (size == 0)                                          /* size = 0? done */
    return;
if (size > 32)                                          /* size > 32? fault */
    RSVD_OPND_FAULT(op_insv);
if (rn != OP_MEM) {                                     /* in registers? */
    if (((uint32) pos) > 31)                            /* pos > 31? fault */
        RSVD_OPND_FAULT(op_insv);
    if ((pos + size) > 32) {                            /* span two reg? */
        if (rn >= nSP)                                  /* if PC, fault */
            RSVD_ADDR_FAULT;
        mask = byte_mask[pos + size - 32];              /* insert fragment */
        val = ins >> (32 - pos);
        R[rn + 1] = (vfldrp1 & ~mask) | (val & mask);
        }
    mask = byte_mask[size] << pos;                      /* insert field */
    val = ins << pos;
    R[rn] = (R[rn] & ~mask) | (val & mask);
    }
else {
    ba = opnd[4] + (pos >> 3);                          /* base byte addr */
    pos = (pos & 07) | ((ba & 03) << 3);                /* bit offset */
    ba = ba & ~03;                                      /* lw align base */
    wd = Read (ba, L_LONG, WA);                         /* read field */
    if ((size + pos) > 32) {                            /* field span lw? */
        wd1 = Read (ba + 4, L_LONG, WA);                /* read 2nd lw */
        mask = byte_mask[pos + size - 32];              /* insert fragment */
        val = ins >> (32 - pos);
        Write (ba + 4, (wd1 & ~mask) | (val & mask), L_LONG, WA);
        }
    mask = byte_mask[size] << pos;                      /* insert field */
    val = ins << pos;
    Write (ba, (wd & ~mask) | (val & mask), L_LONG, WA);
    }
return;
}

/* Find first */

int32 op_ffs (uint32 wd, int32 size)
{
int32 i;

for (i = 0; wd; i++, wd = wd >> 1) {
    if (wd & 1)
        return i;
    }
return size;
}

#define CALL_DV         0x8000                          /* DV set */
#define CALL_IV         0x4000                          /* IV set */
#define CALL_MBZ        0x3000                          /* MBZ */
#define CALL_MASK       0x0FFF                          /* mask */
#define CALL_V_SPA      30                              /* SPA */
#define CALL_M_SPA      03
#define CALL_V_S        29                              /* S flag */
#define CALL_S          (1 << CALL_V_S)
#define CALL_V_MASK     16
#define CALL_PUSH(n)    if ((mask >> (n)) & 1) { \
                            tsp = tsp - 4; \
                            Write (tsp, R[n], L_LONG, WA); \
                            }
#define CALL_GETSPA(x)  (((x) >> CALL_V_SPA) & CALL_M_SPA)
#define RET_POP(n)      if ((spamask >> (n + CALL_V_MASK)) & 1) { \
                            R[n] = Read (tsp, L_LONG, RA); \
                            tsp = tsp + 4; \
                            }
#define PUSHR_PUSH(n)   CALL_PUSH(n)
#define POPR_POP(n)     if ((mask >> (n)) & 1) { \
                            R[n] = Read (SP, L_LONG, RA); \
                            SP = SP + 4; \
                            }

/* CALLG, CALLS

        opnd[0]         =       argument (arg.rx)
        opnd[1]         =       procedure address (adr.ab)
        flg             =       CALLG (0), CALLS (1)    
        acc             =       access mask

        These instructions implement a generalized procedure call and return facility.
        The principal data structure involved is the stack frame.
        CALLS and CALLG build a stack frame in the following format:


        +---------------------------------------------------------------+
        |                 condition handler (initially 0)               |
        +---+-+-+-----------------------+--------------------+----------+       
        |SPA|S|0|   entry mask<11:0>    |   saved PSW<15:5>  | 0 0 0 0 0|
        +---+-+-+-----------------------+--------------------+----------+
        |                           saved AP                            |
        +---------------------------------------------------------------+
        |                           saved FP                            |
        +---------------------------------------------------------------+
        |                           saved PC                            |
        +---------------------------------------------------------------+
        |                           saved R0 (...)                      |
        +---------------------------------------------------------------+
                .                                               .       
                .       (according to entry mask<11:0>)         .       
                .                                               .       
        +---------------------------------------------------------------+
        |                           saved R11 (...)                     |
        +---------------+-----------------------------------------------+
        | #args (CALLS) |       (0-3 bytes needed to align stack)       |
        +---------------+-----------------------------------------------+
        |               |       0       0       0       (CALLS)         |
        +---------------+-----------------------------------------------+

        RET expects to find this structure based at the frame pointer (FP).

        For CALLG and CALLS, the entry mask specifies the new settings of
        DV and IV, and also which registers are to be saved on entry:

         15 14 13 12 11                               0
        +--+--+-----+----------------------------------+
        |DV|IV| MBZ |           register mask          |
        +--+--+-----+----------------------------------+

        CALLG/CALLS operation:

                read the procedure entry mask
                make sure that the stack frame will be accessible
                if CALLS, push the number of arguments onto the stack
                align the stack to the next lower longword boundary
                push the registers specified by the procedure entry mask
                push PC, AP, FP, saved SPA/S0/mask/PSW, condition handler
                update PC, SP, FP, AP
                update PSW traps, clear condition codes
*/

int32 op_call (int32 *opnd, t_bool gs, int32 acc)
{
int32 addr = opnd[1];
int32 mask, stklen, tsp, wd;

mask = Read (addr, L_WORD, RA);                         /* get proc mask */
if (mask & CALL_MBZ)                                    /* test mbz */
    RSVD_OPND_FAULT(op_call);
stklen = rcnt[mask & 077] + rcnt[(mask >> 6) & 077] + (gs? 24: 20);
Read (SP - stklen, L_BYTE, WA);                         /* wchk stk */
if (gs) {
    Write (SP - 4, opnd[0], L_LONG, WA);                /* if S, push #arg */
    SP = SP - 4;                                        /* stack is valid */
    }
tsp = SP & ~CALL_M_SPA;                                 /* lw align stack */
CALL_PUSH (11);                                         /* check mask bits, */
CALL_PUSH (10);                                         /* push sel reg */
CALL_PUSH (9);
CALL_PUSH (8);
CALL_PUSH (7);
CALL_PUSH (6);
CALL_PUSH (5);
CALL_PUSH (4);
CALL_PUSH (3);
CALL_PUSH (2);
CALL_PUSH (1);
CALL_PUSH (0);
Write (tsp - 4, PC, L_LONG, WA);                        /* push PC */
Write (tsp - 8, FP, L_LONG, WA);                        /* push AP */
Write (tsp - 12, AP, L_LONG, WA);                       /* push FP */
wd = ((SP & CALL_M_SPA) << CALL_V_SPA) | (gs << CALL_V_S) |
    ((mask & CALL_MASK) << CALL_V_MASK) | (PSL & 0xFFE0);
Write (tsp - 16, wd, L_LONG, WA);                       /* push spa/s/mask/psw */
Write (tsp - 20, 0, L_LONG, WA);                        /* push cond hdlr */
if (gs)                                                 /* update AP */
    AP = SP;
else AP = opnd[0];
SP = FP = tsp - 20;                                     /* update FP, SP */
PSL = (PSL & ~(PSW_DV | PSW_FU | PSW_IV)) |             /* update PSW */
    ((mask & CALL_DV)? PSW_DV: 0) |
    ((mask & CALL_IV)? PSW_IV: 0);
JUMP (addr + 2);                                        /* new PC */
return 0;                                               /* new cc's */
}

int32 op_ret (int32 acc)
{
int32 spamask, stklen, newpc, nargs;
int32 tsp = FP;

spamask = Read (tsp + 4, L_LONG, RA);                   /* spa/s/mask/psw */
if (spamask & PSW_MBZ)                                  /* test mbz */
    RSVD_OPND_FAULT(op_ret);
stklen = rcnt[(spamask >> CALL_V_MASK) & 077] +
    rcnt[(spamask >> (CALL_V_MASK + 6)) & 077] + ((spamask & CALL_S)? 23: 19);
Read (tsp + stklen, L_BYTE, RA);                        /* rchk stk end */
AP = Read (tsp + 8, L_LONG, RA);                        /* restore AP */
FP = Read (tsp + 12, L_LONG, RA);                       /* restore FP */
newpc = Read (tsp + 16, L_LONG, RA);                    /* get new PC */
tsp = tsp + 20;                                         /* update stk ptr */
RET_POP (0);                                            /* chk mask bits, */
RET_POP (1);                                            /* pop sel regs */
RET_POP (2);
RET_POP (3);
RET_POP (4);
RET_POP (5);
RET_POP (6);
RET_POP (7);
RET_POP (8);
RET_POP (9);
RET_POP (10);
RET_POP (11);
SP = tsp + CALL_GETSPA (spamask);                       /* dealign stack */
if (spamask & CALL_S) {                                 /* CALLS? */
    nargs = Read (SP, L_LONG, RA);                      /* read #args */
    SP = SP + 4 + ((nargs & BMASK) << 2);               /* pop arg list */
    }
PSL = (PSL & ~(PSW_DV | PSW_FU | PSW_IV | PSW_T)) |     /* reset PSW */
    (spamask & (PSW_DV | PSW_FU | PSW_IV | PSW_T));
JUMP_ALWAYS(newpc);                                     /* set new PC */
return spamask & (CC_MASK);                             /* return cc's */
}

/* PUSHR and POPR */

void op_pushr (int32 *opnd, int32 acc)
{
int32 mask = opnd[0] & 0x7FFF;
int32 stklen, tsp;

if (mask == 0)
    return;
stklen = rcnt[(mask >> 7) & 0177] + rcnt[mask & 0177] +
    ((mask & 0x4000)? 4: 0);
Read (SP - stklen, L_BYTE, WA);                         /* wchk stk end */
tsp = SP;                                               /* temp stk ptr */
PUSHR_PUSH (14);                                        /* check mask bits, */
PUSHR_PUSH (13);                                        /* push sel reg */
PUSHR_PUSH (12);
PUSHR_PUSH (11);
PUSHR_PUSH (10);
PUSHR_PUSH (9);
PUSHR_PUSH (8);
PUSHR_PUSH (7);
PUSHR_PUSH (6);
PUSHR_PUSH (5);
PUSHR_PUSH (4);
PUSHR_PUSH (3);
PUSHR_PUSH (2);
PUSHR_PUSH (1);
PUSHR_PUSH (0);
SP = tsp;                                               /* update stk ptr */
return;
}

void op_popr (int32 *opnd, int32 acc)
{
int32 mask = opnd[0] & 0x7FFF;
int32 stklen;

if (mask == 0)
    return;
stklen = rcnt[(mask >> 7) & 0177] + rcnt[mask & 0177] +
    ((mask & 0x4000)? 4: 0);
Read (SP + stklen - 1, L_BYTE, RA);                     /* rchk stk end */
POPR_POP (0);                                           /* check mask bits, */
POPR_POP (1);                                           /* pop sel regs */
POPR_POP (2);
POPR_POP (3);
POPR_POP (4);
POPR_POP (5);
POPR_POP (6);
POPR_POP (7);
POPR_POP (8);
POPR_POP (9);
POPR_POP (10);
POPR_POP (11);
POPR_POP (12);
POPR_POP (13);
if (mask & 0x4000)                                      /* if pop SP, no inc */
    SP = Read (SP, L_LONG, RA);
return;
}

/* INSQUE

        opnd[0] =       entry address (ent.ab)
        opnd[1] =       predecessor address (pred.ab)

   Condition codes returned to caller on comparison of (ent):(ent+4).
   All writes must be checked before any writes are done.

   Pictorially:

        BEFORE                  AFTER

        P:      S               P:      E       W
        P+4:    (n/a)           P+4:    (n/a)

        E:      ---             E:      S       W
        E+4:    ---             E+4:    P       W

        S:      (n/a)           S:      (n/a)
        S+4:    P               S+4:    E       W

   s+4 must be tested with a read modify rather than a probe, as it
   might be misaligned.
*/

int32 op_insque (int32 *opnd, int32 acc)
{
int32 p = opnd[1];
int32 e = opnd[0];
int32 s, cc;

s = Read (p, L_LONG, WA);                               /* s <- (p), wchk */
Read (s + 4, L_LONG, WA);                               /* wchk s+4 */
Read (e + 4, L_LONG, WA);                               /* wchk e+4 */
Write (e, s, L_LONG, WA);                               /* (e) <- s */
Write (e + 4, p, L_LONG, WA);                           /* (e+4) <- p */
Write (s + 4, e, L_LONG, WA);                           /* (s+4) <- ent */
Write (p, e, L_LONG, WA);                               /* (p) <- e */
CC_CMP_L (s, p);                                        /* set cc's */
return cc;
}

/* REMQUE

        opnd[0] =       entry address (ent.ab)
        opnd[1:2] =     destination address (dst.wl)

   Condition codes returned to caller based on (ent):(ent+4).
   All writes must be checked before any writes are done.

   Pictorially:

        BEFORE                  AFTER

        P:      E               P:      S       W
        P+4:    (n/a)           P+4:    (n/a)

        E:      S       W       E:      S
        E+4:    P       W       E+4:    P

        S:      (n/a)           S:      (n/a)
        S+4:    E       W       S+4:    P

*/

int32 op_remque (int32 *opnd, int32 acc)
{
int32 e = opnd[0];
int32 s, p, cc;

s = Read (e, L_LONG, RA);                               /* s <- (e) */
p = Read (e + 4, L_LONG, RA);                           /* p <- (e+4) */
CC_CMP_L (s, p);                                        /* set cc's */
if (e != p) {                                           /* queue !empty? */
    Read (s + 4, L_LONG, WA);                           /* wchk (s+4) */
    if (opnd[1] == OP_MEM)                              /* wchk dest */
        Read (opnd[2], L_LONG, WA);
    Write (p, s, L_LONG, WA);                           /* (p) <- s */
    Write (s + 4, p, L_LONG, WA);                       /* (s+4) <- p */
    }
else cc = cc | CC_V;                                    /* else set v */
if (opnd[1] != OP_MEM)                                  /* store result */
    R[opnd[1]] = e;
else Write (opnd[2], e, L_LONG, WA);
return cc;
}

/* Interlocked insert instructions

        opnd[0] =       entry (ent.ab)
        opnd[1] =       header (hdr.aq)

        Pictorially:

        BEFORE          AFTER INSQHI            AFTER INSQTI

        H:      A-H     H:      D-H     W       H:      A-H     W for interlock
        H+4:    C-H     H+4:    C-H             H+4:    D-H     W

        A:      B-A     A:      B-A             A:      B-A
        A+4:    H-A     A+4:    D-A     W       A+4:    H-A

        B:      C-B     B:      C-B             B:      C-B
        B+4:    A-B     B+4:    A-B             B+4:    A-B

        C:      H-C     C:      H-C             C:      D-C     W
        C+4:    B-C     C+4:    B-C             C+4:    B-C

        D:      ---     D:      A-D     W       D:      H-D     W
        D+4:    ---     D+4:    H-D     W       D+4:    C-D     W

        Note that the queue header, the entry to be inserted, and all
        the intermediate entries that are "touched" in any way must be
        QUADWORD aligned.  In addition, the header and  the entry must
        not be equal.
*/

int32 op_insqhi (int32 *opnd, int32 acc)
{
int32 h = opnd[1];
int32 d = opnd[0];
int32 a;
int32 t;

if ((h == d) || ((h | d) & 07))                         /* h, d quad align? */
    RSVD_OPND_FAULT(op_insqhi);
Read (d, L_BYTE, WA);                                   /* wchk ent */
a = Read (h, L_LONG, WA);                               /* a <- (h), wchk */
if (a & 06)                                             /* chk quad align */
    RSVD_OPND_FAULT(op_insqhi);
if (a & 01)                                             /* busy, cc = 0001 */
    return CC_C;
Write (h, a | 1, L_LONG, WA);                           /* get interlock */
a = a + h;                                              /* abs addr of a */
if (Test (a, WA, &t) < 0)                               /* wtst a, rls if err */
    Write (h, a - h, L_LONG, WA);
Write (a + 4, d - a, L_LONG, WA);                       /* (a+4) <- d-a, flt ok */
Write (d, a - d, L_LONG, WA);                           /* (d) <- a-d */
Write (d + 4, h - d, L_LONG, WA);                       /* (d+4) <- h-d */
Write (h, d - h, L_LONG, WA);                           /* (h) <- d-h, rls int */
return (a == h)? CC_Z: 0;                               /* Z = 1 if a = h */
}

int32 op_insqti (int32 *opnd, int32 acc)
{
int32 h = opnd[1];
int32 d = opnd[0];
int32 a, c;
int32 t;

if ((h == d) || ((h | d) & 07))                         /* h, d quad align? */
    RSVD_OPND_FAULT(op_insqti);
Read (d, L_BYTE, WA);                                   /* wchk ent */
a = Read (h, L_LONG, WA);                               /* a <- (h), wchk */
if (a == 0)                                             /* if empty, ins hd */
    return op_insqhi (opnd, acc);
if (a & 06)                                             /* chk quad align */
    RSVD_OPND_FAULT(op_insqti);
if (a & 01)                                             /* busy, cc = 0001 */
    return CC_C;
Write (h, a | 1, L_LONG, WA);                           /* acquire interlock */
c = Read (h + 4, L_LONG, RA) + h;                       /* c <- (h+4) + h */
if (c & 07) {                                           /* c quad aligned? */
    Write (h, a, L_LONG, WA);                           /* release interlock */
    RSVD_OPND_FAULT(op_insqti);                         /* fault */
    }
if (Test (c, WA, &t) < 0)                               /* wtst c, rls if err */
    Write (h, a, L_LONG, WA);
Write (c, d - c, L_LONG, WA);                           /* (c) <- d-c, flt ok */
Write (d, h - d, L_LONG, WA);                           /* (d) <- h-d */
Write (d + 4, c - d, L_LONG, WA);                       /* (d+4) <- c-d */
Write (h + 4, d - h, L_LONG, WA);                       /* (h+4) <- d-h */
Write (h, a, L_LONG, WA);                               /* release interlock */
return 0;                                               /* q >= 2 entries */
}

/* Interlocked remove instructions

        opnd[0] =       header (hdr.aq)
        opnd[1:2] =     destination address (dst.al)

        Pictorially:

        BEFORE          AFTER REMQHI            AFTER REMQTI

        H:      A-H     H:      B-H     W       H:      A-H     W for interlock
        H+4:    C-H     H+4:    C-H             H+4:    B-H     W

        A:      B-A     A:      B-A     R       A:      B-A
        A+4:    H-A     A+4:    H-A             A+4:    H-A

        B:      C-B     B:      C-B             B:      H-B     W
        B+4:    A-B     B+4:    H-B     W       B+4:    A-B

        C:      H-C     C:      H-C             C:      H-C
        C+4:    B-C     C+4:    B-C             C+4:    B-C     R

        Note that the queue header and all the  entries that are
        "touched" in any way must be QUADWORD aligned.  In addition,
        the header and the destination must not be equal.
*/

int32 op_remqhi (int32 *opnd, int32 acc)
{
int32 h = opnd[0];
int32 ar, a, b;
int32 t;

if (h & 07)                                             /* h quad aligned? */
    RSVD_OPND_FAULT(op_remqhi);
if (opnd[1] == OP_MEM) {                                /* mem destination? */
    if (h == opnd[2])                                   /* hdr = dst? */
        RSVD_OPND_FAULT(op_remqhi);
    Read (opnd[2], L_LONG, WA);                         /* wchk dst */
    }
ar = Read (h, L_LONG, WA);                              /* ar <- (h) */
if (ar & 06)                                            /* a quad aligned? */
    RSVD_OPND_FAULT(op_remqhi);
if (ar & 01)                                            /* busy, cc = 0011 */
    return CC_V | CC_C;
a = ar + h;                                             /* abs addr of a */
if (ar) {                                               /* queue not empty? */
    Write (h, ar | 1, L_LONG, WA);                      /* acquire interlock */
    if (Test (a, RA, &t) < 0)                           /* read tst a */
         Write (h, ar, L_LONG, WA);                     /* release if error */
    b = Read (a, L_LONG, RA) + a;                       /* b <- (a)+a, flt ok */
    if (b & 07) {                                       /* b quad aligned? */
        Write (h, ar, L_LONG, WA);                      /* release interlock */
        RSVD_OPND_FAULT(op_remqhi);                                /* fault */
        }
    if (Test (b, WA, &t) < 0)                           /* write test b */
        Write (h, ar, L_LONG, WA);                      /* release if err */
    Write (b + 4, h - b, L_LONG, WA);                   /* (b+4) <- h-b, flt ok */
    Write (h, b - h, L_LONG, WA);                       /* (h) <- b-h, rls int */
    }
if (opnd[1] != OP_MEM)                                  /* store result */
    R[opnd[1]] = a;
else Write (opnd[2], a, L_LONG, WA);
if (ar == 0)                                            /* empty, cc = 0110 */
    return CC_Z | CC_V;
return (b == h)? CC_Z: 0;                               /* if b = h, q empty */
}

int32 op_remqti (int32 *opnd, int32 acc)
{
int32 h = opnd[0];
int32 ar, b, c;
int32 t;

if (h & 07)                                             /* h quad aligned? */
    RSVD_OPND_FAULT(op_remqti);
if (opnd[1] == OP_MEM) {                                /* mem destination? */
    if (h == opnd[2])                                   /* hdr = dst? */
        RSVD_OPND_FAULT(op_remqti);
    Read (opnd[2], L_LONG, WA);                         /* wchk dst */
    }
ar = Read (h, L_LONG, WA);                              /* a <- (h) */
if (ar & 06)                                            /* a quad aligned? */
    RSVD_OPND_FAULT(op_remqti);
if (ar & 01)                                            /* busy, cc = 0011 */
    return CC_V | CC_C;
if (ar) {                                               /* queue not empty */
    Write (h, ar | 1, L_LONG, WA);                      /* acquire interlock */
    c = Read (h + 4, L_LONG, RA);                       /* c <- (h+4) */
    if (ar == c) {                                      /* single entry? */
        Write (h, ar, L_LONG, WA);                      /* release interlock */
        return op_remqhi (opnd, acc);                   /* treat as remqhi */
        }
    if (c & 07) {                                       /* c quad aligned? */
        Write (h, ar, L_LONG, WA);                      /* release interlock */
        RSVD_OPND_FAULT(op_remqti);                                /* fault */
        }
    c = c + h;                                          /* abs addr of c */
    if (Test (c + 4, RA, &t) < 0)                       /* read test c+4 */
        Write (h, ar, L_LONG, WA);                      /* release if error */
    b = Read (c + 4, L_LONG, RA) + c;                   /* b <- (c+4)+c, flt ok */
    if (b & 07) {                                       /* b quad aligned? */
        Write (h, ar, L_LONG, WA);                      /* release interlock */
        RSVD_OPND_FAULT(op_remqti);                                /* fault */
        }
    if (Test (b, WA, &t) < 0)                           /* write test b */
        Write (h, ar, L_LONG, WA);                      /* release if error */
    Write (b, h - b, L_LONG, WA);                       /* (b) <- h-b */
    Write (h + 4, b - h, L_LONG, WA);                   /* (h+4) <- b-h */
    Write (h, ar, L_LONG, WA);                          /* release interlock */
    }
else c = h;                                             /* empty, result = h */
if (opnd[1] != OP_MEM)                                  /* store result */
    R[opnd[1]] = c;
else Write (opnd[2], c, L_LONG, WA);
if (ar == 0)                                            /* empty, cc = 0110 */
    return CC_Z | CC_V;
return 0;                                               /* q can't be empty */
}

/* String instructions */

#define MVC_FRWD        0                               /* movc state codes */
#define MVC_BACK        1
#define MVC_FILL        3                               /* must be 3 */
#define MVC_M_STATE     3
#define MVC_V_CC        2

/* MOVC3, MOVC5

   if PSL<fpd> = 0 and MOVC3,
        opnd[0] =       length
        opnd[1] =       source address
        opnd[2] =       dest address

   if PSL<fpd> = 0 and MOVC5,
        opnd[0] =       source length
        opnd[1] =       source address
        opnd[2] =       fill
        opnd[3] =       dest length
        opnd[4] =       dest address

   if PSL<fpd> = 1,
        R0      =       delta-PC/fill/initial move length
        R1      =       current source address
        R2      =       current move length
        R3      =       current dest address
        R4      =       dstlen - srclen (loop count if fill state)
        R5      =       cc/state
*/

int32 op_movc (int32 *opnd, int32 movc5, int32 acc)
{
int32 i, cc, fill, wd;
int32 j, lnt, mlnt[3];
static const int32 looplnt[3] = { L_BYTE, L_LONG, L_BYTE };

if (PSL & PSL_FPD) {                                    /* FPD set? */
    SETPC (fault_PC + STR_GETDPC (R[0]));               /* reset PC */
    fill = STR_GETCHR (R[0]);                           /* get fill */
    R[2] = R[2] & STR_LNMASK;                           /* mask lengths */
    if (R[4] > 0)
        R[4] = R[4] & STR_LNMASK;
    }
else {
    R[1] = opnd[1];                                     /* src addr */
    if (movc5) {                                        /* MOVC5? */
        R[2] = (opnd[0] < opnd[3])? opnd[0]: opnd[3];
        R[3] = opnd[4];                                 /* dst addr */
        R[4] = opnd[3] - opnd[0];                       /* dstlen - srclen */
        fill = opnd[2];                                 /* set fill */
        CC_CMP_W (opnd[0], opnd[3]);                    /* set cc's */
        }
    else {
        R[2] = opnd[0];                                 /* mvlen = srclen */
        R[3] = opnd[2];                                 /* dst addr */
        R[4] = fill = 0;                                /* no fill */
        cc = CC_Z;                                      /* set cc's */
        }
    R[0] = STR_PACK (fill, R[2]);                       /* initial mvlen */
    if (R[2]) {                                         /* any move? */
        if (((uint32) R[1]) < ((uint32) R[3])) {
            R[1] = R[1] + R[2];                         /* backward, adjust */
            R[3] = R[3] + R[2];                         /* addr to end */
            R[5] = MVC_BACK;                            /* set state */
            }
        else R[5] = MVC_FRWD;                           /* fwd, set state */
        }
    else R[5] = MVC_FILL;                               /* fill, set state */
    R[5] = R[5] | (cc << MVC_V_CC);                     /* pack with state */
    PSL = PSL | PSL_FPD;                                /* set FPD */
    }

/* At this point,

        R0      =       delta PC'fill'initial move length
        R1      =       current src addr
        R2      =       current move length
        R3      =       current dst addr
        R4      =       dst length - src length
        R5      =       cc'state
*/

switch (R[5] & MVC_M_STATE) {                           /* case on state */

    case MVC_FRWD:                                      /* move forward */
        mlnt[0] = (4 - R[3]) & 3;                       /* length to align */
        if (mlnt[0] > R[2])                             /* cant exceed total */
            mlnt[0] = R[2];
        mlnt[1] = (R[2] - mlnt[0]) & ~03;               /* aligned length */
        mlnt[2] = R[2] - mlnt[0] - mlnt[1];             /* tail */
        for (i = 0; i < 3; i++) {                       /* head, align, tail */
            lnt = looplnt[i];                           /* length for loop */
            for (j = 0; j < mlnt[i]; j = j + lnt, extra_bytes++) {
                wd = Read (R[1], lnt, RA);              /* read src */
                Write (R[3], wd, lnt, WA);              /* write dst */
                R[1] = R[1] + lnt;                      /* inc src addr */
                R[3] = R[3] + lnt;                      /* inc dst addr */
                R[2] = R[2] - lnt;                      /* dec move lnt */
                }
            }
        goto FILL;                                      /* check for fill */

    case MVC_BACK:                                      /* move backward */
        mlnt[0] = R[3] & 03;                            /* length to align */
        if (mlnt[0] > R[2])                             /* cant exceed total */
            mlnt[0] = R[2];
        mlnt[1] = (R[2] - mlnt[0]) & ~03;               /* aligned length */
        mlnt[2] = R[2] - mlnt[0] - mlnt[1];             /* tail */
        for (i = 0; i < 3; i++) {                       /* head, align, tail */
            lnt = looplnt[i];                           /* length for loop */
            for (j = 0; j < mlnt[i]; j = j + lnt, extra_bytes++) {
                wd = Read (R[1] - lnt, lnt, RA);        /* read src */
                Write (R[3] - lnt, wd, lnt, WA);        /* write dst */
                R[1] = R[1] - lnt;                      /* dec src addr */
                R[3] = R[3] - lnt;                      /* dec dst addr */
                R[2] = R[2] - lnt;                      /* dec move lnt */
                }
            }
        R[1] = R[1] + (R[0] & STR_LNMASK);              /* final src addr */
        R[3] = R[3] + (R[0] & STR_LNMASK);              /* final dst addr */

    case MVC_FILL:                                      /* fill */
    FILL:
        if (R[4] <= 0)                                  /* any fill? */
            break;
        R[5] = R[5] | MVC_FILL;                         /* set state */
        mlnt[0] = (4 - R[3]) & 3;                       /* length to align */
        if (mlnt[0] > R[4])                             /* cant exceed total */
            mlnt[0] = R[4];
        mlnt[1] = (R[4] - mlnt[0]) & ~03;               /* aligned length */
        mlnt[2] = R[4] - mlnt[0] - mlnt[1];             /* tail */
        for (i = 0; i < 3; i++) {                       /* head, align, tail */
            lnt = looplnt[i];                           /* length for loop */
            fill = fill & BMASK;                        /* fill for loop */
            if (lnt == L_LONG)
                fill = (((uint32) fill) << 24) | (fill << 16) | (fill << 8) | fill;
            for (j = 0; j < mlnt[i]; j = j + lnt, extra_bytes++) {
                Write (R[3], fill, lnt, WA);            /* write fill */
                R[3] = R[3] + lnt;                      /* inc dst addr */
                R[4] = R[4] - lnt;                      /* dec fill lnt */
                }
            }
        break;

    default:                                            /* bad state */
        RSVD_OPND_FAULT(op_movc);                       /* you lose */
        }

PSL = PSL & ~PSL_FPD;                                   /* clear FPD */
cc = (R[5] >> MVC_V_CC) & CC_MASK;                      /* get cc's */
R[0] = NEG (R[4]);                                      /* set R0 */
R[2] = R[4] = R[5] = 0;                                 /* clear reg */
return cc;
}

/* CMPC3, CMPC5

   if PSL<fpd> = 0 and CMPC3,
        opnd[0] =       length
        opnd[1] =       source1 address
        opnd[2] =       source2 address

   if PSL<fpd> = 0 and CMPC5,
        opnd[0] =       source1 length
        opnd[1] =       source1 address
        opnd[2] =       fill
        opnd[3] =       source2 length
        opnd[4] =       source2 address

   if PSL<fpd> = 1,
        R0      =       delta-PC/fill/source1 length
        R1      =       source1 address
        R2      =       source2 length
        R3      =       source2 address
*/

int32 op_cmpc (int32 *opnd, int32 cmpc5, int32 acc)
{
int32 cc, s1, s2, fill;

if (PSL & PSL_FPD) {                                    /* FPD set? */
    SETPC (fault_PC + STR_GETDPC (R[0]));               /* reset PC */
    fill = STR_GETCHR (R[0]);                           /* get fill */
    }
else {
    R[1] = opnd[1];                                     /* src1len */
    if (cmpc5) {                                        /* CMPC5? */
        R[2] = opnd[3];                                 /* get src2 opnds */
        R[3] = opnd[4];
        fill = opnd[2];
        }
    else {
        R[2] = opnd[0];                                 /* src2len = src1len */
        R[3] = opnd[2];
        fill = 0;
        }
    R[0] = STR_PACK (fill, opnd[0]);                    /* src1len + FPD data */
    PSL = PSL | PSL_FPD;
    }
R[2] = R[2] & STR_LNMASK;                               /* mask src2len */
for (s1 = s2 = 0; ((R[0] | R[2]) & STR_LNMASK) != 0; extra_bytes++) {
    if (R[0] & STR_LNMASK)                              /* src1? read */
        s1 = Read (R[1], L_BYTE, RA);
    else s1 = fill;                                     /* no, use fill */
    if (R[2])                                           /* src2? read */
        s2 = Read (R[3], L_BYTE, RA);
    else s2 = fill;                                     /* no, use fill */
    if (s1 != s2)                                       /* src1 = src2? */
        break;
    if (R[0] & STR_LNMASK) {                            /* if src1, decr */
        R[0] = (R[0] & ~STR_LNMASK) | ((R[0] - 1) & STR_LNMASK);
        R[1] = R[1] + 1;
        }
    if (R[2]) {                                         /* if src2, decr */
        R[2] = (R[2] - 1) & STR_LNMASK;
        R[3] = R[3] + 1;
        }
    }
PSL = PSL & ~PSL_FPD;                                   /* clear FPD */
CC_CMP_B (s1, s2);                                      /* set cc's */
R[0] = R[0] & STR_LNMASK;                               /* clear packup */
return cc;
}

/* LOCC, SKPC

   if PSL<fpd> = 0,
        opnd[0] =       match character
        opnd[1] =       source length
        opnd[2] =       source address

   if PSL<fpd> = 1,
        R0      =       delta-PC/match/source length
        R1      =       source address
*/

int32 op_locskp (int32 *opnd, int32 skpc, int32 acc)
{
int32 c, match;

if (PSL & PSL_FPD) {                                    /* FPD set? */
    SETPC (fault_PC + STR_GETDPC (R[0]));               /* reset PC */
    match = STR_GETCHR (R[0]);                          /* get match char */
    }
else {
    match = opnd[0];                                    /* get operands */
    R[0] = STR_PACK (match, opnd[1]);                   /* src len + FPD data */
    R[1] = opnd[2];                                     /* src addr */
    PSL = PSL | PSL_FPD;
    }
for ( ; (R[0] & STR_LNMASK) != 0; extra_bytes++ ) {    /* loop thru string */
    c = Read (R[1], L_BYTE, RA);                        /* get src byte */
    if ((c == match) ^ skpc)                            /* match & locc? */
        break;
    R[0] = (R[0] & ~STR_LNMASK) | ((R[0] - 1) & STR_LNMASK);
    R[1] = R[1] + 1;                                    /* incr src1adr */
    }
PSL = PSL & ~PSL_FPD;                                   /* clear FPD */
R[0] = R[0] & STR_LNMASK;                               /* clear packup */
return (R[0]? 0: CC_Z);                                 /* set cc's */
}

/* SCANC, SPANC

   if PSL<fpd> = 0,
        opnd[0] =       source length
        opnd[1] =       source address
        opnd[2] =       table address
        opnd[3] =       mask

   if PSL<fpd> = 1,
        R0      =       delta-PC/char/source length
        R1      =       source address
        R3      =       table address
*/

int32 op_scnspn (int32 *opnd, int32 spanc, int32 acc)
{
int32 c, t, mask;

if (PSL & PSL_FPD) {                                    /* FPD set? */
    SETPC (fault_PC + STR_GETDPC (R[0]));               /* reset PC */
    mask = STR_GETCHR (R[0]);                           /* get mask */
    }
else {
    R[1] = opnd[1];                                     /* src addr */
    R[3] = opnd[2];                                     /* tblad */
    mask = opnd[3];                                     /* mask */
    R[0] = STR_PACK (mask, opnd[0]);                    /* srclen + FPD data */
    PSL = PSL | PSL_FPD;
    }
for ( ; (R[0] & STR_LNMASK) != 0; extra_bytes++ ) {    /* loop thru string */
    c = Read (R[1], L_BYTE, RA);                        /* get byte */
    t = Read (R[3] + c, L_BYTE, RA);                    /* get table ent */
    if (((t & mask) != 0) ^ spanc)                      /* test vs instr */
        break;
    R[0] = (R[0] & ~STR_LNMASK) | ((R[0] - 1) & STR_LNMASK);
    R[1] = R[1] + 1;
    }
PSL = PSL & ~PSL_FPD;
R[0] = R[0] & STR_LNMASK;                               /* clear packup */
R[2] = 0;
return (R[0]? 0: CC_Z);
}

/* Operating system interfaces */

/* Interrupt or exception

        vec     =       SCB vector (bit<0> = interrupt in Qbus mode)
        cc      =       condition codes
        ipl     =       new IPL if interrupt
        ei      =       -1: severe exception
                        0:  normal exception
                        1:  interrupt
*/

int32 intexc (int32 vec, int32 cc, int32 ipl, int ei)
{
int32 oldpsl = PSL | cc;
int32 oldcur = PSL_GETCUR (oldpsl);
int32 oldsp = SP;
int32 newpsl;
int32 newpc;
int32 acc;

in_ie = 1;                                              /* flag int/exc */
CLR_TRAPS;                                              /* clear traps */
newpc = ReadLP ((SCBB + vec) & (PAMASK & ~3));          /* read new PC */
if (ei == IE_SVE)                                       /* severe? on istk */
    newpc = newpc | 1;
if (newpc & 2)                                          /* bad flags? */
    ABORT (STOP_ILLVEC);
if (oldpsl & PSL_IS)                                    /* on int stk? */
    newpsl = PSL_IS;
else {
    STK[oldcur] = SP;                                   /* no, save cur stk */
    if (newpc & 1) {                                    /* to int stk? */
        newpsl = PSL_IS;                                /* flag */
        SP = IS;                                        /* new stack */
        }
    else {
        newpsl = 0;                                     /* to ker stk */
        SP = KSP;                                       /* new stack */
        }
    }
if (ei > 0) {                                           /* if int, new IPL */
    int32 newipl;
    if ((VEC_QBUS & vec) != 0)                          /* Qbus vector? */
        newipl = PSL_IPL17;                             /* force IPL 17 */
    else
        newipl = ipl << PSL_V_IPL;                      /* otherwise, int IPL */
    PSL = newpsl | newipl;
    }
else 
    PSL = newpsl |                                      /* exc, old IPL/1F */
        ((newpc & 1)? PSL_IPL1F: (oldpsl & PSL_IPL)) | (oldcur << PSL_V_PRV);
sim_debug (LOG_CPU_I, &cpu_dev, "PC=%08x, PSL=%08x, SP=%08x, VEC=%08x, nPSL=%08x, nSP=%08x ",
             PC, oldpsl, oldsp, vec, PSL, SP);
sim_debug_bits(LOG_CPU_I, &cpu_dev, cpu_psl_bits, oldpsl, PSL, 1);
acc = ACC_MASK (KERN);                                  /* new mode is kernel */
Write (SP - 4, oldpsl, L_LONG, WA);                     /* push old PSL */
Write (SP - 8, PC, L_LONG, WA);                         /* push old PC */
SP = SP - 8;                                            /* update stk ptr */
JUMP_ALWAYS (newpc & ~3);                               /* change PC */
in_ie = 0;                                              /* out of flows */
return 0;
}

/* CHMK, CHME, CHMS, CHMU

        opnd[0] =       operand
*/

int32 op_chm (int32 *opnd, int32 cc, int32 opc)
{
int32 mode = opc & PSL_M_MODE;
int32 cur = PSL_GETCUR (PSL);
int32 tsp, newpc, acc;
int32 sta;

if (PSL & PSL_IS)
    ABORT (STOP_CHMFI);
newpc = ReadLP ((SCBB + SCB_CHMK + (mode << 2)) & PAMASK);
if (cur < mode)                                         /* only inward */
    mode = cur;
STK[cur] = SP;                                          /* save stack */
tsp = STK[mode];                                        /* get new stk */
acc = ACC_MASK (mode);                                  /* set new mode */
if (Test (p2 = tsp - 1, WA, &sta) < 0) {                /* probe stk */
    p1 = MM_WRITE | (sta & MM_EMASK);
    ABORT ((sta & 4)? ABORT_TNV: ABORT_ACV);
    }
if (Test (p2 = tsp - 12, WA, &sta) < 0) {
    p1 = MM_WRITE | (sta & MM_EMASK);
    ABORT ((sta & 4)? ABORT_TNV: ABORT_ACV);
    }
Write (tsp - 12, SXTW (opnd[0]), L_LONG, WA);           /* push argument */
Write (tsp - 8, PC, L_LONG, WA);                        /* push PC */
Write (tsp - 4, PSL | cc, L_LONG, WA);                  /* push PSL */
SP = tsp - 12;                                          /* set new stk */
PSL = (mode << PSL_V_CUR) | (PSL & PSL_IPL) |           /* set new PSL */
    (cur << PSL_V_PRV);                                 /* IPL unchanged */
JUMP_ALWAYS (newpc & ~03);                              /* set new PC */
return 0;                                               /* cc = 0 */
}

/* REI - return from exception or interrupt

The lengthiest part of the REI instruction is the validity checking of the PSL
popped off the stack.  The new PSL is checked against the following eight rules:

let     tmp     =       new PSL popped off the stack
let     PSL     =       current PSL

Rule    SRM formulation                     Comment
----    ---------------                     -------
 1      tmp<25:24> GEQ PSL<25:24>           tmp<cur_mode> GEQ PSL<cur_mode>
 2      tmp<26> LEQ PSL<26>                 tmp<is> LEQ PSL<is>
 3      tmp<26> = 1 => tmp<25:24> = 0       tmp<is> = 1 => tmp<cur_mode> = ker
 4      tmp<26> = 1 => tmp<20:16> > 0       tmp<is> = 1 => tmp<ipl> > 0
 5      tmp<20:16> > 0 => tmp<25:24> = 0    tmp<ipl> > 0 => tmp<cur_mode> = ker
 6      tmp<25:24> LEQ tmp<23:22>           tmp<cur_mode> LEQ tmp<prv_mode>
 7      tmp<20:16> LEQ PSL<20:16>           tmp<ipl> LEQ PSL<ipl>
 8      tmp<31,29:28,21,15:8> = 0           tmp<mbz> = 0
 9      tmp<31> = 1 => tmp<cur_mode> = 3, tmp<prv_mode> = 3>, tmp<fpd,is,ipl,dv,fu,iv> = 0 
*/

#define REI_RSVD_FAULT(desc) do {                                                                                   \
        sim_debug (LOG_CPU_FAULT_RSVD, &cpu_dev, "REI Operand: PC=%08x, PSL=%08x, SP=%08x, nPC=%08x, nPSL=%08x, nSP=%08x - %s\n",\
                     PC, PSL, SP - 8, newpc, newpsl, ((newpsl & IS)? IS: STK[newcur]), desc);                       \
        RSVD_OPND_FAULT(REI); } while (0)

int32 op_rei (int32 acc)
{
int32 newpc = Read (SP, L_LONG, RA);
int32 newpsl = Read (SP + 4, L_LONG, RA);
int32 newcur = PSL_GETCUR (newpsl);
int32 oldcur = PSL_GETCUR (PSL);
int32 newipl, i;

if ((newpsl & PSL_MBZ) ||                               /* rule 8 */
    (newcur < oldcur))                                  /* rule 1 */
    REI_RSVD_FAULT("rule 8 or rule 1");
if (newcur) {                                           /* to esu, skip 2,4,7 */
    if ((newpsl & (PSL_IS | PSL_IPL)) ||                /* rules 3,5 */
        (newcur > PSL_GETPRV (newpsl)))                 /* rule 6 */
        REI_RSVD_FAULT("rule 3,5 or rule 6");           /* end rei to esu */
    }
else {                                                  /* to k, skip 3,5,6 */
    newipl = PSL_GETIPL (newpsl);                       /* get new ipl */
    if ((newpsl & PSL_IS) &&                            /* setting IS? */
        (((PSL & PSL_IS) == 0) || (newipl == 0)))       /* test rules 2,4 */
        REI_RSVD_FAULT("rule 2 or rule 4");             /* else skip 2,4 */
    if (newipl > PSL_GETIPL (PSL))                      /* test rule 7 */
        REI_RSVD_FAULT("rule 7");
    }                                                   /* end if kernel */
if (newpsl & PSL_CM) {                                  /* setting cmode? */
    if (BadCmPSL (newpsl))                              /* validate PSL */
        REI_RSVD_FAULT("cmode invalid PSL");
    for (i = 0; i < 7; i++)                             /* mask R0-R6, PC */
        R[i] = R[i] & WMASK;
    newpc = newpc & WMASK;
    }
SP = SP + 8;                                            /* pop stack */
if (PSL & PSL_IS)                                       /* save stack */
    IS = SP;
else 
    STK[oldcur] = SP;
sim_debug (LOG_CPU_R, &cpu_dev, "PC=%08x, PSL=%08x, SP=%08x, nPC=%08x, nPSL=%08x, nSP=%08x ",
             PC, PSL, SP - 8, newpc, newpsl, ((newpsl & IS)? IS: STK[newcur]));
sim_debug_bits(LOG_CPU_R, &cpu_dev, cpu_psl_bits, PSL, newpsl, 1);
PSL = (PSL & PSL_TP) | (newpsl & ~CC_MASK);             /* set PSL */
if (PSL & PSL_IS)                                       /* set new stack */
    SP = IS;
else {
    SP = STK[newcur];                                   /* if ~IS, chk AST */
    if (newcur >= ASTLVL) {
        sim_debug (LOG_CPU_R, &cpu_dev, "AST delivered\n");
        SISR = SISR | SISR_2;
        }
    }
JUMP_ALWAYS (newpc);                                    /* set new PC */
return newpsl & CC_MASK;                                /* set new cc */
}

/* LDCPTX - load process context */

void op_ldpctx (int32 acc)
{
uint32 newpc, newpsl, pcbpa, t;

if (PSL & PSL_CUR)                                      /* must be kernel */
    RSVD_INST_FAULT(LDPCTX);
pcbpa = PCBB & PAMASK;                                  /* phys address */
KSP = ReadLP (pcbpa);                                   /* restore stk ptrs */
ESP = ReadLP (pcbpa + 4);
SSP = ReadLP (pcbpa + 8);
USP = ReadLP (pcbpa + 12);
R[0] = ReadLP (pcbpa + 16);                             /* restore registers */
R[1] = ReadLP (pcbpa + 20);
R[2] = ReadLP (pcbpa + 24);
R[3] = ReadLP (pcbpa + 28);
R[4] = ReadLP (pcbpa + 32);
R[5] = ReadLP (pcbpa + 36);
R[6] = ReadLP (pcbpa + 40);
R[7] = ReadLP (pcbpa + 44);
R[8] = ReadLP (pcbpa + 48);
R[9] = ReadLP (pcbpa + 52);
R[10] = ReadLP (pcbpa + 56);
R[11] = ReadLP (pcbpa + 60);
R[12] = ReadLP (pcbpa + 64);
R[13] = ReadLP (pcbpa + 68);
newpc = ReadLP (pcbpa + 72);                            /* get PC, PSL */
newpsl = ReadLP (pcbpa + 76);

t = ReadLP (pcbpa + 80);
ML_PXBR_TEST (t);                                       /* validate P0BR */
P0BR = t & BR_MASK;                                     /* restore P0BR */
t = ReadLP (pcbpa + 84);
LP_MBZ84_TEST (t);                                      /* test mbz */
ML_LR_TEST (t & LR_MASK);                               /* validate P0LR */
P0LR = t & LR_MASK;                                     /* restore P0LR */
t = (t >> 24) & AST_MASK;
LP_AST_TEST (t);                                        /* validate AST */
ASTLVL = t;                                             /* restore AST */
t = ReadLP (pcbpa + 88);
ML_PXBR_TEST (t + 0x800000);                            /* validate P1BR */
P1BR = t & BR_MASK;                                     /* restore P1BR */
t = ReadLP (pcbpa + 92);
LP_MBZ92_TEST (t);                                      /* test MBZ */
ML_LR_TEST (t & LR_MASK);                               /* validate P1LR */
P1LR = t & LR_MASK;                                     /* restore P1LR */
pme = (t >> 31) & 1;                                    /* restore PME */

zap_tb (0);                                             /* clear process TB */
set_map_reg ();
sim_debug (LOG_CPU_P, &cpu_dev, ">>LDP: PC=%08x, PSL=%08x, SP=%08x, nPC=%08x, nPSL=%08x, nSP=%08x\n",
             PC, PSL, SP, newpc, newpsl, KSP);
if (PSL & PSL_IS)                                       /* if istk, */
    IS = SP;
PSL = PSL & ~PSL_IS;                                    /* switch to kstk */
SP = KSP - 8;
Write (SP, newpc, L_LONG, WA);                          /* push PC, PSL */
Write (SP + 4, newpsl, L_LONG, WA);
return;
}

/* SVPCTX - save processor context */

void op_svpctx (int32 acc)
{
int32 savpc, savpsl, pcbpa;

if (PSL & PSL_CUR)                                      /* must be kernel */
    RSVD_INST_FAULT(SVPCTX);
savpc = Read (SP, L_LONG, RA);                          /* pop PC, PSL */
savpsl = Read (SP + 4, L_LONG, RA);
sim_debug (LOG_CPU_P, &cpu_dev, ">>SVP: PC=%08x, PSL=%08x, SP=%08x, oPC=%08x, oPSL=%08x\n",
             PC, PSL, SP, savpc, savpsl);
if (PSL & PSL_IS)                                       /* int stack? */
    SP = SP + 8;
else {
    KSP = SP + 8;                                       /* pop kernel stack */
    SP = IS;                                            /* switch to int stk */
    if ((PSL & PSL_IPL) == 0)                           /* make IPL > 0 */
         PSL = PSL | PSL_IPL1;
    PSL = PSL | PSL_IS;                                 /* set PSL<is> */
    }
pcbpa = PCBB & PAMASK;
WriteLP (pcbpa, KSP);                                   /* save stk ptrs */
WriteLP (pcbpa + 4, ESP);
WriteLP (pcbpa + 8, SSP);
WriteLP (pcbpa + 12, USP);
WriteLP (pcbpa + 16, R[0]);                             /* save registers */
WriteLP (pcbpa + 20, R[1]);
WriteLP (pcbpa + 24, R[2]);
WriteLP (pcbpa + 28, R[3]);
WriteLP (pcbpa + 32, R[4]);
WriteLP (pcbpa + 36, R[5]);
WriteLP (pcbpa + 40, R[6]);
WriteLP (pcbpa + 44, R[7]);
WriteLP (pcbpa + 48, R[8]);
WriteLP (pcbpa + 52, R[9]);
WriteLP (pcbpa + 56, R[10]);
WriteLP (pcbpa + 60, R[11]);
WriteLP (pcbpa + 64, R[12]);
WriteLP (pcbpa + 68, R[13]);
WriteLP (pcbpa + 72, savpc);                            /* save PC, PSL */
WriteLP (pcbpa + 76, savpsl);
return;
}

/* PROBER and PROBEW

        opnd[0] =       mode
        opnd[1] =       length
        opnd[2] =       base address
*/

int32 op_probe (int32 *opnd, int32 rw)
{
int32 mode = opnd[0] & PSL_M_MODE;                      /* mask mode */
int32 length = opnd[1];
int32 ba = opnd[2];
int32 prv = PSL_GETPRV (PSL);
int32 acc, sta, sta1;

if (prv > mode)                                         /* maximize mode */
    mode = prv;
acc = ACC_MASK (mode) << (rw? TLB_V_WACC: 0);           /* set acc mask */
Test (ba, acc, &sta);                                   /* probe */
switch (sta) {                                          /* case on status */

    case PR_PTNV:                                       /* pte TNV */
        p1 = MM_PARAM (rw, PR_PTNV);
        p2 = ba;
        ABORT (ABORT_TNV);                              /* force TNV */

    case PR_TNV: case PR_OK:                            /* TNV or ok */
        break;                                          /* continue */

    default:                                            /* other */
        return CC_Z;                                    /* lose */
        }

Test (ba + length - 1, acc, &sta1);                     /* probe end addr */
switch (sta1) {                                         /* case on status */

    case PR_PTNV:                                       /* pte TNV */
        p1 = MM_PARAM (rw, PR_PTNV);
        p2 = ba + length - 1;
        ABORT (ABORT_TNV);                              /* force TNV */

    case PR_TNV: case PR_OK:                            /* TNV or ok */
        break;                                          /* win */

    default:                                            /* other */
        return CC_Z;                                    /* lose */
        }

return 0;
}

/* MTPR - move to processor register

        opnd[0] =       data
        opnd[1] =       register number
*/

int32 op_mtpr (int32 *opnd)
{
uint32 val = (uint32)opnd[0];
uint32 prn = (uint32)opnd[1];
int32 cc;

if (PSL & PSL_CUR)                                      /* must be kernel */
    RSVD_INST_FAULT(MTPR);
if (prn > MT_MAX)                                       /* reg# > max? fault */
    RSVD_OPND_FAULT(op_mtpr);
CC_IIZZ_L (val);                                        /* set cc's */
switch (prn) {                                          /* case on reg # */

    case MT_KSP:                                        /* KSP */
        if (PSL & PSL_IS)                               /* on IS? store KSP */
            KSP = val;
        else SP = val;                                  /* else store SP */
        break;

    case MT_ESP: case MT_SSP: case MT_USP:              /* ESP, SSP, USP */
        STK[prn] = val;                                 /* store stack */
        break;

    case MT_IS:                                         /* IS */
        if (PSL & PSL_IS)                               /* on IS? store SP */
            SP = val;
        else IS = val;                                  /* else store IS */
        break;

    case MT_P0BR:                                       /* P0BR */
        ML_PXBR_TEST (val);                             /* validate */
        P0BR = val & BR_MASK;                           /* lw aligned */
        zap_tb (0);                                     /* clr proc TLB */
        set_map_reg ();
        break;

    case MT_P0LR:                                       /* P0LR */
        ML_LR_TEST (val & LR_MASK);                     /* validate */
        P0LR = val & LR_MASK;
        zap_tb (0);                                     /* clr proc TLB */
        set_map_reg ();
        break;

    case MT_P1BR:                                       /* P1BR */
        ML_PXBR_TEST (val + 0x800000);                  /* validate */
        P1BR = val & BR_MASK;                           /* lw aligned */
        zap_tb (0);                                     /* clr proc TLB */
        set_map_reg ();
        break;

    case MT_P1LR:                                       /* P1LR */
        ML_LR_TEST (val & LR_MASK);                     /* validate */
        P1LR = val & LR_MASK;
        zap_tb (0);                                     /* clr proc TLB */
        set_map_reg ();
        break;

    case MT_SBR:                                        /* SBR */
        ML_SBR_TEST (val);                              /* validate */
        SBR = val & BR_MASK;                            /* lw aligned */
        zap_tb (1);                                     /* clr entire TLB */
        set_map_reg ();
        break;

    case MT_SLR:                                        /* SLR */
        ML_LR_TEST (val & LR_MASK);                     /* validate */
        SLR = val & LR_MASK;
        zap_tb (1);                                     /* clr entire TLB */
        set_map_reg ();
        break;

    case MT_SCBB:                                       /* SCBB */
        ML_PA_TEST (val);                               /* validate */
        SCBB = val & BR_MASK;                           /* lw aligned */
        break;

    case MT_PCBB:                                       /* PCBB */
        ML_PA_TEST (val);                               /* validate */
        PCBB = val & BR_MASK;                           /* lw aligned */
        break;

    case MT_IPL:                                        /* IPL */
        PSL = (PSL & ~PSL_IPL) | ((val & PSL_M_IPL) << PSL_V_IPL);
        if ((VAX_IDLE_BSDNEW & cpu_idle_mask) &&        /* New NetBSD and OpenBSD */
            (0 != (PC & 0x80000000)) &&                 /* System Space (Not BOOT ROM) */
            (val == 1))                                 /* IPL 1 */
            cpu_idle();                                 /* idle loop */
        break;

    case MT_ASTLVL:                                     /* ASTLVL */
        MT_AST_TEST (val);                              /* trim, test val */
        ASTLVL = val;
        break;

    case MT_SIRR:                                       /* SIRR */
        val = val & 0xF;                                /* consider only 4b */
        if (val != 0)                                   /* if not zero */
            SISR = SISR | (1 << val);                   /* set bit in SISR */
        break;

    case MT_SISR:                                       /* SISR */
        SISR = val & SISR_MASK;
        break;

    case MT_MAPEN:                                      /* MAPEN */
        mapen = val & 1;
        /* fall through */
    case MT_TBIA:                                       /* TBIA */
        zap_tb (1);                                     /* clr entire TLB */
        break;

    case MT_TBIS:                                       /* TBIS */
        zap_tb_ent (val);
        break;

    case MT_TBCHK:                                      /* TBCHK */
        if (chk_tb_ent (val))
            cc = cc | CC_V;
        break;

    case MT_PME:                                        /* PME */
        pme = val & 1;
        break;

    default:
        WriteIPR (prn, val);                            /* others */
        break;
        }

return cc;
}

int32 op_mfpr (int32 *opnd)
{
uint32 prn = (uint32)opnd[0];
int32 val;

if (PSL & PSL_CUR)                                      /* must be kernel */
    RSVD_INST_FAULT(MFPR);
if (prn > MT_MAX)                                       /* reg# > max? fault */
    RSVD_OPND_FAULT(op_mtpr);
switch (prn) {                                          /* case on reg# */

    case MT_KSP:                                        /* KSP */
        val = (PSL & PSL_IS)? KSP: SP;                  /* return KSP or SP */
        break;

    case MT_ESP: case MT_SSP: case MT_USP:              /* ESP, SSP, USP */
        val = STK[prn];                                 /* return stk ptr */
        break;

    case MT_IS:                                         /* IS */
        val = (PSL & PSL_IS)? SP: IS;                   /* return SP or IS */
        break;

    case MT_P0BR:                                       /* P0BR */
        val = P0BR;
        break;

    case MT_P0LR:                                       /* P0LR */
        val = P0LR;
        break;

    case MT_P1BR:                                       /* P1BR */
        val = P1BR;
        break;

    case MT_P1LR:                                       /* P1LR */
        val = P1LR;
        break;

    case MT_SBR:                                        /* SBR */
        val = SBR;
        break;

    case MT_SLR:                                        /* SLR */
        val = SLR;
        break;

    case MT_SCBB:                                       /* SCBB */
        val = SCBB;
        break;

    case MT_PCBB:                                       /* PCBB */
        val = PCBB;
        break;

    case MT_IPL:                                        /* IPL */
        val = PSL_GETIPL (PSL);
        break;

    case MT_ASTLVL:                                     /* ASTLVL */
        val = ASTLVL;
        break;

    case MT_SISR:                                       /* SISR */
        val = SISR & SISR_MASK;
        break;

    case MT_MAPEN:                                      /* MAPEN */
        val = mapen & 1;
        break;

    case MT_PME:
        val = pme & 1;
        break;

    case MT_SIRR:
    case MT_TBIA:
    case MT_TBIS:
    case MT_TBCHK:
        RSVD_OPND_FAULT(op_mfpr);                       /* write only */

    default:                                            /* others */
        val = ReadIPR (prn);                            /* read from SSC */
        break;
        }

return val;
}
