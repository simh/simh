/* sel32_defs.h: SEL-32 Concept/32 simulator definitions 

   Copyright (c) 2018-2022, James C. Bevier
   Portions provided by Richard Cornwell, Geert Rolf and other SIMH contributers

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
   JAMES C. BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "sim_defs.h"                   /* simh simulator defns */

/* Simulator stop codes */
#define STOP_IONRDY     1               /* I/O dev not ready */
#define STOP_HALT       2               /* HALT */
#define STOP_IBKPT      3               /* breakpoint */
#define STOP_UUO        4               /* invalid opcode */
#define STOP_INVINS     5               /* invalid instr */
#define STOP_INVIOP     6               /* invalid I/O op */
#define STOP_INDLIM     7               /* indirect limit */
#define STOP_XECLIM     8               /* XEC limit */
#define STOP_IOCHECK    9               /* IOCHECK */
#define STOP_MMTRP      10              /* mm in trap */
#define STOP_TRPINS     11              /* trap inst not BRM */
#define STOP_RTCINS     12              /* rtc inst not MIN/SKR */
#define STOP_ILLVEC     13              /* zero vector */
#define STOP_CCT        14              /* runaway CCT */

/* I/O equates */
/* Channel sense bytes set by device */
#define     SNS_BSY       0x80          /* Unit Busy */
#define     SNS_SMS       0x40          /* Status modified */
#define     SNS_CTLEND    0x20          /* Control unit end */
#define     SNS_ATTN      0x10          /* Unit attention */
#define     SNS_CHNEND    0x08          /* Channel end */
#define     SNS_DEVEND    0x04          /* Device end */
#define     SNS_UNITCHK   0x02          /* Unit check */
#define     SNS_UNITEXP   0x01          /* Unit exception */

/* Command masks */
#define CCMDMSK          0xff000000     /* Mask for command */
#define CMD_CHAN         0x00           /* Channel control */
#define CMD_SENSE        0x04           /* Sense channel command */
#define CMD_TIC          0x08           /* Transfer in channel */
#define CMD_RDBWD        0x0c           /* Read backward */
/* operation types */
#define CMD_TYPE         0x03           /* Type mask */
#define CMD_WRITE        0x01           /* Write command */
#define CMD_READ         0x02           /* Read command */
#define CMD_CTL          0x03           /* Control command */

/* IOCD word 2 status bits */
#define STATUS_ECHO      0x8000         /* Halt I/O and Stop I/O function */
#define STATUS_PCI       0x4000         /* Program controlled interrupt */
#define STATUS_LENGTH    0x2000         /* Incorrect length */
#define STATUS_PCHK      0x1000         /* Channel program check */
#define STATUS_CDATA     0x0800         /* Channel data check */
#define STATUS_CCNTL     0x0400         /* Channel control check */
#define STATUS_INTER     0x0200         /* Channel interface check */
#define STATUS_CHAIN     0x0100         /* Channel chain check */
#define STATUS_BUSY      0x0080         /* Device busy */
#define STATUS_MOD       0x0040         /* Status modified */
#define STATUS_CTLEND    0x0020         /* Controller end */
#define STATUS_ATTN      0x0010         /* Device raised attention */
#define STATUS_CEND      0x0008         /* Channel end */
#define STATUS_DEND      0x0004         /* Device end */
#define STATUS_CHECK     0x0002         /* Unit check */
#define STATUS_EXPT      0x0001         /* Unit exception */
#define STATUS_ERROR     0x3f03         /* bad errors */
//#define STATUS_ERROR   (STATUS_LENGTH|STATUS_PCHK|STATUS_CDATA|STATUS_CCNTL|
//                        STATUS_INTER|STATUS_CHAIN|STATUS_CHECK|STATUS_EXPT)

/* Class F channel bits */
/* bit 32 - 37 of IOCD word 2 (0-5) */
/* ccw_flags bit assignment */
#define FLAG_DC          0x8000         /* Data chain */
#define FLAG_CC          0x4000         /* Chain command */
#define FLAG_SLI         0x2000         /* Suppress length indicator */
#define FLAG_SKIP        0x1000         /* Suppress memory write */
#define FLAG_PCI         0x0800         /* Program controlled interrupt */
#define FLAG_RTO         0x0400         /* Real-Time Option */

/* chan_byte bit assignments */
#define BUFF_EMPTY       0x00           /* Buffer is empty */
#define BUFF_BUSY        0x04           /* Channel program busy & empty */
#define BUFF_NEXT        0x0C           /* 0x08|0x04 Continue Channel with next IOCB */
#define BUFF_CHNEND      0x14           /* 0x10|0x04 Channel end */
#define BUFF_DONE        0x20           /* 0x20 Channel ready for new command */
#define BUFF_POST        0x24           /* 0x20|0x04 Waiting for status to be posted */

/* chan_info bit flags */
#define INFO_SIOCD       0x01           /* Initial IOCD from SIO if set */
#define INFO_CEND        0x02           /* Channel End (chan_end) called if set */
/* bits 0-5 unused */

#define     MAX_CHAN        128         /* max channels that can be defined */
#define     SUB_CHANS       256         /* max sub channels that can be defined */
#define     MAX_DEV         (MAX_CHAN * SUB_CHANS)  /* max possible */

/* simulator devices configuration */
#define NUM_DEVS_IOP    1       /* 1 device IOP channel controller */
#define NUM_UNITS_IOP   1       /* 1 master IOP channel device */
#define NUM_DEVS_MFP    1       /* 1 device MFP channel controller */
#define NUM_UNITS_MFP   1       /* 1 master MFP channel device */
#define NUM_DEVS_COM    2       /* 8-Line async controller */
#define NUM_UNITS_COM   16      /* 8-Line async units */
#define NUM_DEVS_CON    1       /* 1 I/O console controller */
#define NUM_UNITS_CON   2       /* 2 console input & output */
#define NUM_DEVS_MT     1       /* 1 mag tape controllers */
#define NUM_UNITS_MT    4       /* 4 of 8 devices */
#define NUM_DEVS_HSDP   1       /* 1 hspd disk drive controller */
//#define NUM_UNITS_HSDP  2       /* 2 disk drive devices */
#define NUM_UNITS_HSDP  4       /* 4 disk drive devices */
#define NUM_DEVS_DISK   1       /* 1 dp02 disk drive controller */
//#define NUM_UNITS_DISK  2       /* 2 disk drive devices */
#define NUM_UNITS_DISK  4       /* 4 disk drive devices */
#define NUM_DEVS_SCFI   1       /* 1 scfi (SCSI) disk drive units */
//#define NUM_UNITS_SCFI  2       /* 1 of 4 disk drive devices */
#define NUM_UNITS_SCFI  4       /* 1 of 4 disk drive devices */
#define NUM_DEVS_SCSI   2       /* 2 scsi (MFP SCSI) scsi buss units */
#define NUM_UNITS_SCSI  2       /* 2 scsi disk drive devices */
#define NUM_DEVS_RTOM   1       /* 1 IOP RTOM channel */
#define NUM_UNITS_RTOM  1       /* 1 IOP RTOM device (clock & interval timer) */
#define NUM_DEVS_LPR    1       /* 1 IOP Line printer */
#define NUM_UNITS_LPR   1       /* 1 IOP Line printer device */
#define NUM_DEVS_ETHER  1       /* 1 Ethernet controller */
#define NUM_UNITS_ETHER 16      /* 16 Ethernet devices */

extern DEVICE cpu_dev;      /* cpu device */
extern UNIT cpu_unit;       /* the cpu unit */
#ifdef NUM_DEVS_IOP
extern DEVICE iop_dev;      /* IOP channel controller */
#endif
#ifdef NUM_DEVS_MFP
extern DEVICE mfp_dev;      /* MFP channel controller */
#endif
#ifdef NUM_DEVS_RTOM
extern DEVICE rtc_dev;      /* RTOM rtc */
extern DEVICE itm_dev;      /* RTOM itm */
#endif
#ifdef NUM_DEVS_CON
extern DEVICE con_dev;
#endif
#ifdef NUM_DEVS_MT
extern DEVICE mta_dev;
#endif
#if NUM_DEVS_MT > 1
extern DEVICE mtb_dev;
#endif
#ifdef NUM_DEVS_DISK
extern DEVICE dda_dev;
#endif
#if NUM_DEVS_DISK > 1
extern DEVICE ddb_dev;
#endif
#ifdef NUM_DEVS_HSDP
extern DEVICE dpa_dev;
#endif
#if NUM_DEVS_HSDP > 1
extern DEVICE dpb_dev;
#endif
#ifdef NUM_DEVS_SCFI
extern DEVICE sda_dev;
#endif
#if NUM_DEVS_SCFI > 1
extern DEVICE sdb_dev;
#endif
#ifdef NUM_DEVS_SCSI
extern DEVICE sba_dev;
#endif
#if NUM_DEVS_SCSI > 1
extern DEVICE sbb_dev;
#endif
#ifdef NUM_DEVS_COM
extern DEVICE coml_dev;
extern DEVICE com_dev;
#endif
#ifdef NUM_DEVS_LPR
extern DEVICE lpr_dev;
#endif
#ifdef NUM_DEVS_ETHER
extern DEVICE ec_dev;
#endif

/* Memory */

#define MAXMEMSIZE  ((16*1024*1024)/4)  /* max memory size in 32bit words */
#define MEMSIZE     (cpu_unit.capac)    /* actual memory size */
#define MEM_ADDR_OK(x)  (((x)) < MEMSIZE)

/* channel program data for a chan/sub-address */
typedef struct chp {
    /* channel program values */
    UNIT        *unitptr;               /* Back pointer to units structure */
    uint32      chan_inch_addr;         /* Current channel status dw addr in memory */
    uint32      base_inch_addr;         /* Original channel status dw addr in memory */
    uint16      max_inch_addr;          /* maximum inch buffer pointer */
    uint32      chan_caw;               /* Channel command address word */
    uint32      ccw_addr;               /* Channel address */
#ifdef TEST_FOR_IOCL_CHANGE
    uint32      new_iocla;              /* start iocl address */
    uint32      new_iocd1;              /* start word 1 of iocd */
    uint32      new_iocd2;              /* start word 2 of iocd */
#endif
    uint32      chan_buf;               /* Channel data buffer */
    uint16      ccw_count;              /* Channel count */
    uint16      ccw_flags;              /* Channel flags */
    uint16      chan_status;            /* Channel status */
    uint16      chan_dev;               /* Device on channel */
    uint8       ccw_cmd;                /* Channel command and flags */
    uint8       chan_byte;              /* Current byte, empty/full */
    uint8       chan_int;               /* channel interrupt level */
    uint8       chan_info;              /* misc flags for channel */
} CHANP;

/* Device information block */
#define FIFO_SIZE 256                   /* fifo to hold 128 double words of status */
extern  int32   FIFO_Put(uint16 chsa, uint32 entry);
extern  int32   FIFO_Get(uint16 chsa, uint32 *old);
extern  int32   FIFO_Num(uint16 chsa);

#define IOCLQ_SIZE 32                   /* fifo to hold 32 iocl cmds */

typedef struct ioclq {
        uint32  ioclq_fifo[IOCLQ_SIZE];
        int16   ioclq_in;
        int16   ioclq_out;
} IOCLQ;

extern  int32   IOCLQ_Put(IOCLQ *qptr, uint32 entry);
extern  int32   IOCLQ_Get(IOCLQ *qptr, uint32 *old);
extern  int32   IOCLQ_Num(IOCLQ *qptr);

typedef struct dib {
        /* Pre start I/O operation */
        t_stat      (*pre_io)(UNIT *uptr, uint16 chan);
        /* Start a channel command SIO */
        t_stat      (*start_cmd)(UNIT *uptr, uint16 chan, uint8 cmd);
        /* Halt I/O HIO */
        t_stat      (*halt_io)(UNIT *uptr); /* Halt I/O */
        /* Test I/O STOPIO */
        t_stat      (*stop_io)(UNIT *uptr); /* Stop I/O */
        /* Test I/O TESTIO */
        t_stat      (*test_io)(UNIT *uptr); /* Test I/O */
        /* Reset Controller RSCTL */
        t_stat      (*rsctl_io)(UNIT *uptr);    /* Reset Controller */
        /* Reset Controller RSCHNL */
        t_stat      (*rschnl_io)(UNIT *uptr);   /* Reset Channel */
        /* Post I/O processing  */
        t_stat      (*iocl_io)(CHANP *chp, int32 tic_ok);   /* IOCL processing */
        /* Controller init */
        void        (*dev_ini)(UNIT *, t_bool); /* init function */
        UNIT        *units;             /* Pointer to units structure */
        CHANP       *chan_prg;          /* Pointer to channel program */
        IOCLQ       *ioclq_ptr;         /* pointer to array of IOCLQ entries */
        uint8       numunits;           /* number of units */
        uint8       mask;               /* device mask */
        uint16      chan_addr;          /* parent channel address */
        uint32      chan_fifo_in;       /* fifo input index */
        uint32      chan_fifo_out;      /* fifo output index */
        uint32      chan_fifo[FIFO_SIZE];   /* interrupt status fifo for each channel */
} DIB;

extern  DIB     *dib_unit[MAX_DEV];     /* Pointer to Device info block */
extern  DIB     *dib_chan[MAX_CHAN];    /* Pointer to channel mux dib */

/* defined in upper 16 bits of dptr->flags */
#define DEV_CHAN        (1 << DEV_V_UF) /* Device is channel mux if set */
#define DEV_V_UF2       (DEV_V_UF+1)    /* current usage */
#define DEV_BUF_NUM(x)  (((x) & 07) << DEV_V_UF2)
#define GET_DEV_BUF(x)  (((x) >> DEV_V_UF2) & 07)

#ifdef NOT_USED_NOW
//#define DEV_V_ADDR        DEV_V_UF              /* Pointer to device address (16) */
//#define DEV_V_DADDR       (DEV_V_UF + 8)        /* Device address */
//#define DEV_ADDR_MASK     (0x7f << DEV_V_DADDR) /* 24 bits shift */
//#define DEV_V_UADDR       (DEV_V_UF)            /* Device address in Unit */
//#define DEV_UADDR         (1 << DEV_V_UADDR)
//#define GET_DADDR(x)      (0x7f & ((x) >> DEV_V_ADDR))
//#define DEV_ADDR(x)       ((x) << DEV_V_ADDR)
//#define PROTECT_V         UNIT_V_UF+15
//#define PROTECT           (1 << PROTECT_V)
#endif

/* defined in rightmost 8 bits of upper 16 bits of uptr->flags */
/* allow 255 type disks */
#define UNIT_SUBCHAN       (1 << (UNIT_V_UF_31))
#define UNIT_V_TYPE        (UNIT_V_UF + 0)
#define UNIT_TYPE          (0xff << UNIT_V_TYPE)
/* get & set disk types */
#define GET_TYPE(x)        ((UNIT_TYPE & (x)) >> UNIT_V_TYPE)
#define SET_TYPE(x)        (UNIT_TYPE & ((x) << UNIT_V_TYPE))

/* defined in uptr->u3 upper 16 bits */
/* DEV 0x7F000000 UNIT 0x00ff0000 */
#define UNIT_V_ADDR       16
#define UNIT_ADDR_MASK    (0x7fff << UNIT_V_ADDR)
#define GET_UADDR(x)      ((UNIT_ADDR_MASK & x) >> UNIT_V_ADDR)
#define UNIT_ADDR(x)      ((x) << UNIT_V_ADDR)

/* Debugging controls */
#define DEBUG_CMD       0x0000001       /* Show device commands */
#define DEBUG_DATA      0x0000002       /* Show data transfers */
#define DEBUG_DETAIL    0x0000004       /* Show details */
#define DEBUG_INFO      0x0000004       /* Show details */
#define DEBUG_EXP       0x0000008       /* Show error conditions */
#define DEBUG_INST      0x0000010       /* Show instructions */
#define DEBUG_XIO       0x0000020       /* Show XIO I/O instructions */
#define DEBUG_IRQ       0x0000040       /* Show IRQ requests */
#define DEBUG_TRAP      0x0000080       /* Show TRAP requests */

extern DEBTAB dev_debug[];

/* defines for all programs */
#define RMASK           0x0000FFFF      /* right hw 16 bit mask */
#define LMASK           0xFFFF0000      /* left hw 16 bit mask */
#define FMASK           0xFFFFFFFF      /* 32 bit mask */
#define DMASK           0xFFFFFFFFFFFFFFFFLL    /* 64 bit all bits mask */
#define D48LMASK        0xFFFFFFFFFFFF0000LL    /* 64 bit left 48 bits mask */
#define D32LMASK        0xFFFFFFFF00000000LL    /* 64 bit left 32 bits mask */
#define D32RMASK        0x00000000FFFFFFFFLL    /* 64 bit right 32 bits mask */
#define MSIGN           0x80000000      /* 32 bit minus sign */
#define DMSIGN          0x8000000000000000LL    /* 64 bit minus sign */
#define FSIGN           0x80000000      /* 32 bit minus sign */
/* sign extend 16 bit value to uint32 */
#define SEXT16(x)       (x&0x8000?(uint32)(((uint32)x&RMASK)|LMASK):(uint32)x)
/* sign extend 16 bit value to uint64 */
#define DSEXT16(x)      (x&0x8000?(l_uint64)(((l_uint64)x&RMASK)|D48LMASK):(t_uint64)x)
/* sign extend 32 bit value to uint64 */
#define DSEXT32(x)      (x&0x8000?(l_uint64)(((l_uint64)x&D32RMASK)|D32LMASK):(t_uint64)x)
#define NEGATE32(val)   ((~val) + 1)    /* negate a value 16/32/64 bits */

/* defined in rightmost 8 bits of upper 16 bits of uptr->flags */
#define UNIT_V_MODEL    (UNIT_V_UF + 0)
#define UNIT_MODEL      (7 << UNIT_V_MODEL)
#define MODEL(x)        (x << UNIT_V_MODEL)
#define UNIT_V_MSIZE    (UNIT_V_MODEL + 3)
#define UNIT_MSIZE      (0x1F << UNIT_V_MSIZE)
#define MEMAMOUNT(x)    (x << UNIT_V_MSIZE)
#define CPU_MODEL       ((cpu_unit.flags >> UNIT_V_MODEL) & 0x7)    /* cpu model 0-7 */

#define MODEL_55        0               /* 512K Mode Only */
#define MODEL_75        1               /* Extended */
#define MODEL_27        2               /* */
#define MODEL_67        3               /* */
#define MODEL_87        4               /* */
#define MODEL_97        5               /* */
#define MODEL_V6        6               /* V6 CPU */
#define MODEL_V9        7               /* V9 CPU */

#define TMR_RTC         1               /* RTC will not work if set to 0!! */ 
//#define TMR_RTC         0

#define HIST_MIN        64
#define HIST_MAX        10000
#define HIST_PC         0x80000000

/* CC defs Held in CC */
#define CC1BIT   0x40000000             /* CC1 in PSD1 */
#define CC2BIT   0x20000000             /* CC2 in PSD1 */
#define CC3BIT   0x10000000             /* CC3 in PSD1 */
#define CC4BIT   0x08000000             /* CC4 in PSD1 */

#define MAPMODE  0x40                   /* Map mode, PSD 2 bit 0 */
#define RETMODE  0x20                   /* Retain current maps, PSD 2 bit 15 */
#define RETBLKM  0x10                   /* Set retain blocked mode, PSD 2 bit 16 */
#define BLKMODE  0x08                   /* Set blocked mode, PSD 2 bit 17 */

/* PSD mode bits in PSD words 1&2 variable */
#define PRIVBIT  0x80000000             /* Privileged mode  PSD 1 bit 0 */
#define EXTDBIT  0x04000000             /* Extended Addressing PSD 1 bit 5 */
#define BASEBIT  0x02000000             /* Base Mode PSD 1 bit 6 */
#define AEXPBIT  0x01000000             /* Arithmetic exception PSD 1 bit 7 */

#define MAPBIT   0x80000000             /* Map mode, PSD 2 bit 0 */
#define RETMBIT  0x00010000             /* Retain current maps, PSD 2 bit 15 */
#define RETBBIT  0x00008000             /* Retain current blocking state, PSD 2 bit 16 */
#define SETBBIT  0x00004000             /* Set blocked mode, PSD 2 bit 17 */

/* Trap Table Address in memory is pointed to by SPAD 0xF0 */
#define POWERFAIL_TRAP  0x80            /* Power fail trap */
#define POWERON_TRAP    0x84            /* Power-On trap */
#define MEMPARITY_TRAP  0x88            /* Memory Parity Error trap */
#define NONPRESMEM_TRAP 0x8C            /* Non Present Memory trap */
#define UNDEFINSTR_TRAP 0x90            /* Undefined Instruction Trap */
#define PRIVVIOL_TRAP   0x94            /* Privlege Violation Trap */
#define SVCCALL_TRAP    0x98            /* Supervisor Call Trap */
#define MACHINECHK_TRAP 0x9C            /* Machine Check Trap */
#define SYSTEMCHK_TRAP  0xA0            /* System Check Trap */
#define MAPFAULT_TRAP   0xA4            /* Map Fault Trap */
#define IPUUNDEFI_TRAP  0xA8            /* IPU Undefined Instruction Trap */
#define SIGNALIPU_TRAP  0xAC            /* Signal IPU/CPU Trap */
#define ADDRSPEC_TRAP   0xB0            /* Address Specification Trap */
#define CONSOLEATN_TRAP 0xB4            /* Console Attention Trap */
#define PRIVHALT_TRAP   0xB8            /* Privlege Mode Halt Trap */
#define AEXPCEPT_TRAP   0xBC            /* Arithmetic Exception Trap */
#define CACHEERR_TRAP   0xC0            /* Cache Error Trap (V9 Only) */
#define DEMANDPG_TRAP   0xC4            /* Demand Page Fault Trap (V6&V9 Only) */

/* Errors returned from various functions */
#define ALLOK   0x0000                  /* no error, all is OK */
#define MAPFLT  MAPFAULT_TRAP           /* map fault error */
#define NPMEM   NONPRESMEM_TRAP         /* non present memory */
#define MPVIOL  PRIVVIOL_TRAP           /* memory protection violation */
#define DMDPG   DEMANDPG_TRAP           /* Demand Page Fault Trap (V6&V9 Only) */

/* general instruction decode equates */
#define IND     0x00100000              /* indirect bit in instruction, bit 11 */
#define F_BIT   0x00080000              /* byte flag addressing bit 11 in instruction */
#define C_BITS  0x00000003              /* byte number or hw, dw, dw flags bits 30 & 31 */
#define BIT0    0x80000000              /* general use for bit 0 testing */
#define BIT1    0x40000000              /* general use for bit 1 testing */
#define BIT2    0x20000000              /* general use for bit 2 testing */
#define BIT3    0x10000000              /* general use for bit 3 testing */
#define BIT4    0x08000000              /* general use for bit 4 testing */
#define BIT5    0x04000000              /* general use for bit 5 testing */
#define BIT6    0x02000000              /* general use for bit 6 testing */
#define BIT7    0x01000000              /* general use for bit 7 testing */
#define BIT8    0x00800000              /* general use for bit 8 testing */
#define BIT9    0x00400000              /* general use for bit 9 testing */
#define BIT10   0x00200000              /* general use for bit 10 testing */
#define BIT11   0x00100000              /* general use for bit 11 testing */
#define BIT12   0x00080000              /* general use for bit 12 testing */
#define BIT13   0x00040000              /* general use for bit 13 testing */
#define BIT14   0x00020000              /* general use for bit 14 testing */
#define BIT15   0x00010000              /* general use for bit 15 testing */
#define BIT16   0x00008000              /* general use for bit 16 testing */
#define BIT17   0x00004000              /* general use for bit 17 testing */
#define BIT18   0x00002000              /* general use for bit 18 testing */
#define BIT19   0x00001000              /* general use for bit 19 testing */
#define BIT20   0x00000800              /* general use for bit 20 testing */
#define BIT21   0x00000400              /* general use for bit 21 testing */
#define BIT22   0x00000200              /* general use for bit 22 testing */
#define BIT23   0x00000100              /* general use for bit 23 testing */
#define BIT24   0x00000080              /* general use for bit 24 testing */
#define BIT25   0x00000040              /* general use for bit 25 testing */
#define BIT26   0x00000020              /* general use for bit 26 testing */
#define BIT27   0x00000010              /* general use for bit 27 testing */
#define BIT28   0x00000008              /* general use for bit 28 testing */
#define BIT29   0x00000004              /* general use for bit 29 testing */
#define BIT30   0x00000002              /* general use for bit 30 testing */
#define BIT31   0x00000001              /* general use for bit 31 testing */
#define MASK16  0x0000FFFF              /* 16 bit address mask */
#define MASK19  0x0007FFFF              /* 19 bit address mask */
#define MASK20  0x000FFFFF              /* 20 bit address mask */
#define MASK24  0x00FFFFFF              /* 24 bit address mask */
#define MASK32  0xFFFFFFFF              /* 32 bit address mask */

/* SPAD int entry equates, entries accessed by interrupt level number */
#define SINT_RAML   0x80000000          /* ram loaded (n/u) */
#define SINT_EWCS   0x40000000          /* Enabled channel WCS executed (XIO) */
#define SINT_ACT    0x20000000          /* Interrupt active when set (copy is in INTS */
#define SINT_ENAB   0x10000000          /* Interrupt enabled when set (copy is in INTS */
#define SINT_EXTL   0x00800000          /* IOP/RTOM ext interrupt if set, I/O if not set (copy in INTS) */

/* INTS int entry equates, entries accessed by interrupt level number */
#define INTS_NU1    0x80000000          /* Not used */
#define INTS_REQ    0x40000000          /* Interrupt is requesting (use bit 1) */
#define INTS_ACT    0x20000000          /* Interrupt active when set (copy is of SPAD */
#define INTS_ENAB   0x10000000          /* Interrupt enabled when set (copy is of SPAD */
#define INTS_EXTL   0x00800000          /* IOP/RTOM ext interrupt if set, I/O if not set (copy of SPAD) */

/* ReadAddr memory access requested */
#define MEM_RD  0x0                     /* read memory */
#define MEM_WR  0x1                     /* write memory */
#define MEM_EX  0x2                     /* execute memory */

/* Rename of global PC variable to avoid namespace conflicts on some platforms */
#define PC PC_Global

/* memory access macros */
/* The RMW and WMW macros are used to read/write memory words */
/* RMW(addr) or WMW(addr, data) where addr is a byte alligned word address */

#define RMB(a) ((M[(a)>>2]>>(8*(3-(a&3))))&0xff)    /* read memory addressed byte */
#define RMH(a) ((a)&2?(M[(a)>>2]&RMASK):(M[(a)>>2]>>16)&RMASK)    /* read memory addressed halfword */
#define RMW(a) (M[((a)&MASK24)>>2])     /* read memory addressed word */
#define WMW(a,d) (M[((a)&MASK24)>>2]=d) /* write memory addressed word */
/* write halfword to memory address */
#define WMH(a,d) ((a)&2?(M[(a)>>2]=(M[(a)>>2]&LMASK)|((d)&RMASK)):(M[(a)>>2]=(M[(a)>>2]&RMASK)|((d)<<16)))
/* write byte to memory */
#define WMB(a,d) (M[(a)>>2]=(((M[(a)>>2])&(~(0xff<<(8*(3-(a&3))))))|((d&0xff)<<(8*(3-(a&3))))))

/* map register access macros */
/* The RMR and WMR macros are used to read/write the MAPC cache registers */
/* RMR(addr) or WMR(addr, data) where addr is a half word alligned address */
/* read map register halfword from cache address */
#define RMR(a) ((a)&2?(MAPC[(a)>>2]&RMASK):(MAPC[(a)>>2]>>16)&RMASK)
/* write halfword map register to MAP cache address */
#define WMR(a,d) ((a)&2?(MAPC[(a)>>2]=(MAPC[(a)>>2]&LMASK)|((d)&RMASK)):(MAPC[(a)>>2]=(MAPC[(a)>>2]&RMASK)|((d)<<16)))

/* Definitions for commonly used functions */
extern  t_stat  set_dev_addr(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
extern  t_stat  show_dev_addr(FILE * st, UNIT *uptr, int32 v, CONST void *desc);
extern  void    chan_end(uint16 chan, uint16 flags);
extern  int     chan_read_byte(uint16 chsa, uint8 *data);
extern  int     chan_write_byte(uint16 chsa, uint8 *data);
extern  void    set_devattn(uint16 addr, uint16 flags);
extern  void    set_devwake(uint16 chsa, uint16 flags);
extern  t_stat  chan_boot(uint16 addr, DEVICE *dptr);
extern  int     test_write_byte_end(uint16 chsa);
extern  DEVICE *get_dev(UNIT *uptr);
extern  t_stat  set_inch(UNIT *uptr, uint32 inch_addr, uint32 num_inch);    /* set inch addr */
extern  CHANP  *find_chanp_ptr(uint16 chsa);    /* find chanp pointer */

extern  uint32  M[];                    /* our memory */
extern  uint32  SPAD[];                 /* cpu SPAD memory */
extern  uint32  attention_trap;
extern  uint32  RDYQ[];                 /* ready queue */
extern  uint32  RDYQIN;                 /* input index */
extern  uint32  RDYQOUT;                /* output index */
#define RDYQ_SIZE 128
extern  int32   RDYQ_Put(uint32 entry);
extern  int32   RDYQ_Get(uint32 *old);
extern  int32   RDYQ_Num(void);

extern  char *dump_mem(uint32 mp, int cnt);
extern  char *dump_buf(uint8 *mp, int32 off, int cnt);

#define get_chan(chsa)  ((chsa>>8)&0x7f)    /* get channel number from ch/sa */

