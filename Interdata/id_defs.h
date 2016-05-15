/* id_defs.h: Interdata 16b/32b simulator definitions

   Copyright (c) 2000-2012, Robert M. Supnik

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

   The author gratefully acknowledges the help of Carl Friend and Al Kossow,
   who provided key documents about the Interdata product line.

   18-Apr-12    RMS     Added clock coschedule prototype
   22-May-10    RMS     Added check for 64b definitions
   09-Mar-06    RMS     Increased register sets to architectural limit
   25-Jan-04    RMS     Removed local logging support
   22-Sep-03    RMS     Added additional instruction decode types
   21-Jun-03    RMS     Changed subroutine argument for ARM compiler conflict
   25-Apr-03    RMS     Revised for extended file support
   28-Feb-03    RMS     Changed magtape device default to 0x85
*/

#ifndef ID_DEFS_H_
#define ID_DEFS_H_     0

#include "sim_defs.h"                                   /* simulator defns */

/* Rename of global PC variable to avoid namespace conflicts on some platforms */

#define PC PC_Global

#if defined(USE_INT64) || defined(USE_ADDR64)
#error "Interdata 16/32 does not support 64b values!"
#endif

/* Simulator stop codes */

#define STOP_RSRV       1                               /* undef instr */
#define STOP_HALT       2                               /* HALT */
#define STOP_IBKPT      3                               /* breakpoint */
#define STOP_WAIT       4                               /* wait */
#define STOP_VFU        5                               /* runaway VFU */

/* Memory */

#define PAWIDTH16       16
#define PAWIDTH16E      18
#define PAWIDTH32       20
#define MAXMEMSIZE16    (1u << PAWIDTH16)               /* max mem size, 16b */
#define MAXMEMSIZE16E   (1u << PAWIDTH16E)              /* max mem size, 16b E */
#define MAXMEMSIZE32    (1u << PAWIDTH32)               /* max mem size, 32b */
#define PAMASK16        (MAXMEMSIZE16 - 1)              /* phys mem mask */
#define PAMASK16E       (MAXMEMSIZE16E - 1)
#define PAMASK32        (MAXMEMSIZE32 - 1)

#define MEMSIZE         (cpu_unit.capac)                /* act memory size */
#define MEM_ADDR_OK(x)  (((uint32) (x)) < MEMSIZE)

/* Single precision floating point registers */

#if defined (IFP_IN_MEM)
#define ReadFReg(r)     (fp_in_hwre? \
                        F[(r) >> 1]: ReadF (((r) << 1) & ~3, P))
#define WriteFReg(r,v)  if (fp_in_hwre) F[(r) >> 1] = (v); \
                        else WriteF (((r) << 1) & ~3, (v), P)
#else
#define ReadFReg(r)     (F[(r) >> 1])
#define WriteFReg(r,v)  F[(r) >> 1] = (v)
#endif

/* Double precision floating point registers */

typedef struct {
    uint32              h;                              /* high 32b */
    uint32              l;                              /* low 32b */
    } dpr_t;

/* Architectural constants */

#define VAMASK16        (0xFFFF)                        /* 16b virt addr */
#define VAMASK32        (0x000FFFFF)                    /* 32b virt addr */

#define SIGN8           0x80                            /* 8b sign bit */
#define DMASK8          0xFF                            /* 8b data mask */
#define MMASK8          0x7F                            /* 8b magnitude mask */
#define SIGN16          0x8000                          /* 16b sign bit */
#define DMASK16         0xFFFF                          /* 16b data mask */
#define MMASK16         0x7FFF                          /* 16b magnitude mask */
#define SIGN32          0x80000000                      /* 32b sign bit */
#define DMASK32         0xFFFFFFFF                      /* 32b data mask */
#define MMASK32         0x7FFFFFFF                      /* 32b magn mask */

#define CC_C            0x8                             /* carry */
#define CC_V            0x4                             /* overflow */
#define CC_G            0x2                             /* greater than */
#define CC_L            0x1                             /* less than */
#define CC_MASK         (CC_C | CC_V | CC_G | CC_L)

#define PSW_WAIT        0x8000                          /* wait */      
#define PSW_EXI         0x4000                          /* ext intr enable */
#define PSW_MCI         0x2000                          /* machine check enable */
#define PSW_AFI         0x1000                          /* arith fault enb */
#define PSW_AIO         0x0800                          /* auto I/O int enable */
#define PSW_FPF         0x0400                          /* flt fault enb, 16b */
#define PSW_REL         0x0400                          /* reloc enb, 32b */
#define PSW_SQI         0x0200                          /* sys q int enable */
#define PSW_PRO         0x0100                          /* protect mode */
#define PSW_V_MAP       4                               /* mem map, 16b */
#define PSW_M_MAP       0xF
#define PSW_MAP         (PSW_M_MAP << PSW_V_MAP)
#define PSW_V_REG       4                               /* reg set, 32b */
#define PSW_M_REG       0xF
#define PSW_ID4         0xF40F                          /* I3, I4 PSW */
#define PSW_x16         0xFF0F                          /* 7/16, 8/16 PSW */
#define PSW_816E        0xFFFF                          /* 8/16E PSW */
#define PSW_x32         0xFFFF                          /* 7/32, 8/32 PSW */

#define MCKOPSW         0x20                            /* mchk old PSW, 32b */
#define FPFPSW          0x28                            /* flt fault PSW, 16b */
#define ILOPSW          0x30                            /* ill op PSW */
#define MCKPSW          0x38                            /* mach chk PSW */
#define EXIPSW          0x40                            /* ext intr PSW, 16b */
#define AFIPSW          0x48                            /* arith flt PSW */
#define SQP             0x80                            /* system queue ptr */
#define SQIPSW          0x82                            /* sys q int PSW, 16b */
#define SQOP            0x8A                            /* sys q ovf ptr, 16b */
#define SQVPSW          0x8C                            /* sys q ovf PSW, 16b */
#define SQTPSW          0x88                            /* sys q int PSW, 32b */
#define MPRPSW          0x90                            /* mprot int PSW, 32b */
#define SVCAP           0x94                            /* svc arg ptr, 16b */
#define SVOPS           0x96                            /* svc old PS, 16b */
#define SVOPC           0x98                            /* svc old PC, 16b */
#define SVNPS32         0x98                            /* svc new PS, 32b */
#define SVNPS           0x9A                            /* svc new PS, 16b */
#define SVNPC           0x9C                            /* svc new PC */
#define INTSVT          0xD0                            /* int service table */

#define AL_DEV          0x78                            /* autoload: dev */
#define AL_IOC          0x79                            /* command */
#define AL_DSKU         0x7A                            /* disk unit */
#define AL_DSKT         0x7B                            /* disk type */
#define AL_DSKC         0x7C                            /* disk ctrl */
#define AL_SCH          0x7D                            /* sel chan */
#define AL_EXT          0x7E                            /* OS extension */
#define AL_BUF          0x80                            /* buffer start */

#define Q16_SLT         0                               /* list: # slots */
#define Q16_USD         1                               /* # in use */
#define Q16_TOP         2                               /* current top */
#define Q16_BOT         3                               /* next bottom */
#define Q16_BASE        4                               /* base of q */
#define Q16_SLNT        2                               /* slot length */

#define Q32_SLT         0                               /* list: # slots */
#define Q32_USD         2                               /* # in use */
#define Q32_TOP         4                               /* current top */
#define Q32_BOT         6                               /* next bottom */
#define Q32_BASE        8                               /* base of q */
#define Q32_SLNT        4                               /* slot length */

/* CPU event flags */

#define EV_MAC          0x01                            /* MAC interrupt */
#define EV_BLK          0x02                            /* block I/O in prog */
#define EV_INT          0x04                            /* interrupt pending */
#define EV_WAIT         0x08                            /* wait state pending */

/* Block I/O state */

struct BlockIO {
    uint32              dfl;                            /* devno, flags */
    uint32              cur;                            /* current addr */
    uint32              end;                            /* end addr */
    };

#define BL_RD           0x8000                          /* block read */
#define BL_LZ           0x4000                          /* skip 0's */

/* Instruction decode ROM, for all, 16b, 32b */

#define OP_UNDEF        0x0000                          /* undefined */
#define OP_NO           0x0001                          /* all: short or fp rr */
#define OP_RR           0x0002                          /* all: reg-reg */
#define OP_RS           0x0003                          /* 16b: reg-storage */
#define OP_RI1          0x0003                          /* 32b: reg-imm 16b */
#define OP_RX           0x0004                          /* all: reg-mem */
#define OP_RXB          0x0005                          /* all: reg-mem, rd BY */
#define OP_RXH          0x0006                          /* all: reg-mem, rd HW */
#define OP_RXF          0x0007                          /* 32b: reg-mem, rd FW */
#define OP_RI2          0x0008                          /* 32b: reg-imm 32b */
#define OP_MASK         0x000F

#define OP_ID4          0x0010                          /* 16b: ID4 */
#define OP_716          0x0020                          /* 16b: 7/16 */
#define OP_816          0x0040                          /* 16b: 8/16 */
#define OP_816E         0x0080                          /* 16b: 8/16E */

#define OP_DPF          0x4000                          /* all: hwre FP */
#define OP_PRV          0x8000                          /* all: privileged */

#define OP_TYPE(x)      (decrom[(x)] & OP_MASK)
#define OP_DPFP(x)      (decrom[(x)] & OP_DPF)

/* Device information block */

typedef struct {
    uint32              dno;                            /* device number */
    int32               sch;                            /* sch */
    uint32              irq;                            /* interrupt */
    uint8               *tplte;                         /* template */
    uint32              (*iot)(uint32 d, uint32 o, uint32 dat);
    void                (*ini)(t_bool f);
    } DIB;

#define TPL_END         0xFF                            /* template end */

/* Device select return codes */

#define BY              0                               /* 8b only */
#define HW              1                               /* 8b/16b */

/* I/O operations */

#define IO_ADR          0x0                             /* address select */
#define IO_RD           0x1                             /* read byte */
#define IO_RH           0x2                             /* read halfword */
#define IO_WD           0x3                             /* write byte */
#define IO_WH           0x4                             /* write halfword */
#define IO_OC           0x5                             /* output command */
#define IO_SS           0x6                             /* sense status */

/* Device command byte */

#define CMD_V_INT       6                               /* interrupt control */
#define CMD_M_INT       0x3
#define CMD_IENB         1                              /* enable */
#define CMD_IDIS         2                              /* disable */
#define CMD_IDSA         3                              /* disarm */
#define CMD_GETINT(x)   (((x) >> CMD_V_INT) & CMD_M_INT)

/* Device status byte */

#define STA_BSY         0x8                             /* busy */
#define STA_EX          0x4                             /* examine status */
#define STA_EOM         0x2                             /* end of medium */
#define STA_DU          0x1                             /* device unavailable */

/* Default device numbers */

#define DEV_LOW         0x01                            /* lowest intr dev */
#define DEV_MAX         0xFF                            /* highest intr dev */
#define DEVNO           (DEV_MAX + 1)                   /* number of devices */
#define d_DS            0x01                            /* display, switches */
#define d_TT            0x02                            /* teletype */
#define d_PT            0x03                            /* reader */
#define d_CD            0x04                            /* card reader */
#define d_TTP           0x10                            /* PAS as console */
#define d_PAS           0x10                            /* first PAS */
#define o_PASX          0x01                            /* offset to xmt */
#define d_LPT           0x62                            /* line printer */
#define d_PIC           0x6C                            /* interval timer */
#define d_LFC           0x6D                            /* line freq clk */
#define d_MT            0x85                            /* magtape */
#define o_MT0           0x10
#define d_DPC           0xB6                            /* disk controller */
#define o_DP0           0x10
#define o_DPF           0x01                            /* offset to fixed */
#define d_FD            0xC1                            /* floppy disk */
#define d_SCH           0xF0                            /* selector chan */
#define d_IDC           0xFB                            /* MSM disk ctrl */
#define o_ID0           0x01

/* Interrupts

   To make interrupt flags independent of device numbers, each device is
   assigned an interrupt flag in one of four interrupt words

   word 0       DMA devices
   word 1       programmed I/O devices
   word 2-3     PAS devices

   Devices are identified by a level and a bit within a level.  Priorities
   run low to high in the array, right to left within words
*/

#define INTSZ           4                               /* interrupt words */
#define SCH_NUMCH       4                               /* #channels */
#define ID_NUMDR        4                               /* # MSM drives */
#define DP_NUMDR        4                               /* # DPC drives */
#define MT_NUMDR        4                               /* # MT drives */

/* Word 0, DMA devices */

#define i_SCH           0                               /* highest priority */
#define i_IDC           (i_SCH + SCH_NUMCH)             /* MSM disk ctrl */
#define i_DPC           (i_IDC + ID_NUMDR + 1)          /* cartridge disk ctrl */
#define i_MT            (i_DPC + DP_NUMDR + 1)          /* magtape */

#define l_SCH           0
#define l_IDC           0
#define l_DPC           0
#define l_MT            0

#define v_SCH           (l_SCH * 32) + i_SCH
#define v_IDC           (l_IDC * 32) + i_IDC
#define v_DPC           (l_DPC * 32) + i_DPC
#define v_MT            (l_MT * 32) + i_MT

/* Word 1, programmed I/O devices */

#define i_PIC           0                               /* precision clock */
#define i_LFC           1                               /* line clock */
#define i_FD            2                               /* floppy disk */
#define i_CD            3                               /* card reader */
#define i_LPT           4                               /* line printer */
#define i_PT            5                               /* paper tape */
#define i_TT            6                               /* teletype */
#define i_DS            7                               /* display */
#define i_TTP           10                              /* PAS console */

#define l_PIC           1
#define l_LFC           1
#define l_FD            1
#define l_CD            1
#define l_LPT           1
#define l_PT            1
#define l_TT            1
#define l_DS            1
#define l_TTP           1

#define v_PIC           (l_PIC * 32) + i_PIC
#define v_LFC           (l_LFC * 32) + i_LFC
#define v_FD            (l_FD * 32) + i_FD
#define v_CD            (l_CD * 32) + i_CD
#define v_LPT           (l_LPT * 32) + i_LPT
#define v_PT            (l_PT * 32) + i_PT
#define v_TT            (l_TT * 32) + i_TT
#define v_DS            (l_DS * 32) + i_DS
#define v_TTP           (l_TTP * 32) + i_TTP

/* Word 2-3, PAS devices */

#define i_PAS           0
#define l_PAS           2
#define v_PAS           (l_PAS * 32) + i_PAS
#define v_PASX          (v_PAS + 1)                     /* offset to xmt */

/* I/O macros */

#define SET_INT(v)      int_req[(v) >> 5] = int_req[(v) >> 5] | (1u << ((v) & 0x1F))
#define CLR_INT(v)      int_req[(v) >> 5] = int_req[(v) >> 5] & ~(1u << ((v) & 0x1F))
#define SET_ENB(v)      int_enb[(v) >> 5] = int_enb[(v) >> 5] | (1u << ((v) & 0x1F))
#define CLR_ENB(v)      int_enb[(v) >> 5] = int_enb[(v) >> 5] & ~(1u << ((v) & 0x1F))

#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* stop on error */

/* Device accessible macro */

#define DEV_ACC(d)      (dev_tab[d] && !sch_blk (d))

/* Automatic I/O channel programs, 16b */

#define CCB16_CHN       -4                              /* chain */
#define CCB16_DEV       -2                              /* dev no */
#define CCB16_STS       -1                              /* status */
#define CCB16_CCW       0                               /* cmd wd */
#define CCB16_STR       2                               /* start */
#define CCB16_END       4                               /* end */
#define CCB16_IOC       6                               /* OC byte */
#define CCB16_TRM       7                               /* term byte */

#define CCW16_INIT      0x8000                          /* init */
#define CCW16_NOP       0x4000                          /* nop */
#define CCW16_V_FNC     12                              /* function */
#define CCW16_M_FNC     0x3
#define CCW16_FNC(x)    (((x) >> CCW16_V_FNC) & CCW16_M_FNC)
#define  CCW16_RD       0                               /* read */
#define  CCW16_WR       1                               /* write */
#define  CCW16_DMT      2                               /* dec mem */
#define  CCW16_NUL      3                               /* null */
#define CCW16_TRM       0x0400                          /* term char */
#define CCW16_Q         0x0200                          /* queue */
#define CCW16_HI        0x0100                          /* queue hi */
#define CCW16_OC        0x0080                          /* OC */
#define CCW16_CHN       0x0020                          /* chain */
#define CCW16_CON       0x0010                          /* continue */
#define CCW16_V_BPI     0                               /* bytes per int */
#define CCW16_M_BPI     0xF
#define CCW16_BPI(x)    (((x) >> CCW16_V_BPI) & CCW16_M_BPI)

/* Automatic I/O channel programs, 32b */

#define CCB32_CCW       0                               /* cmd wd */
#define CCB32_B0C       2                               /* buf 0 cnt */
#define CCB32_B0E       4                               /* buf 0 end */
#define CCB32_CHK       8                               /* check word */
#define CCB32_B1C       10                              /* buf 1 cnt */
#define CCB32_B1E       12                              /* buf 1 end */
#define CCB32_TAB       16                              /* trans table */
#define CCB32_SUB       20                              /* subroutine */

#define CCW32_V_STA     8                               /* status */
#define CCW32_M_STA     0xFF
#define CCW32_STA(x)    (((x) >> CCW32_V_STA) & CCW32_M_STA)
#define CCW32_EXE       0x80                            /* execute */
#define CCW32_CRC       0x10
#define CCW32_B1        0x08                            /* buffer 1 */
#define CCW32_WR        0x04                            /* write */
#define CCW32_TL        0x02                            /* translate */
#define CCW32_FST       0x01                            /* fast mode */

/* MAC, 32b */

#define P               0                               /* physical */
#define VE              1                               /* virtual inst */              
#define VR              2                               /* virtual read */
#define VW              3                               /* virtual write */

#define MAC_BASE        0x300                           /* MAC base */
#define MAC_STA         0x340                           /* MAC status */
#define MAC_LNT         16
#define VA_V_OFF        0                               /* offset */
#define VA_M_OFF        0xFFFF
#define VA_GETOFF(x)    (((x) >> VA_V_OFF) & VA_M_OFF)
#define VA_V_SEG        16                              /* segment */
#define VA_M_SEG        0xF
#define VA_GETSEG(x)    (((x) >> VA_V_SEG) & VA_M_SEG)

#define SRF_MASK        0x000FFF00                      /* base mask */
#define SRL_MASK        0x0FF00000                      /* limit mask */
#define GET_SRL(x)      ((((x) & SRL_MASK) >> 12) + 0x100)
#define SR_EXP          0x80                            /* execute prot */
#define SR_WPI          0x40                            /* wr prot int */
#define SR_WRP          0x20                            /* wr prot */
#define SR_PRS          0x10                            /* present */
#define SR_MASK         (SRF_MASK|SRL_MASK|SR_EXP|SR_WPI|SR_WRP|SR_PRS)

#define MACS_L          0x10                            /* limit viol */
#define MACS_NP         0x08                            /* not present */
#define MACS_WP         0x04                            /* write prot */
#define MACS_WI         0x02                            /* write int */
#define MACS_EX         0x01                            /* exec prot */

/* Miscellaneous */

#define TMR_LFC         0                               /* LFC = timer 0 */
#define TMR_PIC         1                               /* PIC = timer 1 */
#define LPT_WIDTH       132
#define VFU_LNT         132
#define MIN(x,y)        (((x) < (y))? (x): (y))
#define MAX(x,y)        (((x) > (y))? (x): (y))

/* Function prototypes */

int32 int_chg (uint32 irq, int32 dat, int32 armdis);
int32 io_2b (int32 val, int32 pos, int32 old);
uint32 IOReadB (uint32 loc);
void IOWriteB (uint32 loc, uint32 val);
uint32 IOReadH (uint32 loc);
void IOWriteH (uint32 loc, uint32 val);
uint32 ReadF (uint32 loc, uint32 rel);
void WriteF (uint32 loc, uint32 val, uint32 rel);
uint32 IOReadBlk (uint32 loc, uint32 cnt, uint8 *buf);
uint32 IOWriteBlk (uint32 loc, uint32 cnt, uint8 *buf);
void sch_adr (uint32 ch, uint32 dev);
t_bool sch_actv (uint32 sch, uint32 devno);
void sch_stop (uint32 sch);
uint32 sch_wrmem (uint32 sch, uint8 *buf, uint32 cnt);
uint32 sch_rdmem (uint32 sch, uint8 *buf, uint32 cnt);
t_stat set_sch (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat set_dev (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_sch (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat show_dev (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

int32 lfc_cosched (int32 wait);

extern uint32 PC, dec_flgs;
extern const uint16 decrom[256];

#endif
