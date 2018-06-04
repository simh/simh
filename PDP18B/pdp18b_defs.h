/* pdp18b_defs.h: 18b PDP simulator definitions

   Copyright (c) 1993-2016, Robert M Supnik

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

   17-Mar-16    PLB     Start GRAPHICS-2 support for PDP-7 UNIX
   10-Mar-16    RMS     Added 3-cycle databreak set/show routines
   26-Feb-16    RMS     Added RB09 to PDP-7 for Unix "v0" and RM09 to PDP-9
   13-Sep-15    RMS     Added DR15C
   18-Apr-12    RMS     Added clk_cosched prototype
   22-May-10    RMS     Added check for 64b definitions
   30-Oct-06    RMS     Added infinite loop stop
   14-Jan-04    RMS     Revised IO device call interface
   18-Oct-03    RMS     Added DECtape off reel message
   18-Jul-03    RMS     Added FP15 support
                        Added XVM support
                        Added EAE option for PDP-4
   25-Apr-03    RMS     Revised for extended file support
   04-Feb-03    RMS     Added RB09, LP09 support
   22-Nov-02    RMS     Added PDP-4 drum support
   05-Oct-02    RMS     Added DIB structure
   25-Jul-02    RMS     Added PDP-4 DECtape support
   10-Feb-02    RMS     Added PDP-7 DECtape support
   25-Nov-01    RMS     Revised interrupt structure
   27-May-01    RMS     Added second Teletype support
   21-Jan-01    RMS     Added DECtape support
   14-Apr-99    RMS     Changed t_addr to unsigned
   02-Jan-96    RMS     Added fixed head and moving head disks
   31-Dec-95    RMS     Added memory management
   19-Mar-95    RMS     Added dynamic memory size

   The author gratefully acknowledges the help of Craig St. Clair and
   Deb Tevonian in locating archival material about the 18b PDP's, and of
   Al Kossow and Max Burnet in making documentation and software available.
*/

#ifndef PDP18B_DEFS_H_
#define PDP18B_DEFS_H_ 0

#include "sim_defs.h"                                   /* simulator defns */

/* Rename of global PC variable to avoid namespace conflicts on some platforms */

#define PC PC_Global

#if defined(USE_INT64) || defined(USE_ADDR64)
#error "18b PDP's do not support 64b values!"
#endif

/* Models: only one should be defined

   model memory CPU options             I/O options

   PDP4    8K   Type 18 EAE             Type 65 KSR-28 Teletype (Baudot)
                ??Type 16 mem extension integral paper tape reader
                                        Type 75 paper tape punch
                                        integral real time clock
                                        Type 62 line printer (Hollerith)
                                        Type 550/555 DECtape
                                        Type 24 serial drum

   PDP7    32K  Type 177 EAE            Type 649 KSR-33 Teletype
                Type 148 mem extension  Type 444 paper tape reader
                                        Type 75 paper tape punch
                                        integral real time clock
                                        Type 647B line printer (sixbit)
                                        Type 550/555 DECtape
                                        Type 24 serial drum
                                        RB09 fixed head disk (Unix V0 only)
                                        Bell Labs GRAPHICS-2 (Unix V0 only)

   PDP9    32K  KE09A EAE               KSR-33 Teletype
                KF09A auto pri intr     PC09A paper tape reader and punch
                KG09B mem extension     integral real time clock
                KP09A power detection   Type 647D/E line printer (sixbit)
                KX09A mem protection    LP09 line printer (ASCII)
                                        RF09/RS09 fixed head disk
                                        RB09 fixed head disk
                                        RM09 drum
                                        TC59 magnetic tape
                                        TC02/TU55 DECtape
                                        LT09A additional Teletypes

   PDP15  128K  KE15 EAE                KSR-35 Teletype
                KA15 auto pri intr      PC15 paper tape reader and punch
                KF15 power detection    KW15 real time clock
                KM15 mem protection     LP09 line printer
                KT15 mem relocation     LP15 line printer
                FP15 floating point     RP15 disk pack
                XVM option              RF15/RF09 fixed head disk
                                        TC59D magnetic tape
                                        TC15/TU56 DECtape
                                        LT15/LT19 additional Teletypes
                                        DR15C parallel interface to UC15

   ??Indicates not implemented.  The PDP-4 manual refers to a memory
   ??extension control; there is no documentation on it.
*/

#if !defined (PDP4) && !defined (PDP7) && !defined (PDP9) && !defined (PDP15)
#define PDP15           0                               /* default to PDP-15 */
#endif

/* Simulator stop codes */

#define STOP_RSRV       1                               /* must be 1 */
#define STOP_HALT       2                               /* HALT */
#define STOP_IBKPT      3                               /* breakpoint */
#define STOP_XCT        4                               /* nested XCT's */
#define STOP_API        5                               /* invalid API int */
#define STOP_NONSTD     6                               /* non-std dev num */
#define STOP_MME        7                               /* mem mgt error */
#define STOP_FPDIS      8                               /* fp inst, fpp disabled */
#define STOP_DTOFF      9                               /* DECtape off reel */
#define STOP_LOOP       10                              /* infinite loop */

/* Peripheral configuration */

#if defined (PDP4)
#define ADDRSIZE        13
#define KSR28           0                               /* Baudot terminal */
#define TYPE62          0                               /* Hollerith printer */
#define TYPE550         0                               /* DECtape */
#define DRM             0                               /* drum */
#elif defined (PDP7)
#define ADDRSIZE        15
#define TYPE647         0                               /* sixbit printer */
#define TYPE550         0                               /* DECtape */
#define DRM             0                               /* drum */
#define RB              0                               /* fixed head disk */
#define GRAPHICS2       0                               /* BTL display */
#elif defined (PDP9)
#define ADDRSIZE        15
#define TYPE647         0                               /* sixbit printer */
#define LP09            0                               /* ASCII printer */
#define RB              0                               /* fixed head disk */
#define RF              0                               /* fixed head disk */
#define DRM             0                               /* drum */
#define MTA             0                               /* magtape */
#define TC02            0                               /* DECtape */
#define TTY1            4                               /* second Teletype(s) */
#define BRMASK          0076000                         /* bounds mask */
#elif defined (PDP15)
#define ADDRSIZE        17
#define LP09            0                               /* ASCII printer */
#define LP15            0                               /* DMA printer */
#define RF              0                               /* fixed head disk */
#define RP              0                               /* disk pack */
#define MTA             0                               /* magtape */
#define TC02            0                               /* DECtape */
#define TTY1            16                              /* second Teletype(s) */
#define UC15            0                               /* UC15 */
#define BRMASK          0377400                         /* bounds mask */
#define BRMASK_XVM      0777400                         /* bounds mask, XVM */
#endif

/* Memory */

#define AMASK           ((1 << ADDRSIZE) - 1)           /* address mask */
#define IAMASK          077777                          /* ind address mask */
#define BLKMASK         (AMASK & (~IAMASK))             /* block mask */
#define MAXMEMSIZE      (1 << ADDRSIZE)                 /* max memory size */
#define MEMSIZE         (cpu_unit.capac)                /* actual memory size */
#define MEM_ADDR_OK(x)  (((uint32) (x)) < MEMSIZE)

/* Instructions */

#define I_V_OP          14                              /* opcode */
#define I_M_OP          017
#define I_V_IND         13                              /* indirect */
#define I_V_IDX         12                              /* index */
#define I_IND           (1 << I_V_IND)
#define I_IDX           (1 << I_V_IDX)
#define B_DAMASK        017777                          /* bank mode address */
#define B_EPCMASK       (AMASK & ~B_DAMASK)
#define P_DAMASK        007777                          /* page mode address */
#define P_EPCMASK       (AMASK & ~P_DAMASK)

/* Memory cycles */

#define FE              0
#define DF              1
#define RD              2
#define WR              3

/* Memory status codes */

#define MM_OK           0
#define MM_ERR          1

/* Memory management relocation checks (PDP-15 KT15 and XVM only) */

#define REL_C           -1                              /* console */
#define REL_R           0                               /* read */
#define REL_W           1                               /* write */

/* Architectural constants */

#define DMASK           0777777                         /* data mask */
#define LINK            (DMASK + 1)                     /* link */
#define LACMASK         (LINK | DMASK)                  /* link + data */
#define SIGN            0400000                         /* sign bit */
#define OP_JMS          0100000                         /* JMS */
#define OP_JMP          0600000                         /* JMP */
#define OP_HLT          0740040                         /* HLT */

/* IOT subroutine return codes */

#define IOT_V_SKP       18                              /* skip */
#define IOT_V_REASON    19                              /* reason */
#define IOT_SKP         (1 << IOT_V_SKP)
#define IOT_REASON      (1 << IOT_V_REASON)

#define IORETURN(f,v)   ((f)? (v): SCPE_OK)             /* stop on error */

/* PC change queue */

#define PCQ_SIZE        64                              /* must be 2**n */
#define PCQ_MASK        (PCQ_SIZE - 1)
#define PCQ_ENTRY       pcq[pcq_p = (pcq_p - 1) & PCQ_MASK] = PC

/* XVM memory management registers */

#define MM_RDIS         0400000                         /* reloc disabled */
#define MM_V_GM         15                              /* G mode */
#define MM_M_GM         03
#define MM_GM           (MM_M_GM << MM_V_GM)
#define  MM_G_W0        0077777                         /* virt addr width */
#define  MM_G_W1        0177777
#define  MM_G_W2        0777777
#define  MM_G_W3        0377777
#define  MM_G_B0        0060000                         /* SAS base */
#define  MM_G_B1        0160000
#define  MM_G_B2        0760000
#define  MM_G_B3        0360000
#define MM_UIOT         0040000                         /* user mode IOT's */
#define MM_WP           0020000                         /* share write prot */
#define MM_SH           0010000                         /* share enabled */
#define MM_V_SLR        10                              /* segment length reg */
#define MM_M_SLR        03
#define  MM_SLR_L0      001000                          /* SAS length */
#define  MM_SLR_L1      002000
#define  MM_SLR_L2      010000
#define  MM_SLR_L3      020000
#define MM_SBR_MASK     01777                           /* share base reg */
#define MM_GETGM(x)     (((x) >> MM_V_GM) & MM_M_GM)
#define MM_GETSLR(x)    (((x) >> MM_V_SLR) & MM_M_SLR)

/* Device information block */

#define DEV_MAXBLK      8                               /* max dev block */
#define DEV_MAX         64                              /* total devices */

typedef struct {
    uint32              dev;                            /* base dev number */
    uint32              num;                            /* number of slots */
    int32               (*iors)(void);                  /* IORS responder */
    int32               (*dsp[DEV_MAXBLK])(int32 dev, int32 pulse, int32 dat);
    } DIB;

/* Standard device numbers */

#define DEV_PTR         001                             /* paper tape reader */
#define DEV_PTP         002                             /* paper tape punch */
#define DEV_TTI         003                             /* console input */
#define DEV_TTO         004                             /* console output */
#define DEV_TTI1        041                             /* extra terminals */
#define DEV_TTO1        040
#define DEV_DRM         060                             /* drum */
#define DEV_DR          060                             /* DR15 */
#define DEV_RP          063                             /* RP15 */
#define DEV_LPT         065                             /* line printer */
#define DEV_RF          070                             /* RF09 */
#define DEV_RB          071                             /* RB09 */
#define DEV_MT          073                             /* magtape */
#define DEV_DTA         075                             /* dectape */

#ifdef GRAPHICS2
/* Bell Telephone Labs GRAPHICS-2 Display System ("as large as the PDP-7")
 * Used by PDP-7 UNIX as a "Glass TTY"
 */
#define DEV_G2D1        005                             /* Display Ctrl 1 */
#define DEV_G2D         006                             /* (Display Ctrl 2) */
#define DEV_G2LP        007                             /* (Light Pen) */
#define DEV_G2DS        010                             /* (Display Status) */
#define DEV_G2D3        014                             /* (Display Ctrl 3) */
#define DEV_G2D4        034                             /* (Display Ctrl 4) */

#define DEV_G2UNK       042                             /* (???) */
#define DEV_G2KB        043                             /* Keyboard */
#define DEV_G2BB        044                             /* Button Box */
#define DEV_G2IM        045                             /* (PDP7 int. mask) */

/* PDP-7/9 to 201A Data Phone Interface
 * (status bits retrieved with G2DS IOT)
 * used for UNIX to GCOS Remote Job Entry
 */
#define DEV_DP          047                             /* (Data Phone) */
#endif

/* Interrupt system

   The interrupt system can be modelled on either the flag driven system
   of the PDP-4 and PDP-7 or the API driven system of the PDP-9 and PDP-15.
   If flag based, API is hard to implement; if API based, IORS requires
   extra code for implementation.  I've chosen an API based model.

   API channel  Device          API priority    Notes

        00      software 4              4
        01      software 5              5
        02      software 6              6
        03      software 7              7
        04      TC02/TC15               1
        05      TC59D                   1
        06      drum                    1       PDP-9 only
        07      RB09                    1       PDP-9 only
        10      paper tape reader       2
        11      real time clock         3
        12      power fail              0
        13      memory parity           0
        14      display                 2
        15      card reader             2
        16      line printer            2
        17      A/D converter           0
        20      interprocessor buffer   3
        21      360 link                3       PDP-9 only
        22      data phone              2       PDP-15 only
        23      RF09/RF15               1
        24      RP15                    1       PDP-15 only
        25      plotter                 1       PDP-15 only
        26      -
        27      -
        30      -
        31      -
        32      -
        33      -
        34      LT15 TTO                3       PDP-15 only
        35      LT15 TTI                3       PDP-15 only
        36      -
        37      -

   The DR15C uses four API channels that are assigned by software.

   On the PDP-9, any API level active masks PI, and PI does not mask API.
   On the PDP-15, only the hardware API levels active mask PI, and PI masks
   the API software levels. */

#define API_ML0         0200                            /* API masks: level 0 */
#define API_ML1         0100
#define API_ML2         0040
#define API_ML3         0020
#define API_ML4         0010
#define API_ML5         0004
#define API_ML6         0002
#define API_ML7         0001                            /* level 7 */

#if defined (PDP9)                                      /* levels which mask PI */
#define API_MASKPI      (API_ML0|API_ML1|API_ML2|API_ML3|API_ML4|API_ML5|API_ML6|API_ML7)
#else
#define API_MASKPI      (API_ML0|API_ML1|API_ML2|API_ML3)
#endif

#define API_HLVL        4                               /* hwre levels */
#define ACH_SWRE        040                             /* swre int vec */

/* API level 0 */

#define INT_V_PWRFL     0                               /* powerfail */

#define INT_PWRFL       (1 << INT_V_PWRFL)

#define API_PWRFL       0

#define ACH_PWRFL       052

/* API level 1 */

#define INT_V_DTA       0                               /* DECtape */
#define INT_V_MTA       1                               /* magtape */
#define INT_V_DRM       2                               /* drum */
#define INT_V_RF        3                               /* fixed head disk */
#define INT_V_RP        4                               /* disk pack */
#define INT_V_RB        5                               /* RB disk */

#define INT_DTA         (1 << INT_V_DTA)
#define INT_MTA         (1 << INT_V_MTA)
#define INT_DRM         (1 << INT_V_DRM)
#define INT_RF          (1 << INT_V_RF)
#define INT_RP          (1 << INT_V_RP)
#define INT_RB          (1 << INT_V_RB)

#define API_DTA         1
#define API_MTA         1
#define API_DRM         1
#define API_RF          1
#define API_RP          1
#define API_RB          1

#define ACH_DTA         044
#define ACH_MTA         045
#define ACH_DRM         046
#define ACH_RB          047
#define ACH_RF          063
#define ACH_RP          064

/* API level 2 */

#define INT_V_PTR       0                               /* paper tape reader */
#define INT_V_LPT       1                               /* line printer */
#define INT_V_LPTSPC    2                               /* line printer spc */

#define INT_PTR         (1 << INT_V_PTR)
#define INT_LPT         (1 << INT_V_LPT)
#define INT_LPTSPC      (1 << INT_V_LPTSPC)

#define API_PTR         2
#define API_LPT         2
#define API_LPTSPC      2

#define ACH_PTR         050
#define ACH_LPT         056

/* API level 3 */

#define INT_V_CLK       0                               /* clock */
#define INT_V_TTI1      1                               /* LT15 keyboard */
#define INT_V_TTO1      2                               /* LT15 output */

#define INT_CLK         (1 << INT_V_CLK)
#define INT_TTI1        (1 << INT_V_TTI1)
#define INT_TTO1        (1 << INT_V_TTO1)

#define API_CLK         3
#define API_TTI1        3
#define API_TTO1        3

#define ACH_CLK         051
#define ACH_TTI1        075
#define ACH_TTO1        074

/* PI level */

#define INT_V_TTI       0                               /* console keyboard */
#define INT_V_TTO       1                               /* console output */
#define INT_V_PTP       2                               /* paper tape punch */
#define INT_V_G2        3                               /* BTL GRAPHICS-2 */

#define INT_TTI         (1 << INT_V_TTI)
#define INT_TTO         (1 << INT_V_TTO)
#define INT_PTP         (1 << INT_V_PTP)

#define API_TTI         4                               /* PI level */
#define API_TTO         4
#define API_PTP         4

#ifdef GRAPHICS2
/*
 * A PDP-9 version existed,
 * but we're only interested simulating a PDP-7 without API
 */
#define INT_G2          (1 << INT_V_G2)
#define API_G2          4
#endif

/* Interrupt macros */

#define SET_INT(dv)     int_hwre[API_##dv] = int_hwre[API_##dv] | INT_##dv
#define CLR_INT(dv)     int_hwre[API_##dv] = int_hwre[API_##dv] & ~INT_##dv
#define TST_INT(dv)     (int_hwre[API_##dv] & INT_##dv)

/* The DR15C uses the same relative bit position in all four interrupt levels.
   This allows software to have a single definition for the interrupt bit position,
   regardless of level. The standard macros cannot be used. */

#define INT_V_DR        7                               /* to left of all */
#define INT_DR          (1 << INT_V_DR)
#define API_DR0         0
#define API_DR1         1
#define API_DR2         2
#define API_DR3         3

/* I/O status flags for the IORS instruction

   bit  PDP-4           PDP-7           PDP-9           PDP-15

   0    intr on         intr on         intr on         intr on
   1    tape rdr flag*  tape rdr flag*  tape rdr flag*  tape rdr flag*
   2    tape pun flag*  tape pun flag*  tape pun flag*  tape pun flag*
   3    keyboard flag*  keyboard flag*  keyboard flag*  keyboard flag*
   4    type out flag*  type out flag*  type out flag*  type out flag*
   5    display flag*   display flag*   light pen flag* light pen flag*
   6    clk ovflo flag* clk ovflo flag* clk ovflo flag* clk ovflo flag*
   7    clk enable flag clk enable flag clk enable flag clk enable flag
   8    mag tape flag*  mag tape flag*  tape rdr empty* tape rdr empty*
   9    card rdr col*   *               tape pun empty  tape pun empty
   10   card rdr ~busy                  DECtape flag*   DECtape flag*
   11   card rdr error                  magtape flag*   magtape flag*
   12   card rdr EOF                                    disk pack flag*
   13   card pun row*                   DECdisk flag*   DECdisk flag*
   14   card pun error                                  lpt flag*
   15   lpt flag*       lpt flag*       lpt flag*
   16   lpt space flag* lpt error flag  lpt error flag
   17   drum flag*      drum flag*
*/

#define IOS_ION         0400000                         /* interrupts on */
#define IOS_PTR         0200000                         /* tape reader */
#define IOS_PTP         0100000                         /* tape punch */
#define IOS_TTI         0040000                         /* keyboard */
#define IOS_TTO         0020000                         /* terminal */
#define IOS_LPEN        0010000                         /* light pen */
#define IOS_CLK         0004000                         /* clock */
#define IOS_CLKON       0002000                         /* clock enable */
#define IOS_DTA         0000200                         /* DECtape */
#define IOS_RP          0000040                         /* disk pack */
#define IOS_RF          0000020                         /* fixed head disk */
#define IOS_DRM         0000001                         /* drum */
#if defined (PDP4) || defined (PDP7)
#define IOS_MTA         0001000                         /* magtape */
#define IOS_LPT         0000004                         /* line printer */
#define IOS_LPT1        0000002                         /* line printer stat */
#elif defined (PDP9)
#define IOS_PTRERR      0001000                         /* reader empty */
#define IOS_PTPERR      0000400                         /* punch empty */
#define IOS_MTA         0000100                         /* magtape */
#define IOS_LPT         0000004                         /* line printer */
#define IOS_LPT1        0000002                         /* line printer stat */
#elif defined (PDP15)
#define IOS_PTRERR      0001000                         /* reader empty */
#define IOS_PTPERR      0000400                         /* punch empty */
#define IOS_MTA         0000100                         /* magtape */
#define IOS_LPT         0000010                         /* line printer */
#define IOS_LPT1        0000000                         /* not used */
#endif

/* Function prototypes */

t_stat set_devno (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_devno (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat set_3cyc_reg (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat show_3cyc_reg (FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/* Translation tables */
extern const int32 asc_to_baud[128];
extern const char baud_to_asc[64];
extern const char fio_to_asc[64];

#endif
