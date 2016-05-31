/* i1401_defs.h: IBM 1401 simulator definitions

   Copyright (c) 1993-2010, Robert M. Supnik

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

   06-JUl-10    RMS     Added overlap indicator definitions
   22-May-10    RMS     Added check for 64b definitions
   11-Jul-08    RMS     Added IO mode flag for boot (from Bob Abeles)
   28-Jun-07    RMS     Defined character code for tape mark
   14-Nov-04    RMS     Added column binary support
   27-Oct-04    RMS     Added maximum instruction length
   16-Mar-03    RMS     Fixed mnemonic for MCS
   03-Jun-02    RMS     Added 1311 support
   14-Apr-99    RMS     Converted t_addr to unsigned

   This simulator is based on the 1401 simulator written by Len Fehskens
   with assistance from Sarah Lee Harris and Bob Supnik. This one's for
   you, Len.  I am grateful to Paul Pierce and Charles Owen for their help
   in answering questions, gathering source material, and debugging.
*/

#ifndef I1401_DEFS_H_
#define I1401_DEFS_H_  0

#include "sim_defs.h"

#if defined(USE_INT64) || defined(USE_ADDR64)
#error "1401 does not support 64b values!"
#endif

/* Simulator stop codes */

#define STOP_NXI        1                               /* unimpl instr */
#define STOP_NXM        2                               /* non-exist mem */
#define STOP_NXD        3                               /* non-exist dev */
#define STOP_NOWM       4                               /* no WM under op */
#define STOP_INVA       5                               /* invalid A addr */
#define STOP_INVB       6                               /* invalid B addr */
#define STOP_INVL       7                               /* invalid length */
#define STOP_INVM       8                               /* invalid modifier */
#define STOP_INVBR      9                               /* invalid branch */
#define STOP_IBKPT      10                              /* breakpoint */
#define STOP_HALT       11                              /* halt */
#define STOP_INVMTU     12                              /* invalid MT unit */
#define STOP_MTZ        13                              /* MT zero lnt rec */
#define STOP_MTL        14                              /* MT write lock */
#define STOP_CCT        15                              /* inv CCT channel */
#define STOP_NOCD       16                              /* no cards left */
#define STOP_WRAP       17                              /* AS, BS mem wrap */
#define STOP_IOC        18                              /* I/O check */
#define STOP_INVDSC     19                              /* invalid disk sector */
#define STOP_INVDCN     20                              /* invalid disk count */
#define STOP_INVDSK     21                              /* invalid disk unit */
#define STOP_INVDFN     22                              /* invalid disk func */
#define STOP_INVDLN     23                              /* invalid disk reclen */
#define STOP_WRADIS     24                              /* write address dis */
#define STOP_WRCHKE     25                              /* write check error */
#define STOP_INVDAD     26                              /* invalid disk addr */
#define STOP_INVDCY     27                              /* invalid direct seek */

/* Memory and devices */

#define MAXMEMSIZE      16000                           /* max memory */
#define MEMSIZE         (cpu_unit.capac)                /* current memory */
#define CDR_BUF         1                               /* card rdr buffer */
#define CDR_WIDTH       80                              /* card rdr width */
#define CDP_BUF         101                             /* card punch buffer */
#define CDP_WIDTH       80                              /* card punch width */
#define CD_CBUF1        401                             /* r/p col bin buf 12-3 */
#define CD_CBUF2        501                             /* r/p col bin buf 4-9 */
#define LPT_BUF         201                             /* line print buffer */
#define LPT_WIDTH       132                             /* line print width */
#define CCT_LNT         132                             /* car ctrl length */
#define INQ_WIDTH       80                              /* inq term width */
#define ADDR_ERR(x)     (((uint32) (x)) >= MEMSIZE)

/* Binary address format

        <14:0>          address, with index added in
        <23:16>         index register memory address
        <25:24>         address error bits
*/

#define ADDRMASK        037777                          /* addr mask */
#define INDEXMASK       077777                          /* addr + index mask */
#define V_INDEX         16
#define M_INDEX         0177
#define V_ADDRERR       24
#define BA              (1 << V_ADDRERR)                /* bad addr digit */
#define X1              (87 << V_INDEX)                 /* index reg 1 */
#define X2              (92 << V_INDEX)                 /* index reg 2 */
#define X3              (97 << V_INDEX)                 /* index reg 3 */

/* CPU instruction control flags.  The flag definitions must be harmonized
   with the UNIT flag definitions used by the simulator. */

/* Lengths */

#define L1              0001                            /* 1: op */
#define L2              0002                            /* 2: op d */
#define L4              0004                            /* 4: op aaa */
#define L5              0010                            /* 5: op aaa d */
#define L7              0020                            /* 7: op aaa bbb */
#define L8              0040                            /* 8: op aaa bbb d */
#define MAX_L           8                               /* max length */

/* CPU options, stored in cpu_unit.flags */

#define MDV             (1 << (UNIT_V_UF + 0))          /* multiply/divide */
#define MR              (1 << (UNIT_V_UF + 1))          /* move record */
#define XSA             (1 << (UNIT_V_UF + 2))          /* index, store addr */
#define EPE             (1 << (UNIT_V_UF + 3))          /* expanded edit */
#define MA              (1 << (UNIT_V_UF + 4))          /* modify address */
#define BBE             (1 << (UNIT_V_UF + 5))          /* br bit equal */
#define HLE             (1 << (UNIT_V_UF + 6))          /* high/low/equal */
#define UNIT_MSIZE      (1 << (UNIT_V_UF + 7))          /* fake flag */
#define ALLOPT          (MDV + MR + XSA + EPE + MA + BBE + HLE)
#define STDOPT          (MDV + MR + XSA + EPE + MA + BBE + HLE)

/* Fetch control */

#define AREQ            (1 << (UNIT_V_UF + 8))          /* validate A */
#define BREQ            (1 << (UNIT_V_UF + 9))          /* validate B */
#define MLS             (1 << (UNIT_V_UF + 10))         /* move load store */
#define NOWM            (1 << (UNIT_V_UF + 11))         /* no WM at end */
#define HNOP            (1 << (UNIT_V_UF + 12))         /* halt or nop */
#define IO              (1 << (UNIT_V_UF + 13))         /* IO */
#define UNIT_BCD        (1 << (UNIT_V_UF + 14))         /* BCD strings */

#if (UNIT_V_UF < 6) || ((UNIT_V_UF + 14) > 31)
    Definition error: flags overlap
#endif

/* BCD memory character format */

#define WM              0100                            /* word mark */
#define ZONE            0060                            /* zone */
#define  BBIT           0040                            /* 1 in valid sign */
#define  ABIT           0020                            /* sign (1 = +) */
#define DIGIT           0017                            /* digit */
#define CHAR            0077                            /* character */

#define V_WM            6
#define V_ZONE          4
#define V_DIGIT         0

/* Interesting BCD characters */

#define BCD_BLANK       000
#define BCD_ONE         001
#define BCD_TWO         002
#define BCD_THREE       003
#define BCD_FOUR        004
#define BCD_FIVE        005
#define BCD_SIX         006
#define BCD_SEVEN       007
#define BCD_EIGHT       010
#define BCD_NINE        011
#define BCD_ZERO        012
#define BCD_TAPMRK      017
#define BCD_ALT         020
#define BCD_S           022
#define BCD_U           024
#define BCD_W           026
#define BCD_RECMRK      032
#define BCD_COMMA       033
#define BCD_PERCNT      034
#define BCD_WM          035
#define BCD_BS          036
#define BCD_TS          037
#define BCD_MINUS       040
#define BCD_M           044
#define BCD_R           051
#define BCD_DOLLAR      053
#define BCD_ASTER       054
#define BCD_AMPER       060
#define BCD_A           061
#define BCD_B           062
#define BCD_C           063
#define BCD_E           065
#define BCD_DECIMAL     073
#define BCD_SQUARE      074
#define BCD_GRPMRK      077

/* Opcodes */

#define OP_R            001                             /* read */
#define OP_W            002                             /* write */
#define OP_WR           003                             /* write and read */
#define OP_P            004                             /* punch */
#define OP_RP           005                             /* read and punch */
#define OP_WP           006                             /* write and punch */
#define OP_WRP          007                             /* write read punch */
#define OP_RF           010                             /* reader feed */
#define OP_PF           011                             /* punch feed */
#define OP_MA           013                             /* modify address */
#define OP_MUL          014                             /* multiply */
#define OP_CS           021                             /* clear storage */
#define OP_S            022                             /* subtract */
#define OP_MTF          024                             /* magtape function */
#define OP_BWZ          025                             /* branch wm or zone */
#define OP_BBE          026                             /* branch bit equal */
#define OP_MZ           030                             /* move zone */
#define OP_MCS          031                             /* move suppr zeroes */
#define OP_SWM          033                             /* set word mark */
#define OP_DIV          034                             /* divide */
#define OP_SS           042                             /* select stacker */
#define OP_LCA          043                             /* load characters */
#define OP_MCW          044                             /* move characters */
#define OP_NOP          045                             /* no op */
#define OP_MCM          047                             /* move to rec/grp mk */
#define OP_SAR          050                             /* store A register */
#define OP_ZS           052                             /* zero and subtract */
#define OP_A            061                             /* add */
#define OP_B            062                             /* branch */
#define OP_C            063                             /* compare */
#define OP_MN           064                             /* move numeric */
#define OP_MCE          065                             /* move char and edit */
#define OP_CC           066                             /* carriage control */
#define OP_SBR          070                             /* store B register */
#define OP_ZA           072                             /* zero and add */
#define OP_H            073                             /* halt */
#define OP_CWM          074                             /* clear word mark */

/* I/O addresses */

#define IO_INQ          023                             /* inquiry terminal */
#define IO_MT           024                             /* magtape */
#define IO_MTB          062                             /* binary magtape */
#define IO_DP           066                             /* 1311 diskpack */

/* I/O modes */

#define MD_NORM         0                               /* normal (move) */
#define MD_WM           1                               /* word mark (load) */
#define MD_BIN          2                               /* binary */
#define MD_BOOT         4                               /* boot read */

/* Indicator characters */

#define IN_UNC          000                             /* unconditional */
#define IN_CC9          011                             /* carr ctrl chan 9 */
#define IN_CC12         014                             /* carr ctrl chan 12 */
#define IN_UNQ          021                             /* unequal */
#define IN_EQU          022                             /* equal */
#define IN_LOW          023                             /* low */
#define IN_HGH          024                             /* high */
#define IN_DPW          025                             /* parity/compare check */
#define IN_LNG          026                             /* wrong lnt record */
#define IN_UNA          027                             /* unequal addr cmp */
#define IN_DSK          030                             /* disk error */
#define IN_OVF          031                             /* overflow */
#define IN_LPT          032                             /* printer error */
#define IN_PRO          034                             /* process check */
#define IN_DBY          036                             /* disk busy */
#define IN_TBY          041                             /* tape busy */
#define IN_END          042                             /* end indicator */
#define IN_TAP          043                             /* tape error */
#define IN_ACC          045                             /* access error */
#define IN_BSY          047                             /* printer busy */
#define IN_INR          050                             /* inquiry request */
#define IN_PCB          051                             /* printer carr busy */
#define IN_PNCH         052                             /* punch error */
#define IN_INC          054                             /* inquiry clear */
#define IN_LST          061                             /* last card */
#define IN_SSB          062                             /* sense switch B */
#define IN_SSC          063                             /* sense switch C */
#define IN_SSD          064                             /* sense switch D */
#define IN_SSE          065                             /* sense switch E */
#define IN_SSF          066                             /* sense switch F */
#define IN_SSG          067                             /* sense switch G */
#define IN_RBY          070                             /* reader busy */
#define IN_PBY          071                             /* punch busy */
#define IN_READ         072                             /* reader error */

#define CRETIOE(f,c)    return ((f)? (c): SCPE_OK)

/* Function prototypes */

int32 bcd2ascii (int32 c, t_bool use_h);
int32 ascii2bcd (int32 c);

/* Translation Tables */

extern const char ascii_to_bcd_old[128];
extern char bcd_to_ascii_old[64];
extern const char ascii_to_bcd[128];
extern const char bcd_to_ascii_a[64];
extern const char bcd_to_ascii_h[64];
extern const char bcd_to_pca[64];
extern const char bcd_to_pch[64];
extern const uint32 bcd_to_colbin[64];
extern const int32 bcd_to_bin[16];
extern const int32 bin_to_bcd[16];
extern const int32 one_table[64];
extern const int32 ten_table[64];
extern const int32 hun_table[64];
extern const int32 len_table[9];
extern const int32 op_table[64];


#endif
