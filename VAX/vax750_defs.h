/* vax750_defs.h: VAX 750 model-specific definitions file

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   21-Oct-2012  MB      First Version

   This file covers the VAX 11/750, the second VAX.

   System memory map

          00 0000 - 7F FFFF             main memory
          80 0000 - EF FFFF             reserved
          F0 0000 - F0 FFFF             writeable control store
          F1 0000 - F1 FFFF             reserved
          F2 0000 - F2 0010             memory controller
          F2 0400 - F2 07FF             bootstrap ROM
          F2 8000 - F2 88FF             Massbus adapter 0
          F2 A000 - F2 A8FF             Massbus adapter 1
          F2 C000 - F2 C8FF             Massbus adapter 2
          F3 0000 - F3 09FF             Unibus adapter 0
          F3 2000 - F3 29FF             Unibus adapter 1
*/

#ifndef FULL_VAX
#define FULL_VAX        1
#endif

#ifndef VAX_750_DEFS_H_
#define VAX_750_DEFS_H_        1

/* Microcode constructs */

#define VAX750_SID      (2 << 24)                       /* system ID */
#define VAX750_MICRO    (99 << 8)                       /* ucode revision */
#define VAX750_HWREV    (156)                           /* hw revision */
#define CON_HLTPIN      0x0200                          /* external CPU halt */
#define CON_HLTINS      0x0600                          /* HALT instruction */
#define MCHK_CSPE       0x01                            /* control store parity error */
#define MCHK_BPE        0x02                            /* bus error or tb/cache parity error */
#define VER_FPLA        0x0C                            /* FPLA version */
#define VER_WCSP        (VER_FPLA)                      /* WCS primary version */
#define VER_WCSS        0x12                            /* WCS secondary version */
#define VER_PCS         ((VER_WCSS >> 4) & 0x3)         /* PCS version */

/* Interrupts */

#define IPL_HMAX        0x17                            /* highest hwre level */
#define IPL_HMIN        0x14                            /* lowest hwre level */
#define IPL_HLVL        (IPL_HMAX - IPL_HMIN + 1)       /* # hardware levels */
#define IPL_SMAX        0xF                             /* highest swre level */

/* Nexus constants */

#define NEXUS_NUM       16                              /* number of nexus */
#define MCTL_NUM        2                               /* number of mem ctrl */
#define MBA_NUM         2                               /* number of MBA's */
#define TR_MCTL         0                               /* nexus assignments */
#define TR_MBA0         4
#define TR_MBA1         5
#define TR_UBA          8
#define TR_CI           15
#define NEXUS_HLVL      (IPL_HMAX - IPL_HMIN + 1)
#define SCB_NEXUS       0x100                           /* nexus intr base */
#define SBI_FAULTS      0xFC000000                      /* SBI fault flags */

/* Internal I/O interrupts - relative except for clock and console */

#define IPL_CLKINT      0x18                            /* clock IPL */
#define IPL_TTINT       0x14                            /* console IPL */

#define IPL_MCTL0       (0x15 - IPL_HMIN)
#define IPL_MCTL1       (0x15 - IPL_HMIN)
#define IPL_UBA         (0x15 - IPL_HMIN)
#define IPL_MBA0        (0x15 - IPL_HMIN)
#define IPL_MBA1        (0x15 - IPL_HMIN)
#define IPL_CI          (0x15 - IPL_HMIN)

/* Nexus interrupt macros */

#define SET_NEXUS_INT(dv)       nexus_req[IPL_##dv] |= (1 << TR_##dv)
#define CLR_NEXUS_INT(dv)       nexus_req[IPL_##dv] &= ~(1 << TR_##dv)

/* Machine specific IPRs */

#define MT_CSRS         28                              /* Console storage */
#define MT_CSRD         29
#define MT_CSTS         30
#define MT_CSTD         31
#define MT_CMIE         23                              /* CMI error */
#define MT_TBDR         36                              /* TB disable */
#define MT_CADR         37                              /* Cache disable */
#define MT_MCESR        38                              /* MCHK err sts */
#define MT_CAER         39                              /* Cache error */
#define MT_ACCS         40                              /* FPA control */
#define MT_IORESET      55                              /* Unibus Init */
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

#define MT_AST_TEST(r)  r = (r) & 07; \
                        if ((r) > AST_MAX) RSVD_OPND_FAULT

/* Memory */

#define MAXMEMWIDTH     21                              /* max mem, 16k chips */
#define MAXMEMSIZE      (1 << MAXMEMWIDTH)
#define MAXMEMWIDTH_Y   23                              /* max mem, 64k chips */
#define MAXMEMSIZE_Y    (1 << MAXMEMWIDTH_Y)
#define MAXMEMWIDTH_X   24                              /* max mem, 256k chips */
#define MAXMEMSIZE_X    ((1 << MAXMEMWIDTH_X) - (1 << 20)) /* 15M Max before interfering with Register Space */
#define INITMEMSIZE     (1 << MAXMEMWIDTH)              /* initial memory size */
#define MEMSIZE         (cpu_unit.capac)
#define ADDR_IS_MEM(x)  (((uint32) (x)) < MEMSIZE)
#define MEM_MODIFIERS   { UNIT_MSIZE, (1u << 20), NULL, "1M", &cpu_set_size, NULL, NULL, "Set Memory to 1M bytes" },   \
                        { UNIT_MSIZE, (1u << 21), NULL, "2M", &cpu_set_size, NULL, NULL, "Set Memory to 2M bytes" },   \
                        { UNIT_MSIZE, (1u << 22), NULL, "4M", &cpu_set_size, NULL, NULL, "Set Memory to 4M bytes" },   \
                        { UNIT_MSIZE, (1u << 23), NULL, "8M", &cpu_set_size, NULL, NULL, "Set Memory to 8M bytes" },   \
                        { UNIT_MSIZE, (1u << 23) + (1u << 22), NULL, "12M", &cpu_set_size, NULL, NULL, "Set Memory to 12M bytes" },   \
                        { UNIT_MSIZE, (1u << 23) + (6u << 20), NULL, "14M", &cpu_set_size, NULL, NULL, "Set Memory to 14M bytes" }, \
                        { UNIT_MSIZE, (1u << 23) + (7u << 20), NULL, "15M", &cpu_set_size, NULL, NULL, "Set Memory to 15M bytes" }, \
                        { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "MEMORY", NULL, NULL, &cpu_show_memory, NULL, "Display memory configuration" }
extern t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
#define CPU_MODEL_MODIFIERS { MTAB_XTD|MTAB_VDV, 0, "MODEL",     NULL,                                      \
                              NULL, &cpu_show_model, NULL, "Display the simulator CPU Model" },              \
                            { MTAB_XTD|MTAB_VDV, 0, "BOOTDEV",   "BOOTDEV={A|B|C|D}",                       \
                              &vax750_set_bootdev, &vax750_show_bootdev, NULL, "Set Boot Device" }
extern t_stat vax750_set_bootdev (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat vax750_show_bootdev (FILE *st, UNIT *uptr, int32 val, CONST void *desc);


/* Unibus I/O registers */

#define UBADDRWIDTH     18                              /* Unibus addr width */
#define UBADDRSIZE      (1u << UBADDRWIDTH)             /* Unibus addr length */
#define UBADDRMASK      (UBADDRSIZE - 1)                /* Unibus addr mask */
#define IOPAGEAWIDTH    13                              /* IO addr width */
#define IOPAGESIZE      (1u << IOPAGEAWIDTH)            /* IO page length */
#define IOPAGEMASK      (IOPAGESIZE - 1)                /* IO addr mask */
#define UBADDRBASE      0xFC0000                        /* Unibus addr base */
#define IOPAGEBASE      0xFFE000                        /* IO page base */
#define ADDR_IS_IO(x)   ((((uint32) (x)) >= UBADDRBASE) && \
                        (((uint32) (x)) < (UBADDRBASE + UBADDRSIZE)))
#define ADDR_IS_IOP(x)  (((uint32) (x)) >= IOPAGEBASE)

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
#define NEXUSBASE       (REGBASE + 0x20000)
#define NEXUS_GETNEX(x) (((x) >> REG_V_NEXUS) & REG_M_NEXUS)
#define NEXUS_GETOFS(x) (((x) >> REG_V_OFS) & REG_M_OFS)

/* ROM address space in memory controllers */

#define ROMAWIDTH       10                              /* ROM addr width */
#define ROMSIZE         (1u << ROMAWIDTH)               /* ROM size */
#define ROMAMASK        (ROMSIZE - 1)                   /* ROM addr mask */
#define ROMBASE         (NEXUSBASE + 0x400)
#define ADDR_IS_ROM(x)  ((((uint32) (x)) >= ROMBASE) && \
                        (((uint32) (x)) < (ROMBASE + ROMSIZE)))

/* Other address spaces */

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

#define DZ_MUXES        4                               /* default # of DZV muxes */
#define VH_MUXES        4                               /* max # of DHQ muxes */
#define DLX_LINES       16                              /* max # of KL11/DL11's */
#define DCX_LINES       16                              /* max # of DC11's */
#define MT_MAXFR        (1 << 16)                       /* magtape max rec */

#define DEV_V_UBUS      (DEV_V_UF + 0)                  /* Unibus */
#define DEV_V_MBUS      (DEV_V_UF + 1)                  /* Massbus */
#define DEV_V_NEXUS     (DEV_V_UF + 2)                  /* Nexus */
#define DEV_V_CI        (DEV_V_UF + 3)                  /* CI */
#define DEV_V_FFUF      (DEV_V_UF + 4)                  /* first free flag */
#define DEV_UBUS        (1u << DEV_V_UBUS)
#define DEV_MBUS        (1u << DEV_V_MBUS)
#define DEV_NEXUS       (1u << DEV_V_NEXUS)
#define DEV_CI          (1u << DEV_V_CI)
#define DEV_QBUS        (0)
#define DEV_Q18         (0)

#define UNIBUS          TRUE                            /* Unibus only */

#define DEV_RDX         16                              /* default device radix */

/* Device information block 

   For Massbus devices,
        ba      =       Massbus number
        lnt     =       Massbus ctrl type
        ack[0]  =       abort routine

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
    uint32              ulnt;                           /* IO length per-device */
                                                        /* Only need to be populated */
                                                        /* when numunits != num devices */
    int32               numc;                           /* Number of controllers */
                                                        /* this field handles devices */
                                                        /* where multiple instances are */
                                                        /* simulated through a single */
                                                        /* DEVICE structure (e.g., DZ, VH, DL, DC). */
                                                        /* Populated by auto-configure */
    } DIB;

/* Unibus I/O page layout - see pdp11_io_lib.c for address layout details
   Massbus devices (RP, TU) do not appear in the Unibus IO page */

#define IOBA_AUTO       (0)                             /* Assigned by Auto Configure */


/* Interrupt assignments; within each level, priority is right to left */

#define INT_V_DTA       0                               /* BR6 */
#define INT_V_CR        1

#define INT_V_DZRX      0                               /* BR5 */
#define INT_V_DZTX      1
#define INT_V_HK        2
#define INT_V_RL        3
#define INT_V_RQ        4
#define INT_V_TQ        5
#define INT_V_TS        6
#define INT_V_RY        7
#define INT_V_XU        8
#define INT_V_DMCRX     9
#define INT_V_DMCTX     10
#define INT_V_DUPRX     11
#define INT_V_DUPTX     12
#define INT_V_RK        13

#define INT_V_LPT       0                               /* BR4 */
#define INT_V_PTR       1
#define INT_V_PTP       2
//#define XXXXXXXX        3                             /* Former CR */
#define INT_V_VHRX      4
#define INT_V_VHTX      5
#define INT_V_TDRX      6
#define INT_V_TDTX      7

#define INT_DTA         (1u << INT_V_DTA)
#define INT_CR          (1u << INT_V_CR)
#define INT_DZRX        (1u << INT_V_DZRX)
#define INT_DZTX        (1u << INT_V_DZTX)
#define INT_HK          (1u << INT_V_HK)
#define INT_RL          (1u << INT_V_RL)
#define INT_RQ          (1u << INT_V_RQ)
#define INT_TQ          (1u << INT_V_TQ)
#define INT_TS          (1u << INT_V_TS)
#define INT_RY          (1u << INT_V_RY)
#define INT_XU          (1u << INT_V_XU)
#define INT_LPT         (1u << INT_V_LPT)
#define INT_VHRX        (1u << INT_V_VHRX)
#define INT_VHTX        (1u << INT_V_VHTX)
#define INT_PTR         (1u << INT_V_PTR)
#define INT_PTP         (1u << INT_V_PTP)
#define INT_DMCRX       (1u << INT_V_DMCRX)
#define INT_DMCTX       (1u << INT_V_DMCTX)
#define INT_DUPRX       (1u << INT_V_DUPRX)
#define INT_DUPTX       (1u << INT_V_DUPTX)
#define INT_RK          (1u << INT_V_RK)
#define INT_TDRX        (1u << INT_V_TDRX)
#define INT_TDTX        (1u << INT_V_TDTX)

#define IPL_DTA         (0x16 - IPL_HMIN)
#define IPL_CR          (0x16 - IPL_HMIN)
#define IPL_DZRX        (0x15 - IPL_HMIN)
#define IPL_DZTX        (0x15 - IPL_HMIN)
#define IPL_HK          (0x15 - IPL_HMIN)
#define IPL_RL          (0x15 - IPL_HMIN)
#define IPL_RQ          (0x15 - IPL_HMIN)
#define IPL_TQ          (0x15 - IPL_HMIN)
#define IPL_TS          (0x15 - IPL_HMIN)
#define IPL_RY          (0x15 - IPL_HMIN)
#define IPL_XU          (0x15 - IPL_HMIN)
#define IPL_LPT         (0x14 - IPL_HMIN)
#define IPL_PTR         (0x14 - IPL_HMIN)
#define IPL_PTP         (0x14 - IPL_HMIN)
#define IPL_VHRX        (0x14 - IPL_HMIN)
#define IPL_VHTX        (0x14 - IPL_HMIN)
#define IPL_DMCRX       (0x15 - IPL_HMIN)
#define IPL_DMCTX       (0x15 - IPL_HMIN)
#define IPL_DUPRX       (0x15 - IPL_HMIN)
#define IPL_DUPTX       (0x15 - IPL_HMIN)
#define IPL_RK          (0x15 - IPL_HMIN)
#define IPL_TDRX        (0x14 - IPL_HMIN)
#define IPL_TDTX        (0x14 - IPL_HMIN)

/* Device vectors */

#define VEC_AUTO        (0)                             /* Assigned by Auto Configure */
#define VEC_FLOAT       (0)                             /* Assigned by Auto Configure */

#define VEC_QBUS        0                               /* Unibus system */
#define VEC_SET         0x200                           /* Vector bits to set in Unibus vectors */
#define VEC_MASK        0x3FF                           /* Vector bits to return in Unibus vectors */

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

/* Massbus definitions */

#define MBA_RMASK       0x1F                            /* max 32 reg */
#define MBA_AUTO        (uint32)0xFFFFFFFF              /* Unassigned MBA */
#define MBE_NXD         1                               /* nx drive */
#define MBE_NXR         2                               /* nx reg */
#define MBE_GOE         3                               /* err on GO */

/* Boot definitions */

#define BOOT_MB         0                               /* device codes */
#define BOOT_HK         1                               /* for VMB */
#define BOOT_RL         2
#define BOOT_UDA        17
#define BOOT_CI         32
#define BOOT_TD         64

/* Function prototypes for I/O */

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf);
int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf);
int32 Map_WriteB (uint32 ba, int32 bc, const uint8 *buf);
int32 Map_WriteW (uint32 ba, int32 bc, const uint16 *buf);

int32 mba_rdbufW (uint32 mbus, int32 bc, uint16 *buf);
int32 mba_wrbufW (uint32 mbus, int32 bc, const uint16 *buf);
int32 mba_chbufW (uint32 mbus, int32 bc, uint16 *buf);
int32 mba_get_bc (uint32 mbus);
void mba_upd_ata (uint32 mbus, uint32 val);
void mba_set_exc (uint32 mbus);
void mba_set_don (uint32 mbus);
void mba_set_enbdis (DEVICE *dptr);
t_stat mba_show_num (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

t_stat show_nexus (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

void sbi_set_errcnf (void);

/* Function prototypes for system-specific unaligned support
   11/750 treats unaligned like aligned */

#define ReadIOU(p,l)        ReadIO (p,l)
#define ReadRegU(p,l)       ReadReg (p,l)
#define WriteIOU(p,v,l)     WriteIO (p, v, l)
#define WriteRegU(p,v,l)    WriteReg (p, v, l)

#include "pdp11_io_lib.h"

/* Function prototypes for virtual and physical memory interface (inlined) */

#include "vax_mmu.h"

#endif
