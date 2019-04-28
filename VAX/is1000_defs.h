/* is1000_defs.h: InfoServer 1000 model-specific definitions file

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

   This file covers the InfoServer 1000.

   System memory map

        0000 0000 - 003F FFFF           main memory
        0040 0000 - 2003 FFFF           reserved
        2004 0000 - 2007 FFFF           ROM space
        2008 0000 - 20FF FFFF           reserved
        2100 0000 - 2100 0008           network controller
        2100 0020 - 2100 0024           configuration/test register
        2200 0000 - 2200 00C0           scsi controller
        2300 0000 - 2300 xxxx           watch chip registers
        2400 0000 - 2400 xxxx           local register space
        2400 0060 - 2400 0070           serial line controller
        3002 0000 - 3FFF FFFF           reserved
*/

#ifdef FULL_VAX                                         /* subset VAX */
#undef FULL_VAX
#endif

#ifndef _IS_1000_DEFS_H_
#define _IS_1000_DEFS_H_ 1

/* Microcode constructs */

#define CVAX_SID        (10 << 24)                      /* system ID */
#define CVAX_UREV       6                               /* ucode revision */
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
#define MT_MAX          127                             /* last valid IPR */

/* Cache disable register */

#define CADR_RW         0xF3
#define CADR_MBO        0x0C

/* CPU */

#define CPU_MODEL_MODIFIERS \
                        { 0 }

/* Memory */

#define MAXMEMWIDTH     22                              /* max mem, std KA420 */
#define MAXMEMSIZE      (1 << MAXMEMWIDTH)              /* max mem size */
#define MAXMEMWIDTH_X   22                              /* max mem, KA420 */
#define MAXMEMSIZE_X    (1 << MAXMEMWIDTH_X)
#define INITMEMSIZE     (1 << 24)                       /* initial memory size */
#define MEMSIZE         (cpu_unit.capac)
#define ADDR_IS_MEM(x)  (((uint32) (x)) < MEMSIZE)
#define MEM_MODIFIERS   { UNIT_MSIZE, (1u << 22), NULL, "4M", &cpu_set_size }

/* Read only memory */

#define ROMAWIDTH       19                              /* ROM addr width */
#define ROMSIZE         (1u << ROMAWIDTH)               /* ROM length */
#define ROMAMASK        (ROMSIZE - 1)                   /* ROM addr mask */
#define ROMBASE         0x20040000                      /* ROM base */
#define ADDR_IS_ROM(x)  ((((uint32) (x)) >= ROMBASE) && \
                        (((uint32) (x)) < (ROMBASE + ROMSIZE)))

/* LANCE Ethernet controller */

#define XSSIZE          0x8                             /* XS length */
#define XSBASE          0x21000000                      /* XS base */

/* Config/test register */

#define CFGSIZE         4                               /* CFG length */
#define CFGBASE         0x21000020                      /* CFG base */

/* SCSI disk controller */

#define RZSIZE          0xC0                            /* RZ length */
#define RZBASE          0x22000000                      /* RZ base */

/* Non-volatile RAM - 32KB Bytes long */

#define NVRAWIDTH       15                              /* NVR addr width */
#define NVRSIZE         (1u << NVRAWIDTH)               /* NVR length */
#define NVRAMASK        (NVRSIZE - 1)                   /* NVR addr mask */
#define NVRBASE         0x23000000                      /* NVR base */
#define ADDR_IS_NVR(x)  ((((uint32) (x)) >= NVRBASE) && \
                        (((uint32) (x)) < (NVRBASE + NVRSIZE)))

/* IS1000 board registers */

#define KASIZE          0x60                            /* REG length */
#define KABASE          0x24000000                      /* REG addr base */

/* Serial line controller */

#define DZSIZE          0x10                            /* DZ length */
#define DZBASE          0x24000060                      /* DZ base */

/* Network address ROM */

#define NARAWIDTH       5                               /* NAR addr width */
#define NARSIZE         (1u << NARAWIDTH)               /* NAR length */
#define NARAMASK        (NARSIZE - 1)                   /* NAR addr mask */

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

#define INT_V_SC        0                               /* storage controller */
#define INT_V_XS1       1                               /* network */
#define INT_V_DZTX      2                               /* serial transmitter */
#define INT_V_DZRX      3                               /* serial receiver */
#define INT_V_PE        6                               /* parity error */

#define INT_SC          (1u << INT_V_SC)
#define INT_XS1         (1u << INT_V_XS1)
#define INT_DZTX        (1u << INT_V_DZTX)
#define INT_DZRX        (1u << INT_V_DZRX)
#define INT_PE          (1u << INT_V_PE)

#define IPL_CLK         0x16
#define IPL_HW          0x14                            /* hwre level */
#define IPL_SC          (0x14 - IPL_HMIN)
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

/* Machine specific definitions - RZ94 */

#define RZ_READB(ba,bc,buf)    Map_ReadB(ba, bc, buf, TRUE)
#define RZ_READW(ba,bc,buf)    Map_ReadW(ba, bc, buf, TRUE)
#define RZ_WRITEB(ba,bc,buf)   Map_WriteB(ba, bc, buf, TRUE)
#define RZ_WRITEW(ba,bc,buf)   Map_WriteW(ba, bc, buf, TRUE)

/* Machine specific definitions - XS */

#define XS_ROM_INDEX    -1                              /* no ROM needed */
#define XS_FLAGS        0
#define XS_READB(ba,bc,buf)    Map_ReadB(ba, bc, buf, FALSE)
#define XS_READW(ba,bc,buf)    Map_ReadW(ba, bc, buf, FALSE)
#define XS_WRITEB(ba,bc,buf)   Map_WriteB(ba, bc, buf, FALSE)
#define XS_WRITEW(ba,bc,buf)   Map_WriteW(ba, bc, buf, FALSE)
#define XS_ADRMBO       (0)

/* Function prototypes for I/O */

int32 Map_ReadB (uint32 ba, int32 bc, uint8 *buf, t_bool map);
int32 Map_ReadW (uint32 ba, int32 bc, uint16 *buf, t_bool map);
int32 Map_WriteB (uint32 ba, int32 bc, uint8 *buf, t_bool map);
int32 Map_WriteW (uint32 ba, int32 bc, uint16 *buf, t_bool map);

/* Function prototypes for system-specific unaligned support */

int32 ReadIOU (uint32 pa, int32 lnt);
int32 ReadRegU (uint32 pa, int32 lnt);
void WriteIOU (uint32 pa, int32 val, int32 lnt);
void WriteRegU (uint32 pa, int32 val, int32 lnt);

t_stat auto_config (const char *name, int32 nctrl);

/* Function prototypes for virtual and physical memory interface (inlined) */

#include "vax_mmu.h"

#endif
