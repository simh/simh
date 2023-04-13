/*
 * Copyright (c) 2023 Anders Magnusson.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <setjmp.h>

#include "sim_defs.h"

#include "nd100_defs.h"

#define MAXMEMSIZE      512*1024
#undef  TIMERHACK

#ifdef TIMERHACK
int rtc_int_enabled, rtc_dev_ready, rtccnt;
void chkrtc(void);
#endif

typedef struct {
        int     ir;
        int16   sts;
        int16   d;
        int16   p;
        int16   b;
        int16   l;
        int16   a;
        int16   t;
        int16   x;
} Hist_entry ;

#define HIST_IR_INVALID -1
#define HIST_MIN        0
#define HIST_MAX        1000000

static  int32   hist_p = 0;
static  int32   hist_cnt = 0;
static  Hist_entry *hist = NULL;
static  struct intr *ilnk[4];   /* level 10-13 */
jmp_buf env;

uint16 R[8], RBLK[16][8], regSTH;
int curlvl;             /* current interrupt level */
int iic, iie, iid;      /* IIC/IIE/IID register */
int pid, pie;           /* PID/PIE register */
int ald, eccr, pvl, lmp;

#define SETC()          (regSTL |= STS_C)
#define CLRC()          (regSTL &= ~STS_C)
#define SETQ()          (regSTL |= STS_Q)
#define CLRQ()          (regSTL &= ~STS_Q)
#define SETO()          (regSTL |= STS_O)
#define CLRO()          (regSTL &= ~STS_O)

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);

t_stat hist_set(UNIT * uptr, int32 val, CONST char * cptr, void * desc);
t_stat hist_show(FILE * st, UNIT * uptr, int32 val, CONST void * desc);
static void hist_fprintf(FILE *fp, int itemNum, Hist_entry *hptr);
static void hist_save(int ir);

static int getoff(int ir);
static int iox_check(int dev);
static int nd_trr(int reg);
static int nd_tra(int reg);
static int nd_mcl(int reg);
static int nd_mst(int reg);
static int highest_level(void);
static void intrpt14(int);
static void identrm(int);
static uint16 add3(uint16 a, uint16 d, uint16 c);
int fls(int);


int ins_store(int ir, int addr);
int ins_stdf(int ir, int addr);
int ins_lddf(int ir, int addr);
int ins_min(int ir, int addr);
int ins_load(int ir, int addr);
int ins_add(int ir, int addr);
int ins_andor(int ir, int addr);
void ins_dnz(int ins);
void ins_nlz(int ins);
int ins_fad(int ir, int addr);
int ins_fsb(int ir, int addr);
int ins_fmu(int ir, int addr);
int ins_fdv(int ir, int addr);
int ins_mpy(int ir, int addr);
int ins_jmpl(int ir, int addr);
int ins_cjp(int ir, int addr);
int ins_skp(int ir, int addr);
int ins_skip_ext(int IR);
int ins_rop(int ir, int addr);
int ins_mis(int IR, int addr);
int ins_sht(int IR, int addr);
int ins_na(int IR, int addr);
int ins_iox(int IR, int addr);
int ins_arg(int IR, int addr);
int ins_bop(int IR, int addr);

#define ID(x)   (((x) & ND_MEMMSK) >> ND_MEMSH)

int (*ins_table[32])(int ir, int addr) = {
        ins_store, ins_store, ins_store, ins_store,     /* STZ/STA/STT/STX */
        ins_stdf, ins_lddf, ins_stdf, ins_lddf,         /* STD/LDD/STF/LDF */
        ins_min, ins_load, ins_load, ins_load,          /* MIN/LDA/LDT/LDX */
        ins_add, ins_add, ins_andor, ins_andor,         /* ADD/SUB/ADN/ORA */
        ins_fad, ins_fsb, ins_fmu, ins_fdv,             /* FAD/FSB/FMU/FDV */
        ins_mpy, ins_jmpl, ins_cjp, ins_jmpl,           /* MPY/JMP/CJP/JPL */
        ins_skp, ins_rop, ins_mis, ins_sht,             /* SKP/ROP/MIS/SHT */
        ins_na, ins_iox, ins_arg, ins_bop               /* --/IOX/ARG/BOP  */
};

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
        { ORDATA(STS, R[0], 16) },
        { ORDATA(D, regD, 16) },
        { ORDATA(P, regP, 16) },
        { ORDATA(B, regB, 16) },
        { ORDATA(L, regL, 16) },
        { ORDATA(A, regA, 16) },
        { ORDATA(T, regT, 16) },
        { ORDATA(X, regX, 16) },
        { DRDATA(LVL, curlvl, 4) },
        { DRDATA(LMP, lmp, 16) },
        { DRDATA(PVL, pvl, 4) },
        { ORDATA(PID, pid, 16) },
        { ORDATA(PIE, pie, 16) },
        { ORDATA(IIC, iic, 4) },
        { ORDATA(IIE, iie, 10) },
        { NULL }
};

MTAB cpu_mod[] = {
        { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
            &hist_set, &hist_show },
        { 0 }
};

DEVICE cpu_dev = {
        "CPU", &cpu_unit, cpu_reg, cpu_mod,
        1, 8, 16, 1, 8, 16,
        &cpu_ex, &cpu_dep, &cpu_reset,
        NULL, NULL, NULL
};

t_stat
sim_instr(void)
{
        int IR;
        int reason;
        int n, i;
        int first = 1;
        uint16 off;

        reason = setjmp(env);

while (reason == 0) {
        if (sim_interval <= 0)
                if ((reason = sim_process_event ()))
                        break;

#ifdef TIMERHACK
        chkrtc();
#endif
        if ((pid & pie) > (0177777 >> (15-curlvl)) && ISION()) {
                /* need to interrupt */
                pvl = curlvl;
                n = highest_level();
                for (i = 0; i < 8; i++) {
                        RBLK[curlvl][i] = R[i];
                        R[i] = RBLK[n][i];
                }
                curlvl = n;
                if (curlvl == 14 && iic == 1) /* mon call */
                        regT = SEXT8(IR);
        }

        IR = rdmem(regP);
        sim_interval--;

        if (first == 0 && sim_brk_summ && sim_brk_test (regP, SWMASK ('E'))) {
                reason = STOP_BP;
                break;
        }
        first = 0;

        if (hist_cnt)
                hist_save(IR);

        /*
         * Execute instruction. We intercept it here before calling
         * the instruction routine and just update IR.
         */
        if (ISEXR(IR)) { /* Execute instruction */
                IR = R[(IR >> 3) & 07];
#ifdef notyet
                if (ISEXR(IR))
                        trap...
#endif
                if (hist_cnt)
                        hist_save(IR);
        }

        if (ID(IR) < ND_CJP || ID(IR) == ND_JPL)
                off = getoff(IR);

        reason = (*ins_table[ID(IR)])(IR, off);
        if (reason == 0)
                regP++;
}
        return reason;
}

t_stat
cpu_reset(DEVICE *dptr)
{
        sim_brk_types = sim_brk_dflt = SWMASK ('E');
        regSTH |= STS_N100;
        return SCPE_OK;
}

t_stat
cpu_ex(t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
        if (addr >= MAXMEMSIZE)
                return SCPE_ARG;
        *vptr = rdmem(addr);
        return SCPE_OK;
}

/* Memory deposit */

t_stat
cpu_dep(t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
        return SCPE_ARG;
}

t_stat
cpu_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
        return SCPE_ARG;
}

t_stat
cpu_boot(int32 unitno, DEVICE *dptr)
{
        return SCPE_ARG;
}

/*
 * Store register.
 */
int
ins_store(int IR, int off)
{
        int n = ((IR >> 11) & 03) + 4;
        wrmem(off, n == 4 ? 0 : R[n]);
        return SCPE_OK;
}

/*
 * Store double or triple reg.
 */
int
ins_stdf(int IR, int off)
{
        if (ID(IR) == ID(ND_STF) /* && 48-bit */)
                wrmem(off++, regT);
        wrmem(off++, regA);
        wrmem(off, regD);
        return SCPE_OK;
}

/*
 * Load double or triple reg.
 */
int
ins_lddf(int IR, int off)
{
        if (ID(IR) == ID(ND_LDF) /* && 48-bit */)
                regT = rdmem(off++);
        regA = rdmem(off++);
        regD = rdmem(off);
        return SCPE_OK;
}

/*
 * Load one reg.
 */
int
ins_load(int IR, int off)
{
        R[((IR&014000) >> 11) + 4] = rdmem(off);
        return SCPE_OK;
}

/*
 * Increment (and skip?).
 */
int
ins_min(int IR, int off)
{
        uint16 s;

        wrmem(off, s = rdmem(off) + 1);
        if (s == 0)
                regP++;
        return SCPE_OK;
}

/*
 * Add/sub.
 */
int
ins_add(int IR, int off)
{
        uint16 d = rdmem(off);
        int n = 0;

        if (ID(IR) == ID(ND_SUB))
                n = 1, d = ~d;
        regA = add3(regA, d, n);
        return SCPE_OK;
}

/*
 * And/or
 */
int
ins_andor(int IR, int off)
{
        uint16 s = rdmem(off);

        regA = BIT11(IR) ? (regA | s) : (regA & s);
        return SCPE_OK;
}

/*
 * Byte instructions (BFILL, MOVB, MOVBF).  Requires at least ND100.
 *
 * Byte operands occupy fields within the memory. Operands are specified
 * by two 16-bit words, known as descriptors, giving the start address
 * and the field length.
 *
 * The start address is in register A if source, X if destination (D1)
 * The rest of the descriptor is in D if source, T if destination (D2)
 *
 * In D2 bits
 *      11-0 are the field length (in bytes).
 *      12 - Ignored
 *      13 - Must be 0
 *      14 - 0 = normal PT, 1 = alternate
 *      15 - 0 = left byte start, 1 = right byte.
 *
 */
#define BYTELN(x)       ((x) & 07777)
/*
 * Byte fill (BFILL).
 * 
 * This instruction has only one operand. The destination operand is
 * specified in the X and T registers. The right-most byte in the A-reg
 * (bits 0-7) is filled into the destination field.
 * After execution, the X-register and T-register bit 15 point to the
 * end of the field (after the last byte). The T-register bits (0-11)
 * equal zero.
 * The instruction will always have a skip return (no error condition).
 */
static void
ins_bfill(int IR)
{
        while (regT & 07777) {
                wrbyte(regX, regA, BIT15(regT));
                regT--;
                regT ^= 0100000;
                if (BIT15(regT) == 0)
                        regX++;
        }
        regP++; /* skip return */
}

/*
 * Move bytes (MOVB)
 *
 * This instruction moves a block of bytes from the location specified for
 * the source operand to the location specified for the destination operand.
 * The move operation takes care of source— and destination-field overlap.
 * The number of bytes moved is determined by the shortest field length of
 * the operands.
 * After execution, the AD and XT registers (bit 15 in D and T) point to
 * the end of the field that is moved (after the last byte). D-reg. bits
 * 0-11 equal zero and T-reg. bits 0-11 contain the number of bytes moved.
 * The T—reg. bits 12—13 and the D-reg. bit 12 are used during the execution,
 * and are left cleared. Bit 13 must be zero before execution (used as an
 * interrupt mark).
 * The instruction will always have a skip return (no error condition).
 *
 * Implementation note: Because this instruction can be interrupted (by a 
 * page fault for example) byte for byte must be moved, and every state
 * will be stored in the registers (similar to the microcode implementation).
 * The usage is:
 *      - BIT13(regD): all regs are setup in the middle of execution.
 */
static void
ins_movb(int IR)
{
        int i;

        if (BIT13(regD) == 0) {
                /* setup for copy */
                int len = BYTELN(regD);
                if (BYTELN(regT) < len)
                        len = BYTELN(regT);
                regT = (regT & 0140000) | len;  /* T is max */
                regD = (regD & 0140000);        /* D is zero */
                regD |= (1 << 13);              /* setup done! */
        }

        if (regX > regA) {      /* copy bottom-top */
                for (i = BYTELN(regD); i < BYTELN(regT); i++, regD++) {
                        int8 w = rdbyte(regA, BIT15(regD));
                        wrbyte(regX, w, BIT15(regT));
                        regD ^= 0100000;
                        if (BIT15(regD) == 0) regA++;
                        regT ^= 0100000;
                        if (BIT15(regT) == 0) regX++;
                }
                regD &= 0140000;        /* Clear setup + count bits */
        } else {        /* copy top-bottom */
                
        }
        regP++; /* skip return */
}

/*
 * This instruction moves a block of bytes from the location specified as
 * the source operand to the location specified as the destination operand.
 * The move operation always starts with the first byte (lower address).
 *
 * The number of bytes moved is determined by the shortest field length of
 * the operands. Forbidden overlap exists when the source data to be moved,
 * will be destroyed. That happens when a byte is stored in a word before
 * that word is read from memory. This is reported by an error return (no skip).
 *
 * After successful execution, the A,D and X,T registers (bit 15 in D and T)
 * point to the end of the fields that are moved (after the last byte).
 * The numbers initially contained in the D- and T-registers, bits 0-11,
 * are decremented by the number of bytes moved.
 *
 * The T—reg. bits 12—13 and the D—reg. bit 12 are used during the execution
 * and are left cleared. Bit 13 must be zero before execution (used as an
 * interrupt mark).  The instruction will have a skip—return when
 * no illegal overlap exists.
 */
static void
ins_movbf(int IR)
{
        int i;

        if (BIT13(regD) == 0) {
                /* setup for copy */
                int len = BYTELN(regD);
                if (BYTELN(regT) < len)
                        len = BYTELN(regT);
                regT = (regT & 0140000) | len;  /* T is max */
                regD = (regD & 0140000);        /* D is zero */
                regD |= (1 << 13);              /* setup done! */
                if (regX > regA && regX < (regA + (len >> 1)))
                        return;
        }

        for (i = BYTELN(regD); i < BYTELN(regT); i++, regD++) {
                int8 w = rdbyte(regA, BIT15(regD));
                wrbyte(regX, w, BIT15(regT));
                regD ^= 0100000;
                if (BIT15(regD) == 0) regA++;
                regT ^= 0100000;
                if (BIT15(regT) == 0) regX++;
        }
        regD &= 0140000;        /* Clear setup + count bits */
        regT &= 0140000;        /* Clear setup + count bits */
        regP++; /* skip return */
}

/*
 * Instructions with the same encoding as skip instructions.
 */
int
ins_skip_ext(int IR)
{
        uint16 d;
        int16 ss, sd;
        int32 shc;
        int reason = 0;

        if ((IR & 0177707) == ND_SKP_CLEPT) {
                intrpt14(IIE_II);
                regP--;
        } else if (IR == ND_SKP_MIX3) {
                /* X = (A-1)*3 */
                regX = (regA-1)*3;
        } else if (IR == ND_SKP_IDENT10) {
                identrm(10);
        } else if (IR == ND_SKP_IDENT11) {
                identrm(11);
        } else if (IR == ND_SKP_IDENT12) {
                identrm(12);
        } else if (IR == ND_SKP_IDENT13) {
                identrm(13);
        } else if (IR == ND_SKP_BFILL)
                ins_bfill(IR);
        else if (IR == ND_SKP_MOVB)
                ins_movb(IR);
        else if (IR == ND_SKP_MOVBF)
                ins_movbf(IR);
        else if (IR == ND_SKP_LBYT) {
                d = rdmem(regT + (regX >> 1));
                if (regX & 1)
                        regA = d & 0377;
                else
                        regA = d >> 8;
        } else if (IR == ND_SKP_SBYT) {
                d = regT + (regX >> 1);
                if (regX & 1)
                        wrmem(d, (rdmem(d) & 0xff00) | (regA & 0xff));
                else
                        wrmem(d, (rdmem(d) & 0xff) | (regA << 8));
        } else if ((IR & 0177700) == ND_SKP_RMPY) {
                ss = R[(IR & 070) >> 3];
                sd = R[IR & 07];
                shc = ss * sd;
                regD = shc;
                regA = shc >> 16;
        } else if ((IR & 0177700) == ND_SKP_RDIV) {
                ss = R[(IR & 070) >> 3];
                shc = (regA << 16) | regD;
                regA = shc / ss;
                regD = shc % ss;
        } else if (IR == 0142700) {
                /* ??? what is this? */
                intrpt14(IIE_II);
                regP--;
        } else
                reason = STOP_UNHINS;
        return reason;
}

/*
 * The instruction SRB <level * 8> stores the contents of the register
 * block on the program level specified in the level field of
 * the instruction. The specified register block is stored in
 * succeeding memory locations starting at the location specified by
 * the contents of the X register.
 * If the current program level is specified, the stored P register points
 * to the instruction following SRB.
 *
 * Affected: (EL), +1 +2 +3 +4 +5 +6 +7 
 *             P    X  T  A  D  L STS B
 */
static int s2r[] = { rnP, rnX, rnT, rnA, rnD, rnL, rnSTS, rnB };
static void
ins_srb(int IR)
{
        int i, n = (IR >> 3) & 017;

        /* Save current level (maybe used) to reg block */
        for (i = 0; i < 8; i++)
                RBLK[curlvl][i] = R[i];
        RBLK[curlvl][rnP]++;    /* following insn */

        /* store requested block to memory */
        for (i = 0; i < 8; i++)
                wrmem(regX+i, RBLK[n][s2r[i]]);
}

/*
 * The instruction LRB <level * 8> loads the contents of the register
 * block on program level specified in the level field of the instruction.
 * The specified register block is loaded by the contents of succeeding
 * memory locations starting at the location specified by the contents
 * of the X register.
 * If the current program level is specified, the P register is not affected.
 * Affected: All the registers on specified program level are affected.
 * Note: If the current level is specified, the P register is not affected. 
 */
static void             
ins_lrb(int IR)         
{
        int i, n = (IR >> 3) & 017;

        /* fetch from memory */
        for (i = 0; i < 8; i++)
                RBLK[n][s2r[i]] = rdmem(regX+i);
        RBLK[n][rnSTS] &= 0377;

        if (n == curlvl)
                for (i = 0; i < 8; i++)
                        if (i != rnP)
                                R[i] = RBLK[n][i];
}

/*
 * Miscellaneous instructions.
 */
int
ins_mis(int IR, int off)
{
        int reason = 0;
        int n, i;

        if (IR == ND_MIS_SEX)
                regSTH |= STS_SEXI;
        else if (IR == ND_MIS_DEPO) {
                if ((n = ((regA << 16) | regD)) <= MAXMEMSIZE)
                        PM[n] = regT;
        } else if (IR == ND_MIS_EXAM) {
                if ((n = ((regA << 16) | regD)) <= MAXMEMSIZE)
                        regT = PM[n];
        } else if (IR == ND_MIS_REX) {
                regSTH &= ~STS_SEXI;
        } else if (IR == ND_MIS_PON) {
                regSTH |= STS_PONI;
        } else if (IR == ND_MIS_POF) {
                regSTH &= ~STS_PONI;
        } else if (IR == ND_MIS_ION) {
                regSTH |= STS_IONI;
        } else if (IR == ND_MIS_IOF) {
                regSTH &= ~STS_IONI;
        } else if (IR == ND_MIS_PIOF) {
                regSTH &= ~(STS_IONI|STS_PONI);
        } else if (IR == ND_MIS_IOXT) {
                reason = iox_check(regT);
        } else if ((IR & ND_MIS_TRMSK) == ND_MIS_TRA)
                reason = nd_tra(IR & ~ND_MIS_TRMSK);
        else if ((IR & ND_MIS_TRMSK) == ND_MIS_TRR)
                reason = nd_trr(IR & ~ND_MIS_TRMSK);
        else if ((IR & ND_MIS_TRMSK) == ND_MIS_MST)
                reason = nd_mst(IR & ~ND_MIS_TRMSK);
        else if ((IR & ND_MIS_TRMSK) == ND_MIS_MCL)
                reason = nd_mcl(IR & ~ND_MIS_TRMSK);
        else if ((IR & ND_MIS_IRRMSK) == ND_MIS_IRR) {
                n = (IR >> 3) & 017;
                if (n == curlvl)
                        regA = R[IR & 07];
                else
                        regA = RBLK[n][IR & 07];
        } else if ((IR & ND_MIS_IRRMSK) == ND_MIS_IRW) {
                n = (IR >> 3) & 017;
                RBLK[n][IR & 07] = regA;
                if (n == curlvl && (IR & 07) != rnP)
                        R[IR & 07] = regA;
        } else if ((IR & ND_MIS_RBMSK) == ND_MIS_SRB) {
                ins_srb(IR);
        } else if ((IR & ND_MIS_RBMSK) == ND_MIS_LRB) {
                ins_lrb(IR);
        } else if ((IR & ND_MONMSK) == ND_MON) {
                RBLK[14][rnT] = SEXT8(IR);
                intrpt14(IIE_MC);
        } else if ((IR & ND_MONMSK) == ND_WAIT) {
                if (ISION() == 0) {
                        regP++;
                        reason = STOP_WAIT;
                } else if (curlvl > 0) {
                        pid &= ~(1 << curlvl);
                        n = highest_level();
                        if (curlvl != n) {
                                regP++;
                                for (i = 0; i < 8; i++) {
                                        RBLK[curlvl][i] = R[i];
                                        R[i] = RBLK[n][i];
                                }
                                regP--;
                        }
                        curlvl = n;
                }
        } else if ((IR & ND_MONMSK) == ND_MIS_NLZ) {
                /* convert integer to floating point */
                ins_nlz(IR);
        } else if ((IR & ND_MONMSK) == ND_MIS_DNZ) {
                ins_dnz(IR);
        } else
                reason = STOP_UNHINS;
        return reason;
}

int
ins_sht(int IR, int off)
{
        char sht_reg[] = { rnT, rnD, rnA, 0 };
        int m, n, rs, i;
        uint32 ushc;

        rs = sht_reg[(IR >> 7) & 03];
        n = BIT5(IR) ? 32 - IR & 037 : IR & 037;
        m = BIT7(regSTL);

        ushc = rs ? R[rs] : (regA << 16) | regD;
        if (BIT5(IR)) { /* right */
                int mm = BIT0(ushc);
                for (i = 0; i < n; i++) {
                        m = BIT0(ushc);
                        ushc = ushc >> 1;
                        switch (IR & 03000) {
                        case 0: /* Arithmetic */
                                ushc |= (rs ? (BIT14(ushc) << 15) :
                                    (BIT30(ushc) << 31));
                                break;
                        case 01000: /* ROT */
                                ushc |= (rs ? (m << 15) :
                                    (m << 31));
                                break;
                        case 02000: /* zero end input */
                                break;
                        case 03000: /* link end input */
                                ushc |= (rs ? (mm << 15) :
                                    (mm << 31));
                                break;
                        }
                }
        } else { /* left */
                int mm = (rs ? BIT15(ushc) : BIT31(ushc));
                for (i = 0; i < n; i++) {
                        m = (rs ? BIT15(ushc) : BIT31(ushc));
                        ushc = ushc << 1;
                        switch (IR & 03000) {
                        case 01000: /* ROT */
                                ushc |= m; break;
                        case 0: /* Arithmetic */
                        case 02000: /* zero end input */
                                break;
                        case 03000: /* link end input */
                                ushc |= mm; break;
                        }
                }
        }
        regSTL = (regSTL & ~STS_M) | (m << 7);
        if (rs == 0) {
                regA = ushc >> 16;
                regD = ushc;
        } else
                 R[rs] = ushc;
        return SCPE_OK;
}

int
ins_na(int IR, int addr)
{
        return STOP_UNHINS;
}

int
ins_iox(int IR, int addr)
{
        return iox_check(IR & ND_IOXMSK);
}

int
ins_arg(int IR, int addr)
{
        int rs, n = (IR >> 8) & 03;

        rs = n ? n + 4 : 3;
        R[rs] = add3(BIT10(IR) ? R[rs] : 0, SEXT8(IR), 0);
        return SCPE_OK;
}

int
ins_bop(int IR, int addr) 
{                       

        int rd, n, reason = 0;

        rd = IR & 7;
        n = (IR >> 3) & 017;
        switch ((IR >> 7) & 017) {
        case 000: /* BSET zero/one */
                R[rd] &= ~(1 << n);
                break;

        case 001: 
                R[rd] |= (1 << n);
                break;

        case 002: /* BSET BCM bit = ~bit */
                R[rd] ^= (1 << n);
                break;

        case 003: /* BSET BAC bit = K */
                R[rd] &= ~(1 << n);
                if (regSTL & STS_K)
                        R[rd] |= (1 << n);
                break;

        case 004: /* BSKP zero/one */
        case 005: 
                if (((R[rd] >> n) & 1) == BIT7(IR))
                        regP++;
                break;

        case 006: /* BSKP BCM K == ~bit */
                if (((regSTL & STS_K) != 0) ^ (((R[rd] >> n) & 1) != 0))
                        regP++;
                break;


        case 010: /* BSTA store ~K and set K */
                R[rd] &= ~(1 << n);
                if ((regSTL & STS_K) == 0)
                        R[rd] |= (1 << n);
                regSTL |= STS_K;
                break;

        case 011: /* BSTA store K and clear K */
                R[rd] &= ~(1 << n);
                if (regSTL & STS_K)
                        R[rd] |= (1 << n);
                regSTL &= ~STS_K;
                break;

        case 012: /* BLDA load ~K */
                regSTL &= ~STS_K;
                if (((R[rd] >> n) & 1) == 0)
                        regSTL |= STS_K;
                break;
                        
        case 013: /* BLDA load K */
                regSTL &= ~STS_K;
                if ((R[rd] >> n) & 1)
                        regSTL |= STS_K;
                break;

        case 014: /* BANC K = ~bit & K */
                if (regSTL & STS_K) {
                        if ((R[rd] >> n) & 1)
                                regSTL &= ~STS_K;
                }
                break;

        case 015: /* BAND K = bit & K */
                if (regSTL & STS_K) {
                        if (((R[rd] >> n) & 1) == 0)
                                regSTL &= ~STS_K;
                }
                break;

        case 016: /* BORC K = ~bit | K */
                if ((regSTL & STS_K) == 0) {
                        if (((R[rd] >> n) & 1) == 0)
                                regSTL |= STS_K;
                }
                break;

        case 017: /* BORA K = bit | K */
                if ((regSTL & STS_K) == 0) {
                        if ((R[rd] >> n) & 1)
                                regSTL |= STS_K;
                }
                break;

        default:
                reason = STOP_UNHINS;
                break;
        }
        return reason;
}

/*
 * 48-bit floating point. T has the sign and exponent, A has the most
 * significant bits and D has the least.
 * Exponent is biased 16384, mantissa is 0.5 <= X < 1.0.
 */
struct fp {
        int s;
        int e;
        t_uint64 m;
};

static void
mkfp48(struct fp *fp, uint16 w1, uint16 w2, uint16 w3)
{
        fp->s = BIT15(w1);
        fp->e = (w1 & 077777) - 16384;
        fp->m = ((t_uint64)w2 << 16) + (t_uint64)w3;
}

/*
 * Description of DNZ instruction from the NORD-10 manual:
 *
 * Converts the floating number in the floating accumulator to a
 * single precision fixed point number in the A register, using the
 * scaling of the DNZ instruction as a scaling factor.
 *
 * When converting to integers, the scaling factor should be —16, a
 * greater scaling factor will cause the fixed point number to be
 * greater. After this instruction the contents of the T and D registers
 * will all be zeros.
 *
 * If the conversion causes underflow, the T, A and D registers will all
 * be set to zero.  If the conversion causes overflow, the error
 * indicator 2 is set to one. Overflow occurs if the resulting integer
 * in absolute value is greater than 32767.
 * The conversion will truncate and negative numbers are converted to
 * positive numbers before conversion. The result will again be
 * converted to a negative number.
 */
void
ins_dnz(int ins)
{
        int32 val = 0; /* XXX remove warnings */
        int sh;

        sh = (regT & 077777) - 16384 + SEXT8(ins);
        if (sh < 0) {
                val = regA;
                val >>= -sh;
        } else {
                
        }
#ifdef notyet
        if (val > 32767)
                z = 1;
#endif
        if (regT & 0100000)
                val = -val;
        regT = regD = 0;
        regA = (uint16)val;
}

/*
 * Description of DNZ instruction from the NORD-10 manual:
 *
 * Converts the number in the A register to a standard form floating
 * number in the floating accumulator, using the scaling of the NLZ
 * instruction as a scaling factor. For integers, the scaling factor
 * should be +16, a larger scaling factor will result in a higher floating,
 * point number. Because of the single precision fixed point number,
 * the D register will be cleared. 
 */
void
ins_nlz(int ins)
{
        int sh, s;
        int val;

        s = 0;
        regD = 0;
        if (regA == 0) { /* zero, special case */
                regT = 0;
                return;
        }

        val = (int)(int16)regA;
        sh = 16384 + SEXT8(ins);
        if (val < 0)
                val = -val, s = 0100000;
        if (val > 32767)
                val >>= 1, sh++;
        while ((val & 0100000) == 0) {
                val <<= 1;
                sh--;
        }
        regT = sh + s;
        regA = val;
}

/*
 * Multiplication of two 48-bit floating point numbers.
 *
 * The contents of the floating accumulator are multiplied with the
 * number of the effective floating word locations with the result in
 * the floating accumulator. The previous setting of the carry and overflow
 * indicators are lost.
 * Affected: (T), (A), (D), O, Q, TG
 */
int
ins_fmu(int IR, int addr)
{
        struct fp f1, f2;
        int s3, e3;
        t_uint64 m3;

        /* Fetch from memory */
        mkfp48(&f1, rdmem(addr), rdmem(addr+1), rdmem(addr+2));

        /* From registers */
        mkfp48(&f2, regT, regA, regD);

        /* calc */
        m3 = f1.m * f2.m;
        e3 = f1.e + f2.e;
        s3 = f1.s ^ f2.s;

        /* normalize (if needed) */
        if ((m3 & (1ULL << 63)) == 0) {
                m3 <<= 1;
                e3--;
        }

        /* restore regs */
        regA = (uint16)(m3 >> 48);
        regD = (uint16)(m3 >> 32);
        regT = (e3 + 16384) | (s3 << 15);
        if (m3 == 0 || e3 < -16383)
                regT = regA = regD = 0;
        return SCPE_OK;
}

/*
 * Division of two 48-bit floating point numbers.
 *
 * The contents of the floating accumulator are divided by the number
 * in the effective floating word locations. Result in floating
 * accumulator. If division by zero is attempted, the error indicator 2
 * is set to one. The error indicator 2 may be sensed by a BSKP instruc-
 * tion (see BOP). The previous setting of the carry and overflow
 * indicators are lost.
 * Affected: (T), (A), (D), Z, C, O, Q, TG
 */
int
ins_fdv(int IR, int addr)
{
        struct fp f1, f2;
        int s3, e3;
        t_uint64 m3;

        /* Fetch from memory */
        mkfp48(&f1, rdmem(addr), rdmem(addr+1), rdmem(addr+2));

        /* From registers */
        mkfp48(&f2, regT, regA, regD);
        f2.m <<= 32;

        /* Initial sanity */
        if (f1.m == 0) {
                regSTL |= STS_Z;
                regT |= 077777;
                regA = regD = 0177777;
                intrpt14(IIE_V);
                return SCPE_OK;
        }

        /* calc */
        s3 = f1.s ^ f2.s;
        e3 = f2.e - f1.e;
        m3 = f2.m / f1.m;
        if (f2.m%f1.m)  /* "guard" bit */
                m3++;

        /* normalize (if needed) */
        if (m3 >= (1ULL << 32)) {
                m3 >>= 1;
                e3++;
        }

        /* restore regs */
        regA = (uint16)(m3 >> 16);
        regD = (uint16)m3;
        regT = (e3 + 16384) | (s3 << 15);
        if (f2.m == 0 || e3 < -16383)
                regT = regA = regD = 0;
        return SCPE_OK;
}

/*
 * Add two 48-bit numbers.
 *
 * The contents of the effective location and the two following locations
 * are added to the floating accumulator with the result in the floating
 * accumulator. The previous setting of the carry and overflow indicators
 * are lost.
 * Affected: (T), (A), (D), C, O, Q, TG
 */
static void
add48(struct fp *f1, struct fp *f2)
{
        struct fp *ft;
        t_uint64 m3;
        int scale, gbit;

        /* Ensure f1 is larger */
        if (f2->e > f1->e)
                ft = f1, f1 = f2, f2 = ft;

        if ((scale = f1->e - f2->e) > 31) {
                m3 = f1->m;
                goto done;
        }

        /* get shifted out guard bit */
        gbit = scale ? (((1LL << scale)-1) & f2->m) != 0 : 0;
        f2->m >>= scale;
        m3 = (f1->m + f2->m) | gbit;
        if (m3 > 0xffffffffLL) {
                m3 >>= 1;
                f1->e++;
        }

done:   regT = (f1->e + 16384) | (f1->s << 15);
        regA = (uint16)(m3 >> 16);
        regD = (uint16)m3;
}

/* 
 * Subtract two 48-bit numbers.
 * The numbers are of different sign.
 *
 * The contents of the effective location and the two following locations
 * are subtracted from the floating accumulator with the result
 * in the floating accumulator. The previous setting of the carry and
 * overflow indicators are lost.
 * Affected: (T), (A), (D), C, O, Q, TG 
 */
static void
sub48(struct fp *f1, struct fp *f2, int addr)
{
        struct fp *ft;
        t_uint64 m3;
        int scale, gbit;

        /* Ensure f1 is bigger */
        if (f2->e > f1->e)
                ft = f1, f1 = f2, f2 = ft;

        if ((scale = f1->e - f2->e) > 31) {
                m3 = f1->m;
                goto done;
        }

        /* get shifted out sticky bit */
        gbit = scale ? (((1LL << scale)-1) & f2->m) != 0 : 0;
        f2->m >>= scale;
        f2->e = f1->e;
        /* check for swap of mantissa */
        if (f2->m > f1->m)
                ft = f1, f1 = f2, f2 = ft;
        m3 = (f1->m - f2->m) | gbit;

        if (m3 == 0) {
                regT = regA = regD = 0;
                return;
        }
        while ((m3 & 0x80000000LL) == 0){
                m3 <<= 1;
                f1->e--;
        }

done:   regT = (f1->e + 16384) | (f1->s << 15);
        regA = (uint16)(m3 >> 16);
        regD = (uint16)m3;
}

int
ins_fad(int IR, int addr)
{       
        struct fp f1, f2;

        mkfp48(&f1, rdmem(addr), rdmem(addr+1), rdmem(addr+2));
        mkfp48(&f2, regT, regA, regD);

        if (f1.s ^ f2.s)
                sub48(&f1, &f2, addr);
        else
                add48(&f1, &f2);
        return SCPE_OK;
}

int
ins_fsb(int IR, int addr)
{       
        struct fp f1, f2;

        mkfp48(&f1, rdmem(addr), rdmem(addr+1), rdmem(addr+2));
        mkfp48(&f2, regT, regA, regD);

        f1.s ^= 1; /* swap sign of right op */

        if (f1.s ^ f2.s)
                sub48(&f1, &f2, addr);
        else
                add48(&f1, &f2);
        return SCPE_OK;
}

/*
 * Add three numbers, setting overflow and carry as needed.
 */
uint16
add3(uint16 a, uint16 d, uint16 c)
{
        int32 res;

        CLRC();
        CLRQ();
        res = a + d + c;;

        if (res > 0177777)
                SETC();
        if (((a ^ d) & 0100000) == 0) {
                /* sign bits equal */
                if ((a ^ res) & 0100000)
                        SETQ(), SETO(); /* result sign differ */
        }
        return res;
}

/*
 * Multiply A with memory.  Set flags.
 */
int
ins_mpy(int IR, int off)
{
        int res = (int)(int16)regA * (int)(int16)rdmem(off);
        regA = res;
        regSTL &= ~STS_Q;
        if (res > 32767)
                regSTL |= (STS_Q|STS_O);
        return SCPE_OK;
}

/*
 * Jump or jump and link.
 */
int
ins_jmpl(int IR, int off)
{
        if (BIT12(IR))
                regL = regP+1;
        regP = off-1;
        return SCPE_OK;
}
/*
 * Conditional jump.
 */
int
ins_cjp(int IR, int off)
{
        char cjpmsk[] = { 01, 01, 02, 02, 05, 05, 02, 01 };
        int n, i;
        uint16 s;

        n = (IR & ND_CJPMSK) >> ND_CJPSH;
        if (cjpmsk[n] & 04)
                regX++;
        s = n & 04 ? regX : regA;
        i = (SEXT8(IR)-1);
        if (cjpmsk[n] & 01) {           /* pos/neg */
                if (BIT8(IR) == BIT15(s))
                        regP += i;
        } else if (cjpmsk[n] & 02) {    /* zero/filled */
                if (BIT8(IR) == !!s)
                        regP += i;
        }
        return SCPE_OK;
}

int
ins_skp(int IR, int off)
{
        int c_o, shc, n, rv = SCPE_OK;
        uint16 s, d;

        if (IR & 0300) { /* extended instructions */
                rv = ins_skip_ext(IR);
        } else {
                s = ~(IR & 070 ? R[(IR & 070) >> 3] : 0);
                d = IR & 07 ? R[IR & 07] : 0;
                shc = d + s + 1;
                c_o = (!BIT15(s ^ d) && BIT15(d ^ shc));

                switch ((IR >> 8) & 03) {
                case 0: /* eql */ n = ((shc & 0177777) == 0); break;
                case 1: /* geq */ n = !BIT15(shc); break;
                case 2: /* gre */ n = ((BIT15(shc) ^ c_o) == 0); break;
                case 3: /* mgre */ n = (shc > 0177777); break;
                }
                if (BIT10(IR)) n = !n;
                if (n) regP++;
        }
        return rv;
}

int
ins_rop(int IR, int off)
{
        int n, rs, rd;
        uint16 s, d;

        rs = (IR & 070) >> 3;
        rd = IR & 07;
        s = rs ? R[rs] : 0;
        d = rd ? R[rd] : 0;
        if (rs == 2) s++;
        if (rd == 2) d++;
        if (BIT6(IR)) d = 0;    /* clear dest */
        if (BIT7(IR)) s = ~s;   /* complement src */
        if (BIT10(IR)) { /* add source to destination */
                n = 0;
                if (BIT8(IR)) n = 1;
                else if (BIT9(IR)) n = BIT6(regSTL);
                d = add3(s, d, n);
        } else { /* logical instructons */
                if (BIT8(IR)) {
                        if (BIT9(IR)) d |= s;
                        else d &= s;
                } else {
                        if (BIT9(IR) == 0) { /* swap */
                                if (rs) R[rs] = d;
                                d = s;
                        } else
                                d ^= s;
                }
        }
        if (rd) R[rd] = d;
        if (rd == 2) regP--;
        return SCPE_OK;
}

/*
 * Look for some device at dev.
 */
static int
iox_check(int dev)
{
#ifdef TIMERHACK
        if ((dev & 03777) == 011) {
                rtccnt = 0;
                return SCPE_OK;
        }
        if ((dev & 03777) == 013) {
                rtc_int_enabled = regA & 1;
                if ((regA >> 13) & 1)
                        rtc_dev_ready = 0;
                return SCPE_OK;
        }
#else
        if ((dev & 0177774) == 010)
                return iox_clk(dev);
#endif
        if ((dev & 0177770) == 0300)
                return iox_tty(dev);
        if ((dev & 0177770) == 01560)
                return iox_floppy(dev);

        intrpt14(IIE_IOX);

        return SCPE_OK;

}

static int
getoff(int ir)
{
        int ea;

        ea = BIT8(ir) ? regB : regP;
        if (BIT10(ir) & !BIT9(ir))
                ea = 0;
        ea += SEXT8(ir);
        if (BIT9(ir))
                ea = rdmem(ea);
        if (BIT10(ir))
                ea += regX;
        return ea;
}

/*
 * bit-clear value in internal reg.
 */
static int
nd_mcl(int reg)
{
        int reason = SCPE_OK;

        switch (reg) {
        case IR_STS:
                regSTL &= ~(regA & 0377);
                break;
        case 006: /* PID */
                pid &= ~regA;
                break;

        case 007: /* PIE */
                pie &= ~regA;
                break;

        default:
                reason = STOP_UNHINS;
        }
        return reason;
}

/*
 * Or-set value of internal reg.
 */
static int
nd_mst(int reg)
{
        int reason = SCPE_OK;

        switch (reg) {
        case IR_STS:
                regSTL |= (regA & 0377);
                break;
        case 006: /* PID */
                pid |= regA;
                break;
        case 007: /* PIE */
                pie |= regA;
                break;
                
        default:
                reason = STOP_UNHINS;
        }
        return reason;
}

/*
 * Set value of internal reg.
 */
static int
nd_trr(int reg)
{
        int reason = SCPE_OK;

        switch (reg) {
        case IR_STS:
                regSTL = regA & 0377;
                break;
        case IR_LMP: /* lamp reg */
                lmp = regA;
                break;
        case IR_PCR: /* PCR (paging control register) */
                mm_wrpcr();
                break;
        case 005: /* IIE */
                iie = regA & 02776;
                break;
        case 006: /* PID */
                pid = regA;
                break;
        case 007: /* PIE */
                pie = regA;
                break;
        case IR_ECCR:
                eccr = regA;
                break;
        default:
                reason = STOP_UNHINS;
        }
        return reason;
}

/*
 * Read value of internal reg.
 */
static int
nd_tra(int reg)
{
        int reason = SCPE_OK;

        switch (reg) {
        case IR_STS: /* STS */
                regA = regSTL | regSTH | (curlvl << 8);
                break;
        case IR_PGS: /* 3 */
                regA = 0; /* XXX */
                break;
        case IR_PVL: /* 4 */
                regA = pvl;
                break;
        case IR_IIC:    /* read IIC.  Clears IIC and IID */
                regA = iic;
                iic = iid = 0;
                pid &= ~(1 << 14);
                break;
        case IR_PID:    /* 6 */
                regA = pid;
                break;
        case IR_PIE:
                regA = pie;
                break;
        case IR_CSR: /* CCR */
                regA = 04; /* cache disabled */
                break;
        case 012: /* ALD */
                regA = ald;
                break;
        case 013: /* PES */
                regA = 0; /* XXX */
                break;
        case 014: /* read back PCR */
                mm_rdpcr();
                break;
        case 015: /* */
                regA = 0; /* XXX */
                break;
        default:
                reason = STOP_UNHINS;
        }
        return reason;
}

int
highest_level(void)
{
        int i, d = pid & pie;

        for (i = 15; i >= 0; i--)
                if (d & (1 << i))
                        return i;
        return 0;
}

/*
 * Find last bit set.
 */
int
fls(int msk)
{
        int i, j;

        if (msk == 0)
                return -1;
        for (i = 15, j = 0100000; i >= 0; i--, j >>= 1)
                if (j & msk)
                        return i;
        return -1;
}

/*
 * Post an internal interrupt for the given source.
 */
void
intrpt14(int src)
{
        iid |= src;     /* set detect flipflop */
        for (iic = 0; (src & 1) == 0; iic++, src >>= 1)
                ;

        if (iid & iie)  /* if internal int enabled, post priority int */
                pid |= (1 << 14);
}

void
extint(int lvl, struct intr *ip)
{
        pid |= (1 << lvl);
        lvl -= 10;
        if (ip->inuse)
                return;
        ip->inuse = 1;
        ip->next = ilnk[lvl];
        ilnk[lvl] = ip;
}

/*
 * Fetch ident from interrupting device.
 * If no device interrupting, post IOX error.
 */
void
identrm(int id)
{
        struct intr *ip = ilnk[id-10];

        if (ip == 0) {
                intrpt14(IIE_IOX);
        } else {
                regA = ip->ident;
                ilnk[id-10] = ip->next;
                ip->next = NULL;
                ip->inuse = 0;
                if (ilnk[id-10] == 0)
                        pid &= ~(1 << id);
        }
}

#ifdef TIMERHACK
struct intr rtc_intx = { 0, 1 };
void
chkrtc(void)
{
        if (rtccnt++ < 5001)
                return;
        rtccnt = 0;
        rtc_dev_ready = 1;
        if (rtc_int_enabled) {
                extint(13, &rtc_intx);
        }
}
#endif

static void
hist_save(int ir)
{
        Hist_entry *hist_ptr;

        if (hist == NULL || hist_cnt == 0)
                return;
        if (++hist_p == hist_cnt)
                hist_p = 0;
        hist_ptr = &hist[hist_p];

        hist_ptr->ir = ir;
        hist_ptr->sts = regSTL | regSTH | (curlvl << 8);
        hist_ptr->d = R[1];
        hist_ptr->p = R[2];
        hist_ptr->b = R[3];
        hist_ptr->l = R[4];
        hist_ptr->a = R[5];
        hist_ptr->t = R[6];
        hist_ptr->x = R[7];
}

t_stat
hist_set(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
        int32 i, lnt;
        t_stat r;

        if (cptr == NULL) {
                for (i = 0 ; i < hist_cnt ; i++)
                        hist[i].ir = HIST_IR_INVALID;
                hist_p = 0;
                return SCPE_OK;
        }

        lnt = (int32) get_uint(cptr, 10, HIST_MAX, &r);
        if ((r != SCPE_OK) || (lnt && (lnt < HIST_MIN)))
                return SCPE_ARG;

        hist_p = 0;
        if (hist_cnt) {
                free(hist);
                hist_cnt = 0;
                hist = NULL;
        }

        if (lnt) {
                hist = (Hist_entry *) calloc(lnt, sizeof(Hist_entry));
                if (hist == NULL)
                        return SCPE_MEM;
                hist_cnt = lnt;
        }
        return SCPE_OK;
}

static void
hist_fprintf(FILE *fp, int itemNum, Hist_entry *hptr)
{
        t_value val;

        if (hptr == NULL)
                return;
        if (itemNum == 0)
                fprintf(fp, "\n\n");
        fprintf(fp, "%06o: IR=%06o STS=%06o D=%06o B=%06o "
            "L=%06o A=%06o T=%06o X=%06o ",
            hptr->p & 0177777, hptr->ir & 0177777, hptr->sts & 0177777,
            hptr->d & 0177777, hptr->b & 0177777, hptr->l & 0177777,
            hptr->a & 0177777, hptr->t & 0177777, hptr->x & 0177777);
        val = hptr->ir;
        fprint_sym(fp, hptr->p, &val, 0, SWMASK ('M'));
        fprintf(fp, "\n");
}

static void
ioxprint(FILE *fp, Hist_entry *hptr, int ioaddr)
{
        fprintf(fp, "%06o: iox(%06o) %s A=%06o\n",
            hptr->p, ioaddr & 0177777, ioaddr & 1 ? "out" : "in ",
            hptr->a & 0177777);
}

t_stat
hist_show(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
        int32 k, di, lnt;
        CONST char *cptr = (CONST char *) desc;
        t_stat r;
        Hist_entry *hptr;

        if (hist_cnt == 0)
                return SCPE_NOFNC;

        if (cptr) { /*  number of entries specified  */
                lnt = (int32)get_uint(cptr, 10, hist_cnt, &r);
                if ((r != SCPE_OK) || (lnt == 0))
                        return SCPE_ARG;
        } else
                lnt = hist_cnt;

        di = hist_p - lnt;
        if (di < 0)
                di = di + hist_cnt;

        for (k = 0 ; k < lnt ; ++k) {
                hptr = &hist[(++di) % hist_cnt];
                if (sim_switches & SWMASK('I')) {
                        /* print iox instructions */
                        if ((hptr->ir & ND_MEMMSK) == ND_IOX)
                                ioxprint(st, hptr, hptr->ir & ~ND_MEMMSK);
                        if (hptr->ir == ND_MIS_IOXT)
                                ioxprint(st, hptr, hptr->t);
                } else {
                        if (hptr->ir != HIST_IR_INVALID)
                                hist_fprintf(st, k, hptr);
                }
        }
        return SCPE_OK;
}

