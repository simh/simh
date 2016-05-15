/* sds_defs.h: SDS 940 simulator definitions

   Copyright (c) 2001-2010, Robert M. Supnik

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

   22-May-10    RMS     Added check for 64b definitions
   25-Apr-03    RMS     Revised for extended file support
*/

#ifndef SDS_DEFS_H_
#define SDS_DEFS_H_    0

#include "sim_defs.h"                                   /* simulator defns */

#if defined(USE_INT64) || defined(USE_ADDR64)
#error "SDS 940 does not support 64b values!"
#endif

/* Simulator stop codes */

#define STOP_IONRDY     1                               /* I/O dev not ready */
#define STOP_HALT       2                               /* HALT */
#define STOP_IBKPT      3                               /* breakpoint */
#define STOP_INVDEV     4                               /* invalid dev */
#define STOP_INVINS     5                               /* invalid instr */
#define STOP_INVIOP     6                               /* invalid I/O op */
#define STOP_INDLIM     7                               /* indirect limit */
#define STOP_EXULIM     8                               /* EXU limit */
#define STOP_MMINT      9                               /* mm in intr */
#define STOP_MMTRP      10                              /* mm in trap */
#define STOP_TRPINS     11                              /* trap inst not BRM or BRU */
#define STOP_RTCINS     12                              /* rtc inst not MIN or SKR */
#define STOP_ILLVEC     13                              /* zero vector */
#define STOP_CCT        14                              /* runaway CCT */
#define STOP_MBKPT      15                              /* monitor-mode breakpoint */
#define STOP_NBKPT      16                              /* normal-mode breakpoint */
#define STOP_UBKPT      17                              /* user-mode breakpoint */
#define STOP_DBKPT      18                              /* step-over (dynamic) breakpoint */


/* Trap codes */

#define MM_PRVINS       -040                            /* privileged */
#define MM_NOACC        -041                            /* no access */
#define MM_WRITE        -043                            /* write protect */
#define MM_MONUSR       -044                            /* mon to user */

/* Conditional error returns */

#define CRETINS         return ((stop_invins)? STOP_INVINS: SCPE_OK)
#define CRETDEV         return ((stop_invdev)? STOP_INVDEV: SCPE_OK)
#define CRETIOP         return ((stop_inviop)? STOP_INVIOP: SCPE_OK)
#define CRETIOE(f,c)    return ((f)? c: SCPE_OK)

/* Architectural constants */

#define SIGN            040000000                       /* sign */
#define DMASK           077777777                       /* data mask */
#define EXPS            0400                            /* exp sign */
#define EXPMASK         0777                            /* exp mask */
#define SXT(x)          ((int32) (((x) & SIGN)? ((x) | ~DMASK): \
                        ((x) & DMASK)))
#define SXT_EXP(x)      ((int32) (((x) & EXPS)? ((x) | ~EXPMASK): \
                        ((x) & EXPMASK)))

/* CPU modes */

#define NML_MODE        0
#define MON_MODE        1
#define USR_MODE        2
#define BAD_MODE        3

/* Memory */

#define MAXMEMSIZE      (1 << 16)                       /* max memory size */
#define PAMASK          (MAXMEMSIZE - 1)                /* physical addr mask */
#define MEMSIZE         (cpu_unit.capac)                /* actual memory size */
#define MEM_ADDR_OK(x)  (((uint32) (x)) < MEMSIZE)
#define ReadP(x)        M[x]
#define WriteP(x,y)     if (MEM_ADDR_OK (x)) M[x] = y

/* Virtual addressing */

#define VA_SIZE         (1 << 14)                       /* virtual addr size */
#define VA_MASK         (VA_SIZE - 1)                   /* virtual addr mask */
#define VA_V_PN         11                              /* page number */
#define VA_M_PN         07
#define VA_GETPN(x)     (((x) >> VA_V_PN) & VA_M_PN)
#define VA_POFF         ((1 << VA_V_PN) - 1)            /* offset */
#define VA_USR          (I_USR)                         /* user flag in addr */
#define XVA_MASK        (VA_USR | VA_MASK)

/* Arithmetic */

#define TSTS(x)         ((x) & SIGN)
#define NEG(x)          (-((int32) (x)) & DMASK)
#define ABS(x)          (TSTS (x)? NEG(x): (x))

/* Memory map */

#define MAP_PROT        (040 << VA_V_PN)                /* protected */
#define MAP_PAGE        (037 << VA_V_PN)                /* phys page number */

/* Instruction format */

#define I_USR           (1 << 23)                       /* user */
#define I_IDX           (1 << 22)                       /* indexed */
#define I_POP           (1 << 21)                       /* programmed op */
#define I_V_TAG         21                              /* tag */
#define I_V_OP          15                              /* opcode */
#define I_M_OP          077
#define I_GETOP(x)      (((x) >> I_V_OP) & I_M_OP)
#define I_IND           (1 << 14)                       /* indirect */
#define I_V_SHFOP       11                              /* shift op */
#define I_M_SHFOP       07
#define I_GETSHFOP(x)   (((x) >> I_V_SHFOP) & I_M_SHFOP)
#define I_SHFMSK        0777                            /* shift count */
#define I_V_IOMD        12                              /* IO inst mode */
#define I_M_IOMD        03
#define I_GETIOMD(x)    (((x) >> I_V_IOMD) & I_M_IOMD)
#define I_V_SKCND       7                               /* SKS skip cond */
#define I_M_SKCND       037
#define I_GETSKCND(x)   (((x) >> I_V_SKCND) & I_M_SKCND)
#define I_EOB2          000400000                       /* chan# bit 2 */
#define I_SKB2          000040000                       /* skschan# bit 2 */
#define I_EOB1          020000000                       /* chan# bit 1 */
#define I_EOB0          000000100                       /* chan# bit 0 */
#define I_GETEOCH(x)    ((((x) & I_EOB2)? 4: 0) | \
                        (((x) & I_EOB1)? 2: 0) | \
                        (((x) & I_EOB0)? 1: 0))
#define I_SETEOCH(x)    ((((x) & 4)? I_EOB2: 0) | \
                        (((x) & 2)? I_EOB1: 0) | \
                        (((x) & 1)? I_EOB0: 0))
#define I_GETSKCH(x)    ((((x) & I_SKB2)? 4: 0) | \
                        (((x) & I_EOB1)? 2: 0) | \
                        (((x) & I_EOB0)? 1: 0))
#define I_SETSKCH(x)    ((((x) & 4)? I_SKB2: 0) | \
                        (((x) & 2)? I_EOB1: 0) | \
                        (((x) & 1)? I_EOB0: 0))

/* Globally visible flags */

#define UNIT_V_GENIE    (UNIT_V_UF + 0)
#define UNIT_GENIE      (1 << UNIT_V_GENIE)

/* Timers */

#define TMR_RTC         0                               /* clock */
#define TMR_MUX         1                               /* mux */

/* I/O routine functions */

#define IO_CONN         0                               /* connect */
#define IO_EOM1         1                               /* EOM mode 1 */
#define IO_DISC         2                               /* disconnect */
#define IO_READ         3                               /* read */
#define IO_WRITE        4                               /* write */
#define IO_WREOR        5                               /* write eor */
#define IO_SKS          6                               /* skip signal */

/* Dispatch template */

struct sdsdspt {
    uint32      num;                                    /* # entries */
    uint32      off;                                    /* offset from base */
    };

typedef struct sdsdspt DSPT;

/* Device information block */

struct sdsdib {
    int32       chan;                                   /* channel */
    int32       dev;                                    /* base dev no */
    int32       xfr;                                    /* xfer flag */
    DSPT        *tplt;                                  /* dispatch templates */
    t_stat      (*iop) (uint32 fnc, uint32 dev, uint32 *dat);
    };

typedef struct sdsdib DIB;

/* Channels */

#define NUM_CHAN        8                               /* max num chan */
#define CHAN_W          0                               /* TMCC */
#define CHAN_Y          1
#define CHAN_C          2
#define CHAN_D          3
#define CHAN_E          4                               /* DACC */
#define CHAN_F          5
#define CHAN_G          6
#define CHAN_H          7

/* I/O control EOM */

#define CHC_REV         04000                           /* reverse */
#define CHC_NLDR        02000                           /* no leader */
#define CHC_BIN         01000                           /* binary */
#define CHC_V_CPW       7                               /* char/word */
#define CHC_M_CPW       03
#define CHC_GETCPW(x)   (((x) >> CHC_V_CPW) & CHC_M_CPW)

/* Buffer control (extended) EOM */

#define CHM_CE          04000                           /* compat/ext */
#define CHM_ER          02000                           /* end rec int */
#define CHM_ZC          01000                           /* zero wc int */
#define CHM_V_FNC       7                               /* term func */
#define CHM_M_FNC       03
#define CHM_GETFNC(x)   (((x) & CHM_CE)? (((x) >> CHM_V_FNC) & CHM_M_FNC): CHM_COMP)
#define  CHM_IORD       0                               /* record, disc */
#define  CHM_IOSD       1                               /* signal, disc */
#define  CHM_IORP       2                               /* record, proc */
#define  CHM_IOSP       3                               /* signal, proc */
#define  CHM_COMP       5                               /* compatible */
#define  CHM_SGNL       1                               /* signal bit */
#define  CHM_PROC       2                               /* proceed bit */
#define CHM_V_HMA       5                               /* hi mem addr */
#define CHM_M_HMA       03
#define CHM_GETHMA(x)   (((x) >> CHM_V_HMA) & CHM_M_HMA)
#define CHM_V_HWC       0                               /* hi word count */
#define CHM_M_HWC       037
#define CHM_GETHWC(x)   (((x) >> CHM_V_HWC) & CHM_M_HWC)

/* Channel flags word */

#define CHF_ERR         00001                           /* error */
#define CHF_IREC        00002                           /* interrecord */
#define CHF_ILCE        00004                           /* interlace */
#define CHF_DCHN        00010                           /* data chain */
#define CHF_EOR         00020                           /* end of record */
#define CHF_12B         00040                           /* 12 bit mode */
#define CHF_24B         00100                           /* 24 bit mode */
#define CHF_OWAK        00200                           /* output wake */
#define CHF_SCAN        00400                           /* scan */
#define CHF_TOP         01000                           /* TOP pending */
#define CHF_N_FLG       9                               /* <= 16 */

/* Interrupts and vectors (0 is reserved), highest bit is highest priority */

#define INT_V_PWRO      31                              /* power on */
#define INT_V_PWRF      30                              /* power off */
#define INT_V_CPAR      29                              /* CPU parity err */
#define INT_V_IPAR      28                              /* IO parity err */
#define INT_V_RTCS      27                              /* clock sync */
#define INT_V_RTCP      26                              /* clock pulse */
#define INT_V_YZWC      25                              /* chan Y zero wc */
#define INT_V_WZWC      24                              /* chan W zero wc */
#define INT_V_YEOR      23                              /* chan Y end rec */
#define INT_V_WEOR      22                              /* chan W end rec */
#define INT_V_CZWC      21                              /* chan C */
#define INT_V_CEOR      20
#define INT_V_DZWC      19                              /* chan D */
#define INT_V_DEOR      18
#define INT_V_EZWC      17                              /* chan E */
#define INT_V_EEOR      16
#define INT_V_FZWC      15                              /* chan F */
#define INT_V_FEOR      14
#define INT_V_GZWC      13                              /* chan G */
#define INT_V_GEOR      12
#define INT_V_HZWC      11                              /* chan H */
#define INT_V_HEOR      10
#define INT_V_MUXR      9                               /* mux receive */
#define INT_V_MUXT      8                               /* mux transmit */
#define INT_V_MUXCO     7                               /* SDS carrier on */
#define INT_V_MUXCF     6                               /* SDS carrier off */
#define INT_V_DRM       5                               /* Genie drum */
#define INT_V_FORK      4                               /* fork */

#define INT_PWRO        (1 << INT_V_PWRO)
#define INT_PWRF        (1 << INT_V_PWRF)
#define INT_CPAR        (1 << INT_V_CPAR)
#define INT_IPAR        (1 << INT_V_IPAR)
#define INT_RTCS        (1 << INT_V_RTCS)
#define INT_RTCP        (1 << INT_V_RTCP)
#define INT_YZWC        (1 << INT_V_YZWC)
#define INT_WZWC        (1 << INT_V_WZWC)
#define INT_YEOR        (1 << INT_V_YEOR)
#define INT_WEOR        (1 << INT_V_WEOR)
#define INT_CZWC        (1 << INT_V_CZWC)
#define INT_CEOR        (1 << INT_V_CEOR)
#define INT_DZWC        (1 << INT_V_DZWC)
#define INT_DEOR        (1 << INT_V_DEOR)
#define INT_EZWC        (1 << INT_V_EZWC)
#define INT_EEOR        (1 << INT_V_EEOR)
#define INT_FZWC        (1 << INT_V_FZWC)
#define INT_FEOR        (1 << INT_V_FEOR)
#define INT_GZWC        (1 << INT_V_GZWC)
#define INT_GEOR        (1 << INT_V_GEOR)
#define INT_HZWC        (1 << INT_V_HZWC)
#define INT_HEOR        (1 << INT_V_HEOR)
#define INT_MUXR        (1 << INT_V_MUXR)
#define INT_MUXT        (1 << INT_V_MUXT)
#define INT_MUXCO       (1 << INT_V_MUXCO)
#define INT_MUXCF       (1 << INT_V_MUXCF)
#define INT_DRM         (1 << INT_V_DRM)
#define INT_FORK        (1 << INT_V_FORK)

#define VEC_PWRO        0036
#define VEC_PWRF        0037
#define VEC_CPAR        0056
#define VEC_IPAR        0057
#define VEC_RTCS        0074
#define VEC_RTCP        0075
#define VEC_YZWC        0030
#define VEC_WZWC        0031
#define VEC_YEOR        0032
#define VEC_WEOR        0033
#define VEC_CZWC        0060
#define VEC_CEOR        0061
#define VEC_DZWC        0062
#define VEC_DEOR        0063
#define VEC_EZWC        0064
#define VEC_EEOR        0065
#define VEC_FZWC        0066
#define VEC_FEOR        0067
#define VEC_GZWC        0070
#define VEC_GEOR        0071
#define VEC_HZWC        0072
#define VEC_HEOR        0073
#define VEC_MUXR        0200                            /* term mux rcv */
#define VEC_MUXT        0201                            /* term mux xmt */
#define VEC_MUXCO       0202                            /* SDS: mux carrier on */
#define VEC_MUXCF       0203                            /* SDS: mux carrier off */
#define VEC_DRM         0202                            /* Genie: drum */
#define VEC_FORK        0216                            /* "fork" */

/* Device constants */

#define DEV_MASK        077                             /* device mask */
#define DEV_TTI         001                             /* teletype */
#define DEV_PTR         004                             /* paper tape rdr */
#define DEV_MT          010                             /* magtape */
#define DEV_RAD         026                             /* fixed head disk */
#define DEV_DSK         026                             /* moving head disk */
#define DEV_TTO         041                             /* teletype */
#define DEV_PTP         044                             /* paper tape punch */
#define DEV_LPT         060                             /* line printer */
#define DEV_MTS         020                             /* MT scan/erase */
#define DEV_OUT         040                             /* output flag */
#define DEV3_GDRM       004                             /* Genie drum */
#define DEV3_GMUX       001                             /* Genie mux */
#define DEV3_SMUX       (DEV_MASK)                      /* standard mux */

#define LPT_WIDTH       132                             /* line print width */
#define CCT_LNT         132                             /* car ctrl length */

/* Transfer request flags for devices (0 is reserved) */

#define XFR_V_TTI       1                               /* console */
#define XFR_V_TTO       2
#define XFR_V_PTR       3                               /* paper tape */
#define XFR_V_PTP       4
#define XFR_V_LPT       5                               /* line printer */
#define XFR_V_RAD       6                               /* fixed hd disk */
#define XFR_V_DSK       7                               /* mving hd disk */
#define XFR_V_MT0       8                               /* magtape */

#define XFR_TTI         (1 << XFR_V_TTI)
#define XFR_TTO         (1 << XFR_V_TTO)
#define XFR_PTR         (1 << XFR_V_PTR)
#define XFR_PTP         (1 << XFR_V_PTP)
#define XFR_LPT         (1 << XFR_V_LPT)
#define XFR_RAD         (1 << XFR_V_RAD)
#define XFR_DSK         (1 << XFR_V_DSK)
#define XFR_MT0         (1 << XFR_V_MT0)

/* PIN/POT ordinals (0 is reserved) */

#define POT_ILCY        1                               /* interlace */
#define POT_DCRY        (POT_ILCY + NUM_CHAN)           /* data chain */
#define POT_ADRY        (POT_DCRY + NUM_CHAN)           /* address reg */
#define POT_RL1         (POT_ADRY + NUM_CHAN)           /* RL1 */
#define POT_RL2         (POT_RL1 + 1)                   /* RL2 */
#define POT_RL4         (POT_RL2 + 1)                   /* RL4 */
#define POT_RADS        (POT_RL4 + 1)                   /* fhd sector */
#define POT_RADA        (POT_RADS + 1)                  /* fhd addr */
#define POT_DSK         (POT_RADA + 1)                  /* mhd sec/addr */
#define POT_SYSI        (POT_DSK + 1)                   /* sys intr */
#define POT_MUX         (POT_SYSI + 1)                  /* multiplexor */

/* Opcodes */

enum opcodes {
    HLT, BRU, EOM, EOD = 006,
    MIY = 010, BRI, MIW, POT, ETR, MRG = 016, EOR,
    NOP, OVF = 022, EXU,
    YIM = 030, WIM = 032, PIN, STA = 035, STB, STX,
    SKS, BRX, BRM = 043, RCH = 046,
    SKE = 050, BRR, SKB, SKN, SUB, ADD, SUC, ADC,
    SKR, MIN, XMA, ADM, MUL, DIV, RSH, LSH,
    SKM, LDX, SKA, SKG, SKD, LDB, LDA, EAX
    };

/* Channel function prototypes */

void chan_set_flag (int32 ch, uint32 fl);
void chan_set_ordy (int32 ch);
void chan_disc (int32 ch);
void chan_set_uar (int32 ch, uint32 dev);
t_stat set_chan (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_chan (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat chan_process (void);
t_bool chan_testact (void);

/* Translation tables */
extern const int8 odd_par[64];


#endif
