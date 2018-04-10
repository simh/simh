/* vax610_defs.h: MicroVAX I model-specific definitions file

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

   15-Feb-2012  MB      First Version

   This file covers the MicroVAX I

   System memory map

      0000 0000 - 003F FFFF             main memory
      0040 0000 - 1FFF FFFF             reserved
      2000 0000 - 2000 1FFF             qbus address space
      2000 2000 - 3FFF FFFF             reserved
*/

#ifdef FULL_VAX                                         /* subset VAX */
#undef FULL_VAX
#endif

#ifndef VAX_610_DEFS_H_
#define VAX_610_DEFS_H_ 1

/* Microcode constructs */

#define VAX610_SID      (7 << 24)                       /* system ID */
#define VAX610_FLOAT    (1 << 16)                       /* floating point type */
#define VAX610_MREV     (5 << 8)                        /* microcode revision */
#define VAX610_HWREV    1                               /* hardware revision */
#define CON_HLTPIN      0x0200                          /* external CPU halt */
#define CON_PWRUP       0x0300                          /* powerup code */
#define CON_HLTINS      0x0600                          /* HALT instruction */
#define CON_BADPSL      0x4000                          /* invalid PSL flag */
#define CON_MAPON       0x8000                          /* mapping on flag */
#define MCHK_TBM_P0     0x05                            /* PPTE in P0 */
#define MCHK_TBM_P1     0x06                            /* PPTE in P1 */
#define MCHK_M0_P0      0x07                            /* PPTE in P0 */
#define MCHK_M0_P1      0x08                            /* PPTE in P1 */
#define MCHK_INTIPL     0x09                            /* invalid ireq */
#define MCHK_READ       0x02                            /* read check */
#define MCHK_WRITE      0x02                            /* write check */

/* Machine specific IPRs */

#define MT_TBDR         36                              /* Translation Buffer Disable */
#define MT_CADR         37                              /* Cache Disable Register */
#define MT_MCESR        38                              /* Machine Check Error Summary */
#define MT_CAER         39                              /* Cache Error Register */
#define MT_CONISP       41                              /* Console Saved ISP */
#define MT_CONPC        42                              /* Console Saved PC */
#define MT_CONPSL       43                              /* Console Saved PSL */
#define MT_SBIFS        48                              /* SBI fault status */
#define MT_SBIS         49                              /* SBI silo */
#define MT_SBISC        50                              /* SBI silo comparator */
#define MT_SBIMT        51                              /* SBI maint */
#define MT_SBIER        52                              /* SBI error */
#define MT_SBITA        53                              /* SBI timeout addr */
#define MT_SBIQC        54                              /* SBI timeout clear */
#define MT_IORESET      55                              /* I/O Bus Reset */
#define MT_TBDATA       59                              /* Translation Buffer Data */
#define MT_MBRK         60                              /* microbreak */
#define MT_MAX          63                              /* last valid IPR */

/* CPU */

#define CPU_MODEL_MODIFIERS { MTAB_XTD|MTAB_VDV, 0, "LEDS", NULL,                               \
                              NULL, &cpu_show_leds, NULL, "Display the CPU LED values" },       \
                            { MTAB_XTD|MTAB_VDV, 0, "MODEL", "MODEL={MicroVAX|VAXStation}",     \
                              &cpu_set_model, &cpu_show_model, NULL, "Set/Show the simulator CPU Model" }

/* QVSS memory space */

#define QVMAWIDTH       18                              /* QVSS mem addr width */
#define QVMSIZE         (1u << QVMAWIDTH)               /* QVSS mem length */
#define QVMAMASK        (QVMSIZE - 1)                   /* QVSS mem addr mask */
#define QVMBASE         0x3C0000                        /* QVSS mem base */

/* Memory */

#define MAXMEMWIDTH     22                              /* max mem, KA610 */
#define MAXMEMSIZE      (1 << MAXMEMWIDTH)              /* max mem size */
#define MAXMEMWIDTH_X   22                              /* max mem, KA610 */
#define MAXMEMSIZE_X    (1 << MAXMEMWIDTH_X)
#define INITMEMSIZE     (1 << 22)                       /* initial memory size */
#define VS_MEMSIZE      (((cpu_unit.capac > QVMBASE) ? QVMBASE : cpu_unit.capac))
#define MEMSIZE         (cpu_unit.capac)
#define ADDR_IS_MEM(x)  (((uint32) (x)) < (sys_model ? VS_MEMSIZE : MEMSIZE))
#undef  PAMASK
#define PAMASK          0x203FFFFF                      /* KA610 needs a special mask */
#define MEM_MODIFIERS   { UNIT_MSIZE, (1u << 19), NULL, "512K", &cpu_set_size, NULL, NULL, "Set Memory to 512K bytes" },\
                        { UNIT_MSIZE, (1u << 19) + (1u << 18), NULL, "768K",   &cpu_set_size, NULL, NULL, "Set Memory to 1M bytes" },  \
                        { UNIT_MSIZE, (1u << 20), NULL, "1M",   &cpu_set_size, NULL, NULL, "Set Memory to 1M bytes" },  \
                        { UNIT_MSIZE, (1u << 21), NULL, "2M",   &cpu_set_size, NULL, NULL, "Set Memory to 2M bytes" },  \
                        { UNIT_MSIZE, (1u << 22), NULL, "4M",   &cpu_set_size, NULL, NULL, "Set Memory to 4M bytes" },  \
                        { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "MEMORY", NULL, NULL, &cpu_show_memory, NULL, "Display memory configuration" }
extern t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, CONST void* desc);

/* Qbus I/O page */

#define IOPAGEAWIDTH    13                              /* IO addr width */
#define IOPAGESIZE      (1u << IOPAGEAWIDTH)            /* IO page length */
#define IOPAGEMASK      (IOPAGESIZE - 1)                /* IO addr mask */
#define IOPAGEBASE      0x20000000                      /* IO page base */
#define ADDR_IS_IO(x)   ((((uint32) (x)) >= IOPAGEBASE) && \
                        (((uint32) (x)) < (IOPAGEBASE + IOPAGESIZE)))

/* Other address spaces */

#define ADDR_IS_CDG(x)  (0)
#define ADDR_IS_ROM(x)  (0)
#define ADDR_IS_NVR(x)  (0)

/* Machine specific reserved operand tests (mostly NOPs) */

#define ML_PA_TEST(r)
#define ML_LR_TEST(r)
#define ML_SBR_TEST(r)
#define ML_PXBR_TEST(r)
#define LP_AST_TEST(r)
#define LP_MBZ84_TEST(r)
#define LP_MBZ92_TEST(r)

#define MT_AST_TEST(r)  if ((r) > AST_MAX) RSVD_OPND_FAULT

/* Qbus I/O modes */

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
#define MT_MAXFR        (1 << 16)                       /* magtape max rec */

#define DEV_V_UBUS      (DEV_V_UF + 0)                  /* Unibus */
#define DEV_V_QBUS      (DEV_V_UF + 1)                  /* Qbus */
#define DEV_V_Q18       (DEV_V_UF + 2)                  /* Qbus, mem <= 256KB */
#define DEV_UBUS        (1u << DEV_V_UBUS)
#define DEV_QBUS        (1u << DEV_V_QBUS)
#define DEV_Q18         (1u << DEV_V_Q18)

#define UNIBUS          FALSE                           /* 22b only */

#define DEV_RDX         16                              /* default device radix */

/* Device information block */

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

/* Qbus I/O page layout - see pdp11_io_lib.c for address layout details */

#define IOBA_AUTO       (0)                             /* Assigned by Auto Configure */


/* The KA610 maintains 4 separate hardware IPL levels, IPL 17 to IPL 14;
   however, DEC Qbus controllers all interrupt on IPL 14
   Within each IPL, priority is right to left
*/

/* IPL 17 */

/* IPL 16 */

#define INT_V_CLK       0                               /* clock */

/* IPL 15 */

/* IPL 14 - devices through RY are IPL 15 on Unibus systems */

#define INT_V_RQ        0                               /* RQDX3 */
#define INT_V_RL        1                               /* RLV12/RL02 */
#define INT_V_DZRX      2                               /* DZ11 */
#define INT_V_DZTX      3
#define INT_V_TS        4                               /* TS11/TSV05 */
#define INT_V_TQ        5                               /* TMSCP */
#define INT_V_XQ        6                               /* DEQNA/DELQA */
#define INT_V_RY        7                               /* RXV21 */

#define INT_V_TTI       8                               /* console */
#define INT_V_TTO       9
#define INT_V_PTR       10                              /* PC11 */
#define INT_V_PTP       11
#define INT_V_LPT       12                              /* LP11 */
#define INT_V_CSI       13                              /* SSC cons UART */
#define INT_V_CSO       14
#define INT_V_TMR0      15                              /* SSC timers */
#define INT_V_TMR1      16
#define INT_V_VHRX      17                              /* DHQ11 */
#define INT_V_VHTX      18 
#define INT_V_QDSS      19                              /* QDSS */
#define INT_V_CR        20
#define INT_V_QVSS      21                              /* QVSS */
#define INT_V_TDRX      22
#define INT_V_TDTX      23

#define INT_CLK         (1u << INT_V_CLK)
#define INT_RQ          (1u << INT_V_RQ)
#define INT_RL          (1u << INT_V_RL)
#define INT_DZRX        (1u << INT_V_DZRX)
#define INT_DZTX        (1u << INT_V_DZTX)
#define INT_TS          (1u << INT_V_TS)
#define INT_TQ          (1u << INT_V_TQ)
#define INT_XQ          (1u << INT_V_XQ)
#define INT_RY          (1u << INT_V_RY)
#define INT_TTI         (1u << INT_V_TTI)
#define INT_TTO         (1u << INT_V_TTO)
#define INT_PTR         (1u << INT_V_PTR)
#define INT_PTP         (1u << INT_V_PTP)
#define INT_LPT         (1u << INT_V_LPT)
#define INT_CSI         (1u << INT_V_CSI)
#define INT_CSO         (1u << INT_V_CSO)
#define INT_TMR0        (1u << INT_V_TMR0)
#define INT_TMR1        (1u << INT_V_TMR1)
#define INT_VHRX        (1u << INT_V_VHRX)
#define INT_VHTX        (1u << INT_V_VHTX)
#define INT_QDSS        (1u << INT_V_QDSS)
#define INT_CR          (1u << INT_V_CR)
#define INT_QVSS        (1u << INT_V_QVSS)
#define INT_TDRX        (1u << INT_V_TDRX)
#define INT_TDTX        (1u << INT_V_TDTX)

#define IPL_CLK         (0x16 - IPL_HMIN)                       /* relative IPL */
#define IPL_RQ          (0x14 - IPL_HMIN)
#define IPL_RL          (0x14 - IPL_HMIN)
#define IPL_DZRX        (0x14 - IPL_HMIN)
#define IPL_DZTX        (0x14 - IPL_HMIN)
#define IPL_TS          (0x14 - IPL_HMIN)
#define IPL_TQ          (0x14 - IPL_HMIN)
#define IPL_XQ          (0x14 - IPL_HMIN)
#define IPL_RY          (0x14 - IPL_HMIN)
#define IPL_TTI         (0x14 - IPL_HMIN)
#define IPL_TTO         (0x14 - IPL_HMIN)
#define IPL_PTR         (0x14 - IPL_HMIN)
#define IPL_PTP         (0x14 - IPL_HMIN)
#define IPL_LPT         (0x14 - IPL_HMIN)
#define IPL_CSI         (0x14 - IPL_HMIN)
#define IPL_CSO         (0x14 - IPL_HMIN)
#define IPL_TMR0        (0x14 - IPL_HMIN)
#define IPL_TMR1        (0x14 - IPL_HMIN)
#define IPL_VHRX        (0x14 - IPL_HMIN)
#define IPL_VHTX        (0x14 - IPL_HMIN)
#define IPL_QDSS        (0x14 - IPL_HMIN)
#define IPL_CR          (0x14 - IPL_HMIN)
#define IPL_QVSS        (0x14 - IPL_HMIN)
#define IPL_TDRX        (0x14 - IPL_HMIN)
#define IPL_TDTX        (0x14 - IPL_HMIN)

#define IPL_HMAX        0x17                            /* highest hwre level */
#define IPL_HMIN        0x14                            /* lowest hwre level */
#define IPL_HLVL        (IPL_HMAX - IPL_HMIN + 1)       /* # hardware levels */
#define IPL_SMAX        0xF                             /* highest swre level */

/* Device vectors */

#define VEC_AUTO        (0)                             /* Assigned by Auto Configure */
#define VEC_FLOAT       (0)                             /* Assigned by Auto Configure */

#define VEC_QBUS        1                               /* Qbus system */
#define VEC_SET         0x201                           /* Vector bits to set in Qbus vectors */

/* Interrupt macros */

#define IVCL(dv)        ((IPL_##dv * 32) + INT_V_##dv)
#define IREQ(dv)        int_req[IPL_##dv]
#define SET_INT(dv)     int_req[IPL_##dv] = int_req[IPL_##dv] | (INT_##dv)
#define CLR_INT(dv)     int_req[IPL_##dv] = int_req[IPL_##dv] & ~(INT_##dv)
#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* cond error return */
extern int32 int_req[IPL_HLVL];                         /* intr, IPL 14-17 */

/* Logging */

#define LOG_CPU_I       0x1                             /* intexc */
#define LOG_CPU_R       0x2                             /* REI */
#define LOG_CPU_P       0x4                             /* context */

/* System model */

extern int32 sys_model;

/* Function prototypes for I/O */

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf);
int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf);
int32 Map_WriteB (uint32 ba, int32 bc, const uint8 *buf);
int32 Map_WriteW (uint32 ba, int32 bc, const uint16 *buf);

/* Function prototypes for system-specific unaligned support */

int32 ReadIOU (uint32 pa, int32 lnt);
int32 ReadRegU (uint32 pa, int32 lnt);
void WriteIOU (uint32 pa, int32 val, int32 lnt);
void WriteRegU (uint32 pa, int32 val, int32 lnt);

t_stat cpu_show_leds (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

#include "pdp11_io_lib.h"

/* Function prototypes for virtual and physical memory interface (inlined) */

#include "vax_mmu.h"

#endif
