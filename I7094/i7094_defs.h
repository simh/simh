/* i7094_defs.h: IBM 7094 simulator definitions

   Copyright (c) 2003-2011, Robert M Supnik

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

   This simulator incorporates prior work by Paul Pierce, Dave Pitts, and Rob
   Storey.  Tom Van Vleck, Stan Dunten, Jerry Saltzer, and other CTSS veterans
   helped to reconstruct the CTSS hardware RPQ's.  Dave Pitts gets special
   thanks for patiently coaching me through IBSYS debug.
   
   25-Mar-11    RMS     Updated SDC mask based on 7230 documentation
   22-May-10    RMS     Added check for 64b addresses

*/

#ifndef I7094_DEFS_H_
#define I7094_DEFS_H_  0

#include "sim_defs.h"                                   /* simulator defns */

/* Rename of global PC variable to avoid namespace conflicts on some platforms */

#define PC PC_Global

#if defined(USE_ADDR64)
#error "7094 does not support 64b addresses!"
#endif

/* Simulator stop codes */

#define STOP_HALT       1                               /* halted */
#define STOP_IBKPT      2                               /* breakpoint */
#define STOP_ILLEG      3                               /* illegal instr */
#define STOP_DIVCHK     4                               /* divide check */
#define STOP_XEC        5                               /* XCT loop */
#define STOP_ASTOP      6                               /* address stop */
#define STOP_NXCHN      7                               /* nx channel */
#define STOP_7909       8                               /* ill inst to 7909 */
#define STOP_NT7909     9                               /* ill inst to !7909 */
#define STOP_NXDEV      10                              /* nx device */
#define STOP_ILLCHI     11                              /* illegal channel op */
#define STOP_WRP        12                              /* write protect */
#define STOP_ILLIOP     13                              /* illegal I/O op */
#define STOP_INVFMT     14                              /* invalid disk format */
#define STOP_NOIFREE    15                              /* 7750: no buf for inp */
#define STOP_NOOFREE    16                              /* 7750: no buf for out */
#define STOP_INVLIN     17                              /* 7750: invalid line# */
#define STOP_INVMSG     18                              /* 7750: invalid message */
#define STOP_CHBKPT     19                              /* channel breakpoint */

/* Simulator error codes */

#define ERR_STALL       40                              /* stall */
#define ERR_ENDRC       41                              /* end rec */
#define ERR_NRCF        42                              /* no record found */

/* Instruction history - flags in left half of pc entry */

#define HIST_PC         0x04000000                      /* CPU */
#define HIST_V_CH       28                              /* chan + 1 */
#define HIST_M_CH       0xF
#define HIST_CH(x)      (((x) >> HIST_V_CH) & HIST_M_CH)

typedef struct {
    uint32              pc;
    uint32              ea;
    uint32              rpt;
    t_uint64    ir;
    t_uint64    ac;
    t_uint64    mq;
    t_uint64    si;
    t_uint64    opnd;
    } InstHistory;

/* Architectural constants */

#define A704_SIZE       14                              /* addr width, 704 mode */
#define ASIZE           15                              /* inst addr width */
#define PASIZE          16                              /* phys addr width */
#define STDMEMSIZE      (1u << ASIZE)                   /* standard memory */
#define MAXMEMSIZE      (1u << PASIZE)                  /* maximum memory */
#define A704_MASK       ((1u << A704_SIZE) - 1)
#define PAMASK          ((1u << PASIZE) - 1)
#define MEMSIZE         (cpu_unit.capac)
#define BCORE_V         (ASIZE)                         /* (CTSS) A/B core sel */
#define BCORE_BASE      (1u << BCORE_V)                 /* (CTSS) B core base */

/* Traps */

#define TRAP_STD_SAV    000000                          /* trap save location */
#define TRAP_TRA_PC     000001                          /* trap PC: transfer */
#define TRAP_STR_PC     000002                          /* trap PC: STR */
#define TRAP_FP_PC      000010                          /* trap PC: flt point */
#define TRAP_PROT_SAV   000032                          /* protection trap save */
#define TRAP_PROT_PC    000033                          /* protection trap PC */
#define TRAP_704_SAV    040000                          /* 704 compat trap */
#define TRAP_SEL_PC     040001                          /* 704 trap PC: select */
#define TRAP_CPY_PC     040002                          /* 704 trap PC: copy */

#define TRAP_F_MQ       000001                          /* MQ error */
#define TRAP_F_AC       000002                          /* AC error */
#define TRAP_F_OVF      000004                          /* overflow */
#define TRAP_F_SGL      000010                          /* single precision */
#define TRAP_F_DVC      000020                          /* fake: divide check */
#define TRAP_F_ODD      000040                          /* odd address */
#define TRAP_F_BDATA    020000                          /* (CTSS) data B core */
#define TRAP_F_BINST    040000                          /* (CTSS) inst B core */

/* Integer */

#define DMASK           INT64_C(0777777777777)          /* data mask */
#define SIGN            INT64_C(0400000000000)          /* sign */
#define MMASK           INT64_C(0377777777777)          /* magnitude mask */
#define LMASK           INT64_C(0777777000000)          /* left mask */
#define RMASK           INT64_C(0000000777777)          /* right mask */
#define PMASK           INT64_C(0700000000000)          /* prefix */
#define XMASK           INT64_C(0077777000000)          /* decrement */
#define TMASK           INT64_C(0000000700000)          /* tag */
#define AMASK           INT64_C(0000000077777)          /* address */
#define SCMASK          INT64_C(0000000000377)          /* shift count mask */
#define B1              INT64_C(0200000000000)          /* bit 1 */
#define B9              INT64_C(0000400000000)          /* bit 9 */

/* Accumulator is actually 38b wide */

#define AC_S            INT64_C(02000000000000)         /* sign */
#define AC_Q            INT64_C(01000000000000)         /* Q */
#define AC_P            INT64_C(00400000000000)         /* P */
#define AC_MMASK        INT64_C(01777777777777)         /* Q+P+magnitude */

/* Floating point */

#define FP_N_FR         27                              /* fraction bits */
#define FP_FMASK        ((1u << FP_N_FR) - 1)
#define FP_N_DFR        54                              /* double fraction bits */
#define FP_DFMASK       ((((t_uint64) 1) << FP_N_DFR) - 1)
#define FP_FNORM        (((t_uint64) 1u) << (FP_N_DFR - 1))     /* normalized bit */
#define FP_FCRY         (((t_uint64) 1u) << FP_N_DFR)   /* fraction carry */
#define FP_BIAS         0200                            /* exponent bias */
#define FP_V_CH         (FP_N_FR)                       /* exponent */
#define FP_M_CH         0377                            /* SR char mask */
#define FP_M_ACCH       01777                           /* AC char mask incl Q,P */

/* Instruction format */

#define INST_T_DEC      INT64_C(0300000000000)          /* if nz, takes decr */
#define INST_T_CXR1     INT64_C(0000000100000)          /* if nz, update XR1 */
#define INST_V_OPD      33                              /* decrement opcode */
#define INST_M_OPD      07
#define INST_V_DEC      18                              /* decrement */
#define INST_M_DEC      077777
#define INST_V_OPC      24                              /* normal opcode */
#define INST_M_OPC      0777
#define INST_V_IND      22                              /* indirect */
#define INST_IND        (3 << INST_V_IND)
#define INST_V_CCNT     18                              /* convert count */
#define INST_M_CCNT     0377
#define INST_V_VCNT     18                              /* vlm/vdh count */
#define INST_M_VCNT     077
#define INST_V_TAG      15                              /* index */
#define INST_M_TAG      07
#define INST_V_ADDR     0
#define INST_M_ADDR     077777
#define INST_V_4B       0
#define INST_M_4B       017

#define GET_OPD(x)      ((uint32) (((x) >> INST_V_OPD) & INST_M_OPD))
#define GET_DEC(x)      ((uint32) (((x) >> INST_V_DEC) & INST_M_DEC))
#define GET_OPC(x)      (((uint32) (((x) >> INST_V_OPC) & INST_M_OPC)) | \
                     (((x) & SIGN)? 01000: 0))
#define TST_IND(x)      (((x) & INST_IND) == INST_IND)
#define GET_CCNT(x)     ((uint32) (((x) >> INST_V_CCNT) & INST_M_CCNT))
#define GET_VCNT(x)     ((uint32) (((x) >> INST_V_VCNT) & INST_M_VCNT))
#define GET_TAG(x)      ((uint32) (((x) >> INST_V_TAG) & INST_M_TAG))

/* Instruction decode flags */

#define I_4X            0x01                            /* 7040, 7044 */
#define I_9X            0x02                            /* 7090, 7094, CTSS */
#define I_94            0x04                            /* 7094, CTSS */
#define I_CT            0x08                            /* CTSS */
#define I_MODEL         0x0F                            /* option mask */       
#define I_X             0x10                            /* indexed */
#define I_N             0x20                            /* indirect */
#define I_R             0x40                            /* read */
#define I_D             0x80                            /* double read */

#define I_XN            (I_X|I_N)
#define I_XNR           (I_X|I_N|I_R)
#define I_XND           (I_X|I_N|I_D)

/* Memory protection (CTSS) */

#define VA_V_OFF        0                               /* offset in block */
#define VA_N_OFF        8                               /* width of offset */
#define VA_M_OFF        ((1u << VA_N_OFF) - 1)
#define VA_OFF          (VA_M_OFF << VA_V_OFF)
#define VA_V_BLK        (VA_N_OFF)                      /* block */
#define VA_N_BLK        (ASIZE - VA_N_OFF)              /* width of block */
#define VA_M_BLK        ((1u << VA_N_BLK) - 1)
#define VA_BLK          (VA_M_BLK << VA_V_BLK)

/* Unsigned operations */

#define NEG(x)          (~(x) + 1)
#define BIT_TST(w,b)    (((w) >> (b)) & 1)

/* Device information block */

typedef struct {
    t_stat              (*chsel)(uint32 ch, uint32 sel, uint32 u);
    t_stat              (*write)(uint32 ch, t_uint64 val, uint32 flags);
    } DIB;

/* BCD digits */

#define BCD_MASK        017
#define BCD_ZERO        012
#define BCD_ONE         001
#define BCD_TWO         002
#define BCD_AT          014

/* Channels */

#define NUM_CHAN        8                               /* # channels */
#define CH_A            0                               /* channel A */
#define CH_B            1
#define CH_C            2
#define CH_D            3
#define CH_E            4
#define CH_F            5
#define CH_G            6
#define CH_H            7

#define REQ_CH(x)       (1u << (x))

/* All channel commands */

#define CHI_IND         0000000400000                   /* ch inst indirect */

/* Channel selects - all channels */

#define CHSL_RDS        0001                            /* data selects */
#define CHSL_WRS        0002
#define CHSL_SNS        0003
#define CHSL_CTL        0004
#define CHSL_FMT        0005
#define CHSL_WEF        0010                            /* non-data selects */
#define CHSL_WBT        0011                            /* 704X only */
#define CHSL_BSR        0012
#define CHSL_BSF        0013
#define CHSL_REW        0014
#define CHSL_RUN        0015
#define CHSL_SDN        0016
#define CHSL_2ND        0020                            /* second state */
#define CHSL_3RD        0040                            /* etc */
#define CHSL_4TH        0060
#define CHSL_5TH        0100
#define CHSL_NDS        0010                            /* non-data sel flag */
#define CHSL_NUM        16

/* Channel commands - 7607/7289 - S12'19 */

#define CH6I_NST        0000000200000                   /* ch inst no store */

#define CH6_IOCD        000
#define CH6_TCH         002
#define CH6_IORP        004
#define CH6_IORT        006
#define CH6_IOCP        010
#define CH6_IOCT        012
#define CH6_IOSP        014
#define CH6_IOST        016
#define CH6_OPMASK      016                             /* without nostore */
#define TCH_LIMIT       5                               /* TCH autoresolve limit */

/* Channel data flags - 7607 */

#define CH6DF_EOR       1                               /* end of record */
#define CH6DF_VLD       2                               /* input valid */

/* Channel commands - 7909 - S123'19 */

#define CH9_WTR         000
#define CH9_XMT         001
#define CH9_TCH         004
#define CH9_LIPT        005
#define CH9_CTL         010
#define CH9_CTLR        011
#define CH9_CTLW        012
#define CH9_SNS         013
#define CH9_LAR         014
#define CH9_SAR         015
#define CH9_TWT         016
#define CH9_CPYP        020
#define CH9_CPYD        024
#define CH9_TCM         025
#define CH9_LIP         031
#define CH9_TDC         032
#define CH9_LCC         033
#define CH9_SMS         034
#define CH9_ICC         035
#define CH9_ICCA        037                             /* ignores bit <3> */
#define CH9_OPMASK      037

/* Channel data flags - 7909 */

#define CH9DF_STOP      1                               /* stop */
#define CH9DF_VLD       2                               /* input valid */

/* Extended parts of the command come from the decrement, stored in ch_wc */

#define CH9D_V_MASK     0                               /* condition mask */
#define CH9D_M_MASK     077
#define CH9D_V_COND     12                              /* condition select */
#define CH9D_M_COND     07
#define CH9D_MASK(x)    (((x) >> CH9D_V_MASK) & CH9D_M_MASK)
#define CH9D_COND(x)    (((x) >> CH9D_V_COND) & CH9D_M_COND)

#define CH9D_NST        020000                          /* no store */
#define CH9D_B11        000100

/* Or from the effective address, stored in ch_ca */

#define CH9A_V_LCC      0                               /* counter */
#define CH9A_M_LCC      077
#define CH9A_V_SMS      0                               /* system mask */
#define CH9A_M_SMS      0177
#define CH9A_LCC(x)     (((x) >> CH9A_V_LCC) & CH9A_M_LCC)
#define CH9A_SMS(x)     (((x) >> CH9A_V_SMS) & CH9A_M_SMS)

/* Channel states - common */

#define CHXS_IDLE       0                               /* idle */
#define CHXS_DSX        1                               /* executing */

/* Channel states - 7607/7289 */

#define CH6S_PNDS       2                               /* polling NDS */
#define CH6S_PDS        3                               /* polling DS */
#define CH6S_NDS        4                               /* nds, executing */
#define CH6S_DSW        5                               /* ds, chan wait */

/* Channel traps - 7909 has only CMD (== TWT) */

#define CHTR_V_CME      0                               /* cmd/eof enable */
#define CHTR_V_CLK      17                              /* clock */
#define CHTR_V_TRC      18                              /* tape check */
#define CHTR_V_TWT      (CHTR_V_CME)
#define CHTR_CLK_SAV    006                             /* clock */
#define CHTR_CHA_SAV    012                             /* start of chan block */
#define CHTR_F_CMD      1                               /* CMD flag (in decr) */
#define CHTR_F_TRC      2                               /* TRC flag (in decr) */
#define CHTR_F_EOF      4                               /* EOF flag (in decr) */

/* Channel interrupts - 7909 only */

#define CHINT_CHA_SAV   042                             /* start of chan block */

/* Channel interrupt conditions - 7909 only */

#define CHINT_ADPC      001                             /* adapter check */
#define CHINT_ATN2      002                             /* attention 2 - ni */
#define CHINT_ATN1      004                             /* attention 1 */
#define CHINT_UEND      010                             /* unusual end */
#define CHINT_SEQC      020                             /* sequence check */
#define CHINT_IOC       040                             /* IO check */

/* Channel SMS flags - 7909 only */

#define CHSMS_SEL2      0001                            /* select 2nd - ni */
#define CHSMS_IATN2     0002                            /* inhibit atn2 - ni */
#define CHSMS_IATN1     0004                            /* inhibit atn1 */
#define CHSMS_IUEND     0010                            /* inhibit uend */
#define CHSMS_BCD       0020                            /* BCD conversion - ni */
#define CHSMS_RBCK      0040                            /* read backwards - ni */
#define CHSMS_ENCI      0100                            /* enable noncon - ni */

/* Channel flags (7607 in right half, 7909 in left half) */

#define CHF_CMD         00000000001                     /* cmd done */
#define CHF_TWT         (CHF_CMD)
#define CHF_TRC         00000000002                     /* tape check */
#define CHF_EOF         00000000004                     /* end of file */
#define CHF_BOT         00000000010                     /* beginning of tape */
#define CHF_EOT         00000000020                     /* end of tape */
#define CHF_LDW         00000000040                     /* LCH waiting */
#define CHF_EOR         00000000100                     /* end of record */
#define CHF_IRQ         00001000000                     /* intr request */
#define CHF_INT         00002000000                     /* intr in prog */
#define CHF_WRS         00004000000                     /* write */
#define CHF_RDS         00010000000                     /* read */
#define CHF_PWR         00020000000                     /* prepare to write */
#define CHF_PRD         00040000000                     /* prepare to read */
#define CHF_V_COND      24                              /* cond register */
#define CHF_M_COND      077
#define  CHF_ADPC       (CHINT_ADPC << CHF_V_COND)      /* adapter check */
#define  CHF_ATN2       (CHINT_ATN2 << CHF_V_COND)      /* attention 2 */
#define  CHF_ATN1       (CHINT_ATN1 << CHF_V_COND)      /* attention 1 */
#define  CHF_UEND       (CHINT_UEND << CHF_V_COND)      /* unusual end */
#define  CHF_SEQC       (CHINT_SEQC << CHF_V_COND)      /* sequence check */
#define  CHF_IOC        (CHINT_IOC << CHF_V_COND)       /* IO check */
#define CHF_V_LCC       30                              /* loop ctrl counter */
#define CHF_M_LCC       077

#define CHF_CLR_7909    INT64_C(07775000177)            /* 7909 clear flags */
#define CHF_SDC_7909    INT64_C(07777600000)            /* 7909 SDC flags */

/* Channel characteristics (in dev.flags) */

#define DEV_7909        (1u << (DEV_V_UF + 0))
#define DEV_7289        (1u << (DEV_V_UF + 1))
#define DEV_CDLP        (1u << (DEV_V_UF + 2))
#define DEV_7750        (1u << (DEV_V_UF + 3))
#define DEV_7631        (1u << (DEV_V_UF + 4))

/* Unit addresses - 7607/7289 only */

#define U_V_CH          9                               /* channel number */
#define U_M_CH          077
#define U_V_UNIT        0
#define U_M_UNIT        0777
#define GET_U_CH(x)     (((((uint32) (x)) >> U_V_CH) & U_M_CH) - 1)
#define GET_U_UNIT(x)   ((((uint32) (x)) >> U_V_UNIT) & U_M_UNIT)

#define U_MTBCD         0201                            /* BCD tape */
#define U_MTBIN         0221                            /* binary tape */
#define U_CDR           0321                            /* card reader */
#define U_CDP           0341                            /* card punch */
#define U_LPBCD         0361                            /* BCD print */
#define U_LPBIN         0362                            /* binary print */
#define U_DRM           0330                            /* 7320A drum */

#define MT_NUMDR        10

/* CTSS Chronolog clock */

#define CHRONO_CH       (CH_A)                          /* channel A */
#define CHRONO_UNIT     (7)                             /* unit 7 */

/* Interval timer */

#define CLK_CTR         05                              /* counter */
#define CLK_TPS         60                              /* 60Hz */
#define TMR_CLK         0                               /* use timer 0 */
#define TMR_COM         1                               /* 7750 timer */

/* Function prototypes and macros */

#define ReadP(p)        M[p]
#define WriteP(p,d)     M[p] = d

void cpu_ent_hist (uint32 pc, uint32 ea, t_uint64 ir, t_uint64 opnd);
t_stat ch_show_chan (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ch6_end_nds (uint32 ch);
uint32 ch6_set_flags (uint32 ch, uint32 unit, uint32 flags);
t_stat ch6_err_disc (uint32 ch, uint32 unit, uint32 flags);
t_stat ch6_req_rd (uint32 ch, uint32 unit, t_uint64 val, uint32 flags);
t_stat ch6_req_wr (uint32 ch, uint32 unit);
t_bool ch6_qconn (uint32 ch, uint32 unit);
t_stat ch9_req_rd (uint32 ch, t_uint64 val);
void ch9_set_atn (uint32 ch);
void ch9_set_ioc (uint32 ch);
void ch9_set_end (uint32 ch, uint32 ireq);
t_bool ch9_qconn (uint32 ch);
void ch_set_map (void);
t_bool ch_qidle (void);

extern const uint32 col_masks[12];
extern const t_uint64 bit_masks[36];
extern const char nine_to_ascii_a[64];
extern const char nine_to_ascii_h[64];
extern const char ascii_to_nine[128];
extern const char ascii_to_bcd[128];
extern const char bcd_to_ascii_a[64];
extern const char bcd_to_ascii_h[64];
extern const char bcd_to_pca[64];
extern const char bcd_to_pch[64];
extern const uint32 bcd_to_colbin[64];

extern uint32 PC;
extern uint32 ind_ioc;

#endif
