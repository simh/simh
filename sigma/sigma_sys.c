/* sigma_sys.c: Sigma system interface

   Copyright (c) 2007-2024, Robert M Supnik

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

   03-Apr-2024  RMS     Added CR, CP support (Ken Rector)
   09-Mar-2017  RMS     Added LOAD processor for CCT
*/

#include "sigma_defs.h"
#include <ctype.h>

#define FMTASC(x) ((x) < 0x20)? "<%02X>": "%c", (x)

extern DEVICE cpu_dev;
extern DEVICE map_dev;
extern DEVICE int_dev;
extern DEVICE chan_dev[];
extern DEVICE rtc_dev;
extern DEVICE tt_dev;
extern DEVICE pt_dev;
extern DEVICE lp_dev;
extern DEVICE rad_dev;
extern DEVICE dk_dev;
extern DEVICE dp_dev[];
extern DEVICE mt_dev;
extern DEVICE cr_dev;
extern DEVICE cp_dev;
extern DEVICE mux_dev, muxl_dev;
extern REG cpu_reg[];
extern uint32 *M;
extern UNIT cpu_unit;

t_stat fprint_sym_m (FILE *of, uint32 inst);
t_stat parse_sym_m (CONST char *cptr, t_value *val);
void fprint_ebcdic (FILE *of, uint32 c);

extern t_stat lp_read_cct (FILE *cfile);

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "XDS Sigma";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &map_dev,
    &int_dev,
    &chan_dev[0],
    &chan_dev[1],
    &chan_dev[2],
    &chan_dev[3],
    &chan_dev[4],
    &chan_dev[5],
    &chan_dev[6],
    &chan_dev[7],
    &rtc_dev,                                           /* must be first */
    &tt_dev,
    &pt_dev,
    &lp_dev,
    &mt_dev,
    &rad_dev,
    &dk_dev,
    &dp_dev[0],
    &dp_dev[1],
    &mux_dev,
    &muxl_dev,
    &cr_dev,
    &cp_dev,
    NULL
    };

const char *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "Invalid I/O configuration",
    "Breakpoint",
    "Address stop",
    "Wait, interrupts off",
    "Invalid PSD",
    "Nested EXU's exceed limit",
    "Undefined instruction",
    "Illegal trap or interrupt instruction",
    "Invalid interrupt vector",
    "Nested traps",
    };

/* Character conversion tables (from Sigma 7 manual) */

uint8 ascii_to_ebcdic[128] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x09, 0x06, 0x07,     /* 00 - 1F */
    0x08, 0x05, 0x15, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x0A, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x40, 0x5A, 0x7F, 0x7B, 0x5B, 0x6C, 0x50, 0x7D,     /* 20 - 3F */
    0x4D, 0x5D, 0x5C, 0x4E, 0x6B, 0x60, 0x4B, 0x61,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
    0xF8, 0xF9, 0x7A, 0x5E, 0x4C, 0x7E, 0x6E, 0x6F,
    0x7C, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,     /* 40 - 5F */
    0xC8, 0xC9, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6,
    0xD7, 0xD8, 0xD9, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
    0xE7, 0xE8, 0xE9, 0xB4, 0xB1, 0xB5, 0x6A, 0x6D,
    0x4A, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,     /* 60- 7F */
    0x88, 0x89, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xA9, 0xB2, 0x4F, 0xB3, 0x5F, 0xFF
    };

uint8 ebcdic_to_ascii[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x09, 0x06, 0x07,     /* 00 - 1F */
    0x08, 0x05, 0x15, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x0A, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     /* 20 - 3F */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ' ',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     /* 40 - 5F */
    0x00, 0x00, '`',  '.',  '<',  '(',  '+',  '|',
    '&',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, '!',  '$',  '*',  ')',  ';',  '~',
    '-',  '/',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     /* 60 - 7F */
    0x00, 0x00, '^',  ',',  '%',  '_',  '>',  '?',
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, ':',  '#',  '@',  '\'', '=',  '"',
    0x00, 'a',  'b',  'c',  'd',  'e',  'f',  'g',      /* 80 - 9F */
    'h',  'i',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 'j',  'k',  'l',  'm',  'n',  'o',  'p',
    'q',  'r',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 's',  't',  'u',  'v',  'w',  'x',      /* A0 - BF */
    'y',  'z',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, '\\', '{',  '}',  '[',  ']',  0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 'A',  'B',  'C',  'D',  'E',  'F',  'G',      /* C0 - DF */
    'H',  'I',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 'J',  'K',  'L',  'M',  'N',  'O',  'P',
    'Q',  'R',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 'S',  'T',  'U',  'V',  'W',  'X',      /* E0 - FF */
    'Y',  'Z',  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    '0',  '1',  '2',  '3',  '4',  '5',  '6',  '7',
    '8',  '9',  0x00, 0x00, 0x00, 0x00, 0x00, 0x7F,
    };

uint16 ebcdic_to_hol[256] = {
 /*  T918    T91    T92    T93    T94    T95    T96   T97   0x0x */
   0xB03,  0x901, 0x881, 0x841, 0x821, 0x811, 0x809, 0x805,
 /*  T98,   T189 , T289,  T389,  T489,  T589,  T689, T789   */
   0x803,  0x903, 0x883, 0x843, 0x823, 0x813, 0x80B, 0x807,
 /* TE189    E91    E92    E93    E94    E95    E96   E97   0x1x */
   0xD03,  0x501, 0x481, 0x441, 0x421, 0x411, 0x409, 0x405,
 /*  E98     E918   E928   E938   E948   E958   E968  E978   */
   0x403,  0x503, 0x483, 0x443, 0x423, 0x413, 0x40B, 0x407,
 /*  E0918   091    092    093    094    095    096   097   0x2x */
   0x703,  0x301, 0x281, 0x241, 0x221, 0x211, 0x209, 0x205,
 /*  098     0918  0928   0938    0948   0958   0968  0978   */
   0x203,  0x303, 0x283, 0x243, 0x223, 0x213, 0x20B, 0x207,
 /* TE0918   91    92     93      94     95     96     97   0x3x */
   0xF03,  0x101, 0x081, 0x041, 0x021, 0x011, 0x009, 0x005,
 /*  98      189    289    389    489    589    689    789   */
   0x003,  0x103, 0x083, 0x043, 0x023, 0x013, 0x00B, 0x007,
 /*          T091  T092   T093   T094   T095   T096    T097  0x4x */
   0x000,  0xB01, 0xA81, 0xA41, 0xA21, 0xA11, 0xA09, 0xA05,
 /* T098     T18    T28    T38    T48    T58    T68    T78    */
   0xA03,  0x902, 0x882, 0x842, 0x822, 0x812, 0x80A, 0x806,
 /* T        TE91  TE92   TE93   TE94   TE95   TE96    TE97  0x5x */
   0x800,  0xD01, 0xC81, 0xC41, 0xC21, 0xC11, 0xC09, 0xC05,
 /* TE98     E18    E28    E38    E48    E58    E68    E78   */
   0xC03,  0x502, 0x482, 0x442, 0x422, 0x412, 0x40A, 0x406,
 /* E        01    E092   E093   E094   E095   E096    E097  0x6x */
   0x400,  0x300, 0x681, 0x641, 0x621, 0x611, 0x609, 0x605,
 /* E098     018   TE     038    048     68    068     078    */
   0x603,  0x302, 0xC00, 0x242, 0x222, 0x212, 0x20A, 0x206,
 /* TE0    TE091  TE092  TE093  TE094  TE095  TE096  TE097   0x7x */
   0xE00,  0xF01, 0xE81, 0xE41, 0xE21, 0xE11, 0xE09, 0xE05,
 /* TE098    18     28     38    48      58      68     78    */
   0xE03,  0x102, 0x082, 0x042, 0x022, 0x012, 0x00A, 0x006,
 /* T018     T01    T02    T03    T04    T05    T06    T07   0x8x */
   0xB02,  0xB00, 0xA80, 0xA40, 0xA20, 0xA10, 0xA08, 0xA04,
 /* T08      T09   T028   T038    T048   T058   T068   T078   */
   0xA02,  0xA01, 0xA82, 0xA42, 0xA22, 0xA12, 0xA0A, 0xA06,
 /* TE18     TE1    TE2    TE3    TE4    TE5    TE6    TE7   0x9x */
   0xD02,  0xD00, 0xC80, 0xC40, 0xC20, 0xC10, 0xC08, 0xC04,
 /* TE8      TE9   TE28   TE38    TE48   TE58   TE68   TE78   */
   0xC02,  0xC01, 0xC82, 0xC42, 0xC22, 0xC12, 0xC0A, 0xC06,
 /* E018     E01    E02    E03    E04    E05    E06    E07   0xax */
   0x702,  0x700, 0x680, 0x640, 0x620, 0x610, 0x608, 0x604,
 /* E08      E09   E028   E038    E048   E058   E068   E078   */
   0x602,  0x601, 0x682, 0x642, 0x622, 0x612, 0x60A, 0x606,
 /* TE018    TE01   TE02   TE03   TE04   TE05   TE06   TE07  0xbx */
   0xF02,  0xF00, 0xE80, 0xE40, 0xE20, 0xE10, 0xE08, 0xE04,
 /* TE08     TE09   TE028  TE038  TE048  TE058  TE068  TE078  */
   0xE02,  0xE01, 0xE82, 0xE42, 0xE22, 0xE12, 0xE0A, 0xE06,
 /*  T0      T1     T2     T3     T4     T5     T6     T7    0xcx */
   0xA00,  0x900, 0x880, 0x840, 0x820, 0x810, 0x808, 0x804,
 /* T8       T9     T0928  T0938  T0948  T0958  T0968  T0978  */
   0x802,  0x801, 0xA83, 0xA43, 0xA23, 0xA13, 0xA0B, 0xA07,
 /* E0       E1     E2     E3     E4     E5     E6     E7    0xdx */
   0x600,  0x500, 0x480, 0x440, 0x420, 0x410, 0x408, 0x404,
 /* E8       E9     TE928  TE938  TE948  TE958  TE968  TE978  */
   0x402,  0x401, 0xC83, 0xC43, 0xC23, 0xC13, 0xC0B, 0xC07,
 /* 028      E091   02     03     04     05     06     07    0xex  */
   0x282,  0x701, 0x280, 0x240, 0x220, 0x210, 0x208, 0x204,
 /* 08       09     E0928  E0938  E0948  E0958  E0968  E0978  */
   0x202,  0x201, 0x683, 0x643, 0x623, 0x613, 0x60B, 0x607,
 /* 0        1      2      3      4      5      6      7     0xfx */
   0x200,  0x100, 0x080, 0x040, 0x020, 0x010, 0x008, 0x004,
 /* 8        9     TE0928 TE0938 TE0948 TE0958 TE0968 TE0978  */
   0x002,  0x001, 0xE83, 0xE43, 0xE23, 0xE13, 0xE0B, 0xE07
};

/* Binary loader */

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
return lp_read_cct (fileref);
}

/* Symbol and format tables */

#define IC_V_CL         17                              /* class */
#define IC_M_CL         0x1F
#define IC_V_RN         16                              /* takes rn */
#define IC_RN           (1u << IC_V_RN)
#define IC_V_IND        15                              /* takes ind */
#define IC_IND          (1u << IC_V_IND)
#define IC_V_XR         13                              /* takes xr */
#define IC_M_XR         0x3
#define  IC_NONE        0
#define  IC_XR          1
#define  IC_CTL         2
#define IC_V_AW         7                               /* addr width */
#define IC_M_AW         0x3F
#define IC_V_AP         2                               /* addr position */
#define IC_M_AP         0x1F
#define IC_V_SGN        1                               /* sign allowed */
#define IC_SGN          (1u << IC_V_SGN)
#define IC_V_AOP        0                               /* addr optional */
#define IC_AOP          (1u << IC_V_AOP)

#define ID1_07          0                               /* decode 1-7 */
#define ID1_11          1                               /* decode 1-11 */
#define IDSHFT          2                               /* shift */
#define IDSHFF          3                               /* shift floating */
#define IDMMCX          4                               /* MMC ext */

#define I_C(c,r,i,w,s,x,sn,ao) \
                        (((c) << IC_V_CL) | ((r) << IC_V_RN) | ((i) << IC_V_IND)|\
                         ((w) << IC_V_AW) | ((s) << IC_V_AP) | ((x) << IC_V_XR) |\
                         ((sn) << IC_V_SGN) | ((ao) << IC_V_AOP))

/*                          decode R I wd ps x sn ao    */
#define IC_MRF          I_C(ID1_07,1,1,17, 0,1, 0, 0)   /* mem ref */
#define IC_IMM          I_C(ID1_07,1,0,20, 0,0, 1, 0)   /* immediate */
#define IC_LCFI         I_C(ID1_07,0,0, 8, 0,2, 0, 0)   /* LCFI */
#define IC_LFI          I_C(ID1_11,0,0, 4, 0,0, 0, 0)   /* LFI */
#define IC_LCI          I_C(ID1_11,0,0, 4, 4,0, 0, 0)   /* LCI */
#define IC_SHFT         I_C(IDSHFT,1,0, 7, 0,1, 1, 0)   /* shift */
#define IC_SHFF         I_C(IDSHFF,1,0, 7, 0,1, 1, 0)   /* floating shift */
#define IC_MNOR         I_C(ID1_07,0,1,17, 0,1, 0, 0)   /* mem ref, no reg */
#define IC_MNOX         I_C(ID1_11,0,1,17, 0,1, 0, 0)   /* mef ref ext */
#define IC_NOP          I_C(ID1_07,1,0, 0, 0,0, 0, 0)   /* no operand */
#define IC_NOPX         I_C(ID1_11,1,0, 0, 0,0, 0, 0)   /* no operand ext */
#define IC_MMC          I_C(ID1_07,1,1, 3,17,0, 0, 0)   /* MMC */
#define IC_MMCX         I_C(IDMMCX,1,0, 0, 0,0, 0, 0)   /* MMC extended */
#define IC_MNRI         I_C(ID1_11,0,0, 0, 0,0, 0, 0)   /* no operands */
#define IC_MNRO         I_C(ID1_07,0,1,17, 0,1, 0, 1)   /* mem ref, addr opt */

#define IC_GETCL(x)     (((x) >> IC_V_CL) & IC_M_CL)
#define IC_GETXR(x)     (((x) >> IC_V_XR) & IC_M_XR)
#define IC_GETAW(x)     (((x) >> IC_V_AW) & IC_M_AW)
#define IC_GETAP(x)     (((x) >> IC_V_AP) & IC_M_AP)

static const uint32 masks[] = {
 0x7F000000, 0x7FF00000, 0x7F000700, 0x7F000100,
 0x7F0E0000
 };

/* Opcode tables - extended mnemonics must precede standard mnemonics */

static const uint32 opc_val[] = {
 0x02100000, IC_LFI,  0x02200000, IC_LCI,  0x70100000, IC_MNOX, 0x70200000, IC_MNOX,
 0x25000000, IC_SHFT, 0x25000100, IC_SHFT, 0x25000200, IC_SHFT, 0x25000000, IC_SHFT,
 0x25000400, IC_SHFT, 0x25000500, IC_SHFT, 0x25000600, IC_SHFT, 0x25000700, IC_SHFT,
 0x24000000, IC_SHFT, 0x24000100, IC_SHFT,
 0x68000000, IC_MNOX, 0x68100000, IC_MNOX, 0x68200000, IC_MNOX, 0x68300000, IC_MNOX,
 0x68400000, IC_MNOX, 0x68800000, IC_MNOX,
 0x69000000, IC_MNOX, 0x69100000, IC_MNOX, 0x69200000, IC_MNOX, 0x69300000, IC_MNOX,
 0x69400000, IC_MNOX, 0x69800000, IC_MNOX,
 0x6F020000, IC_MMCX, 0x6F040000, IC_MMCX, 0x6F060000, IC_MMCX, 0x6F080000, IC_MMCX,
 0x6F080000, IC_MMCX, 0x02000000, IC_MNRI,

                                           0x02000000, IC_LCFI,
 0x04000000, IC_MRF,  0x05000000, IC_MRF,  0x06000000, IC_MRF,  0x07000000, IC_MRF,
 0x08000000, IC_MRF,  0x09000000, IC_MRF,  0x0A000000, IC_MRF,  0x0B000000, IC_MRF,
 0x0C000000, IC_MRF,  0x0D000000, IC_NOP,  0x0E000000, IC_MRF,  0x0F000000, IC_MRF,
 0x10000000, IC_MRF,  0x11000000, IC_MRF,  0x12000000, IC_MRF,  0x13000000, IC_MRF,
                      0x15000000, IC_MRF,
 0x18000000, IC_MRF,  0x19000000, IC_MRF,  0x1A000000, IC_MRF,  0x1B000000, IC_MRF,
 0x1C000000, IC_MRF,  0x1D000000, IC_MRF,  0x1E000000, IC_MRF,  0x1F000000, IC_MRF,
 0x20000000, IC_IMM,  0x21000000, IC_IMM,  0x22000000, IC_IMM,  0x23000000, IC_IMM,
 0x24000000, IC_MRF,  0x25000000, IC_MRF,  0x26000000, IC_MRF,
 0x28000000, IC_MRF,  0x29000000, IC_MRF,  0x2A000000, IC_MRF,  0x2B000000, IC_MRF,
 0x2C000000, IC_MRF,  0x2D000000, IC_MRF,  0x2E000000, IC_MNRO, 0x2F000000, IC_MRF,
 0x30000000, IC_MRF,  0x31000000, IC_MRF,  0x32000000, IC_MRF,  0x33000000, IC_MRF,
 0x34000000, IC_MRF,  0x35000000, IC_MRF,  0x36000000, IC_MRF,  0x37000000, IC_MRF,
 0x38000000, IC_MRF,  0x39000000, IC_MRF,  0x3A000000, IC_MRF,  0x3B000000, IC_MRF,
 0x3C000000, IC_MRF,  0x3D000000, IC_MRF,  0x3E000000, IC_MRF,  0x3F000000, IC_MRF,
 0x40000000, IC_IMM,  0x41000000, IC_IMM,
 0x44000000, IC_MRF,  0x45000000, IC_MRF,  0x46000000, IC_MRF,  0x47000000, IC_MRF,
 0x48000000, IC_MRF,  0x49000000, IC_MRF,  0x4A000000, IC_MRF,  0x4B000000, IC_MRF,
 0x4C000000, IC_MRF,  0x4D000000, IC_MRF,  0x4E000000, IC_MRF,  0x4F000000, IC_MRF,
 0x50000000, IC_MRF,  0x51000000, IC_MRF,  0x52000000, IC_MRF,  0x53000000, IC_MRF,
                      0x55000000, IC_MRF,  0x56000000, IC_MRF,  0x57000000, IC_MRF,
 0x58000000, IC_MRF,                       0x5A000000, IC_MRF,  0x5B000000, IC_MRF,

 0x60000000, IC_IMM,  0x61000000, IC_IMM,                       0x63000000, IC_IMM,
 0x64000000, IC_MRF,  0x65000000, IC_MRF,  0x66000000, IC_MRF,  0x67000000, IC_MNOR,
 0x68000000, IC_MRF,  0x69000000, IC_MRF,  0x6A000000, IC_MRF,  0x6B000000, IC_MRF,
 0x6C000000, IC_MRF,  0x6D000000, IC_MRF,  0x6E000000, IC_MRF,  0x6F000000, IC_MMC,
 0x70000000, IC_MRF,  0x71000000, IC_MRF,  0x72000000, IC_MRF,  0x73000000, IC_MRF,
 0x74000000, IC_MNOR, 0x75000000, IC_MRF,  0x76000000, IC_MRF,  0x77000000, IC_MRF,
 0x78000000, IC_MRF,  0x79000000, IC_MRF,  0x7A000000, IC_MRF,  0x7B000000, IC_MRF,
 0x7C000000, IC_MNOR, 0x7D000000, IC_MRF,  0x7E000000, IC_MRF,  0x7F000000, IC_MRF,
 0xFFFFFFFF, 0
 }; 

static const char *opcode[] = {
 "LFI",  "LCI",  "LF",   "LC",                          /* extended mmenomics */
 "SLS",  "SLD",  "SCS",  "SCD",
 "SAS",  "SAD",  "SSS",  "SSD",
 "SFS",  "SFL",
 "B",    "BGE",  "BLE",  "BE",
 "BNOV", "BNC",
 "BNVR", "BL",   "BG",   "BNE",
 "BOV",  "BC",
 "LLOCKS", "LPC", "LLOCKSE", "LMAP",
 "LMAPRE", "NOP",

                 "LCFI",                                /* 00 */
 "CAL1", "CAL2", "CAL3", "CAL4",
 "PLW",  "PSW",  "PLM",  "PSM",
 "PLS",  "PSS",  "LPSD", "XPSD",
 "AD",   "CD",   "LD",   "MSP",                         /* 10 */
 "STD",
 "SD",   "CLM",  "LCD",  "LAD",
 "FSL",  "FAL",  "FDL",  "FML",
 "AI",   "CI",   "LI",   "MI",                          /* 20 */
 "SF",   "S",    "LAS",
 "CVS",  "CVA",  "LM",   "STM",
 "LRA",  "LMS",  "WAIT", "LRP",
 "AW",   "CW",   "LW",   "MTW",                         /* 30 */
 "LVAW", "STW",  "DW",   "MW",
 "SW",   "CLR",  "LCW",  "LAW",
 "FSS",  "FAS",  "FDS",  "FMS",
 "TTBS", "TBS",                                         /* 40 */
 "ANLZ", "CS",   "XW",   "STS",
 "EOR",  "OR",   "LS",   "AND",
 "SIO",  "TIO",  "TDV",  "HIO",
 "AH",   "CH",   "LH",   "MTH",                         /* 50 */
 "STH",  "DH",   "MH",
 "SH",           "LCH",  "LAH",

 "CBS",  "MBS",          "EBS",                         /* 60 */
 "BDR",  "BIR",  "AWM",  "EXU",
 "BCR",  "BCS",  "BAL",  "INT",
 "RD",   "WD",   "AIO",  "MMC",
 "LCF",  "CB",   "LB",   "MTB",                         /* 70 */
 "STCF", "STB",  "PACK", "UNPK",
 "DS",   "DA",   "DD",   "DM",
 "DSA",  "DC",   "DL",   "DST",
 NULL
 };

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       pointer to values
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        return  =       status code
*/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
uint32 inst, sc, rdx, c;
DEVICE *dptr;

inst = val[0];                                          /* get inst */
if (uptr == NULL)                                       /* anon = CPU */
    uptr = &cpu_unit;
else if (uptr != &cpu_unit)                             /* CPU only */
    return SCPE_ARG;
dptr = find_dev_from_unit (uptr);                       /* find dev */
if (dptr == NULL)
    return SCPE_IERR;
if (sw & SWMASK ('D'))                                  /* get radix */
    rdx = 10;
else if (sw & SWMASK ('O'))
    rdx = 8;
else if (sw & SWMASK ('X'))
    rdx = 16;
else rdx = dptr->dradix;

if (sw & SWMASK ('C')) {                                /* char format? */
    for (sc = 0; sc < 32; sc = sc + 8) {                /* print string */
        c = (inst >> (24 - sc)) & BMASK;
        if (sw & SWMASK ('A'))
            fprintf (of, FMTASC (c & 0x7F));
        else fprint_ebcdic (of, c);
        }
    return 0;                                           /* return # chars */
    }
if (sw & SWMASK ('A')) {                                /* ASCII? */
    sc = 24 - ((addr & 0x3) * 8);                       /* shift count */
    c = (inst >> sc) & 0x7F;
    fprintf (of, FMTASC (c));
    return 0;
    }
if (sw & SWMASK ('E')) {                                /* EBCDIC? */
    sc = 24 - ((addr & 0x3) * 8);                       /* shift count */
    c = (inst >> sc) & BMASK;
    fprint_ebcdic (of, c);
    return 0;
    }
if (sw & SWMASK ('B')) {                                /* byte? */
    sc = 24 - ((addr & 0x3) * 8);                       /* shift count */
    c = (inst >> sc) & BMASK;
    fprintf (of, "%02X", c);
    return 0;
    }
if (sw & SWMASK ('H')) {                                /* halfword? */
    c = ((addr & 1)? inst: inst >> 16) & HMASK;
    fprintf (of, "%04X", c);
    return 0;
    }
if ((sw & SWMASK ('M')) &&                              /* inst format? */
    !fprint_sym_m (of, inst))                           /* decode inst */
    return 0;

fprint_val (of, inst, rdx, 32, PV_RZRO);
return 0;
}

/* Instruction decode */

t_stat fprint_sym_m (FILE *of, uint32 inst)
{
uint32 i, j;

for (i = 0; opc_val[i] < 0xFFFFFFFF; i = i + 2) {       /* loop thru ops */
    j = IC_GETCL (opc_val[i + 1]);                      /* get class */
    if (opc_val[i] == (inst & masks[j])) {              /* match? */
        uint32 fl = opc_val[i + 1];                     /* get format */
        uint32 aw = IC_GETAW (fl);
        uint32 ap = IC_GETAP (fl);
        uint32 xr = IC_GETXR (fl);
        uint32 rn = I_GETRN (inst);                     /* get fields */
        uint32 xn = I_GETXR (inst);
        uint32 mask = (1u << aw) - 1;
        uint32 ad = (inst >> ap) & mask;

        fprintf (of, "%s", opcode[i >> 1]);             /* opcode */
        if (fl & IC_RN)                                 /* rn? */
            fprintf (of, ",%d", rn);
        if (TST_IND (inst) || aw || xr) {               /* anything else? */
            fputs (TST_IND (inst)? " *": " ", of);      /* space{*} */
            if (aw) {                                   /* any value? */
                if ((fl & IC_SGN) &&                    /* signed and */
                    (ad & (1u << (aw - 1))))            /* negative? */
                    fprintf (of, "-%X", (mask + 1) - ad);
                else fprintf (of, "%X", ad);
                if ((xr == IC_XR) && xn)                /* any index? */
                    fprintf (of, ",%d", xn);
                else if (xr == IC_CTL)                  /* or control? */
                    fprintf (of, ",%X", rn);
                }
            } 
        return SCPE_OK;
        }
    }
return SCPE_ARG;
}

void fprint_ebcdic (FILE *of, uint32 c)
{
uint32 cv = ebcdic_to_ascii[c];
if ((cv < 0040) || (cv >= 0177))
    fprintf (of, "<%02X>", c);
else fputc (cv, of);
return;
}

/* Symbolic input

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        uptr    =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       error status
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
t_value num;
uint32 i, sc, rdx, c;
t_stat r;
DEVICE *dptr;

if (uptr == NULL)                                       /* anon = CPU */
    uptr = &cpu_unit;
else if (uptr != &cpu_unit)                             /* CPU only */
    return SCPE_ARG;
dptr = find_dev_from_unit (uptr);                       /* find dev */
if (dptr == NULL)
    return SCPE_IERR;
if (sw & SWMASK ('D'))                                  /* get radix */
    rdx = 10;
else if (sw & SWMASK ('O'))
    rdx = 8;
else if (sw & SWMASK ('X'))
    rdx = 16;
else rdx = dptr->dradix;

if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* chars? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    for (i = 0; i < 4; i++) {
        if (cptr[i] == 0)
            break;
        sc = 24 - (i * 8);
        c = (sw & SWMASK ('A'))?
             cptr[i] & 0x7F:
             ascii_to_ebcdic[cptr[i] & 0177];
        val[0] = (val[0] & ~(BMASK << sc)) | (c << sc);
        }
    return 0;
    }
if ((sw & SWMASK ('A')) || ((*cptr == '#') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    sc = 24 - (addr & 0x3) * 8;                         /* shift count */
    val[0] = (val[0] & ~(BMASK << sc)) | (cptr[0] << sc);
    return 0;
    }
if ((sw & SWMASK ('E')) || ((*cptr == '\'') && cptr++)) { /* EBCDIC char? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    sc = 24 - (addr & 0x3) * 8;                         /* shift count */
    val[0] = (val[0] & ~(BMASK << sc)) | (ascii_to_ebcdic[cptr[0] & 0177] << sc);
    return 0;
    }
if (sw & SWMASK ('B')) {                                /* byte? */
    num = get_uint (cptr, rdx, BMASK, &r);              /* get byte */
    if (r != SCPE_OK)
        return SCPE_ARG;
    sc = 24 - (addr & 0x3) * 8;                         /* shift count */
    val[0] = (val[0] & ~(BMASK << sc)) | (num << sc);
    return 0;
    }
if (sw & SWMASK ('H')) {                                /* halfword? */
    num = get_uint (cptr, rdx, HMASK, &r);              /* get half word */
    if (r != SCPE_OK)
        return SCPE_ARG;
    sc = addr & 1? 0: 16;
    val[0] = (val[0] & ~(HMASK << sc)) | (num << sc);
    return 0;
    }
if (!parse_sym_m (cptr, val))
    return 0;

val[0] = get_uint (cptr, rdx, WMASK, &r);               /* get number */
if (r != SCPE_OK)
    return r;
return 0;
}

t_stat parse_sym_m (CONST char *cptr, t_value *val)
{
uint32 i, sgn;
t_stat r;
char *sep;
char gbuf[CBUFSIZE];

cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode+reg*/
if ((sep = strchr (gbuf, ',')))                         /* , in middle? */
    *sep++ = 0;                                         /* split strings */
for (i = 0; opcode[i] != NULL; i++) {                   /* loop thru ops */
    if (strcmp (opcode[i], gbuf) == 0) {                /* string match? */
        uint32 rn, xn, ad;
        uint32 k = i << 1;                              /* index to opval */
        uint32 fl = opc_val[k + 1];
        uint32 aw = IC_GETAW (fl);
        uint32 ap = IC_GETAP (fl);
        uint32 xr = IC_GETXR (fl);
        uint32 mask = (1u << aw) - 1;

        val[0] = opc_val[k];
        if (fl & IC_RN) {                               /* need rn? */
            if (sep == NULL)
                return SCPE_ARG;
            rn = get_uint (sep, 10, INST_M_RN, &r);
            if (r != SCPE_OK)
                return SCPE_ARG;
            val[0] |= rn << INST_V_RN;
            }
        else if (sep)                                   /* rn & not wanted */
            return SCPE_ARG;
        if (aw) {                                       /* more? */
            if (*cptr == 0)
                return (fl & IC_AOP)? SCPE_OK: SCPE_ARG;
            if ((fl & IC_IND) && (*cptr == '*')) {      /* indirect? */
                val[0] |= INST_IND;
                cptr++;
                }
            if ((fl & IC_SGN) &&                        /* signed val? */
                strchr ("+-", *cptr) &&                 /* with sign? */
                (*cptr++ == '-'))                       /* and minus? */ 
                sgn = 1;
            else sgn = 0;                               /* else + */
            cptr = get_glyph (cptr, gbuf, 0);           /* get rest */
            if ((sep = strchr (gbuf, ',')))             /* , in middle? */
                *sep++ = 0;                             /* split strings */
            ad = get_uint (gbuf, 16, mask, &r);
            if (r != SCPE_OK)
                return r;
            if (sgn && ad)                              /* negative, nz? */
                ad = (mask + 1) - ad;                   /* complement */
            val[0] |= (ad << ap);
            if ((xr == IC_XR) && sep) {                 /* index? */
                xn = get_uint (sep, 10, 7, &r);
                if (r != SCPE_OK)
                    return r;
                val[0] |= (xn << INST_V_XR);
                }
            else if (xr == IC_CTL) {                    /* control? */
                if (sep == NULL)
                    return SCPE_ARG;
                xn = get_uint (gbuf, 16, INST_M_RN, &r);
                if (r != SCPE_OK)
                    return r;
                val[0] |= (xn << INST_V_RN);
                }
            else if (sep)
                return SCPE_ARG;
            }
        if (*cptr != 0)
            return SCPE_ARG;
        return SCPE_OK;
        }
    }
return SCPE_ARG;
}
