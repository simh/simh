/* pdp11_hk.c - RK611/RK06/RK07 disk controller

   Copyright (c) 1993-2013, Robert M Supnik

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
   FITNESS FOR A PARTICULAR PUHKOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   hk           RK611/RK06/RK07 disk

   23-Oct-13    RMS     Revised for new boot setup routine
   01-Sep-13    RMS     Revised error handling to command-response model
                        Revised interrupt logic to follow the hardware
   10-Jun-13    RMS     Fixed bug to write through end of sector (Oleg Safiullin)
   18-Apr-13    RMS     Fixed ATN setting on errors
                        Changed wrong drive type to status, not fatal error
   10-Dec-12    RMS     Fixed interrupt logic and CCLR unit cancellation
   19-Mar-12    RMS     Fixed declaration of cpu_opt (Mark Pizzolato)
   29-Apr-07    RMS     NOP and DCLR (at least) do not check drive type [wrong]
                        MR2 and MR3 only updated on NOP
   17-Nov-05    RMS     Removed unused variable
   13-Nov-05    RMS     Fixed overlapped seek interaction with NOP, DCLR, PACK
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   07-Jul-05    RMS     Removed extraneous externs
   18-Mar-05    RMS     Added attached test to detach routine
   03-Oct-04    RMS     Revised Unibus interface
                RMS     Fixed state of output ready for M+
   26-Mar-04    RMS     Fixed warnings with -std=c99
   25-Jan-04    RMS     Revised for device debug support
   04-Jan-04    RMS     Changed sim_fsize calling sequence
   29-Dec-03    RMS     Added 18b Qbus support
   25-Apr-03    RMS     Revised for extended file support

   The RK611 functions only in 18b Unibus systems with I/O maps. The Emulex
   SC02/C was a Qbus work-alike with a unique extension to 22b addressing. It
   was only supported in Ultrix-11 and other third party software.

   This module includes ideas from a previous implementation by Fred Van Kempen.
   However, the interrupt logic has been rewritten to follow the bug-for-bug
   peculiarities of the RK611 controller.
*/

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "RK611 is not supported on the PDP-10!"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"

#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
extern uint32 cpu_opt;
#endif

#define HK_NUMDR        8                               /* #drives */
#define HK_NUMCY6       411                             /* cyl/drive */
#define HK_NUMCY7       815                             /* cyl/drive */
#define HK_NUMSF        3                               /* tracks/cyl */
#define HK_NUMSC        22                              /* sectors/track */
#define HK_NUMWD        256                             /* words/sector */
#define RK06_SIZE       (HK_NUMCY6*HK_NUMSF*HK_NUMSC*HK_NUMWD)
#define RK07_SIZE       (HK_NUMCY7*HK_NUMSF*HK_NUMSC*HK_NUMWD)
#define HK_SIZE(x)      (((x)->flags & UNIT_DTYPE)? RK07_SIZE: RK06_SIZE)
#define HK_CYL(x)       (((x)->flags & UNIT_DTYPE)? HK_NUMCY7: HK_NUMCY6)
#define HK_MAXFR        (1 << 16)

/* Flags in the unit flags word */

#define UNIT_V_WLK      (UNIT_V_UF + 0)                 /* write locked */
#define UNIT_V_DTYPE    (UNIT_V_UF + 1)                 /* disk type */
#define UNIT_V_AUTO     (UNIT_V_UF + 2)                 /* autosize */
#define UNIT_V_DUMMY    (UNIT_V_UF + 3)                 /* dummy flag */
#define UNIT_WLK        (1 << UNIT_V_WLK)
#define UNIT_DTYPE      (1 << UNIT_V_DTYPE)
#define  UNIT_RK06      (0 << UNIT_V_DTYPE)
#define  UNIT_RK07      (1 << UNIT_V_DTYPE)
#define UNIT_AUTO       (1 << UNIT_V_AUTO)
#define UNIT_DUMMY      (1 << UNIT_V_DUMMY)
#define UNIT_WPRT       (UNIT_WLK | UNIT_RO)            /* write prot */

/* Parameters in the unit descriptor */

#define CYL             u3                              /* current cylinder */
#define FNC             u4                              /* function */

/* HKCS1 - 177440 - control/status 1 ^ = calculated dynamically */

#define CS1_GO          CSR_GO                          /* go */
#define CS1_V_FNC       1                               /* function pos */
#define CS1_M_FNC       017                             /* function mask */
#define CS1_FNC         (CS1_M_FNC << CS1_V_FNC)
#define  FNC_NOP        000                             /* no operation */
#define  FNC_PACK       001                             /* pack acknowledge */
#define  FNC_DCLR       002                             /* drive clear */
#define  FNC_UNLOAD     003                             /* unload */
#define  FNC_START      004                             /* start */
#define  FNC_RECAL      005                             /* recalibrate */
#define  FNC_OFFSET     006                             /* offset */
#define  FNC_SEEK       007                             /* seek */
#define FNC_XFER        010
#define  FNC_READ       010                             /* read */
#define  FNC_WRITE      011                             /* write */
#define  FNC_WRITEH     013                             /* write w/ headers */
#define  FNC_READH      012                             /* read w/ headers */
#define  FNC_WCHK       014                             /* write check */
#define FNC_2ND         020                             /* 2nd state flag */
#define CS1_SPA         0000040                         /* spare */
#define CS1_IE          CSR_IE                          /* int enable */
#define CS1_DONE        CSR_DONE                        /* ready */
#define CS1_V_UAE       8                               /* Unibus addr ext */
#define CS1_M_UAE       03
#define CS1_UAE         (CS1_M_UAE << CS1_V_UAE)
#define CS1_DT          0002000                         /* drive type */
#define CS1_CTO         0004000                         /* ctrl timeout NI */
#define CS1_FMT         0010000                         /* 16b/18b NI */
#define CS1_PAR         0020000                         /* par err NI */
#define CS1_DI          0040000                         /* drive intr */
#define CS1_ERR         0100000                         /* error */
#define CS1_CCLR        0100000                         /* ctrl clear */
#define CS1_RW          (CS1_DT|CS1_UAE|CS1_IE|CS1_SPA|CS1_FNC)
#define GET_FNC(x)      (((x) >> CS1_V_FNC) & CS1_M_FNC)
#define GET_UAE(x)      (((x) >> CS1_V_UAE) & CS1_M_UAE)
#define PUT_UAE(x,n)    (((x) & ~ CS1_UAE) | (((n) << CS1_V_UAE) & CS1_UAE))

static const char *hk_funcs[] = {
    "NOP", "PACK", "DCLR", "UNLOAD", "START", "RECAL", "OFFSET", "SEEK", 
    "READ", "WRITE", "READH", "WRITEH", "WCHK"};

BITFIELD hk_cs1_bits[] = {
  BIT(GO),                                  /* go */
  BITFNAM(FNC,4,hk_funcs),                  /* function */
  BIT(SPARE),                               /* Spare Bit */
  BIT(IE),                                  /* Interrupt Enable */
  BIT(RDY),                                 /* Controller Ready */
  BIT(BA16),                                /* Unibus addr ext bit 16 */
  BIT(BA17),                                /* Unibus addr ext bit 17 */
  BIT(DT),                                  /* Controller Drive Type */
  BIT(CTO),                                 /* Controller Timeout */
  BIT(CFMT),                                /* Controller Format */
  BIT(DTCPAR),                              /* Drive-To-Controller Parity Error */
  BIT(DI),                                  /* Drive Interrupt */
  BIT(ERR),                                 /* error */
  ENDBITS
};


/* HKWC - 177442 - word count */

BITFIELD hk_wc_bits[] = {
  BITF(WC,16),                              /* Word Count */
  ENDBITS
};

/* HKBA - 177444 - base address */

#define BA_MBZ          0000001                         /* must be zero */

BITFIELD hk_ba_bits[] = {
  BITF(BA,16),                              /* Bus Address */
  ENDBITS
};

/* HKDA - 177446 - sector/track */

#define DA_V_SC         0                               /* sector pos */
#define DA_M_SC         037                             /* sector mask */
#define DA_V_SF         8                               /* track pos */
#define DA_M_SF         007                             /* track mask */
#define DA_MBZ          0174340
#define GET_SC(x)       (((x) >> DA_V_SC) & DA_M_SC)
#define GET_SF(x)       (((x) >> DA_V_SF) & DA_M_SF)

BITFIELD hk_da_bits[] = {
  BITF(SA,5),                               /* Sector */
  BITNCF(3),                                /* 05:07 Zero */
  BITF(TA,3),                               /* Track */
  BITNCF(5),                                /* 11:15 Zero */
  ENDBITS
};

/* HKCS2 - 177450 - control/status 2 */

#define CS2_V_UNIT      0                               /* unit pos */
#define CS2_M_UNIT      07                              /* unit mask */
#define CS2_UNIT        (CS2_M_UNIT << CS2_V_UNIT)
#define CS2_RLS         0000010                         /* release NI */
#define CS2_UAI         0000020                         /* addr inhibit */
#define CS2_CLR         0000040                         /* controller clear */
#define CS2_IR          0000100                         /* input ready */
#define CS2_OR          0000200                         /* output ready */
#define CS2_UFE         0000400                         /* unit field err NI */
#define CS2_MDS         0001000                         /* multidrive sel NI */
#define CS2_PGE         0002000                         /* program err */
#define CS2_NEM         0004000                         /* nx mem err */
#define CS2_NED         0010000                         /* nx drive err */
#define CS2_PE          0020000                         /* parity err NI */
#define CS2_WCE         0040000                         /* write check err */
#define CS2_DLT         0100000                         /* data late NI */
#define CS2_MBZ         (CS2_CLR)
#define CS2_RW          0000037
#define CS2_ERR         (CS2_UFE | CS2_MDS | CS2_PGE | CS2_NEM | \
                         CS2_NED | CS2_PE | CS2_WCE | CS2_DLT )
#define GET_UNIT(x)     (((x) >> CS2_V_UNIT) & CS2_M_UNIT)

BITFIELD hk_cs2_bits[] = {
  BITF(DS,3),                               /* Drive Select */
  BIT(RLS),                                 /* Release */
  BIT(BAI),                                 /* Bus Address Increment Inhibit */
  BIT(SCLR),                                /* Subsystem Clear */
  BIT(IR),                                  /* Input Ready */
  BIT(OR),                                  /* Output Ready */
  BIT(UFE),                                 /* Unit Field Error */
  BIT(MDS),                                 /* Multiple Drive Select */
  BIT(PGE),                                 /* Programming Error */
  BIT(NEM),                                 /* Nonexistant Memory */
  BIT(NED),                                 /* Nonexistant Drive */
  BIT(UPE),                                 /* Unibus Parity Error */
  BIT(WCE),                                 /* Write Check Error */
  BIT(DLT),                                 /* Data Late Error */
  ENDBITS
};


/* HKDS - 177452 - drive status ^ = calculated dynamically */

#define DS_DRA          0000001                         /* ^drive avail */
#define DS_OF           0000004                         /* ^offset mode */
#define DS_ACLO         0000010                         /* ^AC LO NI */
#define DS_SPLS         0000020                         /* ^speed loss NI */
#define DS_DOT          0000040                         /* ^off track NI */
#define DS_VV           0000100                         /* volume valid */
#define DS_RDY          0000200                         /* ^drive ready */
#define DS_DT           0000400                         /* ^drive type */
#define DS_WRL          0004000                         /* ^write locked */
#define DS_PIP          0020000                         /* pos in progress */
#define DS_ATA          0040000                         /* attention active */
#define DS_VLD          0100000                         /* ^status valid */
#define DS_MBZ          0013002

BITFIELD hk_ds_bits[] = {
  BIT(DRA),                                 /* Drive Available */
  BITNCF(1),                                /* 01 Spare */
  BIT(OFST),                                /* Offset */
  BIT(ACLO),                                /* Drive AC Low */
  BIT(SPLS),                                /* Speed Loss */
  BIT(DROT),                                /* Drive Off Track */
  BIT(VV),                                  /* Volume Valid */
  BIT(DRDY),                                /* Drive Ready */
  BIT(DDT),                                 /* Disk Drive Type */
  BITNCF(2),                                /* 09:10 Spare */
  BIT(WRL),                                 /* Write Lock */
  BITNCF(1),                                /* 12 Spare */
  BIT(PIP),                                 /* Positioning in Progress */
  BIT(ATA),                                 /* Current Drive Attention */
  BIT(SVAL),                                /* Status Valid */
  ENDBITS
};

/* HKER - 177454 - error status */

#define ER_ILF          0000001                         /* illegal func */
#define ER_SKI          0000002                         /* seek incomp */
#define ER_NXF          0000004                         /* non-exec func */
#define ER_PAR          0000010                         /* parity err */
#define ER_FER          0000020                         /* format err */
#define ER_DTY          0000040                         /* drive type err */
#define ER_ECH          0000100                         /* ECC hard err NI */
#define ER_BSE          0000200                         /* bad sector err NI */
#define ER_HCR          0000400                         /* hdr CRC err NI */
#define ER_AOE          0001000                         /* addr ovflo err */
#define ER_IAE          0002000                         /* invalid addr err */
#define ER_WLE          0004000                         /* write lock err */
#define ER_DTE          0010000                         /* drive time err NI */
#define ER_OPI          0020000                         /* op incomplete */
#define ER_UNS          0040000                         /* drive unsafe */
#define ER_DCK          0100000                         /* data check NI */

BITFIELD hk_er_bits[] = {
  BIT(ILF),                                 /* Illegal Function */
  BIT(SKI),                                 /* Seek Incomplete */
  BIT(NXF),                                 /* Nonexecutable Function */
  BIT(DROAR),                               /* Control-to-Drive Parity Error */
  BIT(FMTE),                                /* Format Error */
  BIT(DTYE),                                /* Drive Type Error */
  BIT(ECH),                                 /* Error Correction Hard */
  BIT(BSE),                                 /* Bad Sector Error */
  BIT(HRVC),                                /* Header Virtical Redundancy */
  BIT(COE),                                 /* Cylinder Overflow Error */
  BIT(IDAE),                                /* Invalid Disk Address Error */
  BIT(WLE),                                 /* Write Lock Error */
  BIT(DTE),                                 /* Drive Timing Error */
  BIT(OPI),                                 /* Operation Incomplete */
  BIT(UNS),                                 /* Drive Unsafe */
  BIT(DCK),                                 /* Data Check */
  ENDBITS
};

/* HKAS - 177456 - attention summary/offset */

#define AS_U0           0000400                         /* unit 0 flag */
#define AS_OF           0000277                         /* offset mask */

BITFIELD hk_as_bits[] = {
  BITF(OF,8),                               /* Offset */
  BIT(ATN0),                                /* Attention Drive 0 */
  BIT(ATN1),                                /* Attention Drive 1 */
  BIT(ATN2),                                /* Attention Drive 2 */
  BIT(ATN3),                                /* Attention Drive 3 */
  BIT(ATN4),                                /* Attention Drive 4 */
  BIT(ATN5),                                /* Attention Drive 5 */
  BIT(ATN6),                                /* Attention Drive 6 */
  BIT(ATN7),                                /* Attention Drive 7 */
  ENDBITS
};

/* HKDC - 177460 - desired cylinder */

#define DC_V_CY         0                               /* cylinder pos */
#define DC_M_CY         0001777                         /* cylinder mask */
#define DC_MBZ          0176000
#define GET_CY(x)       (((x) >> DC_V_CY) & DC_M_CY)
#define GET_DA(c,fs)    ((((GET_CY (c) * HK_NUMSF) + \
                        GET_SF (fs)) * HK_NUMSC) + GET_SC (fs))

BITFIELD hk_dc_bits[] = {
  BITF(DC,10),                              /* Desired Cylinder */
  BITNCF(6),                                /* 10:15 Spare */
  ENDBITS
};

/* Spare - 177462 - read/write */

#define XM_KMASK        0177700                         /* Qbus XM key mask */
#define XM_KEY          0022000                         /* Qbus XM "key" */
#define XM_MMASK        0000077                         /* Qbus XM mask */
#define SC02C           (!UNIBUS && ((hkspr & XM_KMASK) == XM_KEY))

/* HKDB - 177464 - read/write */

BITFIELD hk_db_bits[] = {
  BITF(DB,16),                              /* Data Buffer Register */
  ENDBITS
};

/* HKMR - 177466 - maintenance register 1 */

#define MR_V_MS         0                               /* message select */
#define MR_M_MS         03
#define MR_MS           (MR_M_MS << MR_V_MS)
#define GET_MS(x)       (((x) >> MR_V_MS) & MR_M_MS)
#define MR_PAR          0000020                         /* force even parity */
#define MR_DMD          0000040                         /* diagnostic mode */
#define MR_RW           0001777

BITFIELD hk_mr_bits[] = {
  BITF(MS,4),                               /* Message Select */
  BIT(PAT),                                 /* Parity Test */
  BIT(DMD),                                 /* Diagnostic Mode */
  BIT(MSP),                                 /* Maintenance Selector Pulse */
  BIT(MIND),                                /* Maintenance Index */
  BIT(MCLK),                                /* Maintenance Clock */
  BIT(MERD),                                /* Maintenance Encoded Read Data */
  BIT(MEWD),                                /* Maintenance Encoded Write Data */
  BIT(PCA),                                 /* Precompensation Advance */
  BIT(PCD),                                 /* Precompensation Delay */
  BIT(ECCW),                                /* ECC Word */
  BIT(WRTGT),                               /* Write Gate */
  BIT(RDGT),                                /* Read Gate */
  ENDBITS
};

/* HKEC1 - 177470 - ECC status 1 - always reads as 0 */

BITFIELD hk_ec1_bits[] = {
  BITF(EC1,16),                             /* ECC status 1 */
  ENDBITS
};

/* HKEC2 - 177472 - ECC status 2 - always reads as 0 */

BITFIELD hk_ec2_bits[] = {
  BITF(EC2,16),                             /* ECC status 2 */
  ENDBITS
};

/* HKMR2 - 177474 - maintenance register 2 */

#define AX_V_UNIT       0                               /* unit #, all msgs */
#define AX_PAR          0100000                         /* parity, all msgs */

#define A0_DRA          0000040                         /* drive avail */
#define A0_VV           0000100                         /* vol valid */
#define A0_RDY          0000200                         /* drive ready */
#define A0_DT           0000400                         /* drive type */
#define A0_FMT          0001000                         /* format NI */ 
#define A0_OF           0002000                         /* offset mode */
#define A0_WRL          0004000                         /* write lock */
#define A0_SPO          0010000                         /* spindle on */
#define A0_PIP          0020000                         /* pos in prog */
#define A0_ATA          0040000                         /* attention */

#define A1_SRV          0000020                         /* servo */
#define A1_HHM          0000040                         /* heads home */
#define A1_BHM          0000100                         /* brushes home */
#define A1_DOR          0000200                         /* door latched */
#define A1_CAR          0000400                         /* cartridge present */
#define A1_SPD          0001000                         /* speed ok */
#define A1_FWD          0002000                         /* seek fwd */
#define A1_REV          0004000                         /* seek rev */
#define A1_LDH          0010000                         /* loading heads NI */
#define A1_RTZ          0020000                         /* return to zero */
#define A1_UNL          0040000                         /* unloading heads */

#define A2_V_DIF        4                               /* cyl diff */
#define A2_M_DIF        0777

#define A3_V_SNO        3                               /* serial # */

BITFIELD hk_mr2_bits[] = {
  BITF(MR2,16),                             /* Maintenance Register 2 */
  ENDBITS
};

/* HKMR3 - 177476 - maintenance register 3 */

#define B0_IAE          0000040                         /* invalid addr */
#define B0_ACLO         0000100                         /* AC LO NI */
#define B0_FLT          0000200                         /* fault */
#define B0_NXF          0000400                         /* non exec fnc */
#define B0_CDP          0001000                         /* msg parity err */
#define B0_SKI          0002000                         /* seek incomp */
#define B0_WLE          0004000                         /* write lock err */
#define B0_SLO          0010000                         /* speed low NI */
#define B0_OFT          0020000                         /* off track NI */
#define B0_UNS          0040000                         /* rw unsafe NI */

#define B1_SCE          0000020                         /* sector err NI */
#define B1_NWC          0000040                         /* no write curr NI */
#define B1_NWT          0000100                         /* no write trans NI */
#define B1_HFL          0000200                         /* head fault NI */
#define B1_MHS          0000400                         /* multiselect NI */
#define B1_IDX          0001000                         /* index err NI */
#define B1_TRI          0002000                         /* tribit err NI */
#define B1_SVE          0004000                         /* servo err NI */
#define B1_SKI          0010000                         /* seek no mot */
#define B1_LIM          0020000                         /* seek limit NI */
#define B1_SVU          0040000                         /* servo unsafe NI */

#define B2_V_CYL        4                               /* cylinder */

#define B3_V_SEC        4                               /* sector */
#define B3_V_DHA        9                               /* decoded head */

/* Read header */

#define RDH1_V_CYL      0                               /* cylinder */
#define RDH2_V_SEC      0                               /* sector */
#define RDH2_V_DHA      5                               /* decoded head */
#define RDH2_GOOD       0140000                         /* good sector flags */

BITFIELD hk_mr3_bits[] = {
  BITF(MR3,16),                             /* Maintenance Register 3 */
  ENDBITS
};

const char *hk_regnames[] = {
    "HKCS1",
    "HKWC",
    "HKBA",
    "HKDA",
    "HKCS2",
    "HKDS",
    "HKER",
    "HKAS",
    "HKDC",
    "spare",
    "HKDB",
    "HKMR",
    "HKEC1",
    "HKEC2",
    "HKMR2",
    "HKMR3"
    };

BITFIELD *hk_reg_bits[] = {
    hk_cs1_bits,
    hk_wc_bits,
    hk_ba_bits,
    hk_da_bits,
    hk_cs2_bits,
    hk_ds_bits,
    hk_er_bits,
    hk_as_bits,
    hk_dc_bits,
    NULL,
    hk_db_bits,
    hk_mr_bits,
    hk_ec1_bits,
    hk_ec2_bits,
    hk_mr2_bits,
    hk_mr3_bits,
    };

/* Debug detail levels */

#define HKDEB_OPS       001                             /* transactions */
#define HKDEB_RRD       002                             /* reg reads */
#define HKDEB_RWR       004                             /* reg writes */
#define HKDEB_TRC       010                             /* trace */
#define HKDEB_INT       020                             /* interrupts */

extern int32 int_req[IPL_HLVL];

uint16 *hkxb = NULL;                                    /* xfer buffer */
int32 hkcs1 = 0;                                        /* control/status 1 */
int32 hkwc = 0;                                         /* word count */
int32 hkba = 0;                                         /* bus address */
int32 hkda = 0;                                         /* track/sector */
int32 hkcs2 = 0;                                        /* control/status 2 */
int32 hkds[HK_NUMDR] = { 0 };                           /* drive status */
int32 hker[HK_NUMDR] = { 0 };                           /* error status */
int32 hkof = 0;                                         /* offset */
int32 hkmr = 0;                                         /* maint registers */
int32 hkmr2 = 0;
int32 hkmr3 = 0;
int32 hkdc = 0;                                         /* cylinder */
int32 hkspr = 0;                                        /* spare */
int32 hkci = 0;                                         /* ctlr interrupt */
int32 hkdi = 0;                                         /* drive interrupt */
int32 hkei = 0;                                         /* error interrupt */
int32 hk_cwait = 5;                                     /* command time */
int32 hk_swait = 10;                                    /* seek time */
int32 hk_rwait = 10;                                    /* rotate time */
int32 hk_min2wait = 300;                                /* min time to 2nd int */
int16 hkdb[3] = { 0 };                                  /* data buffer silo */
int16 hk_off[HK_NUMDR] = { 0 };                         /* saved offset */
int16 hk_dif[HK_NUMDR] = { 0 };                         /* cylinder diff */
static const uint8 reg_in_drive[16] = {
 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

t_stat hk_rd (int32 *data, int32 PA, int32 access);
t_stat hk_wr (int32 data, int32 PA, int32 access);
t_stat hk_svc (UNIT *uptr);
t_stat hk_reset (DEVICE *dptr);
t_stat hk_boot (int32 unitno, DEVICE *dptr);
t_stat hk_attach (UNIT *uptr, CONST char *cptr);
t_stat hk_detach (UNIT *uptr);
int32 hk_inta (void);
int32 hk_rdmr2 (int32 msg);
int32 hk_rdmr3 (int32 msg);
void update_hkcs (int32 flags, int32 drv);
void update_hkds (int32 drv);
void hk_err (int32 cs1e, int32 cs2e, int32 drve, int32 drv);
void hk_go (int32 drv);
t_stat hk_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat hk_set_bad (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat hk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *hk_description (DEVICE *dptr);

/* HK data structures

   hk_dev       HK device descriptor
   hk_unit      HK unit list
   hk_reg       HK register list
   hk_mod       HK modifier list
*/

#define IOLN_HK         040

DIB hk_dib = {
    IOBA_AUTO, IOLN_HK, &hk_rd, &hk_wr,
    1, IVCL (HK), VEC_AUTO, { &hk_inta }, IOLN_HK,
    };

UNIT hk_unit[] = {
    { UDATA (&hk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+UNIT_RK06, RK06_SIZE) },
    { UDATA (&hk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+UNIT_RK06, RK06_SIZE) },
    { UDATA (&hk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+UNIT_RK06, RK06_SIZE) },
    { UDATA (&hk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+UNIT_RK06, RK06_SIZE) },
    { UDATA (&hk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+UNIT_RK06, RK06_SIZE) },
    { UDATA (&hk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+UNIT_RK06, RK06_SIZE) },
    { UDATA (&hk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+UNIT_RK06, RK06_SIZE) },
    { UDATA (&hk_svc, UNIT_FIX+UNIT_ATTABLE+UNIT_DISABLE+UNIT_AUTO+
             UNIT_ROABLE+UNIT_RK06, RK06_SIZE) }
    };

REG hk_reg[] = {
    { GRDATADF (HKCS1,          hkcs1, DEV_RDX, 16, 0, "control/status 1", hk_cs1_bits) },
    { GRDATADF (HKWC,            hkwc, DEV_RDX, 16, 0, "word count", hk_wc_bits) },
    { GRDATADF (HKBA,            hkba, DEV_RDX, 16, 0, "bus address", hk_ba_bits) },
    { GRDATADF (HKDA,            hkda, DEV_RDX, 16, 0, "desired surface, sector", hk_da_bits) },
    { GRDATADF (HKCS2,          hkcs2, DEV_RDX, 16, 0, "control/status 2", hk_cs2_bits) },
    { BRDATADF (HKDS,            hkds, DEV_RDX, 16, HK_NUMDR, "drive status, drives 0 to 7", hk_ds_bits) },
    { BRDATADF (HKER,            hker, DEV_RDX, 16, HK_NUMDR, "drive errors, drives 0 to 7", hk_er_bits) },
    { BRDATADF (HKDB,            hkdb, DEV_RDX, 16, 3, "data buffer silo", hk_db_bits) },
    { GRDATADF (HKDC,            hkdc, DEV_RDX, 16, 0, "desired cylinder", hk_dc_bits) },
    { GRDATAD (HKOF,            hkof, DEV_RDX,  8, 0, "offset") },
    { GRDATAD (HKMR,            hkmr, DEV_RDX, 16, 0, "maintenance register") },
    { GRDATAD (HKMR2,          hkmr2, DEV_RDX, 16, 0, "maintenance register 2"), REG_RO },
    { GRDATAD (HKMR3,          hkmr3, DEV_RDX, 16, 0, "maintenance register 3"), REG_RO },
    { GRDATAD (HKSPR,          hkspr, DEV_RDX, 16, 0, "spare register") },
    { FLDATAD (HKCI,            hkci, 0,              "ctlr interrupt flop") },
    { FLDATAD (HKDI,            hkdi, 0,              "drive interrupt flop") },
    { FLDATAD (HKEI,            hkei, 0,              "error interrupt flop") },
    { FLDATAD (INT,        IREQ (HK), INT_V_HK,       "interrupt pending flag") },
    { FLDATAD (ERR,            hkcs1, CSR_V_ERR,      "error flag (CSR<15>)") },
    { FLDATAD (DONE,           hkcs1, CSR_V_DONE,     "device done flag (CSR1<7>)") },
    { FLDATAD (IE,             hkcs1, CSR_V_IE,       "interrupt enable flag (CSR1<6>)") },
    { DRDATAD (CTIME,       hk_cwait, 24,             "command time"), REG_NZ + PV_LEFT },
    { DRDATAD (STIME,       hk_swait, 24,             "seek time, per cylinder"), REG_NZ + PV_LEFT },
    { DRDATAD (RTIME,       hk_rwait, 24,             "rotational delay"), REG_NZ + PV_LEFT },
    { DRDATAD (MIN2TIME, hk_min2wait, 24,             "minimum time between DONE and ATA"), REG_NZ + PV_LEFT },
    { URDATA  (FNC,   hk_unit[0].FNC, DEV_RDX, 5, 0,
              HK_NUMDR, REG_HRO) },
    { URDATA  (CYL, hk_unit[0].CYL, DEV_RDX, 10, 0,
              HK_NUMDR, REG_HRO) },
    { BRDATA  (OFFSET,        hk_off, DEV_RDX, 16, HK_NUMDR), REG_HRO },
    { BRDATA  (CYLDIF,        hk_dif, DEV_RDX, 16, HK_NUMDR), REG_HRO },
    { URDATA  (CAPAC, hk_unit[0].capac, 10, T_ADDR_W, 0,
              HK_NUMDR, PV_LEFT | REG_HRO) },
    { GRDATA  (DEVADDR, hk_dib.ba, DEV_RDX, 32, 0), REG_HRO },
    { GRDATA  (DEVVEC, hk_dib.vec, DEV_RDX, 16, 0), REG_HRO },
    { NULL }
    };

MTAB hk_mod[] = {
    { UNIT_WLK,        0, "write enabled", "WRITEENABLED", 
        NULL, NULL, NULL, "Write enable disk drive" },
    { UNIT_WLK, UNIT_WLK, "write locked",  "LOCKED", 
        NULL, NULL, NULL, "Write lock disk drive"  },
    { UNIT_DUMMY,      0, NULL,            "BADBLOCK", 
        &hk_set_bad, NULL, NULL, "write bad block table on last track" },
    { (UNIT_DTYPE+UNIT_ATT), UNIT_RK06 + UNIT_ATT,
      "RK06", NULL, NULL },
    { (UNIT_DTYPE+UNIT_ATT), UNIT_RK07 + UNIT_ATT,
      "RK07", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), UNIT_RK06,
      "RK06", NULL, NULL },
    { (UNIT_AUTO+UNIT_DTYPE+UNIT_ATT), UNIT_RK07,
      "RK07", NULL, NULL },
    { (UNIT_AUTO+UNIT_ATT), UNIT_AUTO, "autosize", NULL},
    { UNIT_AUTO,            UNIT_AUTO, NULL,       "AUTOSIZE",
        NULL, NULL, NULL, "set type based on file size at ATTACH" },
    { (UNIT_AUTO+UNIT_DTYPE), UNIT_RK06, NULL, "RK06", 
        &hk_set_size, NULL, NULL, "Set type to RK06" }, 
    { (UNIT_AUTO+UNIT_DTYPE), UNIT_RK07, NULL, "RK07", 
        &hk_set_size, NULL, NULL, "Set type to RK07" }, 
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0040, "ADDRESS", "ADDRESS",
      &set_addr, &show_addr, NULL, "Bus address" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR,    0, "VECTOR", "VECTOR",
      &set_vec,  &show_vec,  NULL, "Interrupt vector" },
    { 0 }
    };

DEBTAB hk_deb[] = {
    { "OPS",   HKDEB_OPS, "transactions" },
    { "RRD",   HKDEB_RRD, "register reads" },
    { "RWR",   HKDEB_RWR, "register writes" },
    { "INT",   HKDEB_INT, "interrupts" },
    { "TRACE", HKDEB_TRC, "trace" },
    { NULL, 0 }
    };

DEVICE hk_dev = {
    "HK", hk_unit, hk_reg, hk_mod,
    HK_NUMDR, DEV_RDX, 24, 1, DEV_RDX, 16,
    NULL, NULL, &hk_reset,
    &hk_boot, &hk_attach, &hk_detach,
    &hk_dib, DEV_DISABLE | DEV_UBUS | DEV_Q18 | DEV_DEBUG, 0,
    hk_deb, NULL, NULL, &hk_help, NULL, NULL,
    &hk_description
    };

/* I/O dispatch routines, I/O addresses 17777440 - 17777476 */

t_stat hk_rd (int32 *data, int32 PA, int32 access)
{
int32 drv, i, j;

drv = GET_UNIT (hkcs2);                                 /* get current unit */
j = (PA >> 1) & 017;                                    /* get reg offset */
if (reg_in_drive[j] && (hk_unit[drv].flags & UNIT_DIS)) { /* nx disk */
    hk_err (CS1_ERR|CS1_DONE, CS2_NED, 0, drv);         /* set err, stop op */
    *data = 0;
    return SCPE_OK;
    }

update_hkcs (0, drv);                                   /* update status */
switch (j) {                                            /* decode PA<4:1> */

    case 000:                                           /* HKCS1 */
        *data = (hkcs1 & ~CS1_DI) | (hkdi? CS1_DI: 0);  /* DI dynamic */
        break;

    case 001:                                           /* HKWC */
        *data = hkwc;
        break;

    case 002:                                           /* HKBA */
        *data = hkba = hkba & ~BA_MBZ;
        break;

    case 003:                                           /* HKDA */
        *data = hkda = hkda & ~DA_MBZ;
        break;

    case 004:                                           /* HKCS2 */
        *data = hkcs2 = (hkcs2 & ~CS2_MBZ) | CS2_IR;
        break;

    case 005:                                           /* HKDS */
        *data = hkds[drv];
        break;

    case 006:                                           /* HKER */
        *data = hker[drv];
        break;

    case 007:                                           /* HKAS */
        *data = hkof;
        for (i = 0; i < HK_NUMDR; i++) {
            if (hkds[i] & DS_ATA)
                *data = *data | (AS_U0 << i);
            }
        break;

    case 010:                                           /* HKDC */
        *data = hkdc = hkdc & ~DC_MBZ;
        break;

    case 011:                                           /* spare */
        *data = hkspr;
        break;

    case 012:                                           /* HKDB */
        *data = hkdb[0];                                /* top of silo */
        hkdb[0] = hkdb[1];                              /* ripple silo */
        hkdb[1] = hkdb[2];
        hkdb[2] = 0;                                    /* just for READH */
        break;

    case 013:                                           /* HKMR */
        *data = hkmr;
        break;

    case 014:                                           /* HKEC1 */
    case 015:                                           /* HKEC2 */
        *data = 0;                                      /* no ECC */
        break;

    case 016:                                           /* HKMR2 */
        *data = hkmr2;
        break;

    case 017:                                           /* HKMR3 */
        *data = hkmr3;
        break;
        }

sim_debug (HKDEB_RRD, &hk_dev, ">>HK%d read: %s=0%o\n", drv, hk_regnames[j], *data);
sim_debug_bits (HKDEB_RRD, &hk_dev, hk_reg_bits[j], *data, *data, 1);
return SCPE_OK;
}

t_stat hk_wr (int32 data, int32 PA, int32 access)
{
int32 drv, i, j, old_val = 0, new_val = 0;

drv = GET_UNIT (hkcs2);                                 /* get current unit */
j = (PA >> 1) & 017;                                    /* get reg offset */
if (reg_in_drive[j] && (hk_unit[drv].flags & UNIT_DIS)) { /* nx disk */
    hk_err (CS1_ERR|CS1_DONE, CS2_NED, 0, drv);         /* set err, stop op */
    return SCPE_OK;
    }
if ((hkcs1 & CS1_GO) &&                                 /* busy? */
    !(((j == 0) && (data & CS1_CCLR)) ||                /* not cclr or sclr? */
      ((j == 4) && (data & CS2_CLR)))) {
    hk_err (CS1_ERR|CS1_DONE, CS2_PGE, 0, drv);         /* set err, stop op */
    return SCPE_OK;
    }

sim_debug (HKDEB_RWR, &hk_dev, ">>HK%d write: %s=0%o\n", drv, hk_regnames[j], data);
switch (j) {                                            /* decode PA<4:1> */

    case 000:                                           /* HKCS1 */
        old_val = hkcs1;
        if (data & CS1_CCLR) {                          /* controller reset? */
            hkcs1 = CS1_DONE;                           /* CS1 = done */
            hkcs2 = CS2_IR;                             /* CS2 = ready */
            hkmr = hkmr2 = hkmr3 = 0;                   /* maint = 0 */
            hkda = hkdc = 0;
            hkba = hkwc = 0;
            hkspr = hkof = 0;
            hkci = hkdi = hkei = 0;                     /* clr int flops */
            for (i = 0; i < HK_NUMDR; i++) {            /* stop data xfr */
                if (sim_is_active (&hk_unit[i]) &&
                    ((hk_unit[i].FNC & CS1_M_FNC) >= FNC_XFER))
                    sim_cancel (&hk_unit[i]);
                }
            drv = 0;
            break;
            }
        if (((data & CS1_IE) != 0) && ((data & CS1_DONE) != 0)) {
            sim_debug (HKDEB_INT, &hk_dev, "hk_wr(ctlr int)\n");
            hkci = 1;                                   /* set ctlr intr */
            }
        hkcs1 = (hkcs1 & ~CS1_RW) | (data & CS1_RW);    /* merge data */
        if (SC02C)
            hkspr = (hkspr & ~CS1_M_UAE) | GET_UAE (hkcs1);
        if (((data & CS1_GO) != 0) && ((hkcs1 & CS1_ERR) == 0))
            hk_go (drv);                                /* go & ~err? */
        new_val = hkcs1;
        break;  

    case 001:                                           /* HKWC */
        old_val = hkwc;
        new_val = hkwc = data;
        break;

    case 002:                                           /* HKBA */
        old_val = hkba;
        new_val = hkba = data & ~BA_MBZ;
        break;

    case 003:                                           /* HKDA */
        old_val = hkda;
        new_val = hkda = data & ~DA_MBZ;
        break;

    case 004:                                           /* HKCS2 */
        old_val = hkcs2;
        if (data & CS2_CLR)                             /* init? */
            hk_reset (&hk_dev);
        else hkcs2 = (hkcs2 & ~CS2_RW) | (data & CS2_RW) | CS2_IR;
        drv = GET_UNIT (hkcs2);
        new_val = hkcs2;
        break;

    case 007:                                           /* HKAS */
        old_val = hkof;
        new_val = hkof = data & AS_OF;
        break;

    case 010:                                           /* HKDC */
        old_val = hkdc;
        new_val = hkdc = data & ~DC_MBZ;
        break;

    case 011:                                           /* spare */
        old_val = hkspr;
        new_val = hkspr = data;
        if (SC02C)                                      /* SC02C? upd UAE */
            hkcs1 = PUT_UAE (hkcs1, hkspr & 03);
        break;

    case 012:                                           /* HKDB */
        old_val = hkdb[0];
        new_val = hkdb[0] = (int16)data;
        break;

    case 013:                                           /* HKMR */
        old_val = hkmr;
        new_val = hkmr = data & MR_RW;
        break;

    case 014:                                           /* HKEC1 */
        new_val = old_val = hkmr;
        break;

    case 015:                                           /* HKEC2 */
        new_val = old_val = hkmr;
        break;

    case 016:                                           /* HKMR2 */
        new_val = old_val = hkmr2;
        break;

    case 017:                                           /* HKMR3 */
        new_val = old_val = hkmr3;
        break;
        }                                               /* end switch */
sim_debug_bits (HKDEB_RWR, &hk_dev, hk_reg_bits[j], old_val, new_val, 1);

update_hkcs (0, drv);                                   /* update status */
return SCPE_OK;
}

/* Initiate operation - go set, not previously set */

void hk_go (int32 drv)
{
int32 fnc, t;
t_bool dte;
UNIT *uptr;

static uint8 fnc_dte[16] = {
    0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0
    };
static uint8 fnc_nxf[16] = {
    0, 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 0, 0
    };
static uint8 fnc_att[16] = {
    0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0
    };
static uint8 fnc_rdy[16] = {
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0
    };
static uint8 fnc_cyl[16] = {
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0
    };

fnc = GET_FNC (hkcs1);
sim_debug (HKDEB_OPS, &hk_dev, ">>HK%d strt: fnc=%s, cs1=%o, cs2=%o, ds=%o, er=%o, cyl=%o, da=%o, ba=%o, wc=%o\n",
             drv, hk_funcs[fnc], hkcs1, hkcs2, hkds[drv], hker[drv], hkdc, hkda, hkba, hkwc);
uptr = hk_dev.units + drv;                              /* get unit */
dte = ((hkcs1 & CS1_DT) !=0) != ((uptr->flags & UNIT_DTYPE) != 0);

if (fnc != FNC_NOP)                                     /* !nop, clr msg sel */
    hkmr = hkmr & ~MR_MS;
if (uptr->flags & UNIT_DIS) {                           /* nx unit? */
    hk_err (CS1_ERR|CS1_DONE, CS2_NED, 0, drv);         /* set err, no op */
    return;
    }
if ((hkcs1 & CS1_FMT) != 0) {                           /* 18b format? */
   hk_err (CS1_ERR|CS1_DONE, 0, ER_FER, drv);           /* set err, no op */
   return;
   }
if (fnc_dte[fnc] && dte) {                              /* need drv match ? */
    hker[drv] = hker[drv] | ER_DTY;                     /* drive type mismatch? */
    hk_err (CS1_ERR|CS1_DONE, 0, ER_DTY, drv);          /* set err, no op */
    }
if (fnc_nxf[fnc] && ((hkds[drv] & DS_VV) == 0)) {       /* need vol valid? */
    hkds[drv] = hkds[drv] | DS_ATA;                     /* set ATTN */
    hk_err (CS1_ERR|CS1_DI|CS1_DONE, 0, ER_NXF, drv);   /* set err, no op */
    return;
    }
if (fnc_att[fnc] && !(uptr->flags & UNIT_ATT)) {        /* need attached? */
    hkds[drv] = hkds[drv] | DS_ATA;                     /* set ATTN */
    hk_err (CS1_ERR|CS1_DI|CS1_DONE, 0, ER_UNS, drv);   /* set err, no op */
    return;
    }
if (fnc_rdy[fnc] && sim_is_active (uptr))               /* need inactive? */
    return;
if (fnc_cyl[fnc] &&                                     /* need valid cyl */
   ((GET_CY (hkdc) >= HK_CYL (uptr)) ||                 /* bad cylinder */
    (GET_SF (hkda) >= HK_NUMSF))) {                     /* bad surface */
    hk_err (CS1_ERR|CS1_DONE, 0, ER_SKI|ER_IAE, drv);   /* set err, no op */
    return;
    }


hkcs1 = (hkcs1 | CS1_GO) & ~CS1_DONE;                   /* set go, clear done */
hkci = hkdi = hkei = 0;                                 /* clear all intr */
CLR_INT (HK);

switch (fnc) {                                          /* case on function */

/* Instantaneous functions (unit may be busy, can't schedule thread,
   can't overwrite unit function field) */

    case FNC_NOP:                                       /* no operation */
        hkmr2 = hk_rdmr2 (GET_MS (hkmr));               /* get serial msgs */
        hkmr3 = hk_rdmr3 (GET_MS (hkmr));
        if (dte)                                        /* drive type err? */
            hk_err (CS1_ERR|CS1_DONE, 0, ER_DTY, drv);
        else update_hkcs (CS1_DONE, drv);               /* done */
        break;

    case FNC_DCLR:                                      /* drive clear */
        hkds[drv] &= ~DS_ATA;                           /* clr ATA */        
        hker[drv] = 0;                                  /* clr err */
        if (dte)                                        /* drive type err? */
            hk_err (CS1_ERR|CS1_DONE, 0, ER_DTY, drv);
        else update_hkcs (CS1_DONE, drv);               /* done */
        break;

    case FNC_PACK:                                      /* pack acknowledge */
        hkds[drv] = hkds[drv] | DS_VV;                  /* set volume valid */
        update_hkcs (CS1_DONE, drv);                    /* done */
        break;

/* "Fast functions" finish in less than 15 usec */

    case FNC_START:                                     /* start spindle */
    case FNC_UNLOAD:                                    /* unload */
        uptr->FNC = fnc;                                /* save function */
        sim_activate (uptr, hk_cwait);                  /* schedule */
        return;

/* Positioning functions provide two interrupts - an immediate interrupt
   on ctrl done and a second one (if ctrl ready) when the seek is complete */

    case FNC_OFFSET:                                    /* offset mode */
    case FNC_RECAL:                                     /* recalibrate */
    case FNC_SEEK:                                      /* seek */
        hkds[drv] = hkds[drv] | DS_PIP;                 /* set positioning */
        uptr->FNC = fnc;                                /* save function */
        sim_activate (uptr, hk_cwait);                  /* schedule */
        return;

/* Data transfer functions lock the controller for the duration */

    case FNC_WRITEH:                                    /* write headers */
    case FNC_WRITE:                                     /* write */
        hk_off[drv] = 0;                                /* clr offset */
    case FNC_WCHK:                                      /* write check */
    case FNC_READ:                                      /* read */
    case FNC_READH:                                     /* read headers */
        if (GET_SC (hkda) >= HK_NUMSC) {                /* invalid sector? */
            hk_err (CS1_ERR|CS1_DONE, 0, ER_OPI, drv);  /* set err, no op */
            return;
            }
        hk_dif[drv] = (int16)(hkdc - uptr->CYL);        /* cyl diff */
        t = abs (hk_dif[drv]);                          /* |cyl diff| */
        uptr->FNC = fnc;                                /* save function */
        sim_activate (uptr, hk_rwait + (hk_swait * t)); /* schedule */
        uptr->CYL = hkdc;                               /* update cyl */
        return;

    default:
        hk_err (CS1_ERR|CS1_DONE, 0, ER_ILF, drv);      /* not supported */
        break;
        }
return;
}

/* Service unit timeout

   Complete movement or data transfer command
   Unit must exist - can't remove an active unit
   Unit must be attached - detach cancels in progress operations
*/

t_stat hk_svc (UNIT *uptr)
{
int32 i, t, dc, fnc, err;
int32 wc, awc, da;
uint32 drv, ba;
uint16 comp;

drv = (uint32) (uptr - hk_dev.units);                   /* get drv number */
fnc = uptr->FNC & CS1_M_FNC;                            /* get function */
sim_debug (HKDEB_TRC, &hk_dev, "hk_svc(HK%d, fnc=%s)\n", drv, hk_funcs[fnc]);
switch (fnc) {                                          /* case on function */

/* Fast commands - start spindle only provides one interrupt
   because ATTACH implicitly spins up the drive */

    case FNC_UNLOAD:                                    /* unload */
        hk_detach (uptr);                               /* detach unit */
    case FNC_START:                                     /* start spindle */
        update_hkcs (CS1_DONE, drv);                    /* done */
        break;

/* Positioning commands provide two interrupts, an immediate controller done
   and a delayed drive interrupt */

    case FNC_OFFSET:                                    /* offset */
        if (uptr->FNC & FNC_2ND) {                      /* 2nd int? */
            hkds[drv] = (hkds[drv] & ~DS_PIP) | DS_ATA; /* upd sta */
            update_hkcs (CS1_DI, drv);                  /* ATN set */
            }
        else {
            uptr->FNC = uptr->FNC | FNC_2ND;            /* second state */
            hk_off[drv] = hkof & AS_OF;                 /* save offset */
            sim_activate (uptr, hk_min2wait);           /* wait for compl */
            update_hkcs (CS1_DONE, drv);                /* done */
            }           
        break;

    case FNC_RECAL:                                     /* recalibrate */
    case FNC_SEEK:                                      /* seek */
        if (uptr->FNC & FNC_2ND) {                      /* 2nd int? */
            hkds[drv] = (hkds[drv] & ~DS_PIP) | DS_ATA; /* upd sta */
            update_hkcs (CS1_DI, drv);                  /* ATN set */
            }
        else {
            uptr->FNC = uptr->FNC | FNC_2ND;            /* second state */
            hk_off[drv] = 0;                            /* clr offset */
            dc = (fnc == FNC_SEEK)? hkdc: 0;            /* get cyl */
            hk_dif[drv] = (int16)(dc - uptr->CYL);      /* cyl diff */
            t = abs (hk_dif[drv]) * hk_swait;           /* |cyl diff| */
            if (t < hk_min2wait)                        /* min time */
                t = hk_min2wait;
            uptr->CYL = dc;                             /* save cyl */          
            sim_activate (uptr, t);                     /* schedule */
            update_hkcs (CS1_DONE, drv);                /* done */
            }
        break;

/* Data transfer commands only generate one interrupt */

    case FNC_READH:
        hkdb[0] = (int16)(uptr->CYL << RDH1_V_CYL);     /* first word */
        hkdb[1] = (GET_SC (hkda) << RDH2_V_SEC) |       /* second word */
            (1 << (GET_SF (hkda) + RDH2_V_DHA)) | RDH2_GOOD;
        hkdb[2] = hkdb[0] ^ hkdb[1];                    /* checksum */
        update_hkcs (CS1_DONE, drv);                    /* done */
        break;

    case FNC_WRITE:                                     /* write */
        if (uptr->flags & UNIT_WPRT) {                  /* write locked? */
            hk_err (CS1_ERR|CS1_DONE, 0, ER_WLE, drv);  /* set err, stop op */
            return SCPE_OK;
            }
    case FNC_WCHK:                                      /* write check */
    case FNC_READ:                                      /* read */
        if (SC02C)                                      /* 22b addr? */
            ba = ((hkspr & XM_MMASK) << 16) | hkba;
        else ba = (GET_UAE (hkcs1) << 16) | hkba;       /* no, 18b addr */
        da = GET_DA (hkdc, hkda) * HK_NUMWD;            /* get disk addr */
        wc = 0200000 - hkwc;                            /* get true wc */

        if ((da + wc) > HK_SIZE (uptr)) {               /* disk overrun? */
            hker[drv] = hker[drv] | ER_AOE;             /* set err */
            hkds[drv] = hkds[drv] | DS_ATA;             /* set attn */
            wc = HK_SIZE (uptr) - da;                   /* trim xfer */
            if (da >= HK_SIZE (uptr)) {                 /* none left? */
                update_hkcs (CS1_DONE, drv);            /* then done */
                break;
                }
            }

        err = fseek (uptr->fileref, da * sizeof (int16), SEEK_SET);
        if (uptr->FNC == FNC_WRITE) {                   /* write? */
            if (hkcs2 & CS2_UAI) {                      /* no addr inc? */
                if ((t = Map_ReadW (ba, 2, &comp))) {   /* get 1st wd */
                    wc = 0;                             /* NXM, no xfr */
                    hk_err (CS1_ERR, CS2_NEM, 0, drv);
                    }
                for (i = 0; i < wc; i++)
                    hkxb[i] = comp;
                }
            else {                                      /* normal */
                if ((t = Map_ReadW (ba, wc << 1, hkxb))) {/* get buf */
                    wc = wc - (t >> 1);                 /* NXM, adj wc */
                     hk_err (CS1_ERR, CS2_NEM, 0, drv);
                    }
                ba = ba + (wc << 1);                    /* adv ba */
                }
            awc = (wc + (HK_NUMWD - 1)) & ~(HK_NUMWD - 1);
            for (i = wc; i < awc; i++)                  /* fill buf */
                hkxb[i] = 0;
            if (wc && !err) {                           /* write buf */
                fxwrite (hkxb, sizeof (uint16), awc, uptr->fileref);
                err = ferror (uptr->fileref);
                }
            }                                           /* end if wr */
        else if (uptr->FNC == FNC_READ) {               /* read? */
            i = fxread (hkxb, sizeof (uint16), wc, uptr->fileref);
            err = ferror (uptr->fileref);
            for ( ; i < wc; i++)                        /* fill buf */
                hkxb[i] = 0;
            if (hkcs2 & CS2_UAI) {                      /* no addr inc? */
                if ((t = Map_WriteW (ba, 2, &hkxb[wc - 1]))) {
                    wc = 0;                             /* NXM, no xfr */
                    hk_err (CS1_ERR, CS2_NEM, 0, drv);
                    }
                }
            else {                                      /* normal */
                if ((t = Map_WriteW (ba, wc << 1, hkxb))) {/* put buf */
                    wc = wc - (t >> 1);                 /* NXM, adj wc */
                    hk_err (CS1_ERR, CS2_NEM, 0, drv);
                    }
                ba = ba + (wc << 1);                    /* adv ba */
                }
            }                                           /* end if read */
        else {                                          /* wchk */                  
            i = fxread (hkxb, sizeof (uint16), wc, uptr->fileref);
            err = ferror (uptr->fileref);
            for ( ; i < wc; i++)                        /* fill buf */
                hkxb[i] = 0;
            awc = wc;
            for (wc = 0; wc < awc; wc++) {              /* loop thru buf */
                if (Map_ReadW (ba, 2, &comp)) {         /* read word */
                    hk_err (CS1_ERR, CS2_NEM, 0, drv);
                    break;
                    }
                if (comp != hkxb[wc]) {                 /* compare wd */
                    hk_err (CS1_ERR, CS2_WCE, 0, drv);
                    break;
                    }
                if ((hkcs2 & CS2_UAI) == 0)
                    ba = ba + 2;
                }
            }                                           /* end else wchk */

        hkwc = (hkwc + wc) & 0177777;                   /* final word count */
        hkba = (ba & 0177777) & ~BA_MBZ;                /* lower 16b */
        hkcs1 = PUT_UAE (hkcs1, ba >> 16);              /* upper 2b */
        if (SC02C)                                      /* SC02C? upper 6b */
            hkspr = (hkspr & ~XM_MMASK) | ((ba >> 16) & XM_MMASK);
        da = da + wc + (HK_NUMWD - 1);
        da = da / HK_NUMWD;
        hkda = da % HK_NUMSC;
        da = da / HK_NUMSC;
        hkda = hkda | ((da % HK_NUMSF) << DA_V_SF);
        hkdc = da / HK_NUMSF;

        if (err != 0) {                                 /* error? */
            hk_err (CS1_ERR|CS1_DONE, 0, ER_PAR, drv);  /* set drive error */
            sim_perror ("HK I/O error");
            clearerr (uptr->fileref);
            return SCPE_IOERR;
            }

    case FNC_WRITEH:                                    /* write headers stub */
        update_hkcs (CS1_DONE, drv);                    /* set done */
        break;
        }                                               /* end case func */

return SCPE_OK;
}

/* Controller status update

   Update drive status
   Update HKCS1
   Check for done transition
      clock CI from IE
      set DI if any ATN bits set
   Check for DI set if no transition but DONE is set
   Update interrupt request
*/

void update_hkcs (int32 flag, int32 drv)
{
int32 i, old_hkcs1 = hkcs1, old_hkcs2 = hkcs2;

sim_debug (HKDEB_TRC, &hk_dev, "update_hkcs(flag=0%o, drv=%d)\n", flag, drv);
update_hkds (drv);                                      /* upd drv status */
hkcs1 = (hkcs1 & (CS1_ERR|CS1_DT|CS1_UAE|CS1_DONE|CS1_IE|CS1_SPA|CS1_FNC|CS1_GO)) |
    (flag & ~CS1_DI);
if ((hkcs1 & CS1_DONE) != 0) {                          /* done? */
    hkcs1 = hkcs1 & ~CS1_GO;                            /* clear go */
    if ((old_hkcs1 & CS1_DONE) == 0) {                  /* done 0->1? */
        hkci = (hkcs1 & CS1_IE)? 1: 0;                  /* clk CI from IE */
        for (i = 0; i < HK_NUMDR; i++) {                /* if ATA, set DI */
            if (hkds[i] & DS_ATA)
                hkdi = 1;
            }
        }
    else if ((flag & CS1_DI) != 0)                      /* done set; new ATN? */
        hkdi = 1;                                       /* set drv int */
    }
else hkdi = 0;                                          /* not done, clr DI */
if (((hkcs1 & CS1_IE) != 0) && (hkci || hkdi || hkei)) {/* int enab & set? */
    sim_debug (HKDEB_INT, &hk_dev, "update_hkcs(SET_INT)\n");
    SET_INT (HK);
    }
else {
    sim_debug (HKDEB_INT, &hk_dev, "update_hkcs(CLR_INT)\n");
    CLR_INT (HK);
    }
if (old_hkcs1 != hkcs1)
    sim_debug_bits (HKDEB_OPS, &hk_dev, hk_cs1_bits, old_hkcs1, hkcs1, 1);
if (old_hkcs2 != hkcs2)
    sim_debug_bits (HKDEB_OPS, &hk_dev, hk_cs2_bits, old_hkcs2, hkcs2, 1);
if (flag & CS1_DONE) {                                  /* set done */
    sim_debug (HKDEB_OPS, &hk_dev, ">>HK%d done: fnc=%s, cs1=%o, cs2=%o, ds=%o, er=%o, cyl=%o, da=%o, ba=%o, wc=%o, ci=%d, di=%d\n",
             drv, hk_funcs[GET_FNC (hkcs1)], hkcs1, hkcs2, hkds[drv], hker[drv], hkdc, hkda, hkba, hkwc, hkci, hkdi);
    }
if (flag & CS1_DI) {                                  /* set ATA? */
    sim_debug (HKDEB_OPS, &hk_dev, ">>HK%d ATA: fnc=%s, cs1=%o, cs2=%o, ds=%o, er=%o, cyl=%o, da=%o, ba=%o, wc=%o, ci=%d, di=%d\n",
             drv, hk_funcs[GET_FNC (hkcs1)], hkcs1, hkcs2, hkds[drv], hker[drv], hkdc, hkda, hkba, hkwc, hkci, hkdi);
    }
return;
}

/* Drive status update */

void update_hkds (int32 drv)
{
int old_ds = hkds[drv];

if (hk_unit[drv].flags & UNIT_DIS) {                    /* disabled? */
    hkds[drv] = hker[drv] = 0;                          /* all clear */
    return;
    }
sim_debug (HKDEB_TRC, &hk_dev, "update_hkds(drv=%d)\n", drv);
hkds[drv] = (hkds[drv] & (DS_VV | DS_PIP | DS_ATA)) | DS_VLD | DS_DRA;
if (hk_unit[drv].flags & UNIT_RK07)
    hkds[drv] = hkds[drv] | DS_DT;
if (hk_unit[drv].flags & UNIT_ATT) {                    /* attached? */
    if (!sim_is_active (&hk_unit[drv]))                 /* not busy? */
        hkds[drv] = hkds[drv] | DS_RDY;                 /* set RDY */
    if (hk_off[drv])                                    /* offset? set OF */
        hkds[drv] = hkds[drv] | DS_OF;
    if (hk_unit[drv].flags & UNIT_WPRT)                 /* write locked? */
        hkds[drv] = hkds[drv] | DS_WRL;                 /* set WRL */
    }
else
    hkds[drv] = hkds[drv] & ~(DS_PIP | DS_VV);          /* no, clr PIP,VV */
if (old_ds != hkds[drv])
    sim_debug_bits (HKDEB_TRC, &hk_dev, hk_ds_bits, old_ds, hkds[drv], 1);
return;
}

/* Set errors */

void hk_err (int32 cs1e, int32 cs2e, int32 drve, int32 drv)
{
sim_debug (HKDEB_TRC, &hk_dev, "hk_err(drv=%d, cs1e=%d, cs2e=%d, drve=%d)\n", drv, cs1e, cs2e, drve);
hker[drv] = hker[drv] | drve;                           /* set drv error */
hkcs2 = hkcs2 | cs2e;                                   /* set cs2 err */
if ((cs1e & CS1_ERR) != 0)                              /* set combined err? */                              
    hkei = 1;                                           /* then set EI */
if ((cs1e & CS1_DONE) != 0)                             /* set done? */
    update_hkcs (CS1_ERR|CS1_DONE, drv);                /* stop now */
else
    hkcs1 = hkcs1 | cs1e;                               /* no, just upd */
return;
}

/* Interrupt routine */

int32 hk_inta (void)
{
hkci = hkdi = hkei = 0;                                 /* clear all flops */
return hk_dib.vec;                                      /* return vector */
}

/* Diagnostic registers

   It's unclear whether the drivers actually use these values, but the
   Emulex controller bothers to implement them, so we will too */

int32 hk_mrpar (int32 v)
{
int32 bit, wrk;

wrk = v & 077777;                                       /* par on 15b */
v = wrk | ((hkmr & MR_PAR)? 0: AX_PAR);                 /* even/odd */
while (wrk) {                                           /* while 1's */
    bit = wrk & (-wrk);                                 /* lowest 1 */
    wrk = wrk & ~bit;                                   /* clear */
    v = v ^ AX_PAR;                                     /* xor parity */
    }
return v;
}

int32 hk_rdmr2 (int32 msg)
{
int32 drv = GET_UNIT (hkcs2);
int32 v = drv << AX_V_UNIT;
UNIT *uptr = hk_dev.units + drv;
int32 fnc = uptr->FNC & CS1_M_FNC;

switch (msg) {

    case 0:                                             /* message A0 */
        v = v | ((hkds[drv] & DS_ATA)? A0_ATA: 0) |
                ((hkds[drv] & DS_PIP)? A0_PIP: 0) |
                ((uptr->flags & UNIT_WPRT)? A0_WRL: 0) |
                ((hk_off[drv])? A0_OF: 0) |
                ((uptr->flags & UNIT_RK07)? A0_DT: 0) |
                ((hkds[drv] & DS_VV)? A0_VV: 0) | A0_DRA;
        if (uptr->flags & UNIT_ATT)
            v = v | A0_SPO | (!sim_is_active (uptr)? A0_RDY: 0);
        break;

    case 1:                                             /* message A1 */
        if (uptr->flags & UNIT_ATT) {
            if (sim_is_active (uptr)) {
                if (fnc == FNC_UNLOAD)
                    v = v | A1_UNL;
                else if (fnc == FNC_RECAL)
                    v = v | A1_RTZ;
                else if (fnc == FNC_SEEK) {
                    if (hk_dif[drv] < 0)
                        v = v | A1_REV;
                    if (hk_dif[drv] > 0)
                        v = v | A1_FWD;
                    }
                }
            v = v | (A1_SPD|A1_CAR|A1_DOR|A1_HHM|A1_SRV);
            }
        else v = v | A1_HHM;
        break;

    case 2:                                             /* message A2 */
        if (hkds[drv] & DS_OF)
            v = v | ((hk_off[drv] & A2_M_DIF) << A2_V_DIF);
        else v = v | ((hk_dif[drv] & A2_M_DIF) << A2_V_DIF);
        break;

    case 3:                                             /* message A3 */
        v = v | ((012340 + v) << A3_V_SNO);
        break;
        }

return hk_mrpar (v);
}

int32 hk_rdmr3 (int32 msg)
{
int32 drv = GET_UNIT (hkcs2);
int32 v = msg & 03;

switch (msg) {

    case 0:                                             /* message B0 */
        v = v | ((hker[drv] & ER_WLE)? (B0_WLE | B0_FLT): 0) |
                ((hker[drv] & ER_SKI)? (B0_SKI | B0_FLT): 0) |
                ((hker[drv] & ER_NXF)? (B0_NXF | B0_FLT): 0) |
                ((hker[drv] & ER_IAE)? (B0_IAE | B0_FLT): 0);
        break;

    case 1:                                             /* message B1 */
        v = v | ((hker[drv] & ER_SKI)? B1_SKI: 0) |
                ((hker[drv] & ER_UNS)? B1_SVE: 0);
        break;

    case 2:                                             /* message B2 */
        v = v | (hk_unit[drv].CYL << B2_V_CYL);
        break;

    case 3:                                             /* message B3 */
        v = v | (GET_SC (hkda) << B3_V_SEC) |
                (1 << (GET_SF (hkda) + B3_V_DHA));
        break;
        }

return hk_mrpar (v);
}

/* Device reset */

t_stat hk_reset (DEVICE *dptr)
{
int32 i;
UNIT *uptr;

sim_debug (HKDEB_TRC, &hk_dev, "hk_reset()\n");
hkcs1 = CS1_DONE;                                       /* set done */
hkcs2 = CS2_IR;                                         /* clear state */
hkmr = hkmr2 = hkmr3 = 0;
hkda = hkdc = 0;
hkba = hkwc = 0;
hkof = hkspr = 0;
hkci = hkdi = hkei = 0;                                 /* clear intr flops */
CLR_INT (HK);                                           /* clear intr req */
for (i = 0; i < HK_NUMDR; i++) {                        /* stop operations */
    uptr = hk_dev.units + i;
    sim_cancel (uptr);
    if (uptr->flags & UNIT_ATT)
        hkds[i] = hkds[i] & (DS_VV | DS_DT);
    else hkds[i] = 0;
    uptr->CYL = uptr->FNC = 0;                          /* clear state */
    hk_dif[i] = 0;
    hk_off[i] = 0;
    hker[i] = 0;
    }                                                   /* clear errors */
if (hkxb == NULL)
    hkxb = (uint16 *) calloc (HK_MAXFR, sizeof (uint16));
if (hkxb == NULL)
    return SCPE_MEM;
return auto_config (0, 0);
}

/* Device attach */

t_stat hk_attach (UNIT *uptr, CONST char *cptr)
{
uint32 drv, p;
t_stat r;
int32 old_hkds;

uptr->capac = HK_SIZE (uptr);
r = attach_unit (uptr, cptr);                           /* attach unit */
if (r != SCPE_OK)                                       /* error? */
    return r;
drv = (uint32) (uptr - hk_dev.units);                   /* get drv number */
old_hkds = hkds[drv];                                   /* save hkds */
hkds[drv] = DS_ATA | DS_RDY |
    ((uptr->flags & UNIT_WPRT)? DS_WRL: 0) |
    ((uptr->flags & UNIT_DTYPE)? DS_DT: 0);
hker[drv] = 0;                                          /* upd drv status */
hk_off[drv] = 0;
hk_dif[drv] = 0;
uptr->CYL = 0;
if ((old_hkds & DS_ATA) == 0)                           /* ATN transition? */
    update_hkcs (CS1_DI, drv);                          /* upd ctlr status */

p = sim_fsize (uptr->fileref);                          /* get file size */
if (p == 0) {                                           /* new disk image? */
    if (uptr->flags & UNIT_RO)
        return SCPE_OK;
    return pdp11_bad_block (uptr, HK_NUMSC, HK_NUMWD);
    }
if ((uptr->flags & UNIT_AUTO) == 0)                     /* autosize? */
    return SCPE_OK;
if (p > (RK06_SIZE * sizeof (uint16))) {
    uptr->flags = uptr->flags | UNIT_RK07;
    uptr->capac = RK07_SIZE;
    }
else {
    uptr->flags = uptr->flags & ~UNIT_RK07;
    uptr->capac = RK06_SIZE;
    }
return SCPE_OK;
}

/* Device detach */

t_stat hk_detach (UNIT *uptr)
{
uint32 drv;
int32 old_hkds;

if (!(uptr->flags & UNIT_ATT))                          /* attached? */
    return SCPE_OK;
drv = (uint32) (uptr - hk_dev.units);                   /* get drv number */
old_hkds = hkds[drv];
hkds[drv] = (hkds[drv] & ~(DS_RDY | DS_WRL | DS_VV | DS_OF | DS_PIP)) | DS_ATA;
if (sim_is_active (uptr)) {                             /* unit active? */
    sim_cancel (uptr);                                  /* cancel operation */
    hker[drv] = hker[drv] | ER_OPI;                     /* set drive error */
    if ((uptr->FNC & FNC_2ND) == 0)                     /* expecting done? */
        update_hkcs (CS1_ERR|CS1_DONE, drv);            /* set done */
    }
if ((old_hkds & DS_ATA) == 0)                           /* ATN transition? */
    update_hkcs (CS1_DI, drv);                          /* upd ctlr status */
return detach_unit (uptr);
}

/* Set size command validation routine */

t_stat hk_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
uptr->capac = val? RK07_SIZE: RK06_SIZE;
return SCPE_OK;
}

/* Set bad block routine */

t_stat hk_set_bad (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
return pdp11_bad_block (uptr, HK_NUMSC, HK_NUMWD);
}

#if defined (VM_PDP11)

extern uint16 *M;

/* Device bootstrap - does not clear CSR when done */

#define BOOT_START      02000                           /* start */
#define BOOT_ENTRY      (BOOT_START + 002)              /* entry */
#define BOOT_UNIT       (BOOT_START + 010)              /* unit number */
#define BOOT_CSR        (BOOT_START + 014)              /* CSR */
#define BOOT_LEN        (sizeof (boot_rom) / sizeof (int16))

static const uint16 boot_rom[] = {
    0042115,                        /* "MD" */
    0012706, BOOT_START,            /* mov #boot_start, sp */
    0012700, 0000000,               /* mov #unit, r0 */
    0012701, 0177440,               /* mov #HKCS1, r1 */
    0012761, 0000040, 0000010,      /* mov #CS2_CLR, 10(r1) ; reset */
    0010061, 0000010,               /* mov r0, 10(r1)       ; set unit */
    0016102, 0000012,               /* mov 12(r1), r2       ; drv typ */
    0100375,                        /* bpl .-4              ; valid? */
    0042702, 0177377,               /* bic #177377, r2      ; clr rest */
    0006302,                        /* asl r2               ; move */
    0006302,                        /* asl r2 */
    0012703, 0000003,               /* mov #pack+go, r3 */
    0050203,                        /* bis r2, r3           ; merge type */
    0010311,                        /* mov r3, (r1);        ; pack ack */
    0105711,                        /* tstb (r1)            ; wait */
    0100376,                        /* bpl .-2 */
    0012761, 0177000, 0000002,      /* mov #-512.,2(r1)     ; set wc */
    0005061, 0000004,               /* clr 4(r1)            ; clr ba */
    0005061, 0000006,               /* clr 6(r1)            ; clr da */
    0005061, 0000020,               /* clr 20(r1)           ; clr cyl */
    0012703, 0000021,               /* mov #read+go, r3 */
    0050203,                        /* bis r2, r3           ; merge type */
    0010311,                        /* mov r3, (r1);        ; read */
    0105711,                        /* tstb (r1)            ; wait */
    0100376,                        /* bpl .-2 */
    0005002,                        /* clr R2 */
    0005003,                        /* clr R3 */
    0012704, BOOT_START+020,        /* mov #start+020, r4 */
    0005005,                        /* clr R5 */
    0005007                         /* clr PC */
    };

t_stat hk_boot (int32 unitno, DEVICE *dptr)
{
size_t i;

for (i = 0; i < BOOT_LEN; i++)
    M[(BOOT_START >> 1) + i] = boot_rom[i];
M[BOOT_UNIT >> 1] = unitno & CS2_M_UNIT;
M[BOOT_CSR >> 1] = hk_dib.ba & DMASK;
cpu_set_boot (BOOT_ENTRY);
return SCPE_OK;
}

#else

t_stat hk_boot (int32 unitno, DEVICE *dptr)
{
return SCPE_NOFNC;
}

#endif

t_stat hk_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "RK611/RK06,RK07 Cartridge Disk (HK)\n\n");
fprintf (st, "RK611  options include the ability to set units write enabled or write locked,\n");
fprintf (st, "to set the drive type to RK06, RK07, or autosize, and to write a DEC standard\n");
fprintf (st, "044 compliant bad block table on the last track:\n\n");
fprint_set_help (st, dptr);
fprint_show_help (st, dptr);
fprintf (st, "\nThe type options can be used only when a unit is not attached to a file.\n");
fprintf (st, "The bad block option can be used only when a unit is attached to a file.\n");
#if defined (VM_PDP11)
fprintf (st, "The HK device supports the BOOT command.\n");
fprintf (st, "The RK611 is disabled in a Qbus system with more than 256KB of memory.\n");
#endif
fprint_reg_help (st, dptr);
fprintf (st, "\nError handling is as follows:\n\n");
fprintf (st, "    error         STOP_IOE   processed as\n");
fprintf (st, "    not attached  1          report error and stop\n");
fprintf (st, "                  0          disk not ready\n\n");
fprintf (st, "    end of file   x          assume rest of disk is zero\n");
fprintf (st, "    OS I/O error  x          report error and stop\n");
return SCPE_OK;
}

const char *hk_description (DEVICE *dptr)
{
return "RK611/RK06(7) cartridge disk controller";
}
