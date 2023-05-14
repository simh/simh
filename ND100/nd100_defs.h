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

/*
 * masks for instruction matching.
 */
#define ND_MEMMSK       0174000
#define ND_MEMSH        11
#define ND_CJPMSK       0003400
#define ND_CJPSH        8
#define ND_IOXMSK       0003777
#define ND_BOPSH        7
#define ND_BOPMSK       0003600
#define ND_ROPMSK       0177700


/* mem instructions args */
#define NDMEM_B         00400
#define NDMEM_I         01000
#define NDMEM_X         02000
#define NDMEM_OMSK       0377

/*
 * Major group of instructions (ND10 + ND100)
 * All up to CJP (+JPL) uses the memory address syntax.
 */

#define ND_STZ  0000000
#define ND_STA  0004000
#define ND_STT  0010000
#define ND_STX  0014000
#define ND_STD  0020000
#define ND_LDD  0024000
#define ND_STF  0030000
#define ND_LDF  0034000
#define ND_MIN  0040000
#define ND_LDA  0044000
#define ND_LDT  0050000
#define ND_LDX  0054000
#define ND_ADD  0060000
#define ND_SUB  0064000
#define ND_AND  0070000
#define ND_ORA  0074000
#define ND_FAD  0100000
#define ND_FSB  0104000
#define ND_FMU  0110000
#define ND_FDV  0114000
#define ND_MPY  0120000
#define ND_JMP  0124000
#define ND_CJP  0130000
#define ND_JPL  0134000
#define ND_SKP  0140000
#define ND_ROP  0144000
#define ND_MIS  0150000
#define ND_SHT  0154000
#define ND_NA   0160000
#define ND_IOX  0164000
#define ND_ARG  0170000
#define ND_BOP  0174000

#define ND_SKP_CLEPT    0140301
#define ND_SKP_EXR      0140600
#define ND_SKP_ADDD     0140120 /* CE/CX */
#define ND_SKP_BFILL    0140130
#define ND_SKP_MOVB     0140131
#define ND_SKP_MOVBF    0140132
#define ND_SKP_VERSN    0140133 /* ND110 */
#define ND_SKP_RMPY     0141200
#define ND_SKP_RDIV     0141600
#define ND_SKP_LBYT     0142200
#define ND_SKP_SBYT     0142600
#define ND_SKP_MIX3     0143200
#define ND_SKP_LDATX    0143300 /* ND100 */
#define ND_SKP_LDXTX    0143301 /* ND100 */
#define ND_SKP_LDDTX    0143302 /* ND100 */
#define ND_SKP_LDBTX    0143303 /* ND100 */
#define ND_SKP_STATX    0143304 /* ND100 */
#define ND_SKP_STZTX    0143305 /* ND100 */
#define ND_SKP_STDTX    0143306 /* ND100 */
#define ND_SKP_LWCS     0143500 /* NOP on N110 */
#define ND_SKP_IDENT10  0143604
#define ND_SKP_IDENT11  0143611
#define ND_SKP_IDENT12  0143622
#define ND_SKP_IDENT13  0143643

#define ISEXR(x)        (((x) & 0177707) == ND_SKP_EXR)

#define ND_MIS_TRA      0150000
#define ND_MIS_TRR      0150100
#define ND_MIS_MCL      0150200
#define ND_MIS_MST      0150300
#define ND_MIS_TRMSK    0177700
#define ND_MIS_NLZ      0151400
#define ND_MIS_DNZ      0152000
#define ND_MIS_LRB      0152600
#define ND_MIS_SRB      0152402
#define ND_MIS_RBMSK    0177607
#define ND_MIS_IRW      0153400
#define ND_MIS_IRR      0153600
#define ND_MIS_IRRMSK   0177600

#define ND_MON          0153000
#define ND_WAIT         0151000
#define ND_MONMSK       0177400

#define ND_MIS_OPCOM    0150400
#define ND_MIS_IOF      0150401
#define ND_MIS_ION      0150402
#define ND_MIS_POF      0150404
#define ND_MIS_PIOF     0150405
#define ND_MIS_SEX      0150406
#define ND_MIS_REX      0150407
#define ND_MIS_PON      0150410
#define ND_MIS_PION     0150412
#define ND_MIS_IOXT     0150415
#define ND_MIS_EXAM     0150416
#define ND_MIS_DEPO     0150417

/* Internal registers */
#define IR_PANS         000     /* Panel status */
#define IR_STS          001     /* Status reg (as in register stack) */
#define IR_LMP          002     /* Display reg */
#define IR_PCR          003     /* paging control reg */
#define IR_IIC          005     /* Internal interrupt code */
#define IR_IIE          005     /* Internal interrupt enable */
#define IR_PID          006     /* Priority interrupt detect */
#define IR_PIE          007     /* Priority interrupt enable */
#define IR_CSR          010     /* Cache status reg */
#define IR_CCL          010     /* (W) Cache clear reg */
#define IR_LCIL         011     /* (W) Lower cache inhibit limit register */
#define IR_UCIL         012     /* (W) Upper cache inhibit limit register */
#define IR_ECCR         015     /* Error Correction Control Register */

#define IRR_OPR         002     /* Operator reg */
#define IRR_PGS         003     /* paging status reg */
#define  PGS_FF         0100000 /* fetch fault */
#define  PGS_PM         0040000 /* permit violation */
#define IRR_PVL         004     /* Previous level (oddly encoded) */
#define IRR_PES         013     /* Parity error status */
#define  PES_FETCH      0100000 /* Memory error during fetch, EXAM or DEPO */
#define  PES_DMA        0040000 /* Error occurred during DMA */
#define IRR_PGC         014     /* Read paging control reg */
#define IRR_PEA         015     /* Parity error address */
int mm_tra(int reg);

/* internal interrupt enable register */
#define IIE_MC          0000002 /* Monitor call */
#define IIE_PV          0000004 /* Protect Violation */
#define IIE_PF          0000010 /* Page fault */
#define IIE_II          0000020 /* Illegal instruction */
#define IIE_V           0000040 /* Error indicator */
#define IIE_PI          0000100 /* Privileged instruction */
#define IIE_IOX         0000200 /* IOX error */
#define IIE_PTY         0000400 /* Memory parity error */
#define IIE_MOR         0001000 /* Memory out of range */
#define IIE_POW         0002000 /* Power fail interrupt */

/* Status register bits */
#define STS_PTM         0000001 /* page table mode */
#define STS_TG          0000002 /* floating point rounding mode */
#define STS_K           0000004
#define STS_Z           0000010
#define STS_Q           0000020
#define STS_O           0000040
#define STS_C           0000100
#define STS_M           0000200
#define STS_N100        0010000 /* Nord-100 */
#define STS_SEXI        0020000 /* Extended addressing enabled */
#define STS_PONI        0040000 /* Paging turned on */
#define STS_IONI        0100000 /* interrupts turned on */

#define ISION()         BIT15(regSTH)
#define ISPON()         BIT14(regSTH)
#define ISSEX()         BIT13(regSTH)

/* page table bits */
#define PT_WPM          0100000
#define PT_RPM          0040000
#define PT_FPM          0020000
#define PT_WIP          0010000
#define PT_PGU          0004000

/* Simulator-specific stuff */
#define rnSTS   0
#define rnD     1
#define rnP     2
#define rnB     3
#define rnL     4
#define rnA     5
#define rnT     6
#define rnX     7
#define regSTL  R[0]    /* Only low 8 bits valid */
#define regD    R[1]
#define regP    R[2]
#define regB    R[3]
#define regL    R[4]
#define regA    R[5]
#define regT    R[6]
#define regX    R[7]

extern uint16 PM[];
extern uint16 R[8];
extern uint16 regSTH;   /* common for all levels */
extern int ald;         /* Automatic load descriptor - set by boot */
extern int curlvl;      /* Current interrupt level */
extern int userring;    /* Current user ring */

/*
 * interrupt link per device.
 */
struct intr {
        struct intr *next;
        short ident;
        short inuse;
};

extern DEVICE cpu_dev;
extern DEVICE mm_dev;
extern DEVICE tti_dev;
extern DEVICE tto_dev;
extern DEVICE floppy_dev;
extern DEVICE clk_dev;

int iox_floppy(int addr);
int iox_tty(int addr);
int iox_clk(int addr);

/* virtual memory access */
#define M_PHYS  0
#define M_PT    1
#define M_APT   2
#define M_FETCH 3

/* physical memory access */
#define PM_CPU  10      /* CPU requesting (longjmp allowed) */
#define PM_DMA  11      /* device requesting (no longjmp) */

/* Decide page table for late read */
#define SELPT2(IR) ((IR) & 03400 ? M_APT : M_PT)

int dma_rdmem(int addr);
int dma_wrmem(int addr, int val);
uint16 prdmem(int addr, int how);
void pwrmem(int addr, int val, int how);
uint16 rdmem(int addr, int how);
uint8 rdbyte(int vaddr, int lr, int how);
void wrmem(int addr, int val, int how);
void wrbyte(int vaddr, int val, int lr, int how);
void mm_wrpcr(void);
void mm_privcheck(void);

void intrpt14(int, int where);
void extint(int lvl, struct intr *intr);

#define STOP_UNHIOX     1
#define STOP_UNHINS     2
#define STOP_CKSUM      3
#define STOP_BP         4
#define STOP_WAIT       5
#define STOP_END        6

/* Useful bit extraction macros */
#define BIT0(x)         ((x) & 1)
#define BIT1(x)         (((x) >> 1) & 1)
#define BIT2(x)         (((x) >> 2) & 1)
#define BIT3(x)         (((x) >> 3) & 1)
#define BIT4(x)         (((x) >> 4) & 1)
#define BIT5(x)         (((x) >> 5) & 1)
#define BIT6(x)         (((x) >> 6) & 1)
#define BIT7(x)         (((x) >> 7) & 1)
#define BIT8(x)         (((x) >> 8) & 1)
#define BIT9(x)         (((x) >> 9) & 1)
#define BIT10(x)        (((x) >> 10) & 1)
#define BIT11(x)        (((x) >> 11) & 1)
#define BIT12(x)        (((x) >> 12) & 1)
#define BIT13(x)        (((x) >> 13) & 1)
#define BIT14(x)        (((x) >> 14) & 1)
#define BIT15(x)        (((x) >> 15) & 1)
#define BIT30(x)        (((x) >> 30) & 1)
#define BIT31(x)        (((x) >> 31) & 1)

#define SEXT8(x)        ((x & 0377) > 127 ? (int)(x & 0377) - 256 : (x & 0377))
