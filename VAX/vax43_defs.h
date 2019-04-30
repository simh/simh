/* vax43_defs.h: MicroVAX 3100 M76 model-specific definitions file

   Copyright (c) 2019, Matt Burke
   This module incorporates code from SimH, Copyright (c) 1998-2008, Robert M Supnik

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

   This file covers the KA43 ("RigelMAX") systems.

   System memory map

        0000 0000 - 01FF FFFF           main memory
        1000 0000 - 1001 FFFF           cache diagnostic space
        2002 0000 - 2002 0003           configuration/test register
        2004 0000 - 2007 FFFF           ROM space
        2008 0000 - 2008 001F           local register space
        2009 0000 - 2009 007F           network address ROM
        200A 0000 - 200A 000F           serial line controller
        200B 0000 - 200B 00FF           watch chip registers
        200C 0080 - 200C 00FF           scsi controller A
        200C 0180 - 200C 01FF           scsi controller B
        200E 0000 - 200E 0007           ethernet controller
        200F 0000 - 200F 003F           monochrome video cursor chip
        2010 0000 - 2013 FFFF           option ROMs
        202D 0000 - 202E FFFF           128k disk data buffer
        2100 0000 - 2011 FFFF           cache tag store
        2110 0000 - 2110 0003           software error summary register
        2800 0000 - 2009 FFFF           ? diagnostic space
        3000 0000 - 3001 FFFF           monochrome video RAM
        3800 0000 - 3BFF FFFF           SPX video RAM
*/

#ifdef FULL_VAX                                         /* subset VAX */
#undef FULL_VAX
#endif

#ifndef _VAX_43A_DEFS_H_
#define _VAX_43A_DEFS_H_ 1

/* Microcode constructs */

#define VAX43A_SID      (11 << 24)                      /* system ID */
#define VAX43A_UREV     5                               /* ucode revision */
#define CON_HLTPIN      0x0200                          /* external CPU halt */
#define CON_PWRUP       0x0300                          /* powerup code */
#define CON_HLTINS      0x0600                          /* HALT instruction */
#define CON_DBLMCK      0x0500                          /* Machine check in machine check */
#define CON_BADPSL      0x4000                          /* invalid PSL flag */
#define CON_MAPON       0x8000                          /* mapping on flag */
#define MCHK_READ       0x80                            /* read check */
#define MCHK_WRITE      0x82                            /* write check */

/* Machine specific IPRs */

#define MT_CADR         37                              /* Cache disable reg */
#define MT_MCESR        38                              /* Machine check error/status reg */
#define MT_CAER         39                              /* Cache error reg */
#define MT_ACCS         40                              /* FPA control */
#define MT_CONISP       41                              /* Console Saved ISP */
#define MT_CONPC        42                              /* Console Saved PC */
#define MT_CONPSL       43                              /* Console Saved PSL */
#define MT_PCTAG        124                             /* Primary cache tag reg */
#define MT_PCIDX        125                             /* Primary cache index reg */
#define MT_PCERR        126                             /* Primary cache error reg */
#define MT_PCSTS        127                             /* Primary cache status reg */
#define MT_MAX          127                             /* last valid IPR */

/* Cache disable register */

#define CADR_RW         0xF3
#define CADR_MBO        0x0C

/* CPU */

#define CPU_MODEL_MODIFIERS \
                        { MTAB_XTD|MTAB_VDV, 0, "MODEL", "MODEL={MICROVAX|VAXSTATION|VAXSTATIONSPX}", \
                          cpu_set_model, &cpu_show_model, NULL, "Set/Show the simulator CPU Model" },

/* Memory */

#define MAXMEMWIDTH     25                              /* max mem, std KA43A */
#define MAXMEMSIZE      (1 << MAXMEMWIDTH)              /* max mem size */
#define MAXMEMWIDTH_X   25                              /* max mem, KA43A */
#define MAXMEMSIZE_X    (1 << MAXMEMWIDTH_X)
#define INITMEMSIZE     (1 << 24)                       /* initial memory size */
#define MEMSIZE         (cpu_unit.capac)
#define ADDR_IS_MEM(x)  (((uint32) (x)) < MEMSIZE)
#define MEM_MODIFIERS   { UNIT_MSIZE, (1u << 22), NULL, "4M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 23), NULL, "8M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 23) + (1u << 22), NULL, "12M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 24), NULL, "16M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 24) + (1u << 22), NULL, "20M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 24) + (1u << 23), NULL, "24M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 24) + (1u << 23) + (1u << 22), NULL, "28M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 25), NULL, "32M", &cpu_set_size }

/* Cache diagnostic space */

#define CDAAWIDTH       17                              /* cache dat addr width */
#define CDASIZE         (1u << CDAAWIDTH)               /* cache dat length */
#define CDAMASK         (CDASIZE - 1)                   /* cache dat mask */
#define CTGAWIDTH       17                              /* cache tag addr width */
#define CTGSIZE         (1u << CTGAWIDTH)               /* cache tag length */
#define CTGMASK         (CTGSIZE - 1)                   /* cache tag mask */
#define CTGBASE         0x21000000                      /* diag addr base */
#define CDGSIZE         (CDASIZE)                       /* diag addr length */
#define CDGBASE         0x10000000                      /* diag addr base */
#define CDG_GETROW(x)   (((x) & CDAMASK) >> 2)
#define CDG_GETTAG(x)   (((x) >> CDAAWIDTH) & CTGMASK)
#define CTG_V           (1u << (CTGAWIDTH + 0))         /* tag valid */
#define CTG_WP          (1u << (CTGAWIDTH + 1))         /* wrong parity */
#define ADDR_IS_CDG(x)  ((((uint32) (x)) >= CDGBASE) && \
                        (((uint32) (x)) < (CDGBASE + CDGSIZE)))

/* Config/test register */

#define CFGSIZE         4                               /* CFG length */
#define CFGBASE         0x20020000                      /* CFG base */

/* Read only memory */

#define ROMAWIDTH       18                              /* ROM addr width */
#define ROMSIZE         (1u << ROMAWIDTH)               /* ROM length */
#define ROMAMASK        (ROMSIZE - 1)                   /* ROM addr mask */
#define ROMBASE         0x20040000                      /* ROM base */
#define ADDR_IS_ROM(x)  ((((uint32) (x)) >= ROMBASE) && \
                        (((uint32) (x)) < (ROMBASE + ROMSIZE)))

/* KA43A board registers */

#define KAAWIDTH        5                               /* REG addr width */
#define KASIZE          (1u << KAAWIDTH)                /* REG length */
#define KABASE          0x20080000                      /* REG addr base */

/* Network address ROM */

#define NARAWIDTH       7                               /* NAR addr width */
#define NARSIZE         (1u << NARAWIDTH)               /* NAR length */
#define NARAMASK        (NARSIZE - 1)                   /* NAR addr mask */
#define NARBASE         0x20090000                      /* NAR base */

/* Serial line controller */

#define DZSIZE          0x10                            /* DZ length */
#define DZBASE          0x200A0000                      /* DZ base */

/* Non-volatile RAM - 1KB Bytes long */

#define NVRAWIDTH       10                              /* NVR addr width */
#define NVRSIZE         (1u << NVRAWIDTH)               /* NVR length */
#define NVRAMASK        (NVRSIZE - 1)                   /* NVR addr mask */
#define NVRBASE         0x200B0000                      /* NVR base */
#define ADDR_IS_NVR(x)  ((((uint32) (x)) >= NVRBASE) && \
                        (((uint32) (x)) < (NVRBASE + NVRSIZE)))

/* SCSI disk controller */

#define RZSIZE          0x50                            /* RZ length */
#define RZBASE          0x200C0080                      /* RZ base */
#define RZBBASE         0x200C0180                      /* RZB base */

/* LANCE Ethernet controller */

#define XSSIZE          0x8                             /* XS length */
#define XSBASE          0x200E0000                      /* XS base */

/* Cursor chip */

#define CURSIZE         0x40                            /* CUR length */
#define CURBASE         0x200F0000                      /* CUR base */

/* Option ROMs */

#define ORAWIDTH        20                              /* OR addr width */
#define ORSIZE          (1u << ORAWIDTH)                /* OR length */
#define ORMASK          (ORSIZE - 1)                    /* OR addr mask */
#define ORBASE          0x20100000                      /* OR base */

/* 128k disk buffer */

#define D128AWIDTH      17                              /* D128 addr width */
#define D128SIZE        (1u << D128AWIDTH)              /* D128 length */
#define D128AMASK       (D128SIZE - 1)                  /* D128 addr mask */
#define D128BASE        0x202D0000                      /* D128 base */

/* VC memory space */

#define VCAWIDTH        18                              /* VC mem addr width */
#define VCSIZE          (1u << VCAWIDTH)                /* VC mem length */
#define VCAMASK         (VCSIZE - 1)                    /* VC mem addr mask */
#define VCBASE          0x30000000                      /* VC mem base */

/* VE memory space */

#define VEAWIDTH        26                              /* VE mem addr width */
#define VESIZE          (1u << VEAWIDTH)                /* VE mem length */
#define VEAMASK         (VESIZE - 1)                    /* VE mem addr mask */
#define VEBASE          0x38000000                      /* VE mem base */

/* Other address spaces */

#define ADDR_IS_IO(x)   (0)

/* Machine specific reserved operand tests (mostly NOPs) */

#define ML_PA_TEST(r)
#define ML_LR_TEST(r)
#define ML_SBR_TEST(r)
#define ML_PXBR_TEST(r)
#define LP_AST_TEST(r)
#define LP_MBZ84_TEST(r)
#define LP_MBZ92_TEST(r)

#define MT_AST_TEST(r)  if ((r) > AST_MAX) RSVD_OPND_FAULT(MT_AST_TEST)

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

/* SCSI Bus */

#define RZ_SCSI_ID      6                               /* initiator SCSI id */

/* I/O system definitions */

#define MT_MAXFR        (1 << 16)                       /* magtape max rec */

#define DEV_V_4XX       (DEV_V_UF + 0)                  /* KA4xx I/O */
#define DEV_4XX         (1u << DEV_V_4XX)

#define DEV_RDX         16                              /* default device radix */

/* Device information block */

#define VEC_DEVMAX      4                               /* max device vec */

typedef struct {
    int32               rom_index;                      /* option ROM index */
    uint8               *rom_array;                     /* option ROM code */
    t_addr              rom_size;                       /* option ROM size */
    } DIB;

/* Within each IPL, priority is left to right */

/* IPL 14 */

#define INT_V_SCA       0                               /* storage controller 1 */
#define INT_V_SCB       1                               /* storage controller 2 */
#define INT_V_VC2       2                               /* video secondary */
#define INT_V_VC1       3                               /* video primary */
#define INT_V_XS2       4                               /* network secondary */
#define INT_V_XS1       5                               /* network primary */
#define INT_V_DZTX      6                               /* serial transmitter */
#define INT_V_DZRX      7                               /* serial receiver */

#define INT_SCA         (1u << INT_V_SCA)
#define INT_SCB         (1u << INT_V_SCB)
#define INT_VC2         (1u << INT_V_VC2)
#define INT_VC1         (1u << INT_V_VC1)
#define INT_XS2         (1u << INT_V_XS2)
#define INT_XS1         (1u << INT_V_XS1)
#define INT_DZTX        (1u << INT_V_DZTX)
#define INT_DZRX        (1u << INT_V_DZRX)

#define IPL_CLK         0x16
#define IPL_HW          0x14                            /* hwre level */
#define IPL_SCA         (0x14 - IPL_HMIN)
#define IPL_SCB         (0x14 - IPL_HMIN)
#define IPL_XS1         (0x14 - IPL_HMIN)
#define IPL_DZTX        (0x14 - IPL_HMIN)
#define IPL_DZRX        (0x14 - IPL_HMIN)
#define IPL_HMIN        IPL_HW
#define IPL_HMAX        IPL_HW
#define IPL_HLVL        (IPL_HMAX - IPL_HMIN + 1)       /* # hardware levels */
#define IPL_SMAX        0xF                             /* highest swre level */

/* Device vectors */

#define VEC_QBUS        0                               /* Not a Qbus system */
#define VEC_Q           0

/* Interrupt macros */

#define IREQ(dv)        int_req[0]
#define SET_INT(dv)     int_req[0] = int_req[0] | (INT_##dv)
#define CLR_INT(dv)     int_req[0] = int_req[0] & ~(INT_##dv)
#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* cond error return */

/* System model */

extern int32 sys_model;

/* Machine specific definitions - DZ */

#define DZ_L3C          1                               /* line 3 console */

/* Machine specific definitions - OR */

#define OR_COUNT        4                               /* max number of option ROMs */

/* Machine specific definitions - RZ80 */

#define RZ_ROM_INDEX    -1                              /* no ROM needed */
#define DMA_SIZE        0x20000                         /* DMA count register */
#define DCNT_MASK       0x1FFFF
#define RZ_FLAGS        0                               /* permanently enabled */
#define RZB_FLAGS       0                               /* permanently enabled */

/* Machine specific definitions - VC */

#define VC_BYSIZE       2048                            /* buffer height */
#define VC_BUFSIZE      (1u << 16)                      /* number of longwords */
#define VC_ORSC         3                               /* screen origin multiplier */

/* Machine specific definitions - VE */

#define VE_ROM_INDEX    1

/* Machine specific definitions - XS */

#define XS_ROM_INDEX    -1                              /* no ROM needed */
#define XS_FLAGS        0
#define XS_READB        Map_ReadB
#define XS_READW        Map_ReadW
#define XS_WRITEB       Map_WriteB
#define XS_WRITEW       Map_WriteW
#define XS_ADRMBO       (0)

/* Function prototypes for I/O */

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf);
int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf);
int32 Map_WriteB (uint32 ba, int32 bc, uint8 *buf);
int32 Map_WriteW (uint32 ba, int32 bc, uint16 *buf);

/* Function prototypes for disk buffer */

void ddb_WriteB (uint32 ba, uint32 bc, uint8 *buf);
void ddb_WriteW (uint32 ba, uint32 bc, uint16 *buf);
void ddb_ReadB (uint32 ba, uint32 bc, uint8 *buf);
void ddb_ReadW (uint32 ba, uint32 bc, uint16 *buf);

/* Function prototypes for system-specific unaligned support */

int32 ReadIOU (uint32 pa, int32 lnt);
int32 ReadRegU (uint32 pa, int32 lnt);
void WriteIOU (uint32 pa, int32 val, int32 lnt);
void WriteRegU (uint32 pa, int32 val, int32 lnt);

t_stat auto_config (const char *name, int32 nctrl);

/* Function prototypes for virtual and physical memory interface (inlined) */

#include "vax_mmu.h"

#endif
