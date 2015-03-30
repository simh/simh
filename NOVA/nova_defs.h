/* nova_defs.h: NOVA/Eclipse simulator definitions 

   Copyright (c) 1993-2012, Robert M. Supnik

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

   25-Mar-12    RMS     Added missing parameters to prototypes (Mark Pizzolato)
   22-May-10    RMS     Added check for 64b definitions
   04-Jul-07    BKR     BUSY/DONE/INTR "convenience" macros added,
                        INT_TRAP added for Nova 3, 4 trap instruction handling,
                        support for 3rd-party 64KW Nova extensions added,
                        removed STOP_IND_TRP definition due to common INT/TRP handling
   14-Jan-04    BKR     Added support for QTY and ALM
   22-Nov-03    CEO     Added support for PIT device
   19-Jan-03    RMS     Changed CMASK to CDMASK for Apple Dev kit conflict
   03-Oct-02    RMS     Added device information structure
   22-Dec-00    RMS     Added Bruce Ray's second terminal support
   10-Dec-00    RMS     Added Charles Owen's Eclipse support
   08-Dec-00    RMS     Added Bruce Ray's plotter support
   15-Oct-00    RMS     Added stack, byte, trap instructions
   14-Apr-99    RMS     Changed t_addr to unsigned
   16-Mar-95    RMS     Added dynamic memory size
   06-Dec-95    RMS     Added magnetic tape

   The author gratefully acknowledges the help of Tom West, Diana Englebart,
   Carl Friend, Bruce Ray, and Charles Owen in resolving questions about
   the NOVA.
*/

#ifndef NOVA_DEFS_H_
#define NOVA_DEFS_H_   0

#include "sim_defs.h"                                   /* simulator defns */

#if defined(USE_INT64) || defined(USE_ADDR64)
#error "Nova does not support 64b values!"
#endif

/* Simulator stop codes */

#define STOP_RSRV       1                               /* must be 1 */
#define STOP_HALT       2                               /* HALT */
#define STOP_IBKPT      3                               /* breakpoint */
#define STOP_IND        4                               /* indirect loop */
#define STOP_IND_INT    5                               /* ind loop, intr or trap */


/* Memory */

#if defined (ECLIPSE)
                                /*----------------------*/
                                /*        Eclipse       */
                                /*----------------------*/

#define MAXMEMSIZE      1048576                         /* max memory size in 16-bit words */
#define PAMASK          (MAXMEMSIZE - 1)                /* physical addr mask */
#define MEM_ADDR_OK(x)  (((uint32) (x)) < (uint32) MEMSIZE)

#else
                                /*----------------------*/
                                /*          Nova        */
                                /*----------------------*/

#define MAXMEMSIZE      65536                           /* max memory size in 16-bit words: 32KW = DG max, */
                                                        /* 64 KW = 3rd-party extended memory feature  */
#define DFTMEMSIZE      32768                           /* default/initial mem size  */
#define MEM_ADDR_OK(x)  (((uint32) (x)) < (uint32) MEMSIZE)

#endif


#define MEMSIZE         (cpu_unit.capac)                /* actual memory size */
#define A_V_IND         15                              /* ind: indirect */
#define A_IND           (1 << A_V_IND)

/* Architectural constants */

#define SIGN            0100000                         /* sign */
#define DMASK           0177777                         /* data mask */
#define CBIT            (DMASK + 1)                     /* carry bit */
#define CDMASK          (CBIT | DMASK)                  /* carry + data */

/* Reserved memory locations */

#define INT_SAV         0                               /* intr saved PC */
#define INT_JMP         1                               /* intr jmp @ */
#define STK_JMP         3                               /* stack jmp @ */
#define TRP_SAV         046                             /* trap saved PC */
#define TRP_JMP         047                             /* trap jmp @ */

#define AUTO_TOP        037                             /* top of autoindex  */
#define AUTO_DEC        030                             /* start autodec */
#define AUTO_INC        020                             /* start autoinc */


/* Instruction format */

#define I_OPR           0100000                         /* operate */
#define I_M_SRC         03                              /* OPR: src AC */
#define I_V_SRC         13
#define I_GETSRC(x)     (((x) >> I_V_SRC) & I_M_SRC)
#define I_M_DST         03                              /* dst AC */
#define I_V_DST         11
#define I_GETDST(x)     (((x) >> I_V_DST) & I_M_DST)
#define I_M_ALU         07                              /* OPR: ALU op */
#define I_V_ALU         8
#define I_GETALU(x)     (((x) >> I_V_ALU) & I_M_ALU)
#define I_M_SHF         03                              /* OPR: shift */
#define I_V_SHF         6
#define I_GETSHF(x)     (((x) >> I_V_SHF) & I_M_SHF)
#define I_M_CRY         03                              /* OPR: carry */
#define I_V_CRY         4
#define I_GETCRY(x)     (((x) >> I_V_CRY) & I_M_CRY)
#define I_V_NLD         3                               /* OPR: no load */
#define I_NLD           (1 << I_V_NLD)
#define I_M_SKP         07                              /* OPR: skip */
#define I_V_SKP         0
#define I_GETSKP(x)     (((x) >> I_V_SKP) & I_M_SKP)

#define I_M_OPAC        017                             /* MRF: opcode + AC */
#define I_V_OPAC        11
#define I_GETOPAC(x)    (((x) >> I_V_OPAC) & I_M_OPAC)
#define I_V_IND         10                              /* MRF: indirect */
#define I_IND           (1 << I_V_IND)
#define I_M_MODE        03                              /* MRF: mode */
#define I_V_MODE        8
#define I_GETMODE(x)    (((x) >> I_V_MODE) & I_M_MODE)
#define I_M_DISP        0377                            /* MRF: disp */
#define I_V_DISP        0
#define I_GETDISP(x)    (((x) >> I_V_DISP) & I_M_DISP)
#define DISPSIZE        (I_M_DISP + 1)                  /* page size */
#define DISPSIGN        (DISPSIZE >> 1)                 /* page sign */

#define I_M_IOT         07                              /* IOT: code */
#define I_V_IOT         8
#define I_GETIOT(x)     (((x) >> I_V_IOT) & I_M_IOT)
#define I_M_PULSE       03                              /* IOT pulse */
#define I_V_PULSE       6
#define I_GETPULSE(x)   (((x) >> I_V_PULSE) & I_M_PULSE)
#define I_M_DEV         077                             /* IOT: device */
#define I_V_DEV         0
#define I_GETDEV(x)     (((x) >> I_V_DEV) & I_M_DEV)

#define I_M_XOP         037                             /* XOP: code */
#define I_V_XOP         6
#define I_GETXOP(x)     (((x) >> I_V_XOP) & I_M_XOP)

/* IOT return codes */

#define IOT_V_REASON    16                              /* set reason */
#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* stop on error */

/* IOT fields */

#define ioNIO           0                               /* opcode field */
#define ioDIA           1
#define ioDOA           2
#define ioDIB           3
#define ioDOB           4
#define ioDIC           5
#define ioDOC           6
#define ioSKP           7

#define iopN            0                               /* pulse field */
#define iopS            1
#define iopC            2
#define iopP            3

/* Device numbers */

#define DEV_LOW         010                             /* lowest intr dev */
#define DEV_HIGH        051                             /* highest intr dev */
#define DEV_MDV         001                             /* multiply/divide */
#define DEV_ECC         002                             /* ECC memory control */
#define DEV_MAP         003                             /* MMPU control */
#define DEV_TTI         010                             /* console input */
#define DEV_TTO         011                             /* console output */
#define DEV_PTR         012                             /* paper tape reader */
#define DEV_PTP         013                             /* paper tape punch */
#define DEV_CLK         014                             /* clock */
#define DEV_PLT         015                             /* plotter */
#define DEV_CDR         016                             /* card reader */
#define DEV_LPT         017                             /* line printer */
#define DEV_DSK         020                             /* fixed head disk */
#define DEV_MTA         022                             /* magtape */
#define DEV_DCM         024                             /* data comm mux */
#define DEV_ADCV        030                             /* A/D converter */
#define DEV_QTY         030                             /* 4060 multiplexor */
#define DEV_DKP         033                             /* disk pack */
#define DEV_CAS         034                             /* cassette */
#define DEV_ALM         034                             /* ALM/ULM multiplexor */
#define DEV_PIT         043                             /* programmable interval timer */
#define DEV_TTI1        050                             /* second console input */
#define DEV_TTO1        051                             /* second console output */
#define DEV_CPU         077                             /* CPU control */

/* I/O structure

   The NOVA I/O structure is tied together by dev_table, indexed by
   the device number.  Each entry in dev_table consists of

        mask            device mask for busy, done (simulator representation)
        pi              pi disable bit (hardware representation)
        routine         IOT action routine

   dev_table is populated at run time from the device information
   blocks in each device.
*/

struct ndev {
    int32       mask;                                   /* done/busy mask */
    int32       pi;                                     /* assigned pi bit */
    int32       (*routine)(int32, int32, int32);        /* dispatch routine */
    };

typedef struct {
    int32       dnum;                                   /* device number */
    int32       mask;                                   /* done/busy mask */
    int32       pi;                                     /* assigned pi bit */
    int32       (*routine)(int32, int32, int32);        /* dispatch routine */
    } DIB;

/* Device flags (simulator representation)

   Priority (for INTA) runs from low numbers to high
*/

#define INT_V_PIT       2                               /* PIT */
#define INT_V_DKP       3                               /* moving head disk */
#define INT_V_DSK       4                               /* fixed head disk */
#define INT_V_MTA       5                               /* magnetic tape */
#define INT_V_LPT       6                               /* line printer */
#define INT_V_CLK       7                               /* clock */
#define INT_V_PTR       8                               /* paper tape reader */
#define INT_V_PTP       9                               /* paper tape punch */
#define INT_V_PLT       10                              /* plotter */
#define INT_V_TTI       11                              /* keyboard */
#define INT_V_TTO       12                              /* terminal */
#define INT_V_TTI1      13                              /* second keyboard */
#define INT_V_TTO1      14                              /* second terminal */
#define INT_V_QTY       15                              /* QTY multiplexor */
#define INT_V_ALM       16                              /* ALM multiplexor */
#define INT_V_STK       17                              /* stack overflow */
#define INT_V_NO_ION_PENDING 18                         /* ion delay */
#define INT_V_ION       19                              /* interrupts on */
#define INT_V_TRAP      20                              /* trap instruction */

#define INT_PIT         (1 << INT_V_PIT)
#define INT_DKP         (1 << INT_V_DKP)
#define INT_DSK         (1 << INT_V_DSK)
#define INT_MTA         (1 << INT_V_MTA)
#define INT_LPT         (1 << INT_V_LPT)
#define INT_CLK         (1 << INT_V_CLK)
#define INT_PTR         (1 << INT_V_PTR)
#define INT_PTP         (1 << INT_V_PTP)
#define INT_PLT         (1 << INT_V_PLT)
#define INT_TTI         (1 << INT_V_TTI)
#define INT_TTO         (1 << INT_V_TTO)
#define INT_TTI1        (1 << INT_V_TTI1)
#define INT_TTO1        (1 << INT_V_TTO1)
#define INT_QTY         (1 << INT_V_QTY)
#define INT_ALM         (1 << INT_V_ALM)
#define INT_STK         (1 << INT_V_STK)
#define INT_NO_ION_PENDING (1 << INT_V_NO_ION_PENDING)
#define INT_ION         (1 << INT_V_ION)
#define INT_DEV         ((1 << INT_V_STK) - 1)          /* device ints */
#define INT_PENDING     INT_ION+INT_NO_ION_PENDING
#define INT_TRAP        (1 << INT_V_TRAP)

/* PI disable bits */

#define PI_PIT          0001000
#define PI_DKP          0000400
#define PI_DSK          0000100
#define PI_MTA          0000040
#define PI_LPT          0000010
#define PI_CLK          0000004
#define PI_PTR          0000020
#define PI_PTP          0000004
#define PI_PLT          0000010
#define PI_QTY          0000002
#define PI_ALM          0000002
#define PI_TTI          0000002
#define PI_TTO          0000001
#define PI_TTI1         PI_TTI
#define PI_TTO1         PI_TTO
/* #define PI_CDR       0000040 */
/* #define PI_DCM       0100000 */
/* #define PI_CAS       0000040 */
/* #define PI_ADCV      0000002 */


        /*  Macros to clear/set BUSY/DONE/INTR bits  */

#define DEV_SET_BUSY( x )    dev_busy = dev_busy |   (x)
#define DEV_CLR_BUSY( x )    dev_busy = dev_busy & (~(x))
#define DEV_SET_DONE( x )    dev_done = dev_done |   (x)
#define DEV_CLR_DONE( x )    dev_done = dev_done & (~(x))
#define DEV_UPDATE_INTR      int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable)

#define DEV_IS_BUSY( x )    (dev_busy & (x))
#define DEV_IS_DONE( x )    (dev_done & (x))

/* Function prototypes */

int32 MapAddr (int32 map, int32 addr);
t_stat set_enb (UNIT *uptr, int32 val, char *cptr, void *desc);
t_stat set_dsb (UNIT *uptr, int32 val, char *cptr, void *desc);

#endif
