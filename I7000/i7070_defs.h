/* i7070_defs.h: IBM 7070 simulator definitions

   Copyright (c) 2006-2016, Richard Cornwell

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "sim_defs.h"                                   /* simulator defns */

#include "i7000_defs.h"
/* Simulator stop codes */

#define STOP_IONRDY     1               /* I/O dev not ready */
#define STOP_HALT       2               /* HALT */
#define STOP_IBKPT      3               /* breakpoint */
#define STOP_UUO        4               /* invalid opcode */
#define STOP_INDLIM     5               /* indirect limit */
#define STOP_XECLIM     6               /* XEC limit */
#define STOP_IOCHECK    7               /* IOCHECK */
#define STOP_MMTRP      8               /* mm in trap */
#define STOP_INVLIN     9               /* 7750 invalid line number */
#define STOP_INVMSG    10               /* 7750 invalid message */
#define STOP_NOOFREE   11               /* 7750 No free output buffers */
#define STOP_NOIFREE   12               /* 7750 No free input buffers */

/* Trap codes */


/* Conditional error returns */

/* Memory */

#define MEM_ADDR_OK(x)  (((uint32) (x)) < MEMSIZE)
extern t_uint64         M[MAXMEMSIZE];

/* Arithmetic */

/* Instruction format */

/* Globally visible flags */
#define DMASK       0x0FFFFFFFFFFLL
#define IMASK       0x000FFFF0000LL
#define IMASK2      0x00FFFFF0000LL
#define XMASK       0x00F00000000LL
#define XMASK2      0x0F000000000LL
#define AMASK       0x0000000FFFFLL
#define OMASK       0x0FF00000000LL
#define SMASK       0xF0000000000LL
#define PSIGN       0x90000000000LL
#define MSIGN       0x60000000000LL
#define ASIGN       0x30000000000LL
#define NINES       0x09999999999LL     /* All nines */
#define FNINES      0x00099999999LL     /* Floating point all nines */
#define EMASK       0x0FF00000000LL     /* Floating point exponent */
#define FMASK       0x000FFFFFFFFLL     /* Floating point mantisa mask */
#define NMASK       0x000F0000000LL     /* Floating point normalize mask */

/* 7604 channel commands */
#define CHN_ALPHA_MODE  000             /* Recieve alpha words */
#define CHN_NUM_MODE    001             /* Recieve numeric words */
#define CHN_WRITE       002             /* Channel direction */
#define CHN_LAST        004             /* Last transfer */
#define CHN_MODE        0170            /* Mode of channel */
#define CHN_NORMAL      000             /* Normal mode */
#define CHN_COMPRESS    010             /* Compress leading zeros */
#define CHN_RECORD      020             /* Dectect record marks */
#define CHN_SEGMENT     040             /* Search for next segment */
#define CHN_ALPHA       0100            /* Alpha read only */
#define CHN_RM_FND      0200            /* Record mark read */

/* 7070 channel specific functions */

/* Issue a command to a channel */
int chan_cmd(uint16 dev, uint16 cmd, uint16 addr);

/* Decimal helper functions */
int                 dec_add(t_uint64 *a, t_uint64 b);
void                dec_add_noov(t_uint64 *a, t_uint64 b);
void                dec_comp(t_uint64 *a);
int                 dec_cmp(t_uint64 a, t_uint64 b);
void                mul_step(t_uint64 *a, t_uint64 b, int c);
void                div_step(t_uint64 b);
void                bin_dec(t_uint64 *a, uint32 b, int s, int l);
uint32              dec_bin_idx(t_uint64 a);
uint32              dec_bin_lim(t_uint64 a, uint32 b);
int                 get_rdw(t_uint64 a, uint32 *base, uint32 *limit);
void                upd_idx(t_uint64 *a, uint32 b);
int                 scan_irq();

/* Opcodes */
#define OP_HB           0x000
#define OP_B            0x001
#define OP_BLX          0x002
#define OP_CD           0x003
#define OP_EXMEM        0x004
#define OP_DIAGC        0x008
#define OP_DIAGT        0x009
#define OP_BZ1          0x010
#define OP_BV1          0x011
#define OP_ST1          0x012
#define OP_ZA1          0x013
#define OP_A1           0x014
#define OP_C1           0x015
#define OP_ZAA          0x016
#define OP_AA           0x017
#define OP_AS1          0x018
#define OP_AAS1         0x019
#define OP_BZ2          0x020
#define OP_BV2          0x021
#define OP_ST2          0x022
#define OP_ZA2          0x023
#define OP_A2           0x024
#define OP_C2           0x025
#define OP_AS2          0x028
#define OP_AAS2         0x029
#define OP_BZ3          0x030
#define OP_BV3          0x031
#define OP_ST3          0x032
#define OP_ZA3          0x033
#define OP_A3           0x034
#define OP_C3           0x035
#define OP_AS3          0x038
#define OP_AAS3         0x039
#define OP_BL           0x040
#define OP_BFLD         0x041
#define OP_BXN          0x044
#define OP_XL           0x045
#define OP_XZA          0x046
#define OP_XA           0x047
#define OP_XSN          0x048
#define OP_BIX          0x049
#define OP_SC           0x050
#define OP_BSWITCH      0x051
#define OP_M            0x053
#define OP_INQ          0x054
#define OP_PC           0x055
#define OP_ENA          0x056
#define OP_ENB          0x057
#define OP_PRTST        0x060
#define OP_BSW21        0x061
#define OP_BSW22        0x062
#define OP_BSW23        0x063
#define OP_PR           0x064
#define OP_RS           0x065
#define OP_LL           0x066
#define OP_LE           0x067
#define OP_LEH          0x068
#define OP_UREC         0x069
#define OP_FBV          0x070
#define OP_FR           0x071
#define OP_FM           0x073
#define OP_FA           0x074
#define OP_FZA          0x075
#define OP_FAD          0x076
#define OP_FAA          0x077
#define OP_TAPP1        0x081
#define OP_TAPP2        0x082
#define OP_TAPP3        0x083
#define OP_TAPP4        0x084
#define OP_TRNP         0x088
#define OP_CHNP1        0x093
#define OP_CHNP2        0x094
#define OP_CHNP3        0x096
#define OP_CHNP4        0x097
#define OP_HP           0x100
#define OP_NOP          0x101
#define OP_CS           0x103
#define OP_DIAGS        0x108
#define OP_DIAGR        0x109
#define OP_BM1          0x110
#define OP_ZST1         0x111
#define OP_STD1         0x112
#define OP_ZS1          0x113
#define OP_S1           0x114
#define OP_CA           0x115
#define OP_ZSA          0x116
#define OP_SA           0x117
#define OP_SS1          0x118
#define OP_BM2          0x120
#define OP_ZST2         0x121
#define OP_STD2         0x122
#define OP_ZS2          0x123
#define OP_S2           0x124
#define OP_SS2          0x128
#define OP_BM3          0x130
#define OP_ZST3         0x131
#define OP_STD3         0x132
#define OP_ZS3          0x133
#define OP_S3           0x134
#define OP_SS3          0x138
#define OP_BH           0x140
#define OP_BE           0x141
#define OP_BCX          0x143
#define OP_BXM          0x144
#define OP_XU           0x145
#define OP_XZS          0x146
#define OP_XS           0x147
#define OP_XLIN         0x148
#define OP_BDX          0x149
#define OP_CSC          0x150
#define OP_D            0x153
#define OP_ENS          0x156
#define OP_EAN          0x157
#define OP_PRION        0x161
#define OP_PRIOF        0x162
#define OP_RG           0x165
#define OP_FBU          0x170
#define OP_FD           0x173
#define OP_FS           0x174
#define OP_FDD          0x175
#define OP_FADS         0x176
#define OP_FSA          0x177
#define OP_TAP1         0x181
#define OP_TAP2         0x182
#define OP_TAP3         0x183
#define OP_TAP4         0x184
#define OP_TRN          0x188
#define OP_CHN1         0x193
#define OP_CHN2         0x194
#define OP_CHN3         0x196
#define OP_CHN4         0x197




