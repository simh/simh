/* vax860_defs.h: VAX 8600 model-specific definitions file

   Copyright (c) 2011-2012, Matt Burke
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

   26-Dec-2012  MB      First Version

   This file covers the VAX 8600, the fourth VAX.

   System memory map

        0000 0000 - 1FFF FFFF           main memory

        2000 0000 - 2001 FFFF           SBI0 adapter space
        2002 0000 - 200F FFFF           reserved
        2008 0000 - 2008 00BF           SBI0 registers
        2008 00C0 - 200F FFFF           reserved
        2010 0000 - 2013 FFFF           Unibus address space, Unibus 0
        2014 0000 - 2017 FFFF           Unibus address space, Unibus 1
        2018 0000 - 201B FFFF           Unibus address space, Unibus 2
        201C 0000 - 201F FFFF           Unibus address space, Unibus 3
        2020 0000 - 21FF FFFF           reserved

        2200 0000 - 2201 FFFF           SBI1 adapter space
        2202 0000 - 220F FFFF           reserved
        2208 0000 - 2208 00BF           SBI1 registers
        2208 00C0 - 220F FFFF           reserved
        2210 0000 - 2213 FFFF           Unibus address space, Unibus 4
        2214 0000 - 2217 FFFF           Unibus address space, Unibus 5
        2218 0000 - 221B FFFF           Unibus address space, Unibus 6
        221C 0000 - 221F FFFF           Unibus address space, Unibus 7
        2220 0000 - 23FF FFFF           reserved

        2400 0000 - 2401 FFFF           SBI2 adapter space
        2402 0000 - 240F FFFF           reserved
        2408 0000 - 2408 00BF           SBI2 registers
        2408 00C0 - 240F FFFF           reserved
        2410 0000 - 2413 FFFF           Unibus address space, Unibus 8
        2414 0000 - 2417 FFFF           Unibus address space, Unibus 9
        2418 0000 - 241B FFFF           Unibus address space, Unibus 10
        241C 0000 - 241F FFFF           Unibus address space, Unibus 11
        2420 0000 - 25FF FFFF           reserved

        2600 0000 - 2601 FFFF           SBI3 adapter space
        2602 0000 - 260F FFFF           reserved
        2608 0000 - 2608 00BF           SBI3 registers
        2608 00C0 - 260F FFFF           reserved
        2610 0000 - 2613 FFFF           Unibus address space, Unibus 12
        2614 0000 - 2617 FFFF           Unibus address space, Unibus 13
        2618 0000 - 261B FFFF           Unibus address space, Unibus 14
        261C 0000 - 261F FFFF           Unibus address space, Unibus 15
        2620 0000 - 3FFF FFFF           reserved
*/

#ifndef FULL_VAX
#define FULL_VAX        1           /* Full Instruction Set Implemented */
#endif

#ifndef CMPM_VAX
#define CMPM_VAX        1           /* Compatibility Mode Implemented */
#endif

#ifndef VAX_860_DEFS_H_
#define VAX_860_DEFS_H_        1

/* Microcode constructs */

#define VAX860_SID      (4 << 24)                       /* system ID */
#define VAX860_TYP      (0 << 23)                       /* sys type: 8600 */
#define VAX865_TYP      (1 << 23)                       /* sys type: 8650 */
#define VAX860_ECO      (7 << 16)                       /* ucode revision */
#define VAX860_PLANT    (0 << 12)                       /* plant (say we're simh/undefined) */
#define VAX860_SN       (1234)
#define CON_HLTPIN      0x0200                          /* external CPU halt */
#define CON_HLTINS      0x0600                          /* HALT instruction */
#define MCHK_RD_F       0x00                            /* read fault */
#define MCHK_RD_A       0xF4                            /* read abort */
#define MCHK_IBUF       0x0D                            /* read istream */
#define VER_UCODE       0x1                             /* Microcode version */

/* Interrupts */

#define IPL_HMAX        0x17                            /* highest hwre level */
#define IPL_HMIN        0x14                            /* lowest hwre level */
#define IPL_HLVL        (IPL_HMAX - IPL_HMIN + 1)       /* # hardware levels */
#define IPL_SMAX        0xF                             /* highest swre level */

/* SBI Nexus constants */

#define NEXUS_NUM       16                              /* number of nexus */
#define MCTL_NUM        2                               /* number of mem ctrl */
#define MBA_NUM         2                               /* number of MBA's */
#define TR_MCTL0        1                               /* nexus assignments */
#define TR_MCTL1        2
#define TR_UBA          3
#define TR_MBA0         8
#define TR_MBA1         9
#define TR_CI           14
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

#define MT_ACCS         40                              /* FPA control */
#define MT_PAMACC       64
#define MT_PAMLOC       65
#define MT_CSWP         66
#define MT_MDECC        67
#define MT_MENA         68
#define MT_MDCTL        69
#define MT_MCCTL        70
#define MT_MERG         71
#define MT_CRBT         72                              /* Console reboot */
#define MT_DFI          73
#define MT_EHSR         74
#define MT_STXCS        76
#define MT_STXDB        77
#define MT_ESPA         78
#define MT_ESPD         79
#define MT_MAX          MT_ESPD                         /* last valid IPR */

/* Machine specific reserved operand tests */

/* 780 microcode patch 37 - only test LR<23:0> for appropriate length */

#define ML_LR_TEST(r)   if (((uint32)((r) & 0xFFFFFF)) > 0x200000) RSVD_OPND_FAULT(ML_LR_TEST)

/* 780 microcode patch 38 - only test PxBR<31>=1, PxBR<30> = 0, and xBR<1:0> = 0 */

#define ML_PXBR_TEST(r) if (((((uint32)(r)) & 0x80000000) == 0) || \
                            ((((uint32)(r)) & 0x40000003) != 0)) RSVD_OPND_FAULT(ML_PXBR_TEST)
#define ML_SBR_TEST(r)  if ((((uint32)(r)) & 0x00000003) != 0) RSVD_OPND_FAULT(ML_SBR_TEST)

/* 780 microcode patch 78 - test xCBB<1:0> = 0 */

#define ML_PA_TEST(r)   if ((((uint32)(r)) & 0x00000003) != 0) RSVD_OPND_FAULT(ML_PA_TEST)

#define LP_AST_TEST(r)  if ((r) > AST_MAX) RSVD_OPND_FAULT(LP_AST_TEST)
#define LP_MBZ84_TEST(r) if ((((uint32)(r)) & 0xF8C00000) != 0) RSVD_OPND_FAULT(LP_MBZ84_TEST)
#define LP_MBZ92_TEST(r) if ((((uint32)(r)) & 0x7FC00000) != 0) RSVD_OPND_FAULT(LP_MBZ92_TEST)

#define MT_AST_TEST(r)  r = (r) & 07; \
                        if ((r) > AST_MAX) RSVD_OPND_FAULT(MT_AST_TEST)
#define IDX_IMM_TEST

/* Memory */

#define MAXMEMWIDTH     25                              /* max mem, 4MB boards */
#define MAXMEMSIZE      (1 << MAXMEMWIDTH)
#define MAXMEMWIDTH_X   29                              /* max mem space using non-existant 256MB boards */
#define MAXMEMSIZE_X    (1 << MAXMEMWIDTH_X)
#define INITMEMSIZE     (1 << MAXMEMWIDTH)              /* initial memory size */
#define MEMSIZE         (cpu_unit.capac)
#define ADDR_IS_MEM(x)  (((uint32) (x)) < MEMSIZE)
#define MEM_MODIFIERS   { UNIT_MSIZE, (1u << 23), NULL,  "8M", &cpu_set_size, NULL, NULL, "Set Memory to 8M bytes" },                   \
                        { UNIT_MSIZE, (1u << 24), NULL, "16M", &cpu_set_size, NULL, NULL, "Set Memory to 16M bytes" },                  \
                        { UNIT_MSIZE, (1u << 25), NULL, "32M", &cpu_set_size, NULL, NULL, "Set Memory to 32M bytes" },                  \
                        { UNIT_MSIZE, (1u << 25) + (1u << 24), NULL, "48M", &cpu_set_size, NULL, NULL, "Set Memory to 48M bytes" },     \
                        { UNIT_MSIZE, (1u << 26), NULL, "64M", &cpu_set_size, NULL, NULL, "Set Memory to 64M bytes" },                  \
                        { UNIT_MSIZE, (1u << 26) + (1u << 22), NULL, "68M", &cpu_set_size, NULL, NULL, "Set Memory to 68M bytes" },     \
                        { UNIT_MSIZE, (1u << 27), NULL, "128M", &cpu_set_size, NULL, NULL, "Set Memory to 128M bytes" },                \
                        { UNIT_MSIZE, (1u << 28), NULL, "256M", &cpu_set_size, NULL, NULL, "Set Memory to 256M bytes" },                \
                        { UNIT_MSIZE, (1u << 28) + (1u << 22), NULL, "260M", &cpu_set_size, NULL, NULL, "Set Memory to 260M bytes" },   \
                        { UNIT_MSIZE, (1u << 29), NULL, "512M", &cpu_set_size, NULL, NULL, "Set Memory to 512M bytes" },                \
                        { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "MEMORY", NULL, NULL, &cpu_show_memory, NULL, "Display memory configuration" }
extern t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, CONST void* desc);

#define CPU_MODEL_MODIFIERS                                                                     \
                        { MTAB_XTD|MTAB_VDV, 0, "MODEL", "MODEL={8600|8650}",                   \
                              &cpu_set_model, &cpu_show_model, NULL, "Set/Display processor model" },

/* Unibus I/O registers */

#define UBADDRWIDTH     18                              /* Unibus addr width */
#define UBADDRSIZE      (1u << UBADDRWIDTH)             /* Unibus addr length */
#define UBADDRMASK      (UBADDRSIZE - 1)                /* Unibus addr mask */
#define IOPAGEAWIDTH    13                              /* IO addr width */
#define IOPAGESIZE      (1u << IOPAGEAWIDTH)            /* IO page length */
#define IOPAGEMASK      (IOPAGESIZE - 1)                /* IO addr mask */
#define UBADDRBASE      0x20100000                      /* Unibus addr base */
#define IOPAGEBASE      0x2013E000                      /* IO page base */
#define ADDR_IS_IO(x)   ((((uint32) (x)) >= UBADDRBASE) && \
                        (((uint32) (x)) < (UBADDRBASE + UBADDRSIZE)))
#define ADDR_IS_IOP(x)  (((uint32) (x)) >= IOPAGEBASE)

/* Nexus register space */

#define REGAWIDTH       17                              /* REG addr width */
#define REG_V_NEXUS     13                              /* nexus number */
#define REG_M_NEXUS     0xF
#define REG_V_OFS       2                               /* register number */
#define REG_M_OFS       0x7FF   
#define REGSIZE         (1u << REGAWIDTH)               /* REG length */
#define REGBASE         0x20000000                      /* REG addr base */
#define NEXUSBASE       REGBASE                         /* NEXUS addr base */
#define ADDR_IS_REG(x)  ((((uint32) (x)) >= REGBASE) && \
                        (((uint32) (x)) < (REGBASE + REGSIZE)))
#define NEXUS_GETNEX(x) (((x) >> REG_V_NEXUS) & REG_M_NEXUS)
#define NEXUS_GETOFS(x) (((x) >> REG_V_OFS) & REG_M_OFS)

/* SBI adapter space */

#define SBIAWIDTH       19
#define SBIABASE        0x20080000
#define SBIASIZE        (1u << SBIAWIDTH)
#define ADDR_IS_SBIA(x) ((((uint32) (x)) >= SBIABASE) && \
                        (((uint32) (x)) < (SBIABASE + SBIASIZE)))

/* ROM address space in memory controllers */

#define ROMAWIDTH       12                              /* ROM addr width */
#define ROMSIZE         (1u << ROMAWIDTH)               /* ROM size */
#define ROM0BASE        (REGBASE + (TR_MCTL0 << REG_V_NEXUS) + 0x1000)
#define ROM1BASE        (REGBASE + (TR_MCTL1 << REG_V_NEXUS) + 0x1000)
#define ADDR_IS_ROM0(x) ((((uint32) (x)) >= ROM0BASE) && \
                        (((uint32) (x)) < (ROM0BASE + ROMSIZE)))
#define ADDR_IS_ROM1(x) ((((uint32) (x)) >= ROM1BASE) && \
                        (((uint32) (x)) < (ROM1BASE + ROMSIZE)))
#define ADDR_IS_ROM(x)  (ADDR_IS_ROM0 (x) || ADDR_IS_ROM1 (x))

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
#define VH_MUXES        4                               /* max # of DHU muxes */
#define DLX_LINES       16                              /* max # of KL11/DL11's */
#define DCX_LINES       16                              /* max # of DC11's */
#define MT_MAXFR        (1 << 16)                       /* magtape max rec */

#define DEV_V_UBUS      (DEV_V_UF + 0)                  /* Unibus */
#define DEV_V_MBUS      (DEV_V_UF + 1)                  /* Massbus */
#define DEV_V_NEXUS     (DEV_V_UF + 2)                  /* Nexus */
#define DEV_V_FFUF      (DEV_V_UF + 3)                  /* first free flag */
#define DEV_UBUS        (1u << DEV_V_UBUS)
#define DEV_MBUS        (1u << DEV_V_MBUS)
#define DEV_NEXUS       (1u << DEV_V_NEXUS)
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
    DEVICE              *dptr;                          /* back pointer to related device */
                                                        /* Populated by auto-configure */
    } DIB;

/* Unibus I/O page layout - XUB,RQB,RQC,RQD float based on number of DZ's
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
#define INT_V_CH        14

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
#define INT_CH          (1u << INT_V_CH)

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
#define IPL_CH          (0x15 - IPL_HMIN)
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
#define VEC_SET         0x000                           /* Vector bits to set in Unibus vectors */

/* Interrupt macros */

#define IVCL(dv)        ((IPL_##dv * 32) + INT_V_##dv)
#define NVCL(dv)        ((IPL_##dv * 32) + TR_##dv)
#define IREQ(dv)        int_req[IPL_##dv]
#define SET_INT(dv)     int_req[IPL_##dv] = int_req[IPL_##dv] | (INT_##dv)
#define CLR_INT(dv)     int_req[IPL_##dv] = int_req[IPL_##dv] & ~(INT_##dv)
#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* cond error return */

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
#define BOOT_CS         64

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
   8600 treats unaligned like aligned */

#define ReadIOU(p,l)        ReadIO (p,l)
#define ReadRegU(p,l)       ReadReg (p,l)
#define WriteIOU(p,v,l)     WriteIO (p, v, l)
#define WriteRegU(p,v,l)    WriteReg (p, v, l)

#include "pdp11_io_lib.h"

/* Function prototypes for virtual and physical memory interface (inlined) */

#include "vax_mmu.h"

#endif
