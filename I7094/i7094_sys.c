/* i7094_sys.c: IBM 7094 simulator interface

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

   31-Dec-11    RMS     Added SPI, SPI
   16-Jul-10    RMS     Added SPUx, SPTx, SPRx
   29-Oct-06    RMS     Added additional expanded core instructions
   08-Jun-06    RMS     Added Dave Pitts' binary loader
*/

#include "i7094_defs.h"
#include <ctype.h>
#include "i7094_dat.h"

extern DEVICE cpu_dev;
extern DEVICE ch_dev[NUM_CHAN];
extern DEVICE mt_dev[NUM_CHAN];
extern DEVICE drm_dev;
extern DEVICE dsk_dev;
extern DEVICE com_dev, coml_dev;
extern DEVICE cdr_dev, cdp_dev;
extern DEVICE lpt_dev;
extern DEVICE clk_dev;
extern UNIT cpu_unit;
extern REG cpu_reg[];

uint32 cvt_code_to_ascii (uint32 c, int32 sw);
uint32 cvt_ascii_to_code (uint32 c, int32 sw);

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "IBM 7094";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;

DEVICE *sim_devices[] = { 
    &cpu_dev,
    &clk_dev,
    &ch_dev[0],
    &ch_dev[1],
    &ch_dev[2],
    &ch_dev[3],
    &ch_dev[4],
    &ch_dev[5],
    &ch_dev[6],
    &ch_dev[7],
    &mt_dev[0],
    &mt_dev[1],
    &mt_dev[2],
    &mt_dev[3],
    &mt_dev[4],
    &mt_dev[5],
    &mt_dev[6],
    &mt_dev[7],
    &cdr_dev,
    &cdp_dev,
    &lpt_dev,
    &dsk_dev,
    &drm_dev,
    &com_dev,
    &coml_dev,
    NULL
    };

char ch_bkpt_msg[] = "Channel A breakpoint, CLC: xxxxxx";

const char *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "HALT instruction",
    "Breakpoint",
    "Undefined instruction",
    "Divide check",
    "Nested XEC limit exceeded",
    "Address stop",
    "Non-existent channel",
    "Illegal instruction for 7909 channel",
    "Illegal instruction for non-7909 channel",
    "Non-existent device",
    "Undefined channel instruction",
    "Write to protected device",
    "Illegal instruction for device",
    "Invalid 7631 track format",
    "7750 buffer pool empty on input",
    "7750 buffer pool empty on output",
    "7750 invalid line number",
    "7750 invalid message",
    ch_bkpt_msg
    };

/* Modify channel breakpoint message */

t_stat ch_bkpt (uint32 ch, uint32 clc)
{
ch_bkpt_msg[8] = 'A' + ch;
sprintf (&ch_bkpt_msg[27], "%06o", clc);
return STOP_CHBKPT;
}

/* Binary loader, not implemented */

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
extern t_stat binloader (FILE *fd, const char *file, int loadpt);

if (flag == 0)
    return binloader (fileref, cptr, 0);
return SCPE_NOFNC;
}

/* Symbol tables */

#define I_V_FL          39                              /* inst class */
#define I_M_FL          017                             /* class mask */
#define I_NOP           INT64_C(0000000000000000)       /* no operand */
#define I_MXR           INT64_C(0010000000000000)       /* addr(tag) */
#define I_MXN           INT64_C(0020000000000000)       /* *addr(tag) */
#define I_MXV           INT64_C(0030000000000000)       /* var mul/div */
#define I_MXC           INT64_C(0040000000000000)       /* convert */
#define I_DNP           INT64_C(0050000000000000)       /* decr, no oper */
#define I_DEC           INT64_C(0060000000000000)       /* decrement */
#define I_SNS           INT64_C(0070000000000000)       /* sense */
#define I_IMM           INT64_C(0100000000000000)       /* 18b immediate */
#define I_TAG           INT64_C(0110000000000000)       /* tag only */
#define I_IOX           INT64_C(0120000000000000)       /* IO channel */
#define I_TCH           INT64_C(0130000000000000)       /* transfer channel */
#define I_I9N           INT64_C(0140000000000000)       /* 7909 with nostore */
#define I_I9S           INT64_C(0150000000000000)       /* 7909 */
#define I_SPX           INT64_C(0160000000000000)       /* SPU, SPR */
#define IFAKE_7607      INT64_C(0001000000000000)       /* fake op extensions */
#define IFAKE_7909      INT64_C(0002000000000000)
#define DFAKE           (DMASK|IFAKE_7607|IFAKE_7909)
#define I_N_NOP         000
#define I_N_MXR         001
#define I_N_MXN         002
#define I_N_MXV         003
#define I_N_MXC         004
#define I_N_DNP         005
#define I_N_DEC         006
#define I_N_SNS         007
#define I_N_IMM         010
#define I_N_TAG         011
#define I_N_IOX         012
#define I_N_TCH         013
#define I_N_I9N         014
#define I_N_I9S         015
#define I_N_SPX         016

#define INST_P_XIT      0                               /* exit */
#define INST_P_SKP      1                               /* do not print */
#define INST_P_PRA      2                               /* print always */
#define INST_P_PNZ      3                               /* print if nz */
#define INST_P_PNT      4                               /* print if nz, term */

static const t_uint64 masks[15] = {
 INT64_C(03777700000000), INT64_C(03777700000000),
 INT64_C(03777700000000), INT64_C(03777700000000),
 INT64_C(03777400000000), INT64_C(03700000000000),
 INT64_C(03700000000000), INT64_C(03777700077777),
 INT64_C(03777700000000), INT64_C(03777700000000),
 INT64_C(03700000200000), INT64_C(03700000200000),
 INT64_C(03760000200000), INT64_C(03740000200000),
 INT64_C(03777700077760) }; 

static const uint32 fld_max[15][3] = {                  /* addr,tag,decr limit */
 { INST_M_ADDR, INST_M_TAG, 0 },
 { INST_M_ADDR, INST_M_TAG, 0 },
 { INST_M_ADDR, INST_M_TAG, 0 },
 { INST_M_ADDR, INST_M_TAG, INST_M_VCNT },
 { INST_M_ADDR, INST_M_TAG, INST_M_CCNT },
 { INST_M_ADDR, INST_M_TAG, INST_M_DEC },
 { INST_M_ADDR, INST_M_TAG, INST_M_DEC },
 { 0,           INST_M_TAG, 0 },
 { RMASK,       0,          0 },
 { INST_M_ADDR, INST_M_TAG, 0 },
 { INST_M_ADDR, 1,          INST_M_DEC },
 { INST_M_ADDR, 1,          0 },
 { INST_M_ADDR, 1,          0 },
 { INST_M_ADDR, 1,          0 },
 { INST_M_4B,   INST_M_TAG, 0 }
 };

static const uint32 fld_fmt[15][3] = {                  /* addr,tag,decr print */
 { INST_P_PNT, INST_P_PNT, INST_P_XIT },                /* nop: all optional */
 { INST_P_PRA, INST_P_PNT, INST_P_XIT },                /* mxr: tag optional */
 { INST_P_PRA, INST_P_PNT, INST_P_XIT },                /* mxn: tag optional */
 { INST_P_PRA, INST_P_PNZ, INST_P_PRA },                /* mxv: tag optional */
 { INST_P_PRA, INST_P_PNZ, INST_P_PRA },                /* cvt: tag optional */
 { INST_P_PNT, INST_P_PNT, INST_P_PNT },                /* dnp: all optional */
 { INST_P_PRA, INST_P_PRA, INST_P_PRA },                /* dec: print all */
 { INST_P_SKP, INST_P_PNT, INST_P_XIT },                /* sns: skip addr, tag opt */
 { INST_P_PRA, INST_P_XIT, INST_P_XIT },                /* immediate: addr only */
 { INST_P_PNZ, INST_P_PRA, INST_P_XIT },                /* tag: addr optional */
 { INST_P_PRA, INST_P_PNZ, INST_P_PRA },                /* iox: tag optional */
 { INST_P_PRA, INST_P_PNT, INST_P_XIT },                /* tch: tag optional */
 { INST_P_PRA, INST_P_PNT, INST_P_XIT },                /* i9n: tag optional */
 { INST_P_PRA, INST_P_PNT, INST_P_XIT },                /* i9s: tag optional */
 { INST_P_PNZ, INST_P_PNT, INST_P_XIT }                 /* SPx: tag optional */
 };

static const t_uint64 ind_test[15] = {
 0, 0, INST_IND, 0, 0, 0, 0, 0,
 0, 0, CHI_IND, CHI_IND, CHI_IND, CHI_IND, 0
 };

static const char *opcode[] = {
 "TXI", "TIX", "TXH",
 "STR", "TNX", "TXL",
 "HTR", "TRA", "TTR",

 "CLM", "LBT", "CHS",
 "SSP", "ENK", "IOT",
 "COM", "ETM", "RND",
 "FRN", "DCT", "RCT",
 "LMTM", "SLF", "SLN1",
 "SLN2", "SLN3", "SLN4",
 "SWT1", "SWT2", "SWT3",
 "SWT4", "SWT5", "SWT6",
 "BTTA", "BTTB", "BTTC",
 "BTTD", "BTTE", "BTTF",
 "BTTG", "BTTH",
 "RICA", "RICB", "RICC",
 "RICD", "RICE", "RICF",
 "RICG", "RICH",
 "RDCA", "RDCB", "RDCC",
 "RDCD", "RDCE", "RDCF",
 "RDCG", "RDCH",
 "SPUA", "SPUB", "SPUC",
 "SPUD", "SPUE", "SPUF",
 "SPUG", "SPUH",
 "SPTA", "SPTB", "SPTC",
 "SPTD", "SPTE", "SPTF",
 "SPTG", "SPTH",
 "SPRA", "SPRB", "SPRC",
 "SPRD", "SPRE", "SPRF",
 "SPRG", "SPRH",

 "TRCA", "TRCC",
 "TRCE", "TRCG",
 "TEFA", "TEFC",
 "TEFE", "TEFG",
 "TLQ", "IIA", "TIO",
 "OAI", "PAI", "TIF",
 "IIR", "RFT", "SIR",
 "RNT", "RIR",
 "TCOA", "TCOB", "TCOC",
 "TCOD", "TCOE", "TCOF",
 "TCOG", "TCOH", "TSX",
 "TZE", "CVR", "TPL",
 "XCA", "TOV",
 "TQO", "TQP",
 "MPY", "VLM", "VLM1",
 "DVH", "DVP",
 "VDH", "VDP",
 "VDH2", "VDP2",
 "FDH", "FDP",
 "FMP", "DFMP",
 "FAD", "DFAD",
 "FSB", "DFSB",
 "FAM", "DFAM",
 "FSM", "DFSM",
 "ANS", "ERA",
 "CAS", "ACL",
 "ADD", "ADM",
 "SUB", "SBM",
 "HPR", "IIS", "LDI",
 "OSI", "DLD", "OFT",
 "RIS", "ONT",
 "CLA", "CLS",
 "ZET", "XEC",
 "LXA", "LAC",
 "RCHA", "RCHC",
 "RCHE", "RCHG",
 "LCHA", "LCHC",
 "LCHE", "LCHG",
 "RSCA", "RSCC",
 "RSCE", "RSCG",
 "STCA", "STCC",
 "STCE", "STCG",
 "LDQ", "ENB",
 "STZ", "STO", "SLW",
 "STI", "STA", "STD",
 "STT", "STP",
 "SXA", "SCA",
 "SCHA", "SCHC",
 "SCHE", "SCHG",
 "SCDA", "SCDC",
 "SCDE", "SCDG",
 "PAX", "PAC",
 "PXA", "PCA",
 "PSE", "NOP", "RDS",
 "LLS", "BSR", "LRS",
 "WRS", "ALS", "WEF",
 "ARS", "REW", "AXT",
 "SDN",

 "CLM", "PBT", "EFTM",
 "SSM", "LFTM", "ESTM",
 "ECTM", "LTM", "LSNM",
 "EMTM", "SLT1", "SLT2",
 "SLT3", "SLT4",
 "ETTA", "ETTB", "ETTC",
 "ETTD", "ETTE", "ETTF",
 "ETTG", "ETTH",

 "ESNT",
 "TRCB", "TRCD",
 "TRCF", "TRCH",
 "TEFB", "TEFD",
 "TEFF", "TEFH",
 "RIA", "PIA",
 "IIL", "LFT", "SIL",
 "LNT", "RIL",
 "TCNA", "TCNB", "TCNC",
 "TCND", "TCNE", "TCNF",
 "TCNG", "TCNH",
 "TNZ", "CVR", "TMI",
 "XCL", "TNO", "CRQ",
 "MPR", "DFDH", "DFDP",
 "UFM", "DUFM",
 "UFA", "DUFA",
 "UFS", "DUFS",
 "UAM", "DUAM",
 "USM", "DUSM",
 "ANA", "LAS",
 "CAL", "ORA", "NZT",
 "LXD", "LXC",
 "RCHB", "RCHD",
 "RCHF", "RCHH",
 "LCHB", "LCHD",
 "LCHF", "LCHH",
 "RSCB", "RSCD",
 "RSCF", "RSCH",
 "STCB", "STCD",
 "STCF", "STCH",
 "STQ", "SRI", "ORS", "DST",
 "SPI",
 "SLQ", "STL",
 "SXD", "SCD",
 "SCHB", "SCHD",
 "SCHF", "SCHH",
 "SCDB", "SCDD",
 "SCDF", "SCDH",
 "PDX", "PDC",
 "PXD", "PCD",
 "MSE", "LGL", "BSF",
 "LGR", "RQL", "RUN",
 "AXC",

 "TIA", "TIB",
 "LRI", "LPI",
 "SEA", "SEB",
 "IFT", "EFT",

 "IOCD", "IOCDN", "TCH",
 "IORP", "IORPN",
 "IORT", "IORTN",
 "IOCP", "IOCPN",
 "IOCT", "IOCTN",
 "IOSP", "IOSPN",
 "IOST", "IOSTN",

 "WTR", "XMT",
 "TCH", "LIPT",
 "CTL", "CTLN",
 "CTLR", "CTLRN",
 "CTLW", "CTLWN",
 "SNS",
 "LAR", "SAR", "TWT",
 "CPYP",
 "CPYD", "TCM",
 "LIP", "TDC", "LCC",
 "SMS", "ICC",

 NULL
 };

static const t_uint64 opc_v[] = {
 INT64_C(0100000000000)+I_DEC, INT64_C(0200000000000)+I_DEC, INT64_C(0300000000000)+I_DEC,
 INT64_C(0500000000000)+I_DNP, INT64_C(0600000000000)+I_DEC, INT64_C(0700000000000)+I_DEC,
 INT64_C(0000000000000)+I_MXN, INT64_C(0002000000000)+I_MXN, INT64_C(0002100000000)+I_MXN,

 INT64_C(0076000000000)+I_SNS, INT64_C(0076000000001)+I_SNS, INT64_C(0076000000002)+I_SNS,
 INT64_C(0076000000003)+I_SNS, INT64_C(0076000000004)+I_SNS, INT64_C(0076000000005)+I_SNS,
 INT64_C(0076000000006)+I_SNS, INT64_C(0076000000007)+I_SNS, INT64_C(0076000000010)+I_SNS,
 INT64_C(0076000000011)+I_SNS, INT64_C(0076000000012)+I_SNS, INT64_C(0076000000014)+I_SNS,
 INT64_C(0076000000016)+I_SNS, INT64_C(0076000000140)+I_SNS, INT64_C(0076000000141)+I_SNS,
 INT64_C(0076000000142)+I_SNS, INT64_C(0076000000143)+I_SNS, INT64_C(0076000000144)+I_SNS,
 INT64_C(0076000000161)+I_SNS, INT64_C(0076000000162)+I_SNS, INT64_C(0076000000163)+I_SNS,
 INT64_C(0076000000164)+I_SNS, INT64_C(0076000000165)+I_SNS, INT64_C(0076000000166)+I_SNS,
 INT64_C(0076000001000)+I_SNS, INT64_C(0076000002000)+I_SNS, INT64_C(0076000003000)+I_SNS,
 INT64_C(0076000004000)+I_SNS, INT64_C(0076000005000)+I_SNS, INT64_C(0076000006000)+I_SNS,
 INT64_C(0076000007000)+I_SNS, INT64_C(0076000010000)+I_SNS,
 INT64_C(0076000001350)+I_SNS, INT64_C(0076000002350)+I_SNS, INT64_C(0076000003350)+I_SNS,
 INT64_C(0076000004350)+I_SNS, INT64_C(0076000005350)+I_SNS, INT64_C(0076000006350)+I_SNS,
 INT64_C(0076000007350)+I_SNS, INT64_C(0076000010350)+I_SNS,
 INT64_C(0076000001352)+I_SNS, INT64_C(0076000002352)+I_SNS, INT64_C(0076000003352)+I_SNS,
 INT64_C(0076000004352)+I_SNS, INT64_C(0076000005352)+I_SNS, INT64_C(0076000006352)+I_SNS,
 INT64_C(0076000007352)+I_SNS, INT64_C(0076000010352)+I_SNS,
 INT64_C(0076000001340)+I_SNS, INT64_C(0076000002340)+I_SNS, INT64_C(0076000003340)+I_SNS,
 INT64_C(0076000004340)+I_SNS, INT64_C(0076000005340)+I_SNS, INT64_C(0076000006340)+I_SNS,
 INT64_C(0076000007340)+I_SNS, INT64_C(0076000010340)+I_SNS,
 INT64_C(0076000001360)+I_SNS, INT64_C(0076000002360)+I_SNS, INT64_C(0076000003360)+I_SNS,
 INT64_C(0076000004360)+I_SNS, INT64_C(0076000005360)+I_SNS, INT64_C(0076000006360)+I_SNS,
 INT64_C(0076000007360)+I_SNS, INT64_C(0076000010360)+I_SNS,
 INT64_C(0076000001360)+I_SNS, INT64_C(0076000002360)+I_SNS, INT64_C(0076000003360)+I_SNS,
 INT64_C(0076000004360)+I_SNS, INT64_C(0076000005360)+I_SNS, INT64_C(0076000006360)+I_SNS,
 INT64_C(0076000007360)+I_SNS, INT64_C(0076000010360)+I_SNS,

 INT64_C(0002200000000)+I_MXN, INT64_C(0002400000000)+I_MXN,
 INT64_C(0002600000000)+I_MXN, INT64_C(0002700000000)+I_MXN,
 INT64_C(0003000000000)+I_MXN, INT64_C(0003100000000)+I_MXN,
 INT64_C(0003200000000)+I_MXN, INT64_C(0003300000000)+I_MXN,
 INT64_C(0004000000000)+I_MXN, INT64_C(0004100000000)+I_NOP, INT64_C(0004200000000)+I_MXR,
 INT64_C(0004300000000)+I_NOP, INT64_C(0004400000000)+I_NOP, INT64_C(0004600000000)+I_MXR,
 INT64_C(0005100000000)+I_IMM, INT64_C(0005400000000)+I_IMM, INT64_C(0005500000000)+I_IMM,
 INT64_C(0005600000000)+I_IMM, INT64_C(0005700000000)+I_IMM,
 INT64_C(0006000000000)+I_MXN, INT64_C(0006100000000)+I_MXN, INT64_C(0006200000000)+I_MXN,
 INT64_C(0006300000000)+I_MXN, INT64_C(0006400000000)+I_MXN, INT64_C(0006500000000)+I_MXN,
 INT64_C(0006600000000)+I_MXN, INT64_C(0006700000000)+I_MXN, INT64_C(0007400000000)+I_MXR,
 INT64_C(0010000000000)+I_MXN, INT64_C(0011400000000)+I_MXC, INT64_C(0012000000000)+I_MXN,
 INT64_C(0013100000000)+I_NOP, INT64_C(0014000000000)+I_MXN,
 INT64_C(0016100000000)+I_MXN, INT64_C(0016200000000)+I_MXN,
 INT64_C(0020000000000)+I_MXN, INT64_C(0020400000000)+I_MXV, INT64_C(0020500000000)+I_MXV,
 INT64_C(0022000000000)+I_MXN, INT64_C(0022100000000)+I_MXN,
 INT64_C(0022400000000)+I_MXV, INT64_C(0022500000000)+I_MXV,
 INT64_C(0022600000000)+I_MXV, INT64_C(0022700000000)+I_MXV,
 INT64_C(0024000000000)+I_MXN, INT64_C(0024100000000)+I_MXN,
 INT64_C(0026000000000)+I_MXN, INT64_C(0026100000000)+I_MXN,
 INT64_C(0030000000000)+I_MXN, INT64_C(0030100000000)+I_MXN,
 INT64_C(0030200000000)+I_MXN, INT64_C(0030300000000)+I_MXN,
 INT64_C(0030400000000)+I_MXN, INT64_C(0030500000000)+I_MXN,
 INT64_C(0030600000000)+I_MXN, INT64_C(0030700000000)+I_MXN,
 INT64_C(0032000000000)+I_MXN, INT64_C(0032200000000)+I_MXN,
 INT64_C(0034000000000)+I_MXN, INT64_C(0036100000000)+I_MXN,
 INT64_C(0040000000000)+I_MXN, INT64_C(0040100000000)+I_MXN,
 INT64_C(0040200000000)+I_MXN, INT64_C(0440000000000)+I_MXN,
 INT64_C(0042000000000)+I_NOP, INT64_C(0044000000000)+I_MXN, INT64_C(0044100000000)+I_MXN,
 INT64_C(0044200000000)+I_MXN, INT64_C(0044300000000)+I_MXN, INT64_C(0044400000000)+I_MXN,
 INT64_C(0044500000000)+I_MXN, INT64_C(0044600000000)+I_MXN,
 INT64_C(0050000000000)+I_MXN, INT64_C(0050200000000)+I_MXN,
 INT64_C(0052000000000)+I_MXN, INT64_C(0052200000000)+I_MXN,
 INT64_C(0053400000000)+I_MXR, INT64_C(0053500000000)+I_MXR,
 INT64_C(0054000000000)+I_MXN, INT64_C(0054100000000)+I_MXN,
 INT64_C(0054200000000)+I_MXN, INT64_C(0054300000000)+I_MXN,
 INT64_C(0054400000000)+I_MXN, INT64_C(0054500000000)+I_MXN,
 INT64_C(0054600000000)+I_MXN, INT64_C(0054700000000)+I_MXN,
 INT64_C(0054000000000)+I_MXN, INT64_C(0054100000000)+I_MXN,
 INT64_C(0054200000000)+I_MXN, INT64_C(0054300000000)+I_MXN,
 INT64_C(0054400000000)+I_MXN, INT64_C(0054500000000)+I_MXN,
 INT64_C(0054600000000)+I_MXN, INT64_C(0054700000000)+I_MXN,
 INT64_C(0056000000000)+I_MXN, INT64_C(0056400000000)+I_MXN,
 INT64_C(0060000000000)+I_MXN, INT64_C(0060100000000)+I_MXN, INT64_C(0060200000000)+I_MXN,
 INT64_C(0060400000000)+I_MXN, INT64_C(0062100000000)+I_MXN, INT64_C(0062200000000)+I_MXN,
 INT64_C(0062500000000)+I_MXN, INT64_C(0063000000000)+I_MXN,
 INT64_C(0063400000000)+I_MXR, INT64_C(0063600000000)+I_MXR,
 INT64_C(0064000000000)+I_MXN, INT64_C(0064000000000)+I_MXN,
 INT64_C(0064200000000)+I_MXN, INT64_C(0064300000000)+I_MXN,
 INT64_C(0064400000000)+I_MXN, INT64_C(0064500000000)+I_MXN,
 INT64_C(0064600000000)+I_MXN, INT64_C(0064700000000)+I_MXN,
 INT64_C(0073400000000)+I_TAG, INT64_C(0073700000000)+I_TAG,
 INT64_C(0075400000000)+I_TAG, INT64_C(0075600000000)+I_TAG,
 INT64_C(0076000000000)+I_MXR, INT64_C(0076100000000)+I_NOP, INT64_C(0076200000000)+I_MXR,
 INT64_C(0076300000000)+I_MXR, INT64_C(0076400000000)+I_MXR, INT64_C(0076500000000)+I_MXR,
 INT64_C(0076600000000)+I_MXR, INT64_C(0076700000000)+I_MXR, INT64_C(0077000000000)+I_MXR,
 INT64_C(0077100000000)+I_MXR, INT64_C(0077200000000)+I_MXR, INT64_C(0077400000000)+I_MXR,
 INT64_C(0077600000000)+I_MXR,

 INT64_C(0476000000000)+I_SNS, INT64_C(0476000000001)+I_SNS, INT64_C(0476000000002)+I_SNS,
 INT64_C(0476000000003)+I_SNS, INT64_C(0476000000004)+I_SNS, INT64_C(0476000000005)+I_SNS,
 INT64_C(0476000000006)+I_SNS, INT64_C(0476000000007)+I_SNS, INT64_C(0476000000010)+I_SNS,
 INT64_C(0476000000016)+I_SNS, INT64_C(0476000000141)+I_SNS, INT64_C(0476000000142)+I_SNS,
 INT64_C(0476000000143)+I_SNS, INT64_C(0476000000144)+I_SNS,
 INT64_C(0476000001000)+I_SNS, INT64_C(0476000002000)+I_SNS, INT64_C(0476000003000)+I_SNS,
 INT64_C(0476000004000)+I_SNS, INT64_C(0476000005000)+I_SNS, INT64_C(0476000006000)+I_SNS,
 INT64_C(0476000007000)+I_SNS, INT64_C(0476000010000)+I_SNS,

 INT64_C(0402100000000)+I_MXN,
 INT64_C(0402200000000)+I_MXN, INT64_C(0402400000000)+I_MXN,
 INT64_C(0402600000000)+I_MXN, INT64_C(0402700000000)+I_MXN,
 INT64_C(0403000000000)+I_MXN, INT64_C(0403100000000)+I_MXN,
 INT64_C(0403200000000)+I_MXN, INT64_C(0403300000000)+I_MXN,
 INT64_C(0404200000000)+I_NOP, INT64_C(0404600000000)+I_NOP,
 INT64_C(0405100000000)+I_IMM, INT64_C(0405400000000)+I_IMM, INT64_C(0405500000000)+I_IMM,
 INT64_C(0405600000000)+I_IMM, INT64_C(0405700000000)+I_IMM,
 INT64_C(0406000000000)+I_MXN, INT64_C(0406100000000)+I_MXN, INT64_C(0406200000000)+I_MXN,
 INT64_C(0406300000000)+I_MXN, INT64_C(0406400000000)+I_MXN, INT64_C(0406500000000)+I_MXN,
 INT64_C(0406600000000)+I_MXN, INT64_C(0406700000000)+I_MXN, 
 INT64_C(0410000000000)+I_MXN, INT64_C(0411400000000)+I_MXC, INT64_C(0412000000000)+I_MXN,
 INT64_C(0413000000000)+I_NOP, INT64_C(0414000000000)+I_MXN, INT64_C(0415400000000)+I_MXC,
 INT64_C(0420000000000)+I_MXN, INT64_C(0424000000000)+I_MXN, INT64_C(0424100000000)+I_MXN,
 INT64_C(0426000000000)+I_MXN, INT64_C(0426100000000)+I_MXN,
 INT64_C(0430000000000)+I_MXN, INT64_C(0430100000000)+I_MXN,
 INT64_C(0430200000000)+I_MXN, INT64_C(0430300000000)+I_MXN,
 INT64_C(0430400000000)+I_MXN, INT64_C(0430500000000)+I_MXN,
 INT64_C(0430600000000)+I_MXN, INT64_C(0430700000000)+I_MXN,
 INT64_C(0432000000000)+I_MXN, INT64_C(0434000000000)+I_MXN,
 INT64_C(0450000000000)+I_MXN, INT64_C(0450100000000)+I_MXN, INT64_C(0452000000000)+I_MXN,
 INT64_C(0453400000000)+I_MXR, INT64_C(0453500000000)+I_MXR,
 INT64_C(0454000000000)+I_MXN, INT64_C(0454100000000)+I_MXN,
 INT64_C(0454200000000)+I_MXN, INT64_C(0454300000000)+I_MXN,
 INT64_C(0454400000000)+I_MXN, INT64_C(0454500000000)+I_MXN,
 INT64_C(0454600000000)+I_MXN, INT64_C(0454700000000)+I_MXN,
 INT64_C(0454000000000)+I_MXN, INT64_C(0454100000000)+I_MXN,
 INT64_C(0454200000000)+I_MXN, INT64_C(0454300000000)+I_MXN,
 INT64_C(0454400000000)+I_MXN, INT64_C(0454500000000)+I_MXN,
 INT64_C(0454600000000)+I_MXN, INT64_C(0454700000000)+I_MXN,
 INT64_C(0460000000000)+I_MXN, INT64_C(0460100000000)+I_MXN, INT64_C(0460200000000)+I_MXN, INT64_C(0460300000000)+I_MXN,
 INT64_C(0460400000000)+I_MXN, 
 INT64_C(0462000000000)+I_MXN, INT64_C(0462500000000)+I_MXN,
 INT64_C(0463400000000)+I_MXR, INT64_C(0463600000000)+I_MXR,
 INT64_C(0464000000000)+I_MXN, INT64_C(0464000000000)+I_MXN,
 INT64_C(0464200000000)+I_MXN, INT64_C(0464300000000)+I_MXN,
 INT64_C(0464400000000)+I_MXN, INT64_C(0464500000000)+I_MXN,
 INT64_C(0464600000000)+I_MXN, INT64_C(0464700000000)+I_MXN,
 INT64_C(0473400000000)+I_TAG, INT64_C(0473700000000)+I_TAG,
 INT64_C(0475400000000)+I_TAG, INT64_C(0475600000000)+I_TAG,
 INT64_C(0476000000000)+I_MXR, INT64_C(0476300000000)+I_MXR, INT64_C(0476400000000)+I_MXR,
 INT64_C(0476500000000)+I_MXR, INT64_C(0477300000000)+I_MXR, INT64_C(0477200000000)+I_MXR,
 INT64_C(0477400000000)+I_MXR,

 INT64_C(0010100000000)+I_MXN, INT64_C(0410100000000)+I_MXN,
 INT64_C(0056200000000)+I_MXN, INT64_C(0456400000000)+I_MXN,
 INT64_C(0476100000041)+I_SNS, INT64_C(0476100000042)+I_SNS,
 INT64_C(0476100000043)+I_SNS, INT64_C(0476100000044)+I_SNS,

 INT64_C(01000000000000)+I_IOX, INT64_C(01000000200000)+I_IOX, INT64_C(01100000000000)+I_TCH,
 INT64_C(01200000000000)+I_IOX, INT64_C(01200000200000)+I_IOX,
 INT64_C(01300000000000)+I_IOX, INT64_C(01300000200000)+I_IOX,
 INT64_C(01400000000000)+I_IOX, INT64_C(01400000200000)+I_IOX,
 INT64_C(01500000000000)+I_IOX, INT64_C(01500000200000)+I_IOX,
 INT64_C(01600000000000)+I_IOX, INT64_C(01600000200000)+I_IOX,
 INT64_C(01700000000000)+I_IOX, INT64_C(01700000200000)+I_IOX,

 INT64_C(02000000000000)+I_TCH, INT64_C(02000000200000)+I_IOX,
 INT64_C(02100000000000)+I_TCH, INT64_C(02100000200000)+I_TCH,
 INT64_C(02200000000000)+I_I9N, INT64_C(02220000000000)+I_TCH,
 INT64_C(02200000200000)+I_I9N, INT64_C(02220000200000)+I_TCH,
 INT64_C(02240000000000)+I_I9N, INT64_C(02260000000000)+I_TCH,
 INT64_C(02240000200000)+I_I9N,
 INT64_C(02300000000000)+I_I9S, INT64_C(02300000200000)+I_I9S,
 INT64_C(02340000000000)+I_I9S,
 INT64_C(02400000000000)+I_IOX,
 INT64_C(02500000000000)+I_IOX, INT64_C(02500000200000)+I_IOX,
 INT64_C(02600000200000)+I_I9S, INT64_C(02640000000000)+I_I9S, INT64_C(02640000200000)+I_I9S,
 INT64_C(02700000000000)+I_I9S, INT64_C(02700000200000)+I_IOX,

 0
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
uint32 i, j, k, l, fmt, c, fld[3];
DEVICE *dptr;
t_uint64 inst;

inst = val[0];
if (uptr == NULL)
    uptr = &cpu_unit;
dptr = find_dev_from_unit (uptr);
if (dptr == NULL)
    return SCPE_IERR;

if (sw & SWMASK ('C')) {                                /* character? */
    c = (uint32) (inst & 077);
    fprintf (of, "%c", cvt_code_to_ascii (c, sw));
    return SCPE_OK;
    }
if (sw & SWMASK ('S')) {                                /* string? */
    for (i = 36; i > 0; i = i - 6) {
        c = (uint32) ((inst >> (i - 6)) & 077);
        fprintf (of, "%c", cvt_code_to_ascii (c, sw));
        }       
    return SCPE_OK;
    }
if (!(sw & (SWMASK ('M')|SWMASK ('I')|SWMASK ('N'))) || /* M, N or I? */
    (dptr->dwidth != 36))
    return SCPE_ARG;

/* Instruction decode */

fld[0] = ((uint32) inst & 0777777);
fld[1] = GET_TAG (inst);                                /* get 3 fields */
fld[2] = GET_DEC (inst);
if (sw & SWMASK ('I'))                                  /* decode as 7607? */
    inst |= IFAKE_7607;
if (sw & SWMASK ('N'))                                  /* decode as 7909? */
    inst |= IFAKE_7909;

for (i = 0; opc_v[i] > 0; i++) {                        /* loop thru ops */
    j = (int32) ((opc_v[i] >> I_V_FL) & I_M_FL);        /* get class */
    if ((opc_v[i] & DFAKE) == (inst & masks[j])) {      /* match? */
        if (inst & ind_test[j])                         /* indirect? */
            fprintf (of, "%s*", opcode[i]);
        else fprintf (of, "%s", opcode[i]);             /* opcode */
        for (k = 0; k < 3; k++)
            fld[k] = fld[k] & fld_max[j][k];
        for (k = 0; k < 3; k++) {                       /* loop thru fields */
            fmt = fld_fmt[j][k];                        /* get format */
            if (fmt == INST_P_XIT)
                return SCPE_OK;
            switch (fmt) {                              /* case on format */

            case INST_P_PNT:                            /* print nz, else term */
                for (l = k, c = 0; l < 3; l++)
                    c |= fld[k];
                if (c == 0)
                    return SCPE_OK;
            case INST_P_PNZ:                            /* print non-zero */
                fputc (k? ',': ' ', of);
                if (fld[k])
                    fprintf (of, "%-o", fld[k]);
                break;
            case INST_P_PRA:                            /* print always */
                fputc (k? ',': ' ', of);
                fprintf (of, "%-o", fld[k]);
                break;
            case INST_P_SKP:                            /* skip */
                break;
                }                                       /* end switch */
            }                                           /* end for k */
        return SCPE_OK;                                 /* done */
        }                                               /* end if */
    }                                                   /* end for i */
return SCPE_ARG;
}

/* Convert character to code to ASCII

   -b       BCD
   -a       business-chain */

uint32 cvt_code_to_ascii (uint32 c, int32 sw)
{
if (sw & SWMASK ('B')) {
    if (sw & SWMASK ('A'))
        return bcd_to_ascii_a[c];
    else return bcd_to_ascii_h[c];
    }
else if (sw & SWMASK ('A'))
    return nine_to_ascii_a[c];
else return nine_to_ascii_h[c];
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
uint32 i, j, c;
t_uint64 fld[3];
t_bool ind;
t_stat r;
char gbuf[CBUFSIZE];

while (isspace (*cptr)) cptr++;
if ((sw & SWMASK ('C')) || ((*cptr == '\'') && cptr++)) { /* character? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    val[0] = (t_value) cvt_ascii_to_code (cptr[0] & 0177, sw);
    return SCPE_OK;
    }
if ((sw & SWMASK ('S')) || ((*cptr == '"') && cptr++)) { /* sixbit string? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    for (i = 0; i < 6; i++) {
        c = cptr[0] & 0177;
        if (c)
            val[0] = (val[0] << 6) | ((t_value) cvt_ascii_to_code (c, sw));
        else {
            val[0] = val[0] << (6 * (6 - i));
            break;
            }
        }
    return SCPE_OK;
    }

cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
j = strlen (gbuf);                                      /* get length */
if (gbuf[j - 1] == '*') {                               /* indirect? */
    ind = TRUE;
    gbuf[j - 1] = 0;
    }
else ind = FALSE;
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL)
    return SCPE_ARG;
j = (uint32) ((opc_v[i] >> I_V_FL) & I_M_FL);           /* get class */
val[0] = opc_v[i] & DMASK;
if (ind) {
    if (ind_test[j])
        val[0] |= ind_test[j];
    else return SCPE_ARG;
    }

for (i = 0; i < 3; i++)                                 /* clear inputs */
    fld[i] = 0;
for (i = 0; (i < 3) && *cptr; i++) {                    /* parse inputs */
    if (i < 2)                                          /* get glyph */
        cptr = get_glyph (cptr, gbuf, ',');
    else cptr = get_glyph (cptr, gbuf, 0);
    if (gbuf[0]) {                                      /* anything? */
        fld[i] = get_uint (gbuf, 8, fld_max[j][i], &r);
        if ((r != SCPE_OK) || (fld_max[j][i] == 0))
            return SCPE_ARG;
        }
    }
if (*cptr != 0)                                         /* junk at end? */
    return SCPE_ARG;

val[0] = val[0] | fld[0] | (fld[1] << INST_V_TAG) | (fld[2] << INST_V_DEC);
return SCPE_OK;
}

/* Convert ASCII to character code

   -b       BCD */

uint32 cvt_ascii_to_code (uint32 c, int32 sw)
{
if (sw & SWMASK ('B'))
    return ascii_to_bcd[c];
else return ascii_to_nine[c];
}
