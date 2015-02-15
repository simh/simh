/* lgp_cpu.c: LGP CPU simulator

   Copyright (c) 2004-2008, Robert M. Supnik

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

   cpu          LGP-30 [LGP-21] CPU

   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   04-Sep-05    RMS     Fixed missing returns (Peter Schorn)
   04-Jan-05    RMS     Modified VM pointer setup

   The system state for the LGP-30 [LGP-21] is:

   A<0:31>              accumulator
   C<0:11>              counter (PC)
   OVF                  overflow flag [LGP-21 only]

   The LGP-30 [LGP-21] has just one instruction format:

                        1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   |S|                     |opcode |   |    operand address    |   | 
   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    LGP-30 instructions:

    <0,12:15>           operation

    0                   stop
    1                   A <- M[ea]
    2                   M[ea]<addr> <- A<addr>
    3                   M[ea]<addr> <- C + 1
    4                   input
    5                   A <- A / M[ea]
    6                   A <- A * M[ea], low result
    7                   A <- A * M[ea], high result
    8                   output
    9                   A <- A & M[ea]
    A                   C <- ea
    B                   C <- ea if A < 0
    -B                  C <- ea if (A < 0) || T-switch set
    C                   M[ea] <- A
    D                   M[ea] <- A, A <- 0
    E                   A <- A + M[ea]
    F                   A <- A - M[ea]

    LGP-21 instructions:

    <0,12:15>           operation

    0                   stop; sense and skip
    -0                  stop; sense overflow and skip
    1                   A <- M[ea]
    2                   M[ea]<addr> <- A<addr>
    3                   M[ea]<addr> <- C + 1
    4                   6b input
    -4                  4b input
    5                   A <- A / M[ea]
    6                   A <- A * M[ea], low result
    7                   A <- A * M[ea], high result
    8                   6b output
    -8                  4b output
    9                   A <- A & M[ea]
    A                   C <- ea
    B                   C <- ea if A < 0
    -B                  C <- ea if (A < 0) || T-switch set
    C                   M[ea] <- A
    D                   M[ea] <- A, A <- 0
    E                   A <- A + M[ea]
    F                   A <- A - M[ea]

    The LGP-30 [LGP-21] has 4096 32b words of memory.  The low order
    bit is always read and stored as 0.  The LGP-30 uses a drum for
    memory, with 64 tracks of 64 words.  The LGP-21 uses a disk for
    memory, with 32 tracks of 128 words.

   This routine is the instruction decode routine for the LGP-30
   [LGP-21].  It is called from the simulator control program to
   execute instructions in simulated memory, starting at the simulated
   PC.  It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        STOP instruction
        breakpoint encountered
        overflow [LGP-30]
        I/O error in I/O simulator

   2. Interrupts.  There are no interrupts.

   3. Non-existent memory.  All of memory always exists.

   4. Adding I/O devices.  The LGP-30 could not support additional
      I/O devices.  The LGP-21 could but none are known.
*/

#include "lgp_defs.h"

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = (PC - 1) & AMASK;
#define M16             0xFFFF
#define M32             0xFFFFFFFF
#define NEG(x)          ((~(x) + 1) & DMASK)
#define ABS(x)          (((x) & SIGN)? NEG (x): (x))

uint32 M[MEMSIZE] = { 0 };                              /* memory */
uint32 PC = 0;                                          /* counter */
uint32 A = 0;                                           /* accumulator */
uint32 IR = 0;                                          /* instr register */
uint32 OVF = 0;                                         /* overflow indicator */
uint32 t_switch = 0;                                    /* transfer switch */
uint32 bp32 = 0;                                        /* BP32 switch */
uint32 bp16 = 0;                                        /* BP16 switch */
uint32 bp8 = 0;                                         /* BP8 switch */
uint32 bp4 = 0;                                         /* BP4 switch */
uint32 inp_strt = 0;                                    /* input started */
uint32 inp_done = 0;                                    /* input done */
uint32 out_strt = 0;                                    /* output started */
uint32 out_done = 0;                                    /* output done */
uint32 lgp21_sov = 0;                                   /* LGP-21 sense pending */
int32 delay = 0;
int16 pcq[PCQ_SIZE] = { 0 };                            /* PC queue */
int32 pcq_p = 0;                                        /* PC queue ptr */
REG *pcq_r = NULL;                                      /* PC queue reg ptr */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_set_model (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, void *desc);
t_stat cpu_set_30opt (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_30opt_i (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_30opt_o (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_fill (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_set_exec (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat cpu_one_inst (uint32 opc, uint32 ir);
uint32 Mul64 (uint32 a, uint32 b, uint32 *low);
t_bool Div32 (uint32 dvd, uint32 dvr, uint32 *q);
uint32 I_delay (uint32 opc, uint32 ea, uint32 op);
uint32 shift_in (uint32 a, uint32 dat, uint32 sh4);

extern t_stat op_p (uint32 dev, uint32 ch);
extern t_stat op_i (uint32 dev, uint32 ch, uint32 sh4);
extern void lgp_vm_init (void);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX+UNIT_IN4B+UNIT_TTSS_D, MEMSIZE) };

REG cpu_reg[] = {
    { DRDATA (C, PC, 12), REG_VMAD },
    { HRDATA (A, A, 32), REG_VMIO },
    { HRDATA (IR, IR, 32), REG_VMIO },
    { FLDATA (OVF, OVF, 0) },
    { FLDATA (TSW, t_switch, 0) },
    { FLDATA (BP32, bp32, 0) },
    { FLDATA (BP16, bp16, 0) },
    { FLDATA (BP8, bp8, 0) },
    { FLDATA (BP4, bp4, 0) },
    { FLDATA (INPST, inp_strt, 0) },
    { FLDATA (INPDN, inp_done, 0) },
    { FLDATA (OUTST, out_strt, 0) },
    { FLDATA (OUTDN, out_done, 0) },
    { DRDATA (DELAY, delay, 7) },
    { BRDATA (CQ, pcq, 16, 12, PCQ_SIZE), REG_RO + REG_CIRC },
    { HRDATA (CQP, pcq_p, 6), REG_HRO },
    { HRDATA (WRU, sim_int_char, 8) },
    { NULL }
    };

MTAB cpu_mod[] = {
    { UNIT_LGP21, UNIT_LGP21, "LGP-21", "LGP21", &cpu_set_model, &cpu_show_model },
    { UNIT_LGP21, 0,          "LGP-30", "LGP30", &cpu_set_model, &cpu_show_model },
    { UNIT_TTSS_D, UNIT_TTSS_D, 0, "TRACK" },
    { UNIT_TTSS_D, 0,           0, "NORMAL" },
    { UNIT_LGPH_D, UNIT_LGPH_D, 0, "LGPHEX" },
    { UNIT_LGPH_D, 0,           0, "STANDARDHEX" },
    { UNIT_MANI, UNIT_MANI, NULL, "MANUAL" },
    { UNIT_MANI, 0,         NULL, "TAPE" },
    { UNIT_IN4B, UNIT_IN4B, NULL, "4B", &cpu_set_30opt },
    { UNIT_IN4B, 0,         NULL, "6B", &cpu_set_30opt },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "INPUT", &cpu_set_30opt_i },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "OUTPUT", &cpu_set_30opt_o },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "EXECUTE", &cpu_set_exec },
    { MTAB_XTD|MTAB_VDV, 0, NULL, "FILL", &cpu_set_fill },
    { 0 }
    };

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 10, 12, 1, 16, 32,
    &cpu_ex, &cpu_dep, &cpu_reset,
    NULL, NULL, NULL
    };

/* Timing tables */

/* Optimization minima and maxima
     Z   B   Y   R   I   D   N   M   P   E   U   T   H   C   A   S */

static const int32 min_30[16] = {
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2
     };
static const int32 max_30[16] = {
     7,  7,  7,  7,  7,  5,  8,  6,  7,  7,  0,  0,  7,  7,  7,  7
     };
static const int32 min_21[16] = {
     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2
     };
static const int32 max_21[16] = {
     0, 16, 16, 16,  0, 58, 81, 79,  0, 16,  0,  0, 16, 16, 16, 16
     };

static const uint32 log_to_phys_30[NSC_30] = {          /* drum interlace chart */
    0, 57, 50, 43, 36, 29, 22, 15, 8 ,
    1, 58, 51, 44, 37, 30, 23, 16, 9 ,
    2, 59, 52, 45, 38, 31, 24, 17, 10,
    3, 60, 53, 46, 39, 32, 25, 18, 11,
    4, 61, 54, 47, 40, 33, 26, 19, 12,
    5, 62, 55, 48, 41, 32, 27, 20, 13,
    6, 63, 56, 49, 42, 33, 28, 21, 14,
    7
    };

static const uint32 log_to_phys_21[NSC_21] = {          /* disk interlace chart */
    0, 64, 57, 121, 50, 114, 43, 107, 36, 100, 29, 93, 22, 86, 15, 79,  8, 72,
    1, 65, 58, 122, 51, 115, 44, 108, 37, 101, 30, 94, 23, 87, 16, 80,  9, 73,
    2, 66, 59, 123, 52, 116, 45, 109, 38, 102, 31, 95, 24, 88, 17, 81, 10, 74,
    3, 67, 60, 124, 53, 117, 46, 110, 39, 103, 32, 96, 25, 89, 18, 82, 11, 75,
    4, 68, 61, 125, 54, 118, 47, 111, 40, 104, 33, 97, 26, 90, 19, 83, 12, 76,
    5, 69, 62, 126, 55, 119, 48, 112, 41, 105, 34, 98, 27, 91, 20, 84, 12, 77,
    6, 70, 63, 127, 56, 120, 49, 113, 42, 106, 35, 99, 28, 92, 21, 85, 13, 78,
    7, 71
    };

t_stat sim_instr (void)
{
t_stat r = 0;
uint32 oPC;

/* Restore register state */

PC = PC & AMASK;                                        /* mask PC */
sim_cancel_step ();                                     /* defang SCP step */
if (lgp21_sov) {                                        /* stop sense pending? */
    lgp21_sov = 0;
    if (!OVF)                                           /* ovf off? skip */
        PC = (PC + 1) & AMASK;
    else OVF = 0;                                       /* on? reset */
    }

/* Main instruction fetch/decode loop */

do {
    if (sim_interval <= 0) {                            /* check clock queue */
        if ((r = sim_process_event ()))
            break;
        }

    if (delay > 0) {                                    /* delay to next instr */
        delay = delay - 1;                              /* count down delay */
        sim_interval = sim_interval - 1;
        continue;                                       /* skip execution */
        }

    if (sim_brk_summ &&                                 /* breakpoint? */
        sim_brk_test (PC, SWMASK ('E'))) {
        r = STOP_IBKPT;                                 /* stop simulation */
        break;
        }

    IR = Read (oPC = PC);                               /* get instruction */
    PC = (PC + 1) & AMASK;                              /* increment PC */
    sim_interval = sim_interval - 1;

    if ((r = cpu_one_inst (oPC, IR))) {                 /* one instr; error? */
        if (r == STOP_STALL) {                          /* stall? */
            PC = oPC;                                   /* back up PC */
            delay = r = 0;                              /* no delay */
            }
        else break;
        }

    if (sim_step && (--sim_step <= 0))                  /* do step count */
        r = SCPE_STOP;

    } while (r == 0);                                   /* loop until halted */
pcq_r->qptr = pcq_p;                                    /* update pc q ptr */
return r;
}

/* Execute one instruction */

t_stat cpu_one_inst (uint32 opc, uint32 ir)
{
uint32 ea, op, dat, res, dev, sh4, ch;
t_bool ovf_this_cycle = FALSE;
t_stat reason = 0;

op = I_GETOP (ir);                                      /* opcode */
ea = I_GETEA (ir);                                      /* address */
switch (op) {                                           /* case on opcode */

/* Loads, stores, transfers instructions */

    case OP_B:                                          /* bring */
        A = Read (ea);                                  /* A <- M[ea] */
        delay = I_delay (opc, ea, op);
        break;

    case OP_H:                                          /* hold */
        Write (ea, A);                                  /* M[ea] <- A */
        delay = I_delay (opc, ea, op);
        break;

    case OP_C:                                          /* clear */
        Write (ea, A);                                  /* M[ea] <- A */
        A = 0;                                          /* A <- 0 */
        delay = I_delay (opc, ea, op);
        break;

    case OP_Y:                                          /* store address */
        dat = Read (ea);                                /* get operand */
        dat = (dat & ~I_EA) | (A & I_EA);               /* merge address */
        Write (ea, dat);
        delay = I_delay (opc, ea, op);
        break;

    case OP_R:                                          /* return address */
        dat = Read (ea);                                /* get operand */
        dat = (dat & ~I_EA) | (((PC + 1) & AMASK) << I_V_EA);
        Write (ea, dat);
        delay = I_delay (opc, ea, op);
        break;

    case OP_U:                                          /* uncond transfer */
        PCQ_ENTRY;
        PC = ea;                                        /* transfer */
        delay = I_delay (opc, ea, op);
        break;

    case OP_T:                                          /* conditional transfer */
        if ((A & SIGN) ||                               /* A < 0 or */
            ((ir & SIGN) && t_switch)) {                /* -T and Tswitch set? */
            PCQ_ENTRY;
            PC = ea;                                    /* transfer */
            }
        delay = I_delay (opc, ea, op);
        break;

/* Arithmetic and logical instructions */

    case OP_A:                                          /* add */
        dat = Read (ea);                                /* get operand */
        res = (A + dat) & DMASK;                        /* add */
        if ((~A ^ dat) & (dat ^ res) & SIGN)            /* calc overflow */
            ovf_this_cycle = TRUE;
        A = res;                                        /* save result */
        delay = I_delay (opc, ea, op);
        break;

    case OP_S:                                          /* sub */
        dat = Read (ea);                                /* get operand */
        res = (A - dat) & DMASK;                        /* subtract */
        if ((A ^ dat) & (~dat ^ res) & SIGN)            /* calc overflow */
            ovf_this_cycle = TRUE;
        A = res;
        delay = I_delay (opc, ea, op);
        break;

    case OP_M:                                          /* multiply high */
        dat = Read (ea);                                /* get operand */
        A = (Mul64 (A, dat, NULL) << 1) & DMASK;        /* multiply */
        delay = I_delay (opc, ea, op);
        break;

    case OP_N:                                          /* multiply low */
        dat = Read (ea);                                /* get operand */
        Mul64 (A, dat, &res);                           /* multiply */
        A = res;                                        /* keep low result */
        delay = I_delay (opc, ea, op);                  /* total delay */
        break;

    case OP_D:                                          /* divide */
        dat = Read (ea);                                /* get operand */
        if (Div32 (A, dat, &A))                         /* divide; overflow? */
            ovf_this_cycle = TRUE;
        delay = I_delay (opc, ea, op);
        break;

    case OP_E:                                          /* extract */
        dat = Read (ea);                                /* get operand */
        A = A & dat;                                    /* and */
        delay = I_delay (opc, ea, op);
        break;

/* IO instructions */

    case OP_P:                                          /* output */
        if (Q_LGP21) {                                  /* LGP-21 */
            ch = A >> 26;                               /* char, 6b */
            if (ir & SIGN)                              /* 4b? convert */
                ch = (ch & 0x3C) | 2;
            dev = I_GETTK (ir);                         /* device select */
            }
        else {                                          /* LGP-30 */
            ch = I_GETTK (ir);                          /* char, always 6b */
            dev = Q_OUTPT? DEV_PT: DEV_TT;              /* device select */
            }
        reason = op_p (dev & DEV_MASK, ch);             /* output */
        delay = I_delay (sim_grtime (), ea, op);        /* next instruction */
        break;

    case OP_I:                                          /* input */
        if (Q_LGP21) {                                  /* LGP-21 */
            ch = 0;                                     /* initial shift */
            sh4 = ir & SIGN;                            /* 4b/6b select */
            dev = I_GETTK (ir);                         /* device select */
            }
        else {                                          /* LGP-30 */
            ch = I_GETTK (ir);                          /* initial shift */
            sh4 = Q_IN4B;                               /* 4b/6b select */
            dev = Q_INPT? DEV_PT: DEV_TT;               /* device select */
            }
        if (dev == DEV_SHIFT)                           /* shift? */
            A = shift_in (A, 0, sh4);                   /* shift 4/6b */
        else reason = op_i (dev & DEV_MASK, ch, sh4);   /* input */
        delay = I_delay (sim_grtime (), ea, op);        /* next instruction */
        break;

    case OP_Z:
        if (Q_LGP21) {                                  /* LGP-21 */
            if (ea & 0xF80) {                           /* no stop? */
                if (((ea & 0x800) && !bp32) ||          /* skip if any */
                    ((ea & 0x400) && !bp16) ||          /* selected switch */
                    ((ea & 0x200) && !bp8) ||           /* is off */
                    ((ea & 0x100) && !bp4) ||           /* or if */
                    ((ir & SIGN) && !OVF))              /* ovf sel and off */
                    PC = (PC + 1) & AMASK;
                if (ir & SIGN)                          /* -Z? clr overflow */
                    OVF = 0;
                }
            else {                                      /* stop */
                lgp21_sov = (ir & SIGN)? 1: 0;          /* pending sense? */
                reason = STOP_STOP;                     /* stop */
                }
            }
        else {                                          /* LGP-30 */
            if (out_done)                               /* P complete? */
                out_done = 0;
            else if (((ea & 0x800) && bp32) ||          /* bpt switch set? */
                ((ea & 0x400) && bp16) ||
                ((ea & 0x200) && bp8) ||
                ((ea & 0x100) && bp4)) ;                /* don't stop or stall */
            else if (out_strt)                          /* P pending? stall */
                reason = STOP_STALL;
            else reason = STOP_STOP;                    /* no, stop */
            }
        delay = I_delay (sim_grtime (), ea, op);        /* next instruction */
        break;                                          /* end switch */
        }

if (ovf_this_cycle) {
    if (Q_LGP21)                                        /* LGP-21? set OVF */
        OVF = 1;
    else reason = STOP_OVF;                             /* LGP-30? stop */
    }
return reason;
}

/* Support routines */

uint32 Read (uint32 ea)
{
return M[ea] & MMASK;
}

void Write (uint32 ea, uint32 dat)
{
M[ea] = dat & MMASK;
return;
}

/* Input shift */

uint32 shift_in (uint32 a, uint32 dat, uint32 sh4)
{
if (sh4)
    return (((a << 4) | (dat >> 2)) & DMASK);
return (((a << 6) | dat) & DMASK);
}

/* 32b * 32b multiply, signed */

uint32 Mul64 (uint32 a, uint32 b, uint32 *low)
{
uint32 sgn = a ^ b;
uint32 ah, bh, al, bl, rhi, rlo, rmid1, rmid2;

if ((a == 0) || (b == 0)) {                             /* zero argument? */
    if (low)
        *low = 0;
    return 0;
    }
a = ABS (a);
b = ABS (b);
ah = (a >> 16) & M16;                                   /* split operands */
bh = (b >> 16) & M16;                                   /* into 16b chunks */
al = a & M16;
bl = b & M16;
rhi = ah * bh;                                          /* high result */
rmid1 = ah * bl;
rmid2 = al * bh;
rlo = al * bl;
rhi = rhi + ((rmid1 >> 16) & M16) + ((rmid2 >> 16) & M16);
rmid1 = (rlo + (rmid1 << 16)) & M32;                    /* add mid1 to lo */
if (rmid1 < rlo)                                        /* carry? incr hi */
    rhi = rhi + 1;
rmid2 = (rmid1 + (rmid2 << 16)) & M32;                  /* add mid2 to to */
if (rmid2 < rmid1)                                      /* carry? incr hi */
    rhi = rhi + 1;
if (sgn & SIGN) {                                       /* result negative? */
    rmid2 = NEG (rmid2);                                /* negate */
    rhi = (~rhi + (rmid2 == 0)) & M32;
    }
if (low)                                                /* low result */
    *low = rmid2;
return rhi & M32;
}

/* 32b/32b divide (done as 32b'0/32b) */

t_bool Div32 (uint32 dvd, uint32 dvr, uint32 *q)
{
uint32 sgn = dvd ^ dvr;
uint32 i, quo;

dvd = ABS (dvd);
dvr = ABS (dvr);
if (dvd >= dvr)
    return TRUE;
for (i = quo = 0; i < 31; i++) {                        /* 31 iterations */
    quo = quo << 1;                                     /* shift quotient */
    dvd = dvd << 1;                                     /* shift dividend */
    if (dvd >= dvr) {                                   /* step work? */
        dvd = (dvd - dvr) & M32;                        /* subtract dvr */
        quo = quo + 1;
        }
    }
quo = (quo + 1) & MMASK;                                /* round low bit */
if (sgn & SIGN)                                         /* result -? */
    quo = NEG (quo);
if (q)                                                  /* return quo */
    *q = quo;
return FALSE;                                           /* no overflow */
}

/* Rotational delay */

uint32 I_delay (uint32 opc, uint32 ea, uint32 op)
{
uint32 tmin = Q_LGP21? min_21[op]: min_30[op];
uint32 tmax = Q_LGP21? max_21[op]: max_30[op];
uint32 nsc, curp, newp, oprp, pcdelta, opdelta;

if (Q_LGP21) {                                          /* LGP21 */
    nsc = NSC_21;                                       /* full rotation delay */
    curp = log_to_phys_21[opc & SCMASK_21];             /* current phys pos */
    newp = log_to_phys_21[PC & SCMASK_21];              /* new PC phys pos */
    oprp = log_to_phys_21[ea & SCMASK_21];              /* ea phys pos */
    pcdelta = (newp - curp + NSC_21) & SCMASK_21;
    opdelta = (oprp - curp + NSC_21) & SCMASK_21;
    }
else {
    nsc = NSC_30;
    curp = log_to_phys_30[opc & SCMASK_30];
    newp = log_to_phys_30[PC & SCMASK_30];
    oprp = log_to_phys_30[ea & SCMASK_30];
    pcdelta = (newp - curp + NSC_30) & SCMASK_30;
    opdelta = (oprp - curp + NSC_30) & SCMASK_30;
    }
if (tmax == 0) {                                        /* skip ea calc? */
    if (pcdelta >= tmin)                                /* new PC >= min? */
        return pcdelta - 1;
    return pcdelta + nsc - 1;
    }
if ((opdelta >= tmin) && (opdelta <= tmax))
    return pcdelta - 1;
return pcdelta + nsc - 1;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
OVF = 0;
inp_strt = 0;
inp_done = 0;
out_strt = 0;
out_done = 0;
lgp21_sov = 0;
delay = 0;
lgp_vm_init ();
pcq_r = find_reg ("CQ", NULL, dptr);
if (pcq_r)
    pcq_r->qptr = 0;
else return SCPE_IERR;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Validate option, must be LGP30 */

t_stat cpu_set_30opt (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (Q_LGP21)
    return SCPE_ARG;
return SCPE_OK;
}

/* Validate input option, must be LGP30 */

t_stat cpu_set_30opt_i (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (Q_LGP21 || (cptr == NULL))
    return SCPE_ARG;
if (strcmp (cptr, "TTI") == 0)
    uptr->flags = uptr->flags & ~UNIT_INPT;
else if (strcmp (cptr, "PTR") == 0)
    uptr->flags = uptr->flags | UNIT_INPT;
else return SCPE_ARG;
return SCPE_OK;
}

/* Validate output option, must be LGP30 */

t_stat cpu_set_30opt_o (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (Q_LGP21 || (cptr == NULL))
    return SCPE_ARG;
if (strcmp (cptr, "TTO") == 0)
    uptr->flags = uptr->flags & ~UNIT_OUTPT;
else if (strcmp (cptr, "PTP") == 0)
    uptr->flags = uptr->flags | UNIT_OUTPT;
else return SCPE_ARG;
return SCPE_OK;
}

/* Set CPU to LGP21 or LPG30 */

t_stat cpu_set_model (UNIT *uptr, int32 val, char *cptr, void *desc)
{
if (val)
    uptr->flags = uptr->flags & ~(UNIT_IN4B|UNIT_INPT|UNIT_OUTPT);
return reset_all (0);
}

/* Show CPU type and all options */

t_stat cpu_show_model (FILE *st, UNIT *uptr, int32 val, void *desc)
{
fputs (Q_LGP21? "LGP-21": "LGP-30", st);
if (uptr->flags & UNIT_TTSS_D)
    fputs (", track/sector", st);
if (uptr->flags & UNIT_LGPH_D)
    fputs (", LGP hex", st);
fputs (Q_MANI? ", manual": ", tape", st);
if (!Q_LGP21) {
    fputs (Q_IN4B? ", 4b": ", 6b", st);
    fputs (Q_INPT? ", in=PTR": ", in=TTI", st);
    fputs (Q_OUTPT? ", out=PTP": ", out=TTO", st);
    }
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
if (vptr != NULL)
    *vptr = Read (addr);
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (addr >= MEMSIZE)
    return SCPE_NXM;
Write (addr, val);
return SCPE_OK;
}

/* Execute */

t_stat cpu_set_exec (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 inst;
t_stat r;

if (cptr) {
    inst = get_uint (cptr, 16, DMASK, &r);
    if (r != SCPE_OK)
        return r;
    }
else inst = IR;
while ((r = cpu_one_inst (PC, inst)) == STOP_STALL) {
    sim_interval = 0;
    if ((r = sim_process_event ()))
        return r;
    }
return r;
}

/* Fill */

t_stat cpu_set_fill (UNIT *uptr, int32 val, char *cptr, void *desc)
{
uint32 inst;
t_stat r;

if (cptr) {
    inst = get_uint (cptr, 16, DMASK, &r);
    if (r != SCPE_OK)
        return r;
    IR = inst;
    }
else IR = A;
return SCPE_OK;
}
