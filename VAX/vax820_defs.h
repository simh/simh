/* vax820_defs.h: VAX 8200 model-specific definitions file

   Copyright (c) 2019, Matt Burke
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

   This file covers the VAX 8200, the fifth VAX.

   System memory map

        0000 0000 - 1FFF FFFF           main memory
        2000 0000 - 2001 FFFF           bi node space
        2002 0000 - 2007 FFFF           reserved
        2008 0000 - 2008 00FC           ka820 biic internal registers
        2008 0200 - 2008 0203           rxcd register
        2008 0204 - 2008 FFFF           reserved
        2009 0000 - 2009 1FFF           boot RAM
        2009 2000 - 2009 7FFF           reserved
        2009 8000 - 2009 FFFF           eeprom
        200A 0000 - 200A FFFF           reserved
        200B 0000 - 200B 0017           rcx50
        200B 0020 - 200B 7FFF           reserved
        200B 8000 - 200B 807F           watch chip
        200B 8080 - 203F FFFF           reserved
        2040 0000 - 207F FFFF           bi window space
        2080 0000 - 3FFF FFFF           reserved
*/

#ifndef FULL_VAX
#define FULL_VAX        1
#endif

#ifndef _VAX_820_DEFS_H_
#define _VAX_820_DEFS_H_        1

/* Microcode constructs */

#define VAX820_SID      (5 << 24)                       /* system ID */
#define VAX820_TYP      (0 << 23)                       /* sys type: 8200 */
#define VAX825_TYP      (1 << 23)                       /* sys type: 8250 */
#define VAX820_REV      (5 << 19)                       /* CPU revision */
#define VAX820_PATCH    (21 << 9)                       /* patch revision */
#define VAX820_UCODE    (20)                            /* ucode revision */
#define CON_HLTPIN      0x0200                          /* external CPU halt */
#define CON_HLTINS      0x0600                          /* HALT instruction */
#define MCHK_BIERR      0x10                            /* BI bus error */
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
#define KA_NUM          2                               /* number of CPU's */
#define MCTL_NUM        2                               /* number of mem ctrl */
#define MBA_NUM         2                               /* number of MBA's */
#define TR_KA0          0                               /* nexus assignments */
#define TR_KA1          1
#define TR_MCTL0        2
#define TR_MCTL1        3
#define TR_UBA          4
#define NEXUS_HLVL      (IPL_HMAX - IPL_HMIN + 1)
#define SCB_NEXUS       0x100                           /* nexus intr base */

/* Internal I/O interrupts - relative except for clock and console */

#define IPL_CLKINT      0x18                            /* clock IPL */
#define IPL_IPRINT      0x14                            /* interprocessor IPL */
#define IPL_RXCDINT     0x14                            /* RXCD IPL */
#define IPL_TTINT       0x14                            /* console IPL */
#define IPL_FLINT       0x14                            /* console floppy IPL */

#define SCB_RXCD        0x58
#define SCB_IPRINT      0x80
#define SCB_FLINT       0xF0

#define IPL_MCTL0       (0x15 - IPL_HMIN)
#define IPL_MCTL1       (0x15 - IPL_HMIN)
#define IPL_UBA         (0x15 - IPL_HMIN)

/* Nexus interrupt macros */

#define SET_NEXUS_INT(dv)       nexus_req[IPL_##dv] |= (1 << TR_##dv)
#define CLR_NEXUS_INT(dv)       nexus_req[IPL_##dv] &= ~(1 << TR_##dv)

/* Machine specific IPRs */

#define MT_IPIR         22                              /* interprocessor interrupt */
#define MT_TBDR         36                              /* translation buffer disable */
#define MT_CADR         37                              /* cache disable */
#define MT_MCESR        38                              /* MCHK error summary */
#define MT_ACCS         40                              /* FPA control */
#define MT_WCSA         44                              /* WCS address */
#define MT_WCSD         45                              /* WCS data */
#define MT_WCSL         46                              /* WCS load */
#define MT_RXCS1        80                              /* Serial line 1 rx ctrl */
#define MT_RXDB1        81                              /* Serial line 1 rx data */
#define MT_TXCS1        82                              /* Serial line 1 tx ctrl */
#define MT_TXDB1        83                              /* Serial line 1 tx data */
#define MT_RXCS2        84                              /* Serial line 2 rx ctrl */
#define MT_RXDB2        85                              /* Serial line 2 rx data */
#define MT_TXCS2        86                              /* Serial line 2 tx ctrl */
#define MT_TXDB2        87                              /* Serial line 2 tx data */
#define MT_RXCS3        88                              /* Serial line 3 rx ctrl */
#define MT_RXDB3        89                              /* Serial line 3 rx data */
#define MT_TXCS3        90                              /* Serial line 3 tx ctrl */
#define MT_TXDB3        91                              /* Serial line 3 tx data */
#define MT_RXCD         92                              /* rx console data */
#define MT_CACHEX       93                              /* Cache invalidate */
#define MT_BINID        94                              /* BI node ident */
#define MT_BISTOP       95                              /* BI stop */
#define MT_MAX          95                              /* last valid IPR */

/* Machine specific reserved operand tests (all NOPs) */

#define ML_PA_TEST(r)
#define ML_LR_TEST(r)
#define ML_SBR_TEST(r)
#define ML_PXBR_TEST(r)
#define LP_AST_TEST(r)
#define LP_MBZ84_TEST(r)
#define LP_MBZ92_TEST(r)
#define MT_AST_TEST(r)  r = (r) & 07; \
                        if ((r) > AST_MAX) RSVD_OPND_FAULT

/* CPU */

#define CPU_MODEL_MODIFIERS                                                                     \
                        { MTAB_XTD|MTAB_VDV, 0, "MODEL", "MODEL={8200|8250}",                   \
                              &cpu_set_model, &cpu_show_model, NULL, "Set/Display processor model" }

/* Memory */

#define MAXMEMWIDTH     22                              /* max mem, std MS820 */
#define MAXMEMSIZE      (1 << MAXMEMWIDTH)
#define MAXMEMWIDTH_X   29                              /* max mem, extended */
#define MAXMEMSIZE_X    (1 << MAXMEMWIDTH_X)
#define INITMEMSIZE     (1 << MAXMEMWIDTH)              /* initial memory size */
#define MEMSIZE         (cpu_unit.capac)
#define ADDR_IS_MEM(x)  (((uint32) (x)) < MEMSIZE)
#define MEM_MODIFIERS   { UNIT_MSIZE, (1u << 22), NULL,  "4M", &cpu_set_size, NULL, NULL, "Set Memory to 4M bytes" },                   \
                        { UNIT_MSIZE, (1u << 23), NULL,  "8M", &cpu_set_size, NULL, NULL, "Set Memory to 8M bytes" },                   \
                        { UNIT_MSIZE, (1u << 24), NULL, "16M", &cpu_set_size, NULL, NULL, "Set Memory to 16M bytes" },                  \
                        { UNIT_MSIZE, (1u << 25), NULL, "32M", &cpu_set_size, NULL, NULL, "Set Memory to 32M bytes" },                  \
                        { UNIT_MSIZE, (1u << 25) + (1u << 24), NULL, "48M", &cpu_set_size, NULL, NULL, "Set Memory to 48M bytes" },     \
                        { UNIT_MSIZE, (1u << 26), NULL, "64M", &cpu_set_size, NULL, NULL, "Set Memory to 64M bytes" },                  \
                        { UNIT_MSIZE, (1u << 27), NULL, "128M", &cpu_set_size, NULL, NULL, "Set Memory to 128M bytes" },                \
                        { UNIT_MSIZE, (1u << 28), NULL, "256M", &cpu_set_size, NULL, NULL, "Set Memory to 256M bytes" },                \
                        { UNIT_MSIZE, (1u << 29), NULL, "512M", &cpu_set_size, NULL, NULL, "Set Memory to 512M bytes" },                \
                        { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "MEMORY", NULL, NULL, &cpu_show_memory, NULL, "Display memory configuration" }
extern t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, CONST void* desc);

/* Node window space */

#define WINAWIDTH       18                              /* VAXBI node window width */
#define WINBASE         0x20400000                      /* VAXBI node window base */
#define WINADDR(n)      (WINBASE + (n << WINAWIDTH))    /* node -> window addr */

/* Unibus I/O registers */

#define UBADDRWIDTH     18                              /* Unibus addr width */
#define UBADDRSIZE      (1u << UBADDRWIDTH)             /* Unibus addr length */
#define UBADDRMASK      (UBADDRSIZE - 1)                /* Unibus addr mask */
#define IOPAGEAWIDTH    13                              /* IO addr width */
#define IOPAGESIZE      (1u << IOPAGEAWIDTH)            /* IO page length */
#define IOPAGEMASK      (IOPAGESIZE - 1)                /* IO addr mask */
#define UBADDRBASE      WINADDR(TR_UBA)                 /* Unibus addr base */
#define IOPAGEBASE      (UBADDRBASE + 0x3E000)          /* IO page base */
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
#define ADDR_IS_REG(x)  ((((uint32) (x)) >= REGBASE) && \
                        (((uint32) (x)) < (REGBASE + REGSIZE)))
#define NEXUS_GETNEX(x) (((x) >> REG_V_NEXUS) & REG_M_NEXUS)
#define NEXUS_GETOFS(x) (((x) >> REG_V_OFS) & REG_M_OFS)

/* Watch Chip */

#define WATCHWIDTH      7                               /* WATCH addr width */
#define WATCHSIZE       (1u << REGAWIDTH)               /* WATCH length */
#define WATCHBASE       0x200B8000                      /* WATCH addr base */

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
#define VH_MUXES        4                               /* max # of DHU muxes */
#define DLX_LINES       16                              /* max # of KL11/DL11's */
#define DCX_LINES       16                              /* max # of DC11's */
#define DUP_LINES       8                               /* max # of DUP11's */
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

#define VEC_QBUS        0
#define VEC_Q           0000

/* Interrupt macros */

#define IVCL(dv)        ((IPL_##dv * 32) + INT_V_##dv)
#define NVCL(dv)        ((IPL_##dv * 32) + TR_##dv)
#define IREQ(dv)        int_req[IPL_##dv]
#define SET_INT(dv)     int_req[IPL_##dv] = int_req[IPL_##dv] | (INT_##dv)
#define CLR_INT(dv)     int_req[IPL_##dv] = int_req[IPL_##dv] & ~(INT_##dv)
#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* cond error return */

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

/* Function prototypes for system-specific unaligned support
   8200 treats unaligned like aligned? */

#define ReadIOU(p,l)        ReadIO (p,l)
#define ReadRegU(p,l)       ReadReg (p,l)
#define WriteIOU(p,v,l)     WriteIO (p, v, l)
#define WriteRegU(p,v,l)    WriteReg (p, v, l)

#include "pdp11_io_lib.h"
#include "vax_bi.h"

/* Function prototypes for virtual and physical memory interface (inlined) */

#include "vax_mmu.h"

#endif
