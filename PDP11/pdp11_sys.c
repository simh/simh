/* pdp11_sys.c: PDP-11 simulator interface

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
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   02-Sep-13    RMS     Added third Massbus, RS03/RS04
   29-Apr-12    RMS     Fixed compiler warning (Mark Pizzolato)
   19-Nov-08    RMS     Moved I/O support routines to I/O library
   15-May-08    RMS     Added KE11-A, DC11 support
                        Renamed DL11
   04-Feb-08    RMS     Modified to allow -A, -B use with 8b devices
   25-Jan-08    RMS     Added RC11, KG11A support from John Dundas
   10-Sep-07    RMS     Cleaned up binary loader
   20-Dec-06    RMS     Added TA11 support
   12-Nov-06    RMS     Fixed operand order in EIS instructions (W.F.J. Mueller)
   14-Jul-06    RMS     Reordered device list
   06-Jul-06    RMS     Added multiple KL11/DL11 support
   26-Jun-06    RMS     Added RF11 support
   17-May-06    RMS     Added CR11/CD11 support (John Dundas)
   16-Aug-05    RMS     Fixed C++ declaration and cast problems
   22-Jul-05    RMS     Fixed missing , in initializer (Doug Gwyn)
   22-Dec-03    RMS     Added second DEUNA/DELUA support
   18-Oct-03    RMS     Added DECtape off reel message
   14-Sep-03    PLB     Added VT11 support
   06-May-03    RMS     Added support for second DEQNA/DELQA
   09-Jan-03    RMS     Added DELUA/DEUNA support
   17-Oct-02    RMS     Fixed bugs in branch, SOB address parsing
   09-Oct-02    RMS     Added DELQA support
   12-Sep-02    RMS     Added TMSCP, KW11P, RX211 support, RAD50 examine
   29-Nov-01    RMS     Added read only unit support
   17-Sep-01    RMS     Removed multiconsole support
   26-Aug-01    RMS     Added DZ11
   20-Aug-01    RMS     Updated bad block inquiry
   17-Jul-01    RMS     Fixed warning from VC++ 6.0
   27-May-01    RMS     Added multiconsole support
   05-Apr-01    RMS     Added support for TS11/TSV05
   14-Mar-01    RMS     Revised load/dump interface (again)
   11-Feb-01    RMS     Added DECtape support
   30-Oct-00    RMS     Added support for examine to file
   14-Apr-99    RMS     Changed t_addr to unsigned
   09-Nov-98    RMS     Fixed assignments of ROR/ROL (John Wilson)
   27-Oct-98    RMS     V2.4 load interface
   08-Oct-98    RMS     Fixed bug in bad block routine
   30-Mar-98    RMS     Fixed bug in floating point display
   12-Nov-97    RMS     Added bad block table routine
*/

#include "pdp11_defs.h"
#include <ctype.h>

extern DEVICE cpu_dev;
extern DEVICE sys_dev;
extern DEVICE ptr_dev;
extern DEVICE ptp_dev;
extern DEVICE tti_dev;
extern DEVICE tto_dev;
extern DEVICE lpt_dev;
extern DEVICE cr_dev;
extern DEVICE clk_dev;
extern DEVICE pclk_dev;
extern DEVICE dli_dev;
extern DEVICE dlo_dev;
extern DEVICE dci_dev;
extern DEVICE dco_dev;
extern DEVICE dz_dev;
extern DEVICE vh_dev;
extern DEVICE dt_dev;
extern DEVICE rc_dev;
extern DEVICE rf_dev;
extern DEVICE rk_dev;
extern DEVICE rl_dev;
extern DEVICE hk_dev;
extern DEVICE rx_dev;
extern DEVICE ry_dev;
extern DEVICE mba_dev[];
extern DEVICE rp_dev;
extern DEVICE rs_dev;
extern DEVICE rq_dev, rqb_dev, rqc_dev, rqd_dev;
extern DEVICE tm_dev;
extern DEVICE tq_dev;
extern DEVICE ts_dev;
extern DEVICE tu_dev;
extern DEVICE ta_dev;
#ifdef USE_DISPLAY
extern DEVICE vt_dev;
#endif
extern DEVICE xq_dev, xqb_dev;
extern DEVICE xu_dev, xub_dev;
extern DEVICE ke_dev;
extern DEVICE kg_dev;
extern DEVICE dmc_dev;
extern DEVICE dup_dev;
extern DEVICE dpv_dev;
extern DEVICE kmc_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];
extern uint16 *M;
extern int32 saved_PC;

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "PDP-11";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 4;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &sys_dev,
    &mba_dev[0],
    &mba_dev[1],
    &mba_dev[2],
    &clk_dev,
    &pclk_dev,
    &ptr_dev,
    &ptp_dev,
    &tti_dev,
    &tto_dev,
    &cr_dev,
    &lpt_dev,
    &dli_dev,
    &dlo_dev,
    &dci_dev,
    &dco_dev,
    &dz_dev,
    &vh_dev,
    &rc_dev,
    &rf_dev,
    &rk_dev,
    &rl_dev,
    &hk_dev,
    &rx_dev,
    &ry_dev,
    &rp_dev,
    &rs_dev,
    &rq_dev,
    &rqb_dev,
    &rqc_dev,
    &rqd_dev,
    &dt_dev,
    &tm_dev,
    &ts_dev,
    &tq_dev,
    &tu_dev,
    &ta_dev,
#ifdef USE_DISPLAY
    &vt_dev,
#endif
    &xq_dev,
    &xqb_dev,
    &xu_dev,
    &xub_dev,
    &ke_dev,
    &kg_dev,
    &dmc_dev,
    &dup_dev,
    &dpv_dev,
    &kmc_dev,
    NULL
    };

const char *sim_stop_messages[] = {
    "Unknown error",
    "Red stack trap",
    "Odd address trap",
    "Memory management trap",
    "Non-existent memory trap",
    "Parity error trap",
    "Privilege trap",
    "Illegal instruction trap",
    "BPT trap",
    "IOT trap",
    "EMT trap",
    "TRAP trap",
    "Trace trap",
    "Yellow stack trap",
    "Powerfail trap",
    "Floating point exception",
    "HALT instruction",
    "Breakpoint",
    "Wait state",
    "Trap vector fetch abort",
    "Trap stack push abort",
    "RQDX3 consistency error",
    "Sanity timer expired",
    "DECtape off reel"
    };

/* Binary loader.

   Loader format consists of blocks, optionally preceded, separated, and
   followed by zeroes.  Each block consists of:

        001             ---
        xxx              |
        lo_count         |
        hi_count         |
        lo_origin        > count bytes
        hi_origin        |
        data byte        |
        :                |
        data byte       ---
        checksum

   If the byte count is exactly six, the block is the last on the tape, and
   there is no checksum.  If the origin is not 000001, then the origin is
   the PC at which to start the program.
*/

t_stat sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
int32 c[6], d, i, cnt, csum;
uint32 org;

if ((*cptr != 0) || (flag != 0))
    return SCPE_ARG;
do {                                                    /* block loop */
    csum = 0;                                           /* init checksum */
    for (i = 0; i < 6; ) {                              /* 6 char header */
        if ((c[i] = getc (fileref)) == EOF)
            return SCPE_FMT;
        if ((i != 0) || (c[i] == 1))                    /* 1st must be 1 */
            csum = csum + c[i++];                       /* add into csum */
        }
    cnt = (c[3] << 8) | c[2];                           /* count */
    org = (c[5] << 8) | c[4];                           /* origin */
    if (cnt < 6)                                        /* invalid? */
        return SCPE_FMT;
    if (cnt == 6) {                                     /* end block? */
        if (org != 1)                                   /* set PC? */
            saved_PC = org & 0177776;
        return SCPE_OK;
        }
    for (i = 6; i < cnt; i++) {                         /* exclude hdr */
        if ((d = getc (fileref)) == EOF)                /* data char */
            return SCPE_FMT;
        csum = csum + d;                                /* add into csum */
        if (org >= MEMSIZE)                             /* invalid addr? */
            return SCPE_NXM;
        M[org >> 1] = (org & 1)?                        /* store data */
            (M[org >> 1] & 0377) | (d << 8):
            (M[org >> 1] & 0177400) | d;
        org = (org + 1) & 0177777;                      /* inc origin */
        }
    if ((d = getc (fileref)) == EOF)                    /* get csum */
        return SCPE_FMT;
    csum = csum + d;                                    /* add in */
    } while ((csum & 0377) == 0);                       /* result mbz */
return SCPE_CSUM;
}

/* Symbol tables */

#define I_V_L           16                              /* long mode */
#define I_V_D           17                              /* double mode */
#define I_L             (1 << I_V_L)
#define I_D             (1 << I_V_D)

/* Warning: for literals, the class number MUST equal the field width!! */

#define I_V_CL          18                              /* class bits */
#define I_M_CL          037                             /* class mask */
#define I_V_NPN         0                               /* no operands */
#define I_V_REG         1                               /* reg */
#define I_V_SOP         2                               /* operand */
#define I_V_3B          3                               /* 3b literal */
#define I_V_FOP         4                               /* flt operand */
#define I_V_AFOP        5                               /* fac, flt operand */
#define I_V_6B          6                               /* 6b literal */
#define I_V_BR          7                               /* cond branch */
#define I_V_8B          8                               /* 8b literal */
#define I_V_SOB         9                               /* reg, disp */
#define I_V_RSOP        10                              /* reg, operand */
#define I_V_ASOP        11                              /* fac, operand */
#define I_V_ASMD        12                              /* fac, moded int op */
#define I_V_DOP         13                              /* double operand */
#define I_V_CCC         14                              /* CC clear */
#define I_V_CCS         15                              /* CC set */
#define I_V_SOPR        16                              /* operand, reg */
#define I_NPN           (I_V_NPN << I_V_CL)
#define I_REG           (I_V_REG << I_V_CL)
#define I_3B            (I_V_3B << I_V_CL)
#define I_SOP           (I_V_SOP << I_V_CL)
#define I_FOP           (I_V_FOP << I_V_CL)
#define I_6B            (I_V_6B << I_V_CL)
#define I_BR            (I_V_BR << I_V_CL)
#define I_8B            (I_V_8B << I_V_CL)
#define I_AFOP          (I_V_AFOP << I_V_CL)
#define I_ASOP          (I_V_ASOP << I_V_CL)
#define I_RSOP          (I_V_RSOP << I_V_CL)
#define I_SOB           (I_V_SOB << I_V_CL)
#define I_ASMD          (I_V_ASMD << I_V_CL)
#define I_DOP           (I_V_DOP << I_V_CL)
#define I_CCC           (I_V_CCC << I_V_CL)
#define I_CCS           (I_V_CCS << I_V_CL)
#define I_SOPR          (I_V_SOPR << I_V_CL)

static const int32 masks[] = {
0177777, 0177770, 0177700, 0177770,
0177700+I_D, 0177400+I_D, 0177700, 0177400,
0177400, 0177000, 0177000, 0177400,
0177400+I_D+I_L, 0170000, 0177777, 0177777,
0177000
};

static const char *opcode[] = {
"HALT","WAIT","RTI","BPT",
"IOT","RESET","RTT","MFPT",
"JMP","RTS","SPL",
"NOP","CLC","CLV","CLV CLC",
"CLZ","CLZ CLC","CLZ CLV","CLZ CLV CLC",
"CLN","CLN CLC","CLN CLV","CLN CLV CLC",
"CLN CLZ","CLN CLZ CLC","CLN CLZ CLC","CCC",
"NOP","SEC","SEV","SEV SEC",
"SEZ","SEZ SEC","SEZ SEV","SEZ SEV SEC",
"SEN","SEN SEC","SEN SEV","SEN SEV SEC",
"SEN SEZ","SEN SEZ SEC","SEN SEZ SEC","SCC",
"SWAB","BR","BNE","BEQ",
"BGE","BLT","BGT","BLE",
"JSR",
"CLR","COM","INC","DEC",
"NEG","ADC","SBC","TST",
"ROR","ROL","ASR","ASL",
"MARK","MFPI","MTPI","SXT",
"CSM",        "TSTSET","WRTLCK",
"MOV","CMP","BIT","BIC",
"BIS","ADD",
"MUL","DIV","ASH","ASHC",
"XOR", 
"FADD","FSUB","FMUL","FDIV",
"L2DR",
"MOVC","MOVRC","MOVTC",
"LOCC","SKPC","SCANC","SPANC",
"CMPC","MATC",
"ADDN","SUBN","CMPN","CVTNL",
"CVTPN","CVTNP","ASHN","CVTLN",
"L3DR",
"ADDP","SUBP","CMPP","CVTPL",
"MULP","DIVP","ASHP","CVTLP",
"MOVCI","MOVRCI","MOVTCI",
"LOCCI","SKPCI","SCANCI","SPANCI",
"CMPCI","MATCI",
"ADDNI","SUBNI","CMPNI","CVTNLI",
"CVTPNI","CVTNPI","ASHNI","CVTLNI",
"ADDPI","SUBPI","CMPPI","CVTPLI",
"MULPI","DIVPI","ASHPI","CVTLPI",
"SOB",
"BPL","BMI","BHI","BLOS",
"BVC","BVS","BCC","BCS",
"BHIS","BLO",                                           /* encode only */
"EMT","TRAP",
"CLRB","COMB","INCB","DECB",
"NEGB","ADCB","SBCB","TSTB",
"RORB","ROLB","ASRB","ASLB",
"MTPS","MFPD","MTPD","MFPS",
"MOVB","CMPB","BITB","BICB",
"BISB","SUB",
"CFCC","SETF","SETI","SETD","SETL",
"LDFPS","STFPS","STST",
"CLRF","CLRD","TSTF","TSTD",
"ABSF","ABSD","NEGF","NEGD",
"MULF","MULD","MODF","MODD",
"ADDF","ADDD","LDF","LDD",
"SUBF","SUBD","CMPF","CMPD",
"STF","STD","DIVF","DIVD",
"STEXP",
"STCFI","STCDI","STCFL","STCDL",
"STCFD","STCDF",
"LDEXP",
"LDCIF","LDCID","LDCLF","LDCLD",
"LDCFD","LDCDF",
NULL
};

static const int32 opc_val[] = {
0000000+I_NPN, 0000001+I_NPN, 0000002+I_NPN, 0000003+I_NPN,
0000004+I_NPN, 0000005+I_NPN, 0000006+I_NPN, 0000007+I_NPN,
0000100+I_SOP, 0000200+I_REG, 0000230+I_3B,
0000240+I_CCC, 0000241+I_CCC, 0000242+I_CCC, 0000243+I_NPN, 
0000244+I_CCC, 0000245+I_NPN, 0000246+I_NPN, 0000247+I_NPN, 
0000250+I_CCC, 0000251+I_NPN, 0000252+I_NPN, 0000253+I_NPN, 
0000254+I_NPN, 0000255+I_NPN, 0000256+I_NPN, 0000257+I_CCC, 
0000260+I_CCS, 0000261+I_CCS, 0000262+I_CCS, 0000263+I_NPN, 
0000264+I_CCS, 0000265+I_NPN, 0000266+I_NPN, 0000267+I_NPN, 
0000270+I_CCS, 0000271+I_NPN, 0000272+I_NPN, 0000273+I_NPN, 
0000274+I_NPN, 0000275+I_NPN, 0000276+I_NPN, 0000277+I_CCS, 
0000300+I_SOP, 0000400+I_BR, 0001000+I_BR, 0001400+I_BR,
0002000+I_BR, 0002400+I_BR, 0003000+I_BR, 0003400+I_BR,
0004000+I_RSOP,
0005000+I_SOP, 0005100+I_SOP, 0005200+I_SOP, 0005300+I_SOP,
0005400+I_SOP, 0005500+I_SOP, 0005600+I_SOP, 0005700+I_SOP,
0006000+I_SOP, 0006100+I_SOP, 0006200+I_SOP, 0006300+I_SOP,
0006400+I_6B, 0006500+I_SOP, 0006600+I_SOP, 0006700+I_SOP,
0007000+I_SOP,                0007200+I_SOP, 0007300+I_SOP,
0010000+I_DOP, 0020000+I_DOP, 0030000+I_DOP, 0040000+I_DOP,
0050000+I_DOP, 0060000+I_DOP,
0070000+I_SOPR, 0071000+I_SOPR, 0072000+I_SOPR, 0073000+I_SOPR,
0074000+I_RSOP,
0075000+I_REG, 0075010+I_REG, 0075020+I_REG, 0075030+I_REG,
0076020+I_REG,
0076030+I_NPN, 0076031+I_NPN, 0076032+I_NPN,
0076040+I_NPN, 0076041+I_NPN, 0076042+I_NPN, 0076043+I_NPN,
0076044+I_NPN, 0076045+I_NPN, 
0076050+I_NPN, 0076051+I_NPN, 0076052+I_NPN, 0076053+I_NPN,
0076054+I_NPN, 0076055+I_NPN, 0076056+I_NPN, 0076057+I_NPN,
0076060+I_REG,
0076070+I_NPN, 0076071+I_NPN, 0076072+I_NPN, 0076073+I_NPN,
0076074+I_NPN, 0076075+I_NPN, 0076076+I_NPN, 0076077+I_NPN,
0076130+I_NPN, 0076131+I_NPN, 0076132+I_NPN,
0076140+I_NPN, 0076141+I_NPN, 0076142+I_NPN, 0076143+I_NPN,
0076144+I_NPN, 0076145+I_NPN, 
0076150+I_NPN, 0076151+I_NPN, 0076152+I_NPN, 0076153+I_NPN,
0076154+I_NPN, 0076155+I_NPN, 0076156+I_NPN, 0076157+I_NPN,
0076170+I_NPN, 0076171+I_NPN, 0076172+I_NPN, 0076173+I_NPN,
0076174+I_NPN, 0076175+I_NPN, 0076176+I_NPN, 0076177+I_NPN,
0077000+I_SOB,
0100000+I_BR, 0100400+I_BR, 0101000+I_BR, 0101400+I_BR,
0102000+I_BR, 0102400+I_BR, 0103000+I_BR, 0103400+I_BR,
0103000+I_BR, 0103400+I_BR,
0104000+I_8B, 0104400+I_8B,
0105000+I_SOP, 0105100+I_SOP, 0105200+I_SOP, 0105300+I_SOP,
0105400+I_SOP, 0105500+I_SOP, 0105600+I_SOP, 0105700+I_SOP,
0106000+I_SOP, 0106100+I_SOP, 0106200+I_SOP, 0106300+I_SOP,
0106400+I_SOP, 0106500+I_SOP, 0106600+I_SOP, 0106700+I_SOP,
0110000+I_DOP, 0120000+I_DOP, 0130000+I_DOP, 0140000+I_DOP,
0150000+I_DOP, 0160000+I_DOP,
0170000+I_NPN, 0170001+I_NPN, 0170002+I_NPN, 0170011+I_NPN, 0170012+I_NPN,
0170100+I_SOP, 0170200+I_SOP, 0170300+I_SOP,
0170400+I_FOP, 0170400+I_FOP+I_D, 0170500+I_FOP, 0170500+I_FOP+I_D,
0170600+I_FOP, 0170600+I_FOP+I_D, 0170700+I_FOP, 0170700+I_FOP+I_D,
0171000+I_AFOP, 0171000+I_AFOP+I_D, 0171400+I_AFOP, 0171400+I_AFOP+I_D,
0172000+I_AFOP, 0172000+I_AFOP+I_D, 0172400+I_AFOP, 0172400+I_AFOP+I_D, 
0173000+I_AFOP, 0173000+I_AFOP+I_D, 0173400+I_AFOP, 0173400+I_AFOP+I_D,
0174000+I_AFOP, 0174000+I_AFOP+I_D, 0174400+I_AFOP, 0174400+I_AFOP+I_D,
0175000+I_ASOP,
0175400+I_ASMD, 0175400+I_ASMD+I_D, 0175400+I_ASMD+I_L, 0175400+I_ASMD+I_D+I_L, 
0176000+I_AFOP, 0176000+I_AFOP+I_D,
0176400+I_ASOP, 
0177000+I_ASMD, 0177000+I_ASMD+I_D, 0177000+I_ASMD+I_L, 0177000+I_ASMD+I_D+I_L, 
0177400+I_AFOP, 0177400+I_AFOP+I_D,
-1
};

static const char *rname [] = {
 "R0", "R1", "R2", "R3", "R4", "R5", "SP", "PC"
 };

static const char *fname [] = {
 "F0", "F1", "F2", "F3", "F4", "F5", "?6", "?7"
 };

static const char r50_to_asc[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$._0123456789";

/* Specifier decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        spec    =       specifier
        nval    =       next word
        flag    =       TRUE if decoding for CPU
        iflag   =       TRUE if decoding integer instruction
   Outputs:
        count   =       -number of extra words retired
*/

int32 fprint_spec (FILE *of, t_addr addr, int32 spec, t_value nval,
    int32 flag, int32 iflag)
{
int32 reg, mode;
static const int32 rgwd[8] = { 0, 0, 0, 0, 0, 0, -1, -1 };
static const int32 pcwd[8] = { 0, 0, -1, -1, 0, 0, -1, -1 };

reg = spec & 07;
mode = ((spec >> 3) & 07);
switch (mode) {

    case 0:
        if (iflag)
            fprintf (of, "%s", rname[reg]);
        else fprintf (of, "%s", fname[reg]);
        break;

    case 1:
        fprintf (of, "(%s)", rname[reg]);
        break;

    case 2:
        if (reg != 7)
            fprintf (of, "(%s)+", rname[reg]);
        else fprintf (of, "#%-o", nval);
        break;

    case 3:
        if (reg != 7)
            fprintf (of, "@(%s)+", rname[reg]);
        else fprintf (of, "@#%-o", nval);
        break;

    case 4:
        fprintf (of, "-(%s)", rname[reg]);
        break;

    case 5:
        fprintf (of, "@-(%s)", rname[reg]);
        break;

    case 6:
        if ((reg != 7) || !flag)
            fprintf (of, "%-o(%s)", nval, rname[reg]);
        else fprintf (of, "%-o", (nval + addr + 4) & 0177777);
        break;

    case 7:
        if ((reg != 7) || !flag)
            fprintf (of, "@%-o(%s)", nval, rname[reg]);
        else fprintf (of, "@%-o", (nval + addr + 4) & 0177777);
        break;
        }                                               /* end case */

return ((reg == 07)? pcwd[mode]: rgwd[mode]);
}

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       values to decode
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        return  =       if >= 0, error code
                        if < 0, number of extra words retired
*/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 cflag, i, j, c1, c2, c3, inst, fac, srcm, srcr, dstm, dstr;
int32 bflag, l8b, brdisp, wd1, wd2;
extern int32 FPS;

bflag = 0;                                              /* assume 16b */
cflag = (uptr == NULL) || (uptr == &cpu_unit);          /* cpu? */
if (!cflag) {                                           /* not cpu? */
    DEVICE *dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    if (dptr->dwidth < 16)
        bflag = 1;
    }

if (sw & SWMASK ('A')) {                                /* ASCII? */
    if (bflag)
        c1 = val[0] & 0177;
    else c1 = (val[0] >> ((addr & 1)? 8: 0)) & 0177;
    fprintf (of, (c1 < 040)? "<%03o>": "%c", c1);
    return 0;
    }
if (sw & SWMASK ('B')) {                                /* byte? */
    if (bflag)
        c1 = val[0] & 0177;
    else c1 = (val[0] >> ((addr & 1)? 8: 0)) & 0377;
    fprintf (of, "%o", c1);
    return 0;
    }
if (bflag)                                              /* 16b only */
    return SCPE_ARG;

if (sw & SWMASK ('C')) {                                /* character? */
    c1 = val[0] & 0177;
    c2 = (val[0] >> 8) & 0177;
    fprintf (of, (c1 < 040)? "<%03o>": "%c", c1);
    fprintf (of, (c2 < 040)? "<%03o>": "%c", c2);
    return -1;
    }
if (sw & SWMASK ('R')) {                                /* radix 50? */
    if (val[0] > 0174777)                               /* max value */
        return SCPE_ARG;
    c3 = val[0] % 050;
    c2 = (val[0] / 050) % 050;
    c1 = val[0] / (050 * 050);
    fprintf (of, "%c%c%c", r50_to_asc[c1],
            r50_to_asc[c2], r50_to_asc[c3]);
    return -1;
    }
if (!(sw & SWMASK ('M')))
    return SCPE_ARG;

inst = val[0] | ((FPS << (I_V_L - FPS_V_L)) & I_L) |
    ((FPS << (I_V_D - FPS_V_D)) & I_D);                 /* inst + fp mode */
for (i = 0; opc_val[i] >= 0; i++) {                     /* loop thru ops */
    j = (opc_val[i] >> I_V_CL) & I_M_CL;                /* get class */
    if ((opc_val[i] & 0777777) == (inst & masks[j])) {  /* match? */
        srcm = (inst >> 6) & 077;                       /* opr fields */
        srcr = srcm & 07;
        fac = srcm & 03;
        dstm = inst & 077;
        dstr = dstm & 07;
        l8b = inst & 0377;
        wd1 = wd2 = 0;
        switch (j) {                                    /* case on class */

        case I_V_NPN: case I_V_CCC: case I_V_CCS:       /* no operands */
            fprintf (of, "%s", opcode[i]);
            break;

        case I_V_REG:                                   /* reg */
            fprintf (of, "%s %-s", opcode[i], rname[dstr]);
            break;

        case I_V_SOP:                                   /* sop */
            fprintf (of, "%s ", opcode[i]);
            wd1 = fprint_spec (of, addr, dstm, val[1], cflag, TRUE);
            break;

        case I_V_3B:                                    /* 3b */
            fprintf (of, "%s %-o", opcode[i], dstr);
            break;

        case I_V_FOP:                                   /* fop */
            fprintf (of, "%s ", opcode[i]);
            wd1 = fprint_spec (of, addr, dstm, val[1], cflag, FALSE);
            break;

        case I_V_AFOP:                                  /* afop */
            fprintf (of, "%s %s,", opcode[i], fname[fac]);
            wd1 = fprint_spec (of, addr, dstm, val[1], cflag, FALSE);
            break;

        case I_V_6B:                                    /* 6b */
            fprintf (of, "%s %-o", opcode[i], dstm);
            break;

        case I_V_BR:                                    /* cond branch */
            fprintf (of, "%s ", opcode[i]);
            brdisp = (l8b + l8b + ((l8b & 0200)? 0177002: 2)) & 0177777;
            if (cflag)
                fprintf (of, "%-o", (addr + brdisp) & 0177777);
            else if (brdisp < 01000)
                fprintf (of, ".+%-o", brdisp);
            else fprintf (of, ".-%-o", 0200000 - brdisp);
            break;

        case I_V_8B:                                    /* 8b */
            fprintf (of, "%s %-o", opcode[i], l8b);
            break;

        case I_V_SOB:                                   /* sob */
            fprintf (of, "%s %s,", opcode[i], rname[srcr]);
            brdisp = (dstm * 2) - 2;
            if (cflag)
                fprintf (of, "%-o", (addr - brdisp) & 0177777);
            else if (brdisp <= 0)
                fprintf (of, ".+%-o", -brdisp);
            else fprintf (of, ".-%-o", brdisp);
            break;

        case I_V_RSOP:                                  /* rsop */
            fprintf (of, "%s %s,", opcode[i], rname[srcr]);
            wd1 = fprint_spec (of, addr, dstm, val[1], cflag, TRUE);
            break;

        case I_V_SOPR:                                  /* sopr */
            fprintf (of, "%s ", opcode[i]);
            wd1 = fprint_spec (of, addr, dstm, val[1], cflag, TRUE);
            fprintf (of, ",%s", rname[srcr]);
            break;

        case I_V_ASOP: case I_V_ASMD:                   /* asop, asmd */
            fprintf (of, "%s %s,", opcode[i], fname[fac]);
            wd1 = fprint_spec (of, addr, dstm, val[1], cflag, TRUE);
            break;

        case I_V_DOP:                                   /* dop */
            fprintf (of, "%s ", opcode[i]);
            wd1 = fprint_spec (of, addr, srcm, val[1], cflag, TRUE);
            fprintf (of, ",");
            wd2 = fprint_spec (of, addr - wd1 - wd1, dstm,
                val[1 - wd1], cflag, TRUE);
            break;
            }                                           /* end case */
        return ((wd1 + wd2) * 2) - 1;
        }                                               /* end if */
    }                                                   /* end for */
return SCPE_ARG;                                        /* no match */
}

#define A_PND   100                                     /* # seen */
#define A_MIN   040                                     /* -( seen */
#define A_PAR   020                                     /* (Rn) seen */
#define A_REG   010                                     /* Rn seen */
#define A_PLS   004                                     /* + seen */
#define A_NUM   002                                     /* number seen */
#define A_REL   001                                     /* relative addr seen */

/* Register number

   Inputs:
        *cptr   =       pointer to input string
        *strings =      pointer to register names
        mchar   =       character to match after register name
   Outputs:
        rnum    =       0..7 if a legitimate register
                        < 0 if error
*/

int32 get_reg (char *cptr, const char *strings[], char mchar)
{
int32 i;

if (*(cptr + 2) != mchar)
    return -1;
for (i = 0; i < 8; i++) {
    if (strncmp (cptr, strings[i], 2) == 0)
        return i;
    }
return -1;
}

/* Number or memory address

   Inputs:
        *cptr   =       pointer to input string
        *dptr   =       pointer to output displacement
        *pflag  =       pointer to accumulating flags
   Outputs:
        cptr    =       pointer to next character in input string
                        NULL if parsing error

   Flags: 0 (no result), A_NUM (number), A_REL (relative)
*/

char *get_addr (char *cptr, int32 *dptr, int32 *pflag)
{
int32 val, minus;
char *tptr;

minus = 0;

if (*cptr == '.') {                                     /* relative? */
    *pflag = *pflag | A_REL;
    cptr++;
    }
if (*cptr == '+') {                                     /* +? */
    *pflag = *pflag | A_NUM;
    cptr++;
    }
if (*cptr == '-') {                                     /* -? */
    *pflag = *pflag | A_NUM;
    minus = 1;
    cptr++;
    }
errno = 0;
val = strtoul (cptr, &tptr, 8);
if (cptr == tptr) {                                     /* no number? */
    if (*pflag == (A_REL + A_NUM))                      /* .+, .-? */
        return NULL;
    *dptr = 0;
    return cptr;
    }
if (errno || (*pflag == A_REL))                         /* .n? */
    return NULL;
*dptr = (minus? -val: val) & 0177777;
*pflag = *pflag | A_NUM;
return tptr;
}

/* Specifier decode

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        n1      =       0 if no extra word used
                        -1 if extra word used in prior decode
        *sptr   =       pointer to output specifier
        *dptr   =       pointer to output displacement
        cflag   =       true if parsing for the CPU
        iflag   =       true if integer specifier
   Outputs:
        status  =       = -1 extra word decoded
                        =  0 ok
                        = +1 error
*/

t_stat get_spec (char *cptr, t_addr addr, int32 n1, int32 *sptr, t_value *dptr,
    int32 cflag, int32 iflag)
{
int32 reg, indir, pflag, disp;

indir = 0;                                              /* no indirect */
pflag = 0;

if (*cptr == '@') {                                     /* indirect? */
    indir = 010;
    cptr++;
    }
if (*cptr == '#') {                                     /* literal? */
    pflag = pflag | A_PND;
    cptr++;
    }
if (strncmp (cptr, "-(", 2) == 0) {                     /* autodecrement? */
    pflag = pflag | A_MIN;
    cptr++;
    }
else if ((cptr = get_addr (cptr, &disp, &pflag)) == NULL)
    return 1;
if (*cptr == '(') {                                     /* register index? */
    pflag = pflag | A_PAR;
    if ((reg = get_reg (cptr + 1, rname, ')')) < 0)
        return 1;
    cptr = cptr + 4;
    if (*cptr == '+') {                                 /* autoincrement? */
        pflag = pflag | A_PLS;
        cptr++;
        }
    }
else if ((reg = get_reg (cptr, iflag? rname: fname, 0)) >= 0) {
    pflag = pflag | A_REG;
    cptr = cptr + 2;
    }
if (*cptr != 0)                                         /* all done? */
    return 1;
switch (pflag) {                                        /* case on syntax */

    case A_REG:                                         /* Rn, @Rn */
        *sptr = indir + reg;
        return 0;

    case A_PAR:                                         /* (Rn), @(Rn) */
        if (indir) {                                    /* @(Rn) = @0(Rn) */
            *sptr = 070 + reg;
            *dptr = 0;
            return -1;
            }
        else *sptr = 010 + reg;
        return 0;

    case A_PAR+A_PLS:                                   /* (Rn)+, @(Rn)+ */
        *sptr = 020 + indir + reg;
        return 0;

    case A_MIN+A_PAR:                                   /* -(Rn), @-(Rn) */
        *sptr = 040 + indir + reg;
        return 0;

    case A_NUM+A_PAR:                                   /* d(Rn), @d(Rn) */
        *sptr = 060 + indir + reg;
        *dptr = disp;
        return -1;

    case A_PND+A_REL: case A_PND+A_REL+A_NUM:           /* #.+n, @#.+n */
        if (!cflag)
            return 1;
        disp = (disp + addr) & 0177777;                 /* fall through */
    case A_PND+A_NUM:                                   /* #n, @#n */
        *sptr = 027 + indir;
        *dptr = disp;
        return -1;

    case A_REL: case A_REL+A_NUM:                       /* .+n, @.+n */
        *sptr = 067 + indir;
        *dptr = (disp - 4 + (2 * n1)) & 0177777;
        return -1;

    case A_NUM:                                         /* n, @n */
        if (cflag) {                                    /* CPU - use rel */
            *sptr = 067 + indir;
            *dptr = (disp - addr - 4 + (2 * n1)) & 0177777;
            }
        else {
            if (indir) return 1;                        /* other - use abs */
            *sptr = 037;
            *dptr = disp;
            }
        return -1;

    default:
        return 1;
        }                                               /* end case */
}

/* Symbolic input

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        *uptr   =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       > 0   error code
                        <= 0  -number of extra words
*/

t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 bflag, cflag, d, i, j, reg, spec, n1, n2, disp, pflag;
t_value by;
t_stat r;
char *tptr, gbuf[CBUFSIZE];

bflag = 0;                                              /* assume 16b */
cflag = (uptr == NULL) || (uptr == &cpu_unit);          /* cpu? */
if (!cflag) {                                           /* not cpu? */
    DEVICE *dptr = find_dev_from_unit (uptr);
    if (dptr == NULL)
        return SCPE_IERR;
    if (dptr->dwidth < 16)
        bflag = 1;
    }

while (isspace (*cptr)) cptr++;                         /* absorb spaces */
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    if (bflag)
        val[0] = (t_value) cptr[0];
    else val[0] = (addr & 1)?
        (val[0] & 0377) | (((t_value) cptr[0]) << 8):
        (val[0] & ~0377) | ((t_value) cptr[0]);
    return 0;
    }
if (sw & SWMASK ('B')) {                                /* byte? */
    by = get_uint (cptr, 8, 0377, &r);                  /* get byte */
    if (r != SCPE_OK)
        return SCPE_ARG;
    if (bflag)
        val[0] = by;
    else val[0] = (addr & 1)?
        (val[0] & 0377) | (by << 8):
        (val[0] & ~0377) | by;
    return 0;
    }
if (bflag)
    return SCPE_ARG;

if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* ASCII string? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    val[0] = ((t_value) cptr[1] << 8) | (t_value) cptr[0];
    return -1;
    }
if (sw & SWMASK ('R'))                                  /* radix 50 */
    return SCPE_ARG;

cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
n1 = n2 = pflag = 0;
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL)
    return SCPE_ARG;
val[0] = opc_val[i] & 0177777;                          /* get value */
j = (opc_val[i] >> I_V_CL) & I_M_CL;                    /* get class */

switch (j) {                                            /* case on class */

    case I_V_NPN:                                       /* no operand */
        break;

    case I_V_REG:                                       /* register */
        cptr = get_glyph (cptr, gbuf, 0);               /* get glyph */
        if ((reg = get_reg (gbuf, rname, 0)) < 0)
            return SCPE_ARG;
        val[0] = val[0] | reg;
        break;

    case I_V_3B: case I_V_6B: case I_V_8B:              /* xb literal */
        cptr = get_glyph (cptr, gbuf, 0);               /* get literal */
        d = get_uint (gbuf, 8, (1 << j) - 1, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        val[0] = val[0] | d;                            /* put in place */
        break;

    case I_V_BR:                                        /* cond br */
        cptr = get_glyph (cptr, gbuf, 0);               /* get address */
        tptr = get_addr (gbuf, &disp, &pflag);          /* parse */
        if ((tptr == NULL) || (*tptr != 0))
            return SCPE_ARG;
        if ((pflag & A_REL) == 0) {
            if (cflag)
                disp = (disp - addr) & 0177777;
            else return SCPE_ARG;
            }
        if ((disp & 1) || ((disp > 0400) && (disp < 0177402)))
            return SCPE_ARG;
        val[0] = val[0] | (((disp - 2) >> 1) & 0377);
        break;

    case I_V_SOB:                                       /* sob */
        cptr = get_glyph (cptr, gbuf, ',');             /* get glyph */
        if ((reg = get_reg (gbuf, rname, 0)) < 0)
            return SCPE_ARG;
        val[0] = val[0] | (reg << 6);
        cptr = get_glyph (cptr, gbuf, 0);               /* get address */
        tptr = get_addr (gbuf, &disp, &pflag);          /* parse */
        if ((tptr == NULL) || (*tptr != 0))
            return SCPE_ARG;
        if ((pflag & A_REL) == 0) {
            if (cflag)
                disp = (disp - addr) & 0177777;
            else return SCPE_ARG;
            }
        if ((disp & 1) || ((disp > 2) && (disp < 0177604)))
            return SCPE_ARG;
        val[0] = val[0] | (((2 - disp) >> 1) & 077);
        break;

    case I_V_RSOP:                                      /* reg, sop */
        cptr = get_glyph (cptr, gbuf, ',');             /* get glyph */
        if ((reg = get_reg (gbuf, rname, 0)) < 0)
            return SCPE_ARG;
        val[0] = val[0] | (reg << 6);                   /* fall through */
    case I_V_SOP:                                       /* sop */
        cptr = get_glyph (cptr, gbuf, 0);               /* get glyph */
        if ((n1 = get_spec (gbuf, addr, 0, &spec, &val[1], cflag, TRUE)) > 0)
            return SCPE_ARG;
        val[0] = val[0] | spec;
        break;

    case I_V_SOPR:                                      /* dop, reg */
        cptr = get_glyph (cptr, gbuf, ',');             /* get glyph */
        if ((n1 = get_spec (gbuf, addr, 0, &spec, &val[1], cflag, TRUE)) > 0)
            return SCPE_ARG;
        val[0] = val[0] | spec;
        cptr = get_glyph (cptr, gbuf, 0);               /* get glyph */
        if ((reg = get_reg (gbuf, rname, 0)) < 0)
            return SCPE_ARG;
        val[0] = val[0] | (reg << 6);
        break;

    case I_V_AFOP: case I_V_ASOP: case I_V_ASMD:        /* fac, (s)fop */
        cptr = get_glyph (cptr, gbuf, ',');             /* get glyph */
        if ((reg = get_reg (gbuf, fname, 0)) < 0)
            return SCPE_ARG;
        if (reg > 3)
            return SCPE_ARG;
        val[0] = val[0] | (reg << 6);                   /* fall through */
    case I_V_FOP:                                       /* fop */
        cptr = get_glyph (cptr, gbuf, 0);               /* get glyph */
        if ((n1 = get_spec (gbuf, addr, 0, &spec, &val[1], cflag, 
            (j == I_V_ASOP) || (j == I_V_ASMD))) > 0)
            return SCPE_ARG;
        val[0] = val[0] | spec;
        break;

    case I_V_DOP:                                       /* double op */
        cptr = get_glyph (cptr, gbuf, ',');             /* get glyph */
        if ((n1 = get_spec (gbuf, addr, 0, &spec, &val[1], cflag, TRUE)) > 0)
            return SCPE_ARG;
        val[0] = val[0] | (spec << 6);
        cptr = get_glyph (cptr, gbuf, 0);               /* get glyph */
        if ((n2 = get_spec (gbuf, addr, n1, &spec, &val[1 - n1],
            cflag, TRUE)) > 0)
            return SCPE_ARG;
        val[0] = val[0] | spec;
        break;

    case I_V_CCC: case I_V_CCS:                         /* cond code oper */
        for (cptr = get_glyph (cptr, gbuf, 0); gbuf[0] != 0;
            cptr = get_glyph (cptr, gbuf, 0)) {
            for (i = 0; (opcode[i] != NULL) &&
                (strcmp (opcode[i], gbuf) != 0) ; i++)
                ;
             if ((((opc_val[i] >> I_V_CL) & I_M_CL) != j) ||
                (opcode[i] == NULL))
                return SCPE_ARG;
            val[0] = val[0] | (opc_val[i] & 0177777);
            }
        break;

    default:
        return SCPE_ARG;
        }

if (*cptr != 0)                                         /* junk at end? */
    return SCPE_ARG;
return ((n1 + n2) * 2) - 1;
}
