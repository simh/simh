/* vaxmod_defs.h: VAX model-specific definitions file

   Copyright (c) 1998-2019, Robert M Supnik

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

   05-May-19    RMS     Added Qbus memory space to ADDR_IS_IO test
   18-May-17    RMS     Added model-specific AST validation test
   20-Dec-13    RMS     Added prototypes for unaligned IO and register handling
   11-Dec-11    RMS     Moved all Qbus devices to BR4; deleted RP definitions
   25-Nov-11    RMS     Added VEC_QBUS definition
   29-Apr-07    RMS     Separated checks for PxBR and SBR
   17-May-06    RMS     Added CR11/CD11 support
   10-May-06    RMS     Added NOP'd reserved operand checking macros
   05-Oct-05    RMS     Added XU definitions for autoconfigure
   15-Jun-05    RMS     Added QDSS support
   12-Sep-04    RMS     Removed map_address prototype
   16-Jun-04    RMS     Added DHQ11 support
   21-Mar-04    RMS     Added RXV21 support
   25-Jan-04    RMS     Removed local debug logging support
                RMS,MP  Added "KA655X" support
   29-Dec-03    RMS     Added Q18 definition for PDP11 compatibility
   22-Dec-02    RMS     Added BDR halt enable definition
   11-Nov-02    RMS     Added log bits for XQ
   10-Oct-02    RMS     Added DEQNA/DELQA, multiple RQ, autoconfigure support
   29-Sep-02    RMS     Revamped bus support macros
   06-Sep-02    RMS     Added TMSCP support
   14-Jul-02    RMS     Added additional console halt codes
   28-Apr-02    RMS     Fixed DZV vector base and number of lines

   This file covers the KA65x ("Mayfair") series of CVAX-based Qbus systems.
   The simulator defines an extended physical memory variant of the KA655,
   called the KA655X.  It has a maximum memory size of 512MB instead of 64MB.

   System memory map

        0000 0000 - 03FF FFFF           main memory (KA655)
        0400 0000 - 0FFF FFFF           reserved (KA655), main memory (KA655X)
        1000 0000 - 13FF FFFF           cache diagnostic space (KA655), main memory (KA655X)
        1400 0000 - 1FFF FFFF           reserved (KA655), main memory (KA655X)

        2000 0000 - 2000 1FFF           Qbus I/O page
        2000 2000 - 2003 FFFF           reserved
        2004 0000 - 2005 FFFF           ROM space, halt protected
        2006 0000 - 2007 FFFF           ROM space, halt unprotected
        2008 0000 - 201F FFFF           Local register space
        2020 0000 - 2FFF FFFF           reserved
        3000 0000 - 303F FFFF           Qbus memory space
        3400 0000 - 3FFF FFFF           reserved
*/

#ifdef FULL_VAX                     /* subset VAX */
#undef FULL_VAX
#endif

#ifdef CMPM_VAX
#undef CMPM_VAX                     /* No Compatibility Mode */
#endif

#ifndef VAXMOD_DEFS_H_
#define VAXMOD_DEFS_H_ 1

/* Microcode constructs */

#define CVAX_SID        (10 << 24)                      /* system ID */
#define CVAX_UREV       6                               /* ucode revision */
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
#define MCHK_READ       0x80                            /* read check */
#define MCHK_WRITE      0x82                            /* write check */

/* Machine specific IPRs */

#define MT_CADR         37
#define MT_MSER         39
#define MT_CONPC        42
#define MT_CONPSL       43
#define MT_IORESET      55
#define MT_MAX          63                              /* last valid IPR */

/* Memory system error register */

#define MSER_HM         0x80                            /* hit/miss */
#define MSER_CPE        0x40                            /* CDAL par err */
#define MSER_CPM        0x20                            /* CDAL mchk */

/* Cache disable register */

#define CADR_RW         0xF3
#define CADR_MBO        0x0C

/* Memory */

#define MAXMEMWIDTH     26                              /* max mem, std KA655 */
#define MAXMEMSIZE      (1 << MAXMEMWIDTH)              /* max mem size */
#define MAXMEMWIDTH_X   29                              /* max mem, KA655X */
#define MAXMEMSIZE_X    (1 << MAXMEMWIDTH_X)
#define INITMEMSIZE     (1 << 24)                       /* initial memory size */
#define MEMSIZE         (cpu_unit.capac)
#define ADDR_IS_MEM(x)  (((uint32) (x)) < MEMSIZE)
#define MEM_MODIFIERS   { UNIT_MSIZE, (1u << 24), NULL, "16M", &cpu_set_size, NULL, NULL, "Set Memory to 16M bytes" },              \
                        { UNIT_MSIZE, (1u << 25), NULL, "32M", &cpu_set_size, NULL, NULL, "Set Memory to 32M bytes" },              \
                        { UNIT_MSIZE, (1u << 25) + (1u << 24), NULL, "48M", &cpu_set_size, NULL, NULL, "Set Memory to 48M bytes" }, \
                        { UNIT_MSIZE, (1u << 26), NULL, "64M", &cpu_set_size, NULL, NULL, "Set Memory to 64M bytes" },              \
                        { UNIT_MSIZE, (1u << 27), NULL, "128M", &cpu_set_size, NULL, NULL, "Set Memory to 128M bytes" },            \
                        { UNIT_MSIZE, (1u << 28), NULL, "256M", &cpu_set_size, NULL, NULL, "Set Memory to 256M bytes" },            \
                        { UNIT_MSIZE, (1u << 29), NULL, "512M", &cpu_set_size, NULL, NULL, "Set Memory to 512M bytes" },            \
                        { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "MEMORY", NULL, NULL, &cpu_show_memory, NULL, "Display memory configuration" }
extern t_stat cpu_show_memory (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
#define CPU_MODEL_MODIFIERS { MTAB_XTD|MTAB_VDV, 0, "MODEL", "MODEL={VAXserver|MicroVAX|VAXstation}",       \
                              &cpu_set_model, &cpu_show_model, NULL, "Set/Display processor model" },       \
                            { MTAB_XTD|MTAB_VDV, 0,          "AUTOBOOT",   "AUTOBOOT",                      \
                              &sysd_set_halt, &sysd_show_halt, NULL, "Enable autoboot (Disable Halt)" },    \
                            { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "NOAUTOBOOT", "NOAUTOBOOT",                    \
                              &sysd_set_halt, &sysd_show_halt, NULL, "Disable autoboot (Enable Halt)" },


/* Cache diagnostic space */

#define CDAAWIDTH       16                              /* cache dat addr width */
#define CDASIZE         (1u << CDAAWIDTH)               /* cache dat length */
#define CDAMASK         (CDASIZE - 1)                   /* cache dat mask */
#define CTGAWIDTH       10                              /* cache tag addr width */
#define CTGSIZE         (1u << CTGAWIDTH)               /* cache tag length */
#define CTGMASK         (CTGSIZE - 1)                   /* cache tag mask */
#define CDGSIZE         (CDASIZE * CTGSIZE)             /* diag addr length */
#define CDGBASE         0x10000000                      /* diag addr base */
#define CDG_GETROW(x)   (((x) & CDAMASK) >> 2)
#define CDG_GETTAG(x)   (((x) >> CDAAWIDTH) & CTGMASK)
#define CTG_V           (1u << (CTGAWIDTH + 0))         /* tag valid */
#define CTG_WP          (1u << (CTGAWIDTH + 1))         /* wrong parity */
#define ADDR_IS_CDG(x)  ((((uint32) (x)) >= CDGBASE) && \
                        (((uint32) (x)) < (CDGBASE + CDGSIZE)))

/* Qbus I/O registers */

#define IOPAGEAWIDTH    13                              /* IO addr width */
#define IOPAGESIZE      (1u << IOPAGEAWIDTH)            /* IO page length */
#define IOPAGEMASK      (IOPAGESIZE - 1)                /* IO addr mask */
#define IOPAGEBASE      0x20000000                      /* IO page base */
#define ADDR_IS_IOP(x)  ((((uint32) (x)) >= IOPAGEBASE) && \
                        (((uint32) (x)) < (IOPAGEBASE + IOPAGESIZE)))

/* Read only memory - appears twice */

#define ROMAWIDTH       17                              /* ROM addr width */
#define ROMSIZE         (1u << ROMAWIDTH)               /* ROM length */
#define ROMAMASK        (ROMSIZE - 1)                   /* ROM addr mask */
#define ROMBASE         0x20040000                      /* ROM base */
#define ADDR_IS_ROM(x)  ((((uint32) (x)) >= ROMBASE) && \
                        (((uint32) (x)) < (ROMBASE + ROMSIZE + ROMSIZE)))

/* Local register space */

#define REGAWIDTH       19                              /* REG addr width */
#define REGSIZE         (1u << REGAWIDTH)               /* REG length */
#define REGBASE         0x20080000                      /* REG addr base */

/* KA655 board registers */

#define KAAWIDTH        3                               /* KA reg width */
#define KASIZE          (1u << KAAWIDTH)                /* KA reg length */
#define KABASE          (REGBASE + 0x4000)              /* KA650 addr base */

/* CQBIC registers */

#define CQBICSIZE       (5 << 2)                        /* 5 registers */
#define CQBICBASE       (REGBASE)                       /* CQBIC addr base */
#define CQMAPASIZE      15                              /* map addr width */
#define CQMAPSIZE       (1u << CQMAPASIZE)              /* map length */
#define CQMAPAMASK      (CQMAPSIZE - 1)                 /* map addr mask */
#define CQMAPBASE       (REGBASE + 0x8000)              /* map addr base */
#define CQIPCSIZE       2                               /* 2 bytes only */
#define CQIPCBASE       (REGBASE + 0x1F40)              /* ipc reg addr */

/* CMCTL registers */

// #define CMCTLSIZE    (18 << 2)                       /* 18 registers */
#define CMCTLSIZE       (19 << 2)                       /* KA655X extra reg */
#define CMCTLBASE       (REGBASE + 0x100)               /* CMCTL addr base */

/* SSC registers */

#define SSCSIZE         0x150                           /* SSC size */
#define SSCBASE         0x20140000                      /* SSC base */

/* Non-volatile RAM - 1KB long */

#define NVRAWIDTH       10                              /* NVR addr width */
#define NVRSIZE         (1u << NVRAWIDTH)               /* NVR length */
#define NVRAMASK        (NVRSIZE - 1)                   /* NVR addr mask */
#define NVRBASE         0x20140400                      /* NVR base */
#define ADDR_IS_NVR(x)  ((((uint32) (x)) >= NVRBASE) && \
                        (((uint32) (x)) < (NVRBASE + NVRSIZE)))

/* CQBIC Qbus memory space (seen from CVAX) */

#define CQMAWIDTH       22                              /* Qmem addr width */
#define CQMSIZE         (1u << CQMAWIDTH)               /* Qmem length */
#define CQMAMASK        (CQMSIZE - 1)                   /* Qmem addr mask */
#define CQMBASE         0x30000000                      /* Qmem base */
#define ADDR_IS_CQM(x)  ((((uint32) (x)) >= CQMBASE) && \
                        (((uint32) (x)) < (CQMBASE + CQMSIZE)))

/* Reflect to IO on either IO space or Qbus memory */

#define ADDR_IS_IO(x)   (ADDR_IS_IOP(x) || ADDR_IS_CQM(x))

/* QVSS memory space */

#define QVMAWIDTH       18                              /* QVSS mem addr width */
#define QVMSIZE         (1u << QVMAWIDTH)               /* QVSS mem length */
#define QVMAMASK        (QVMSIZE - 1)                   /* QVSS mem addr mask */
#define QVMBASE         (CQMBASE + CQMSIZE - QVMSIZE)   /* QVSS mem base - end of Qbus memory space */
#define ADDR_IS_QVM(x)  (vc_buf &&                      \
                         (((uint32) (x)) >= QVMBASE) && \
                         (((uint32) (x)) < (QVMBASE + QVMSIZE)))
extern uint32 *vc_buf;

/* Machine specific reserved operand tests (mostly NOPs) */

#define ML_PA_TEST(r)
#define ML_LR_TEST(r)
#define ML_SBR_TEST(r)
#define ML_PXBR_TEST(r)
#define LP_AST_TEST(r)
#define LP_MBZ84_TEST(r)
#define LP_MBZ92_TEST(r)

#define MT_AST_TEST(r)  if ((r) > AST_MAX) RSVD_OPND_FAULT(MT_AST_TEST)


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
    DEVICE              *dptr;                          /* back pointer to related device */
                                                        /* Populated by auto-configure */
    } DIB;

/* Qbus I/O page layout - see pdp11_io_lib.c for address layout details */

#define IOBA_AUTO       (0)                             /* Assigned by Auto Configure */


/* The KA65x maintains 4 separate hardware IPL levels, IPL 17 to IPL 14;
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
#define INT_V_DMCRX     22                              /* DMC11 */
#define INT_V_DMCTX     23
#define INT_V_TDRX      24                              /* TU58 */
#define INT_V_TDTX      25

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
#define INT_DMCRX       (1u << INT_V_DMCRX)
#define INT_DMCTX       (1u << INT_V_DMCTX)
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
#define IPL_DMCRX       (0x14 - IPL_HMIN)
#define IPL_DMCTX       (0x14 - IPL_HMIN)
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

/* Function prototypes for I/O */

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf);
int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf);
int32 Map_WriteB (uint32 ba, int32 bc, const uint8 *buf);
int32 Map_WriteW (uint32 ba, int32 bc, const uint16 *buf);

#include "pdp11_io_lib.h"

extern t_stat sysd_set_halt (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern t_stat sysd_show_halt (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* Function prototypes for system-specific unaligned support */

int32 ReadIOU (uint32 pa, int32 lnt);
int32 ReadRegU (uint32 pa, int32 lnt);
void WriteIOU (uint32 pa, int32 val, int32 lnt);
void WriteRegU (uint32 pa, int32 val, int32 lnt);

/* Function prototypes for virtual and physical memory interface (inlined) */

#include "vax_mmu.h"

#endif
