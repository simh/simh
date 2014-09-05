/* vax730_defs.h: VAX 730 model-specific definitions file

   Copyright (c) 2010-2011, Matt Burke
   This module incorporates code from SimH, Copyright (c) 2004-2008, Robert M Supnik

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
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   29-Mar-2011  MB      First Version

   This file covers the VAX 11/730, the third VAX.

   System memory map

        00 0000 - EF FFFF               main memory
        F0 0000 - F1 FFFF               reserved
        F2 0000 - F3 FFFF               nexus register space
        F4 0000 - FB FFFF               reserved
        FC 0000 - FF FFFF               Unibus address space
*/

#ifndef FULL_VAX
#define FULL_VAX        1
#endif

#ifndef VAX_730_DEFS_H_
#define VAX_730_DEFS_H_        1

/* Microcode constructs */

#define VAX730_SID      (3 << 24)                       /* system ID */
#define VAX730_MICRO    (123 << 8)                      /* ucode revision */
#define CON_HLTPIN      0x0200                          /* external CPU halt */
#define CON_HLTINS      0x0600                          /* HALT instruction */
#define MCHK_NXM        0x08                            /* NXM */
#define MCHK_IIA        0x0A                            /* illegal i/o addr */
#define MCHK_IUA        0x0B                            /* illegal unibus addr */

/* Interrupts */

#define IPL_HMAX        0x17                            /* highest hwre level */
#define IPL_HMIN        0x14                            /* lowest hwre level */
#define IPL_HLVL        (IPL_HMAX - IPL_HMIN + 1)       /* # hardware levels */
#define IPL_SMAX        0xF                             /* highest swre level */

/* Nexus constants */

#define NEXUS_NUM       16                              /* number of nexus */
#define TR_MCTL         0                               /* nexus assignments */
#define TR_UBA          3
#define NEXUS_HLVL      (IPL_HMAX - IPL_HMIN + 1)
#define SCB_NEXUS       0x100                           /* nexus intr base */

/* Internal I/O interrupts - relative except for clock and console */

#define IPL_CLKINT      0x18                            /* clock IPL */
#define IPL_TTINT       0x14                            /* console IPL */
#define IPL_CSINT       0x14                            /* console storage IPL */
#define IPL_UBA         (0x15 - IPL_HMIN)

/* Machine specific IPRs */

#define MT_CSRS         28                              /* Console storage */
#define MT_CSRD         29
#define MT_CSTS         30
#define MT_CSTD         31
#define MT_CDR          37                              /* Cache disable */
#define MT_MCESR        38                              /* MCHK err sts */
#define MT_ACCS         40                              /* FPA control */
#define MT_ACCR         41                              /* FPA maint */
#define MT_SBIFS        48                              /* SBI fault status */
#define MT_SBIS         49                              /* SBI silo */
#define MT_SBISC        50                              /* SBI silo comparator */
#define MT_SBIMT        51                              /* SBI maint */
#define MT_SBIER        52                              /* SBI error */
#define MT_SBITA        53                              /* SBI timeout addr */
#define MT_SBIQC        54                              /* SBI timeout clear */
#define MT_UBINIT       55                              /* Unibus Init */
#define MT_MAX          63                              /* last valid IPR */

/* Machine specific reserved operand tests */

/* 780 microcode patch 37 - only test LR<23:0> for appropriate length */

#define ML_LR_TEST(r)   if (((uint32)((r) & 0xFFFFFF)) > 0x200000) RSVD_OPND_FAULT

/* 780 microcode patch 38 - only test PxBR<31>=1, PxBR<30> = 0, and xBR<1:0> = 0 */

#define ML_PXBR_TEST(r) if (((((uint32)(r)) & 0x80000000) == 0) || \
                            ((((uint32)(r)) & 0x40000003) != 0)) RSVD_OPND_FAULT
#define ML_SBR_TEST(r)  if ((((uint32)(r)) & 0x00000003) != 0) RSVD_OPND_FAULT

/* 780 microcode patch 78 - test xCBB<1:0> = 0 */

#define ML_PA_TEST(r)   if ((((uint32)(r)) & 0x00000003) != 0) RSVD_OPND_FAULT

#define LP_AST_TEST(r)  if ((r) > AST_MAX) RSVD_OPND_FAULT
#define LP_MBZ84_TEST(r) if ((((uint32)(r)) & 0xF8C00000) != 0) RSVD_OPND_FAULT
#define LP_MBZ92_TEST(r) if ((((uint32)(r)) & 0x7FC00000) != 0) RSVD_OPND_FAULT

/* Memory */

#define MAXMEMWIDTH     21                              /* max mem, 16k chips */
#define MAXMEMSIZE      (1 << MAXMEMWIDTH)
#define MAXMEMWIDTH_X   23                              /* max mem, 64k chips */
#define MAXMEMSIZE_X    (1 << MAXMEMWIDTH_X)
#define INITMEMSIZE     (1 << MAXMEMWIDTH)              /* initial memory size */
#define MEMSIZE         (cpu_unit.capac)
#define ADDR_IS_MEM(x)  (((uint32) (x)) < MEMSIZE)
#define MEM_MODIFIERS   { UNIT_MSIZE, (1u << 20), NULL, "1M", &cpu_set_size, NULL, NULL, "Set Memory to 1M bytes" }, \
                        { UNIT_MSIZE, (2u << 20), NULL, "2M", &cpu_set_size, NULL, NULL, "Set Memory to 2M bytes" }, \
                        { UNIT_MSIZE, (3u << 20), NULL, "3M", &cpu_set_size, NULL, NULL, "Set Memory to 3M bytes" }, \
                        { UNIT_MSIZE, (4u << 20), NULL, "4M", &cpu_set_size, NULL, NULL, "Set Memory to 4M bytes" }, \
                        { UNIT_MSIZE, (5u << 20), NULL, "5M", &cpu_set_size, NULL, NULL, "Set Memory to 5M bytes" }, \
                        { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "MEMORY", NULL, NULL, &cpu_show_memory, NULL, "Display memory configuration" }
extern t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, void* desc);
#define CPU_MODEL_MODIFIERS                                                                     \
                        { MTAB_XTD|MTAB_VDV, 0, "MODEL", NULL,                                  \
                              NULL, &cpu_show_model, NULL, "Display the simulator CPU Model" }

/* Unibus I/O registers */

#define UBADDRWIDTH     18                              /* Unibus addr width */
#define UBADDRSIZE      (1u << UBADDRWIDTH)             /* Unibus addr length */
#define UBADDRMASK      (UBADDRSIZE - 1)                /* Unibus addr mask */
#define IOPAGEAWIDTH    13                              /* IO addr width */
#define IOPAGESIZE      (1u << IOPAGEAWIDTH)            /* IO page length */
#define IOPAGEMASK      (IOPAGESIZE - 1)                /* IO addr mask */
#define UBAMAPWIDTH     11                              /* Unibus map width */
#define UBAMAPSIZE      0x7FC                           /* Unibus map length */
#define UBADDRBASE      0xFC0000                        /* Unibus addr base */
#define IOPAGEBASE      0xFFE000                        /* IO page base */
#define UBAMAPBASE      0xF26800                        /* Unibus map base */
#define ADDR_IS_IO(x)   ((((uint32) (x)) >= UBADDRBASE) && \
                        (((uint32) (x)) < (UBADDRBASE + UBADDRSIZE)))
#define ADDR_IS_IOP(x)  (((uint32) (x)) >= IOPAGEBASE)
#define ADDR_IS_IOM(x)  ((((uint32) (x)) >= UBAMAPBASE) && \
                        (((uint32) (x)) < (UBAMAPBASE + UBAMAPSIZE)))

/* Nexus register space */

#define REGAWIDTH       19                              /* REG addr width */
#define REG_V_NEXUS     13                              /* nexus number */
#define REG_M_NEXUS     0xF
#define REG_V_OFS       2                               /* register number */
#define REG_M_OFS       0x7FF   
#define REGSIZE         (1u << REGAWIDTH)               /* REG length */
#define REGBASE         0xF00000                        /* REG addr base */
#define ADDR_IS_REG(x)  ((((uint32) (x)) >= REGBASE) && \
                        (((uint32) (x)) < (REGBASE + REGSIZE)))
#define NEXUS_GETNEX(x) (((x) >> REG_V_NEXUS) & REG_M_NEXUS)
#define NEXUS_GETOFS(x) (((x) >> REG_V_OFS) & REG_M_OFS)

/* Other address spaces */

#define ADDR_IS_ROM(x)  (0)
#define ADDR_IS_CDG(x)  (0)
#define ADDR_IS_NVR(x)  (0)

/* Unibus I/O modes */

#define READ            0                               /* PDP-11 compatibility */
#define WRITE           (L_WORD)
#define WRITEB          (L_BYTE)

/* Common CSI flags */

#define CSR_V_GO        0                               /* go */
#define CSR_V_IE        6                               /* interrupt enable */
#define CSR_V_DONE      7                               /* done */
#define CSR_V_BUSY      11                              /* busy */
#define CSR_V_ERR       15                              /* error */
#define CSR_GO          (1u << CSR_V_GO)
#define CSR_IE          (1u << CSR_V_IE)
#define CSR_DONE        (1u << CSR_V_DONE)
#define CSR_BUSY        (1u << CSR_V_BUSY)
#define CSR_ERR         (1u << CSR_V_ERR)

/* Timers */

#define TMR_CLK         0                               /* 100Hz clock */

/* I/O system definitions */

#define DZ_MUXES        4                               /* max # of DZV muxes */
#define DZ_LINES        8                               /* lines per DZV mux */
#define VH_MUXES        4                               /* max # of DHQ muxes */
#define DLX_LINES       16                              /* max # of KL11/DL11's */
#define DCX_LINES       16                              /* max # of DC11's */
#define DUP_LINES       8                               /* max # of DUP11's */
#define MT_MAXFR        (1 << 16)                       /* magtape max rec */

#define DEV_V_UBUS      (DEV_V_UF + 0)                  /* Unibus */
#define DEV_V_NEXUS     (DEV_V_UF + 1)                  /* Nexus */
#define DEV_V_FFUF      (DEV_V_UF + 2)                  /* first free flag */
#define DEV_UBUS        (1u << DEV_V_UBUS)
#define DEV_NEXUS       (1u << DEV_V_NEXUS)
#define DEV_QBUS        (0)
#define DEV_Q18         (0)

#define UNIBUS          TRUE                            /* Unibus only */

#define DEV_RDX         16                              /* default device radix */

/* Device information block 

   For Nexus devices,
        ba      =       Nexus number
        lnt     =       number of consecutive nexi */

#define VEC_DEVMAX      4                               /* max device vec */

typedef struct {
    uint32              ba;                             /* base addr */
    uint32              lnt;                            /* length */
    t_stat              (*rd)(int32 *dat, int32 ad, int32 md);
    t_stat              (*wr)(int32 dat, int32 ad, int32 md);
    int32               vnum;                           /* vectors: number */
    int32               vloc;                           /* locator */
    int32               vec;                            /* value */
    int32               (*ack[VEC_DEVMAX])(void);       /* ack routine */
    uint32              ulnt;                           /* IO length per unit */
    } DIB;

/* Unibus I/O page layout - see pdp11_io_lib.c for address layout details */

#define IOBA_AUTO       (0)                             /* Assigned by Auto Configure */

/* Interrupt assignments; within each level, priority is right to left */

#define INT_V_DZRX      0                               /* BR5 */
#define INT_V_DZTX      1
#define INT_V_HK        2
#define INT_V_RL        3
#define INT_V_RB        4
#define INT_V_RQ        5
#define INT_V_TQ        6
#define INT_V_TS        7
#define INT_V_RY        8
#define INT_V_XU        9
#define INT_V_DMCRX     10
#define INT_V_DMCTX     11
#define INT_V_DUPRX     12
#define INT_V_DUPTX     13

#define INT_V_LPT       0                               /* BR4 */
#define INT_V_PTR       1
#define INT_V_PTP       2
#define INT_V_CR        3
#define INT_V_VHRX      4
#define INT_V_VHTX      5

#define INT_DZRX        (1u << INT_V_DZRX)
#define INT_DZTX        (1u << INT_V_DZTX)
#define INT_HK          (1u << INT_V_HK)
#define INT_RL          (1u << INT_V_RL)
#define INT_RQ          (1u << INT_V_RQ)
#define INT_TQ          (1u << INT_V_TQ)
#define INT_TS          (1u << INT_V_TS)
#define INT_RY          (1u << INT_V_RY)
#define INT_XU          (1u << INT_V_XU)
#define INT_RB          (1u << INT_V_RB)
#define INT_LPT         (1u << INT_V_LPT)
#define INT_VHRX        (1u << INT_V_VHRX)
#define INT_VHTX        (1u << INT_V_VHTX)
#define INT_PTR         (1u << INT_V_PTR)
#define INT_PTP         (1u << INT_V_PTP)
#define INT_CR          (1u << INT_V_CR)
#define INT_DMCRX       (1u << INT_V_DMCRX)
#define INT_DMCTX       (1u << INT_V_DMCTX)
#define INT_DUPRX       (1u << INT_V_DUPRX)
#define INT_DUPTX       (1u << INT_V_DUPTX)

#define IPL_DZRX        (0x15 - IPL_HMIN)
#define IPL_DZTX        (0x15 - IPL_HMIN)
#define IPL_HK          (0x15 - IPL_HMIN)
#define IPL_RL          (0x15 - IPL_HMIN)
#define IPL_RQ          (0x15 - IPL_HMIN)
#define IPL_TQ          (0x15 - IPL_HMIN)
#define IPL_TS          (0x15 - IPL_HMIN)
#define IPL_RY          (0x15 - IPL_HMIN)
#define IPL_XU          (0x15 - IPL_HMIN)
#define IPL_RB          (0x15 - IPL_HMIN)
#define IPL_LPT         (0x14 - IPL_HMIN)
#define IPL_PTR         (0x14 - IPL_HMIN)
#define IPL_PTP         (0x14 - IPL_HMIN)
#define IPL_CR          (0x14 - IPL_HMIN)
#define IPL_VHRX        (0x14 - IPL_HMIN)
#define IPL_VHTX        (0x14 - IPL_HMIN)
#define IPL_DMCRX       (0x15 - IPL_HMIN)
#define IPL_DMCTX       (0x15 - IPL_HMIN)
#define IPL_DUPRX       (0x15 - IPL_HMIN)
#define IPL_DUPTX       (0x15 - IPL_HMIN)

/* Device vectors */

#define VEC_AUTO        (0)                             /* Assigned by Auto Configure */
#define VEC_FLOAT       (0)                             /* Assigned by Auto Configure */

#define VEC_QBUS        0
#define VEC_Q           0x200

/* Interrupt macros */

#define IVCL(dv)        ((IPL_##dv * 32) + INT_V_##dv)
#define NVCL(dv)        ((IPL_##dv * 32) + TR_##dv)
#define IREQ(dv)        int_req[IPL_##dv]
#define SET_INT(dv)     int_req[IPL_##dv] = int_req[IPL_##dv] | (INT_##dv)
#define CLR_INT(dv)     int_req[IPL_##dv] = int_req[IPL_##dv] & ~(INT_##dv)
#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* cond error return */

/* Logging */

#define LOG_CPU_I       0x1                             /* intexc */
#define LOG_CPU_R       0x2                             /* REI */
#define LOG_CPU_P       0x4                             /* context */

/* Boot definitions */

#define BOOT_HK         1                               /* device codes */
#define BOOT_RL         2                               /* for VMB */
#define BOOT_RB         3
#define BOOT_UDA        17
#define BOOT_TK         18
#define BOOT_TD         64

/* Function prototypes for virtual memory interface */

int32 Read (uint32 va, int32 lnt, int32 acc);
void Write (uint32 va, int32 val, int32 lnt, int32 acc);

/* Function prototypes for physical memory interface (inlined) */

SIM_INLINE int32 ReadB (uint32 pa);
SIM_INLINE int32 ReadW (uint32 pa);
SIM_INLINE int32 ReadL (uint32 pa);
SIM_INLINE int32 ReadLP (uint32 pa);
SIM_INLINE void WriteB (uint32 pa, int32 val);
SIM_INLINE void WriteW (uint32 pa, int32 val);
SIM_INLINE void WriteL (uint32 pa, int32 val);
void WriteLP (uint32 pa, int32 val);

/* Function prototypes for I/O */

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf);
int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf);
int32 Map_WriteB (uint32 ba, int32 bc, uint8 *buf);
int32 Map_WriteW (uint32 ba, int32 bc, uint16 *buf);

t_stat show_nexus (FILE *st, UNIT *uptr, int32 val, void *desc);

void sbi_set_errcnf (void);

/* Function prototypes for system-specific unaligned support
   11/730 treats unaligned like aligned */

#define ReadIOU(p,l)        ReadIO (p,l)
#define ReadRegU(p,l)       ReadReg (p,l)
#define WriteIOU(p,v,l)     WriteIO (p, v, l)
#define WriteRegU(p,v,l)    WriteReg (p, v, l)

#include "pdp11_io_lib.h"

#endif
