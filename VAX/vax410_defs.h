/* vax410_defs.h: MicroVAX 2000 model-specific definitions file

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

   This file covers the KA410 ("TeamMate") system.

   System memory map

        0000 0000 - 00FF FFFF           main memory
        0100 0000 - 201F FFFF           reserved
        2002 0000 - 2002 0003           configuration/test register
        2004 0000 - 2007 FFFF           ROM space
        2008 0000 - 2008 000F           local register space
        2009 0000 - 2009 007F           network address ROM
        200A 0000 - 200A 000F           serial line controller
        200B 0000 - 200B 00FF           watch chip registers
        200C 0000 - 200C 0007           disk controller
        200C 0080 - 200C 00FF           tape controller
        200D 0000 - 200D 3FFF           disk/tape data buffer
        200F 0000 - 200F 003F           monochrome video cursor chip
        2010 0000 - 2013 FFFF           option ROMs
        3000 0000 - 3001 FFFF           monochrome video RAM
        3002 0000 - 3FFF FFFF           reserved
*/

#ifdef FULL_VAX                     /* subset VAX */
#undef FULL_VAX
#endif

#ifdef CMPM_VAX
#undef CMPM_VAX                     /* No Compatibility Mode */
#endif

#ifndef NOEXS_VAX
#define NOEXS_VAX       1           /* No Extra String Instructions Implemented */
#endif

#ifndef _VAX_410_DEFS_H_
#define _VAX_410_DEFS_H_ 1

/* Microcode constructs */

#define VAX410_SID      (8 << 24)                       /* system ID */
#define VAX410_UREV     0                               /* ucode revision */
#define CON_HLTPIN      0x0200                          /* external CPU halt */
#define CON_PWRUP       0x0300                          /* powerup code */
#define CON_HLTINS      0x0600                          /* HALT instruction */
#define CON_DBLMCK      0x0500                          /* Machine check in machine check */
#define CON_BADPSL      0x4000                          /* invalid PSL flag */
#define CON_MAPON       0x8000                          /* mapping on flag */
#define MCHK_READ       0x80                            /* read check */
#define MCHK_WRITE      0x82                            /* write check */

/* Machine specific IPRs */

#define MT_CONISP       41                              /* Console Saved ISP */
#define MT_CONPC        42                              /* Console Saved PC */
#define MT_CONPSL       43                              /* Console Saved PSL */
#define MT_MAX          127                             /* last valid IPR */

/* CPU */

#define CPU_MODEL_MODIFIERS \
                        { MTAB_XTD|MTAB_VDV, 0, "MODEL", "MODEL={MICROVAX|VAXSTATION|VAXSTATIONGPX}", \
                          cpu_set_model, &cpu_show_model, NULL, "Set/Show the simulator CPU Model" }

/* Memory */

#define MAXMEMWIDTH     24                              /* max mem, std KA410 */
#define MAXMEMSIZE      (1 << MAXMEMWIDTH)              /* max mem size */
#define MAXMEMWIDTH_X   24                              /* max mem, KA410 */
#define MAXMEMSIZE_X    (1 << MAXMEMWIDTH_X)
#define INITMEMSIZE     (1 << 24)                       /* initial memory size */
#define MEMSIZE         (cpu_unit.capac)
#define ADDR_IS_MEM(x)  (((uint32) (x)) < MEMSIZE)
#define MEM_MODIFIERS   { UNIT_MSIZE, (1u << 21), NULL, "2M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 21) + (1u << 20), NULL, "3M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 22), NULL, "4M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 22) + (1u << 21), NULL, "6M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 23), NULL, "8M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 23) + (1u << 21), NULL, "10M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 23) + (1u << 22) + (1u << 21), NULL, "14M", &cpu_set_size }, \
                        { UNIT_MSIZE, (1u << 24), NULL, "16M", &cpu_set_size }

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

/* KA410 board registers */

#define KAAWIDTH        4                               /* REG addr width */
#define KASIZE          (1u << KAAWIDTH)                /* REG length */
#define KABASE          0x20080000                      /* REG addr base */

/* Network address ROM */

#define NARAWIDTH       5                               /* NAR addr width */
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

/* MFM disk controller */

#define RDSIZE          0x8                             /* RD length */
#define RDBASE          0x200C0000                      /* RD base */

/* SCSI disk controller */

#define RZSIZE          0x50                            /* RZ length */
#define RZBASE          0x200C0080                      /* RZ base */

/* 16k disk buffer */

#define D16AWIDTH       14                              /* D16 addr width */
#define D16SIZE         (1u << D16AWIDTH)               /* D16 length */
#define D16AMASK        (D16SIZE - 1)                   /* D16 addr mask */
#define D16BASE         0x200D0000                      /* D16 base */

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

/* VC memory space */

#define VCAWIDTH        17                              /* VC mem addr width */
#define VCSIZE          (1u << VCAWIDTH)                /* VC mem length */
#define VCAMASK         (VCSIZE - 1)                    /* VC mem addr mask */
#define VCBASE          0x30000000                      /* VC mem base */

/* VA memory space */

#define VAAWIDTH        16                              /* VA mem addr width */
#define VASIZE          (1u << VAAWIDTH)                /* VA mem length */
#define VAAMASK         (VASIZE - 1)                    /* VA mem addr mask */
#define VABASE          0x3C000000                      /* VA mem base */

/* Other address spaces */

#define ADDR_IS_IO(x)   (0)
#define ADDR_IS_CDG(x)  (0)

/* Machine specific reserved operand tests (mostly NOPs) */

#define ML_PA_TEST(r)
#define ML_LR_TEST(r)
#define ML_SBR_TEST(r)
#define ML_PXBR_TEST(r)
#define LP_AST_TEST(r)
#define LP_MBZ84_TEST(r)
#define LP_MBZ92_TEST(r)

#define MT_AST_TEST(r)  if ((r) > AST_MAX) RSVD_OPND_FAULT

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
#define DMA_SIZE        0x4000                          /* DMA count register */
#define DCNT_MASK       0xFFFF
#define RZ_FLAGS        0                               /* permanently enabled */
#define RZB_FLAGS       (DEV_DIS)                       /* not present */
#define RZ_SCSI_ID      0                               /* initiator SCSI id */

/* Machine specific definitions - RD */

#define RD_ROM_INDEX    -1                              /* no ROM needed */
#define RD_FLAGS        0                               /* permanently enabled */

/* Machine specific definitions - VA */

#define VA_ROM_INDEX    1
#define VA_PLANES       4                               /* 4bpp */

/* Machine specific definitions - VC */

#define VC_BYSIZE       1024                            /* buffer height */
#define VC_BUFSIZE      (1u << 15)                      /* number of longwords */
#define VC_ORSC         2                               /* screen origin multiplier */

/* Machine specific definitions - XS */

#define XS_ROM_INDEX    0
#define XS_FLAGS        (DEV_DIS | DEV_DISABLE)
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
