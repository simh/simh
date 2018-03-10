/* alpha_sys.c: Alpha simulator interface

   Copyright (c) 2003-20017, Robert M Supnik

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

   26-May-17    RMS     Fixed bad mnemonics and reversed definitions in opcode 12
*/

#include "alpha_defs.h"
#include <ctype.h>

extern UNIT cpu_unit;
extern REG cpu_reg[];
extern uint32 pal_type;

t_stat fprint_sym_m (FILE *of, t_addr addr, uint32 inst);
t_stat parse_sym_m (CONST char *cptr, t_addr addr, t_value *inst);
int32 parse_reg (CONST char *cptr);

extern t_stat fprint_pal_hwre (FILE *of, uint32 inst);
extern t_stat parse_pal_hwre (CONST char *cptr, t_value *inst);
extern t_bool rom_wr (t_uint64 pa, t_uint64 val, uint32 lnt);

/* SCP data structures and interface routines

   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words for examine
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;

const char *sim_stop_messages[] = {
    "Unknown error",
    "HALT instruction",
    "Breakpoint",
    "Unsupported PAL variation",
    "Kernel stack not valid",
    "Unknown abort code",
    "Memory management error"
    };

/* Binary loader

   The binary loader handles absolute system images, that is, system
   images linked /SYSTEM.  These are simply a byte stream, with no
   origin or relocation information.

   -r           load ROM
   -o           specify origin
*/

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
t_stat r;
int32 i;
t_uint64 origin;

if (flag) return SCPE_ARG;                              /* dump? */
origin = 0;                                             /* memory */
if (sim_switches & SWMASK ('O')) {                      /* origin? */
    origin = get_uint (cptr, 16, 0xFFFFFFFF, &r);
    if (r != SCPE_OK) return SCPE_ARG;
    }

while ((i = getc (fileref)) != EOF) {                   /* read byte stream */
    if (sim_switches & SWMASK ('R')) {                  /* ROM? */
        if (!rom_wr (origin, i, L_BYTE))
            return SCPE_NXM;
        }
    else if (ADDR_IS_MEM (origin))                      /* valid memory? */
        WritePB (origin, i);
    else return SCPE_NXM;
    origin = origin + 1;
    }
return SCPE_OK;
}

/* Opcode mnemonics table */

#define CL_NO           0                               /* no operand */
#define CL_BR           1                               /* branch */
#define CL_MR           2                               /* memory reference */
#define CL_IO           3                               /* integer opr */
#define CL_FO           4                               /* floating opr */
#define CL_MO           5                               /* memory opr */
#define CL_JP           6                               /* jump */
#define CL_HW           7                               /* hardware */
#define CL_M_PAL        0x00F0
#define CL_V_PAL        4
#define CL_VMS          (1u << (PAL_VMS + CL_V_PAL))
#define CL_UNIX         (1u << (PAL_UNIX + CL_V_PAL))
#define CL_NT           (1u << (PAL_NT + CL_V_PAL))
#define FL_RA           0x0100
#define FL_RB           0x0200
#define FL_RC           0x0400
#define FL_RBI          0x0800
#define FL_MDP          0x1000
#define FL_BDP          0x2000
#define FL_JDP          0x4000
#define FL_LIT          0x8000
#define CL_CLASS        0x000F
#define PAL_MASK(x)     (1u << (pal_type + CL_V_PAL))

#define C_NO            CL_NO
#define C_PCM           CL_NO | CL_VMS | CL_UNIX | CL_NT
#define C_PVM           CL_NO | CL_VMS
#define C_PUN           CL_NO | CL_UNIX
#define C_PNT           CL_NO | CL_NT   
#define C_BR            CL_BR | FL_RA | FL_BDP
#define C_MR            CL_MR | FL_RA | FL_RB | FL_RBI | FL_MDP
#define C_FE            CL_MO | FL_RB | FL_RBI
#define C_RV            CL_MO | FL_RA
#define C_MO            CL_MO | FL_RA | FL_RB
#define C_IO            CL_IO | FL_RA | FL_RB | FL_RC | FL_LIT
#define C_IAC           CL_IO | FL_RA | FL_RC
#define C_IBC           CL_IO | FL_RB | FL_RC | FL_LIT
#define C_FO            CL_FO | FL_RA | FL_RB | FL_RC
#define C_FAC           CL_FO | FL_RA | FL_RC
#define C_FBC           CL_FO | FL_RB | FL_RC
#define C_JP            CL_JP | FL_RA | FL_RB | FL_RBI | FL_JDP
#define C_HW            CL_HW

uint32 masks[8] = {
 0xFFFFFFFF, 0xFC000000,
 0xFC000000, 0xFC000FE0,
 0xFC00FFE0, 0xFC00FFFF,
 0xFC00C000, 0xFC000000
 };

const char *opcode[] = {
 "HALT", "DRAINA", "CFLUSH", "LDQP",                    /* VMS PALcode */
 "STQP", "SWPCTX", "MFPR_ASN", "MTPR_ASTEN",
 "MTPR_ASTSR", "CSERVE", "SWPPAL", "MFPR_FEN",
 "MTPR_FEN", "MTPR_IPIR", "MFPR_IPL", "MTPR_IPL",
 "MFPR_MCES", "MTPR_MCES", "MFPR_PCBB", "MFPR_PRBR",
 "MTPR_PRBR", "MFPR_PTBR", "MFPR_SCBB", "MTPR_SCBB",
 "MTPR_SIRR", "MFPR_SISR", "MFPR_TBCHK", "MTPR_TBIA",
 "MTPR_TBIAP", "MTPR_TBIS", "MFPR_ESP", "MTPR_ESP",
 "MFPR_SSP", "MTPR_SSP", "MFPR_USP", "MTPR_USP",
 "MTPR_TBISD", "MTPR_TBISI", "MFPR_ASTEN", "MFPR_ASTSR",
 "MFPR_VTBR", "MTPR_VTBR", "MTPR_PERFMON", "MTPR_DATFX",
 "MFPR_VIRBND", "MTPR_VIRBND", "MFPR_SYSPTBR", "MTPR_SYSPTBR",
 "WTINT", "MFPR_WHAMI",
 "BPT", "BUGCHK", "CHME", "CHMK",
 "CHMS", "CHMU", "IMB", "INSQHIL",
 "INSQTIL", "INSQHIQ", "INSQTIQ", "INSQUEL",
 "INSQUEQ", "INSQUEL/D", "INSQUEQ/D", "PROBER",
 "PROBEW", "RD_PS", "REI", "REMQHIL",
 "REMQTIL", "REMQHIQ", "REMQTIQ", "REMQUEL",
 "REMQUEQ", "REMQUEL/D", "REMQUEQ/D", "SWASTEN",
 "WR_PS_SW", "RSCC", "RD_UNQ", "WR_UNQ",
 "AMOVRR", "AMOVRM", "INSQHILR", "INSQTILR",
 "INSQHIQR", "INSQTIQR", "REMQHILR", "REMQTILR",
 "REMQHIQR", "REMQTIQR", "GENTRAP", "CLRFEN",
 "RDMCES", "WRMCES", "WRVIRBND", "WRSYSPTBR",           /* UNIX PALcode */
 "WRFEN", "WRVPTPTR", "WRASN",
 "SWPCTX", "WRVAL", "RDVAL", "TBI",
 "WRENT", "SWPIPL", "RDPS", "WRKGP",
 "WRUSP", "WRPERFMON", "RDUSP",
 "WHAMI", "RETSYS", "RTI",
 "URTI", "RDUNIQUE", "WRUNIQUE",
 "LDA", "LDAH", "LDBU", "LDQ_U",
 "LDWU", "STW", "STB", "STQ_U",
 "ADDL", "S4ADDL", "SUBL", "S4SUBL",
 "CMPBGE", "S8ADDL", "S8SUBL", "CMPULT",
 "ADDQ", "S4ADDQ", "SUBQ", "S4SUBQ",
 "CMPEQ", "S8ADDQ", "S8SUBQ", "CMPULE",
 "ADDL/V", "SUBL/V", "CMPLT",
 "ADDQ/V", "SUBQ/V", "CMPLE",
 "AND", "BIC", "CMOVLBS", "CMOVLBC",
 "BIS", "CMOVEQ", "CMOVNE", "ORNOT",
 "XOR", "CMOVLT", "CMOVGE", "EQV",
 "CMOVLE", "CMOVGT",
 "MSKBL", "EXTBL", "INSBL",
 "MSKWL", "EXTWL", "INSWL",
 "MSKLL", "EXTLL", "INSLL",
 "ZAP", "ZAPNOT", "MSKQL", "SRL",
 "EXTQL", "SLL", "INSQL", "SRA",
 "MSKWH", "INSWH", "EXTWH",
 "MSKLH", "INSLH", "EXTLH",
 "MSKQH", "INSQH", "EXTQH",
 "MULL", "MULQ", "UMULH",
 "MULL/V", "MULLQ/V",
 "ITOFS", "ITOFF", "ITOFT",
 "SQRTF/C", "SQRTF", "SQRTF/UC", "SQRTF/U",
 "SQRTF/SC", "SQRTF/S", "SQRTF/SUC", "SQRTF/SU",
 "SQRTG/C", "SQRTG", "SQRTG/UC", "SQRTG/U",
 "SQRTG/SC", "SQRTG/S", "SQRTG/SUC", "SQRTG/SU",
 "SQRTS/C", "SQRTS/M", "SQRTS", "SQRTS/D",
 "SQRTS/UC", "SQRTS/UM", "SQRTS/U", "SQRTS/UD",
 "SQRTS/SUC", "SQRTS/SUM", "SQRTS/SU", "SQRTS/SUD",
 "SQRTS/SUIC", "SQRTS/SUIM", "SQRTS/SUI", "SQRTS/SUID",
 "SQRTT/C", "SQRTT/M", "SQRTT", "SQRTT/D",
 "SQRTT/UC", "SQRTT/UM", "SQRTT/U", "SQRTT/UD",
 "SQRTT/SUC", "SQRTT/SUM", "SQRTT/SU", "SQRTT/SUD",
 "SQRTT/SUIC", "SQRTT/SUIM", "SQRTT/SUI", "SQRTT/SUID",
 "ADDF/C", "ADDF", "ADDF/UC", "ADDF/U",
 "ADDF/SC", "ADDF/S", "ADDF/SUC", "ADDF/SU",
 "SUBF/C", "SUBF", "SUBF/UC", "SUBF/U",
 "SUBF/SC", "SUBF/S", "SUBF/SUC", "SUBF/SU",
 "MULF/C", "MULF", "MULF/UC", "MULF/U",
 "MULF/SC", "MULF/S", "MULF/SUC", "MULF/SU",
 "DIVF/C", "DIVF", "DIVF/UC", "DIVF/U",
 "DIVF/SC", "DIVF/S", "DIVF/SUC", "DIVF/SU",
 "ADDG/C", "ADDG", "ADDG/UC", "ADDG/U",
 "ADDG/SC", "ADDG/S", "ADDG/SUC", "ADDG/SU",
 "SUBG/C", "SUBG", "SUBG/UC", "SUBG/U",
 "SUBG/SC", "SUBG/S", "SUBG/SUC", "SUBG/SU",
 "MULG/C", "MULG", "MULG/UC", "MULG/U",
 "MULG/SC", "MULG/S", "MULG/SUC", "MULG/SU",
 "DIVG/C", "DIVG", "DIVG/UC", "DIVG/U",
 "DIVG/SC", "DIVG/S", "DIVG/SUC", "DIVG/SU",
 "CVTDG/C", "CVTDG", "CVTDG/UC", "CVTDG/U",
 "CVTDG/SC", "CVTDG/S", "CVTDG/SUC", "CVTDG/SU",
 "CVTGF/C", "CVTGF", "CVTGF/UC", "CVTGF/U",
 "CVTGF/SC", "CVTGF/S", "CVTGF/SUC", "CVTGF/SU",
 "CVTGD/C", "CVTGD", "CVTGD/UC", "CVTGD/U",
 "CVTGD/SC", "CVTGD/S", "CVTGD/SUC", "CVTGD/SU",
 "CVTGQ/C", "CVTGQ", "CVTGQ/VC", "CVTGQ/V",
 "CVTGQ/SC", "CVTGQ/S", "CVTGQ/SVC", "CVTGQ/SV",
 "CVTQF/C", "CVTQF", "CVTQG/C", "CVTQG",
 "CMPGEQ/C", "CMPGEQ/SC", "CMPGLT/C", "CMPGLT/SC",
 "CMPGLE/C", "CMPGLE/SC",
 "ADDS/C", "ADDS/M", "ADDS", "ADDS/D",
 "ADDS/UC", "ADDS/UM", "ADDS/U", "ADDS/UD",
 "ADDS/SUC", "ADDS/SUM", "ADDS/SU", "ADDS/SUD",
 "ADDS/SUIC", "ADDS/SUIM", "ADDS/SUI", "ADDS/SUID",
 "SUBS/C", "SUBS/M", "SUBS", "SUBS/D",
 "SUBS/UC", "SUBS/UM", "SUBS/U", "SUBS/UD",
 "SUBS/SUC", "SUBS/SUM", "SUBS/SU", "SUBS/SUD",
 "SUBS/SUIC", "SUBS/SUIM", "SUBS/SUI", "SUBS/SUID",
 "MULS/C", "MULS/M", "MULS", "MULS/D",
 "MULS/UC", "MULS/UM", "MULS/U", "MULS/UD",
 "MULS/SUC", "MULS/SUM", "MULS/SU", "MULS/SUD",
 "MULS/SUIC", "MULS/SUIM", "MULS/SUI", "MULS/SUID",
 "DIVS/C", "DIVS/M", "DIVS", "DIVS/D",
 "DIVS/UC", "DIVS/UM", "DIVS/U", "DIVS/UD",
 "DIVS/SUC", "DIVS/SUM", "DIVS/SU", "DIVS/SUD",
 "DIVS/SUIC", "DIVS/SUIM", "DIVS/SUI", "DIVS/SUID",
 "ADDT/C", "ADDT/M", "ADDT", "ADDT/D",
 "ADDT/UC", "ADDT/UM", "ADDT/U", "ADDT/UD",
 "ADDT/SUC", "ADDT/SUM", "ADDT/SU", "ADDT/SUD",
 "ADDT/SUIC", "ADDT/SUIM", "ADDT/SUI", "ADDT/SUID",
 "SUBT/C", "SUBT/M", "SUBT", "SUBT/D",
 "SUBT/UC", "SUBT/UM", "SUBT/U", "SUBT/UD",
 "SUBT/SUC", "SUBT/SUM", "SUBT/SU", "SUBT/SUD",
 "SUBT/SUIC", "SUBT/SUIM", "SUBT/SUI", "SUBT/SUID",
 "MULT/C", "MULT/M", "MULT", "MULT/D",
 "MULT/UC", "MULT/UM", "MULT/U", "MULT/UD",
 "MULT/SUC", "MULT/SUM", "MULT/SU", "MULT/SUD",
 "MULT/SUIC", "MULT/SUIM", "MULT/SUI", "MULT/SUID",
 "DIVT/C", "DIVT/M", "DIVT", "DIVT/D",
 "DIVT/UC", "DIVT/UM", "DIVT/U", "DIVT/UD",
 "DIVT/SUC", "DIVT/SUM", "DIVT/SU", "DIVT/SUD",
 "DIVT/SUIC", "DIVT/SUIM", "DIVT/SUI", "DIVT/SUID",
 "CVTTS/C", "CVTTS/M", "CVTTS", "CVTTS/D",
 "CVTTS/UC", "CVTTS/UM", "CVTTS/U", "CVTTS/UD",
 "CVTTS/SUC", "CVTTS/SUM", "CVTTS/SU", "CVTTS/SUD",
 "CVTTS/SUIC", "CVTTS/SUIM", "CVTTS/SUI", "CVTTS/SUID",
 "CVTTQ/C", "CVTTQ/M", "CVTTQ", "CVTTQ/D",
 "CVTTQ/VC", "CVTTQ/VM", "CVTTQ/V", "CVTTQ/VD",
 "CVTTQ/SVC", "CVTTQ/SVM", "CVTTQ/SV", "CVTTQ/SVD",
 "CVTTQ/SVIC", "CVTTQ/SVIM", "CVTTQ/SVI", "CVTTQ/SVID",
 "CVTQS/C", "CVTQS/M", "CVTQS", "CVTQS/D",
 "CVTQS/SUIC", "CVTQS/SUIM", "CVTQS/SUI", "CVTQS/SUID",
 "CVTQT/C", "CVTQT/M", "CVTQT", "CVTQT/D",
 "CVTQT/SUIC", "CVTQT/SUIM", "CVTQT/SUI", "CVTQT/SUID",
 "CMPTUN/C", "CMPTUN/S", "CMPTEQ/C", "CMPTEQ/S",
 "CMPTLT/C", "CMPTLT/S", "CMPTLE/C", "CMPTLE/S",
 "CVTLQ", "CPYS", "CPYSN", "CPYSE",
 "MT_FPCR", "MF_FPCR",
 "FCMOVEQ", "FCMOVNE", "FCMOVLT",
 "FCMOVGE", "FCMOVLE", "FCMOVGT",
 "CVTQL", "CVTQL/V", "CVTQL/SV",
 "TRAPB", "EXCB", "MB", "WMB",
 "FETCH", "FETCH_M", "RPCC",
 "RC", "RS",
 "JMP", "JSR", "RET", "JSR_COROUTINE",
 "SEXTB", "SEXTW",
 "CTPOP", "PERR", "CTLZ", "CTTZ",
 "UNPKBW", "UNPKBL", "PKWB", "PKLB",
 "MINSB8", "MINSW4", "MINUB8", "MINUW4",
 "MAXSB8", "MAXSW4", "MAXUB8", "MAXUW4",
 "FTOIT", "FTOIS",
 "LDF", "LDG", "LDS", "LDT",
 "STS", "STG", "STS", "STT",
 "LDL", "LDQ", "LDL_L", "LDQ_L",
 "STL", "STQ", "STL_L", "STQ_L",
 "BR", "FBEQ", "FBLT", "FBLE",
 "BSR", "FBNE", "BFGE", "FBGT",
 "BLBC", "BEQ", "BLT", "BLE",
 "BLBS", "BNE", "BGE", "BGT",
 NULL
 };

const uint32 opval[] = {
 0x00000000, C_PCM, 0x00000001, C_PCM, 0x00000002, C_PCM, 0x00000003, C_PVM,
 0x00000004, C_PVM, 0x00000005, C_PVM, 0x00000006, C_PVM, 0x00000007, C_PVM,
 0x00000008, C_PVM, 0x00000009, C_PCM, 0x0000000A, C_PCM, 0x0000000B, C_PVM,
 0x0000000C, C_PVM, 0x0000000D, C_PVM, 0x0000000E, C_PVM, 0x0000000F, C_PVM,
 0x00000010, C_PVM, 0x00000011, C_PVM, 0x00000012, C_PVM, 0x00000013, C_PVM,
 0x00000014, C_PVM, 0x00000015, C_PVM, 0x00000016, C_PVM, 0x00000017, C_PVM,
 0x00000018, C_PVM, 0x00000019, C_PVM, 0x0000001A, C_PVM, 0x0000001B, C_PVM,
 0x0000001C, C_PVM, 0x0000001D, C_PVM, 0x0000001E, C_PVM, 0x0000001F, C_PVM,
 0x00000020, C_PVM, 0x00000021, C_PVM, 0x00000022, C_PVM, 0x00000023, C_PVM,
 0x00000024, C_PVM, 0x00000025, C_PVM, 0x00000026, C_PVM, 0x00000027, C_PVM,
 0x00000029, C_PVM, 0x0000002A, C_PVM, 0x0000002B, C_PVM, 0x0000002E, C_PVM,
 0x00000030, C_PVM, 0x00000031, C_PVM, 0x00000032, C_PVM, 0x00000033, C_PVM,
 0x0000003E, C_PCM, 0x0000003F, C_PVM,
 0x00000080, C_PCM, 0x00000081, C_PCM, 0x00000082, C_PVM, 0x00000083, C_PVM,
 0x00000084, C_PVM, 0x00000085, C_PVM, 0x00000086, C_PCM, 0x00000087, C_PVM,
 0x00000088, C_PVM, 0x00000089, C_PVM, 0x0000008A, C_PVM, 0x0000008B, C_PVM,
 0x0000008C, C_PVM, 0x0000008D, C_PVM, 0x0000008E, C_PVM, 0x0000008F, C_PVM,
 0x00000090, C_PVM, 0x00000091, C_PVM, 0x00000092, C_PVM, 0x00000093, C_PVM,
 0x00000094, C_PVM, 0x00000095, C_PVM, 0x00000096, C_PVM, 0x00000097, C_PVM,
 0x00000098, C_PVM, 0x00000099, C_PVM, 0x0000009A, C_PVM, 0x0000009B, C_PVM,
 0x0000009C, C_PVM, 0x0000009D, C_PVM, 0x0000009E, C_PVM, 0x0000009F, C_PVM,
 0x000000A0, C_PVM, 0x000000A1, C_PVM, 0x000000A2, C_PVM, 0x000000A3, C_PVM,
 0x000000A4, C_PVM, 0x000000A5, C_PVM, 0x000000A6, C_PVM, 0x000000A7, C_PVM,
 0x000000A8, C_PVM, 0x000000A9, C_PVM, 0x000000AA, C_PCM, 0x000000AE, C_PCM,
 0x00000010, C_PUN, 0x00000011, C_PUN, 0x00000013, C_PUN, 0x00000014, C_PUN,
 0x0000002B, C_PUN, 0x0000002D, C_PUN, 0x0000002E, C_PUN,
 0x00000030, C_PUN, 0x00000031, C_PUN, 0x00000032, C_PUN, 0x00000033, C_PUN,
 0x00000034, C_PUN, 0x00000035, C_PUN, 0x00000036, C_PUN, 0x00000037, C_PUN,
 0x00000038, C_PUN, 0x00000039, C_PUN, 0x0000003A, C_PUN,
 0x0000003C, C_PUN, 0x0000003D, C_PUN, 0x0000003F, C_PUN,
 0x00000092, C_PUN, 0x0000009E, C_PUN, 0x0000009F, C_PUN,
 0x20000000, C_MR, 0x24000000, C_MR, 0x28000000, C_MR, 0x2C000000, C_MR,
 0x30000000, C_MR, 0x34000000, C_MR, 0x38000000, C_MR, 0x3C000000, C_MR,
 0x40000000, C_IO, 0x40000040, C_IO, 0x40000120, C_IO, 0x40000160, C_IO,
 0x400001C0, C_IO, 0x40000240, C_IO, 0x40000360, C_IO, 0x400003A0, C_IO,
 0x40000400, C_IO, 0x40000440, C_IO, 0x40000520, C_IO, 0x40000560, C_IO,
 0x400005A0, C_IO, 0x40000640, C_IO, 0x40000760, C_IO, 0x400007A0, C_IO,
 0x40000800, C_IO, 0x40000920, C_IO, 0x400009A0, C_IO,
 0x40000C00, C_IO, 0x40000D20, C_IO, 0x40000DA0, C_IO,
 0x44000000, C_IO, 0x44000100, C_IO, 0x44000280, C_IO, 0x440002C0, C_IO,
 0x44000400, C_IO, 0x44000480, C_IO, 0x440004C0, C_IO, 0x44000500, C_IO,
 0x44000800, C_IO, 0x44000880, C_IO, 0x440008C0, C_IO, 0x44000900, C_IO,
 0x44000C80, C_IO, 0x44000CC0, C_IO,
 0x48000040, C_IO, 0x480000C0, C_IO, 0x48000160, C_IO,
 0x48000240, C_IO, 0x480002C0, C_IO, 0x48000360, C_IO,
 0x48000440, C_IO, 0x480004C0, C_IO, 0x48000560, C_IO,
 0x48000600, C_IO, 0x48000620, C_IO, 0x48000640, C_IO, 0x48000680, C_IO,
 0x480006C0, C_IO, 0x48000720, C_IO, 0x48000760, C_IO, 0x48000780, C_IO,
 0x48000A40, C_IO, 0x48000AE0, C_IO, 0x48000B40, C_IO,
 0x48000C40, C_IO, 0x48000CE0, C_IO, 0x48000D40, C_IO,
 0x48000E40, C_IO, 0x48000EE0, C_IO, 0x48000F40, C_IO,
 0x4C000000, C_IO, 0x4C000400, C_IO, 0x4C000600, C_IO,
 0x4C000800, C_IO, 0x4C000C00, C_IO,
 0x501F0080, C_FAC, 0x501F0280, C_FAC, 0x501F0480, C_FAC,
 0x53E00140, C_FBC, 0x53E01140, C_FBC, 0x53E02140, C_FBC, 0x53E03140, C_FBC,
 0x53E08140, C_FBC, 0x53E09140, C_FBC, 0x53E0A140, C_FBC, 0x53E0B140, C_FBC,
 0x53E00540, C_FBC, 0x53E01540, C_FBC, 0x53E02540, C_FBC, 0x53E03540, C_FBC,
 0x53E08540, C_FBC, 0x53E09540, C_FBC, 0x53E0A540, C_FBC, 0x53E0B540, C_FBC,
 0x53E00160, C_FBC, 0x53E00960, C_FBC, 0x53E01160, C_FBC, 0x53E01960, C_FBC,
 0x53E02160, C_FBC, 0x53E02960, C_FBC, 0x53E03160, C_FBC, 0x53E03960, C_FBC,
 0x53E0A160, C_FBC, 0x53E0A960, C_FBC, 0x53E0B160, C_FBC, 0x53E0B960, C_FBC,
 0x53E0E160, C_FBC, 0x53E0E960, C_FBC, 0x53E0F160, C_FBC, 0x53E0F960, C_FBC,
 0x53E00560, C_FBC, 0x53E00D60, C_FBC, 0x53E01560, C_FBC, 0x53E01D60, C_FBC,
 0x53E02560, C_FBC, 0x53E02D60, C_FBC, 0x53E03560, C_FBC, 0x53E03D60, C_FBC,
 0x53E0A560, C_FBC, 0x53E0AD60, C_FBC, 0x53E0B560, C_FBC, 0x53E0BD60, C_FBC,
 0x53E0E560, C_FBC, 0x53E0ED60, C_FBC, 0x53E0F560, C_FBC, 0x53E0FD60, C_FBC,
 0x54000000, C_FO, 0x54001000, C_FO, 0x54002000, C_FO, 0x54003000, C_FO,
 0x54008000, C_FO, 0x54009000, C_FO, 0x5400A000, C_FO, 0x5400B000, C_FO,
 0x54000020, C_FO, 0x54001020, C_FO, 0x54002020, C_FO, 0x54003020, C_FO,
 0x54008020, C_FO, 0x54009020, C_FO, 0x5400A020, C_FO, 0x5400B020, C_FO,
 0x54000040, C_FO, 0x54001040, C_FO, 0x54002040, C_FO, 0x54003040, C_FO,
 0x54008040, C_FO, 0x54009040, C_FO, 0x5400A040, C_FO, 0x5400B040, C_FO,
 0x54000060, C_FO, 0x54001060, C_FO, 0x54002060, C_FO, 0x54003060, C_FO,
 0x54008060, C_FO, 0x54009060, C_FO, 0x5400A060, C_FO, 0x5400B060, C_FO,
 0x54000400, C_FO, 0x54001400, C_FO, 0x54002400, C_FO, 0x54003400, C_FO,
 0x54008400, C_FO, 0x54009400, C_FO, 0x5400A400, C_FO, 0x5400B400, C_FO,
 0x54000420, C_FO, 0x54001420, C_FO, 0x54002420, C_FO, 0x54003420, C_FO,
 0x54008420, C_FO, 0x54009420, C_FO, 0x5400A420, C_FO, 0x5400B420, C_FO,
 0x54000440, C_FO, 0x54001440, C_FO, 0x54002440, C_FO, 0x54003440, C_FO,
 0x54008440, C_FO, 0x54009440, C_FO, 0x5400A440, C_FO, 0x5400B440, C_FO,
 0x54000460, C_FO, 0x54001460, C_FO, 0x54002460, C_FO, 0x54003460, C_FO,
 0x54008460, C_FO, 0x54009460, C_FO, 0x5400A460, C_FO, 0x5400B460, C_FO,
 0x57E003C0, C_FBC, 0x57E013C0, C_FBC, 0x57E023C0, C_FBC, 0x57E033C0, C_FBC,
 0x57E083C0, C_FBC, 0x57E093C0, C_FBC, 0x57E0A3C0, C_FBC, 0x57E0B3C0, C_FBC,
 0x57E00580, C_FBC, 0x57E01580, C_FBC, 0x57E02580, C_FBC, 0x57E03580, C_FBC,
 0x57E08580, C_FBC, 0x57E09580, C_FBC, 0x57E0A580, C_FBC, 0x57E0B580, C_FBC,
 0x57E005A0, C_FBC, 0x57E015A0, C_FBC, 0x57E025A0, C_FBC, 0x57E035A0, C_FBC,
 0x57E085A0, C_FBC, 0x57E095A0, C_FBC, 0x57E0A5A0, C_FBC, 0x57E0B5A0, C_FBC,
 0x57E005E0, C_FBC, 0x57E015E0, C_FBC, 0x57E025E0, C_FBC, 0x57E035E0, C_FBC,
 0x57E085E0, C_FBC, 0x57E095E0, C_FBC, 0x57E0A5E0, C_FBC, 0x57E0B5E0, C_FBC,
 0x57E00780, C_FBC, 0x57E01780, C_FBC, 0x57E007C0, C_FBC, 0x57E017C0, C_FBC,
 0x540014A0, C_FO, 0x540094A0, C_FO, 0x540014C0, C_FO, 0x540094C0, C_FO,
 0x540014E0, C_FO, 0x540094E0, C_FO,
 0x58000000, C_FO, 0x58000800, C_FO, 0x58001000, C_FO, 0x58001800, C_FO,
 0x58002000, C_FO, 0x58002800, C_FO, 0x58003000, C_FO, 0x58003800, C_FO,
 0x5800A000, C_FO, 0x5800A800, C_FO, 0x5800B000, C_FO, 0x5800B800, C_FO,
 0x5800E000, C_FO, 0x5800E800, C_FO, 0x5800F000, C_FO, 0x5800F800, C_FO,
 0x58000020, C_FO, 0x58000820, C_FO, 0x58001020, C_FO, 0x58001820, C_FO,
 0x58002020, C_FO, 0x58002820, C_FO, 0x58003020, C_FO, 0x58003820, C_FO,
 0x5800A020, C_FO, 0x5800A820, C_FO, 0x5800B020, C_FO, 0x5800B820, C_FO,
 0x5800E020, C_FO, 0x5800E820, C_FO, 0x5800F020, C_FO, 0x5800F820, C_FO,
 0x58000040, C_FO, 0x58000840, C_FO, 0x58001040, C_FO, 0x58001840, C_FO,
 0x58002040, C_FO, 0x58002840, C_FO, 0x58003040, C_FO, 0x58003840, C_FO,
 0x5800A040, C_FO, 0x5800A840, C_FO, 0x5800B040, C_FO, 0x5800B840, C_FO,
 0x5800E040, C_FO, 0x5800E840, C_FO, 0x5800F040, C_FO, 0x5800F840, C_FO,
 0x58000060, C_FO, 0x58000860, C_FO, 0x58001060, C_FO, 0x58001860, C_FO,
 0x58002060, C_FO, 0x58002860, C_FO, 0x58003060, C_FO, 0x58003860, C_FO,
 0x5800A060, C_FO, 0x5800A860, C_FO, 0x5800B060, C_FO, 0x5800B860, C_FO,
 0x5800E060, C_FO, 0x5800E860, C_FO, 0x5800F060, C_FO, 0x5800F860, C_FO,
 0x58000400, C_FO, 0x58000C00, C_FO, 0x58001400, C_FO, 0x58001C00, C_FO,
 0x58002400, C_FO, 0x58002C00, C_FO, 0x58003400, C_FO, 0x58003C00, C_FO,
 0x5800A400, C_FO, 0x5800AC00, C_FO, 0x5800B400, C_FO, 0x5800BC00, C_FO,
 0x5800E400, C_FO, 0x5800EC00, C_FO, 0x5800F400, C_FO, 0x5800FC00, C_FO,
 0x58000420, C_FO, 0x58000C20, C_FO, 0x58001420, C_FO, 0x58001C20, C_FO,
 0x58002420, C_FO, 0x58002C20, C_FO, 0x58003420, C_FO, 0x58003C20, C_FO,
 0x5800A420, C_FO, 0x5800AC20, C_FO, 0x5800B420, C_FO, 0x5800BC20, C_FO,
 0x5800E420, C_FO, 0x5800EC20, C_FO, 0x5800F420, C_FO, 0x5800FC20, C_FO,
 0x58000440, C_FO, 0x58000C40, C_FO, 0x58001440, C_FO, 0x58001C40, C_FO,
 0x58002440, C_FO, 0x58002C40, C_FO, 0x58003440, C_FO, 0x58003C40, C_FO,
 0x5800A440, C_FO, 0x5800AC40, C_FO, 0x5800B440, C_FO, 0x5800BC40, C_FO,
 0x5800E440, C_FO, 0x5800EC40, C_FO, 0x5800F440, C_FO, 0x5800FC40, C_FO,
 0x58000460, C_FO, 0x58000C60, C_FO, 0x58001460, C_FO, 0x58001C60, C_FO,
 0x58002460, C_FO, 0x58002C60, C_FO, 0x58003460, C_FO, 0x58003C60, C_FO,
 0x5800A460, C_FO, 0x5800AC60, C_FO, 0x5800B460, C_FO, 0x5800BC60, C_FO,
 0x5800E460, C_FO, 0x5800EC60, C_FO, 0x5800F460, C_FO, 0x5800FC60, C_FO,
 0x5BE00580, C_FBC, 0x5BE00D80, C_FBC, 0x5BE01580, C_FBC, 0x5BE01D80, C_FBC,
 0x5BE02580, C_FBC, 0x5BE02D80, C_FBC, 0x5BE03580, C_FBC, 0x5BE03D80, C_FBC,
 0x5BE0A580, C_FBC, 0x5BE0AD80, C_FBC, 0x5BE0B580, C_FBC, 0x5BE0BD80, C_FBC,
 0x5BE0E580, C_FBC, 0x5BE0ED80, C_FBC, 0x5BE0F580, C_FBC, 0x5BE0FD80, C_FBC,
 0x5BE005E0, C_FBC, 0x5BE00DE0, C_FBC, 0x5BE015E0, C_FBC, 0x5BE01DE0, C_FBC,
 0x5BE025E0, C_FBC, 0x5BE02DE0, C_FBC, 0x5BE035E0, C_FBC, 0x5BE03DE0, C_FBC,
 0x5BE0A5E0, C_FBC, 0x5BE0ADE0, C_FBC, 0x5BE0B5E0, C_FBC, 0x5BE0BDE0, C_FBC,
 0x5BE0E5E0, C_FBC, 0x5BE0EDE0, C_FBC, 0x5BE0F5E0, C_FBC, 0x5BE0FDE0, C_FBC,
 0x5BE00780, C_FBC, 0x5BE00F80, C_FBC, 0x5BE01780, C_FBC, 0x5BE01F80, C_FBC,
 0x5BE0E780, C_FBC, 0x5BE0EF80, C_FBC, 0x5BE0F780, C_FBC, 0x5BE0FF80, C_FBC,
 0x5BE007C0, C_FBC, 0x5BE00FC0, C_FBC, 0x5BE017C0, C_FBC, 0x5BE01FC0, C_FBC,
 0x5BE0E7C0, C_FBC, 0x5BE0EFC0, C_FBC, 0x5BE0F7C0, C_FBC, 0x5BE0FFC0, C_FBC,
 0x58001480, C_FO, 0x58009480, C_FO, 0x580014A0, C_FO, 0x580094A0, C_FO,
 0x580014C0, C_FO, 0x580094C0, C_FO, 0x580014E0, C_FO, 0x580094E0, C_FO,
 0x5FE00200, C_IBC, 0x5C000400, C_IO, 0x5C000420, C_IO, 0x5C000440, C_IO,
 0x5C000480, C_IO, 0x5C0004A0, C_IO,
 0x5C000540, C_IO, 0x5C000560, C_IO, 0x5C000580, C_IO,
 0x5C0005A0, C_IO, 0x5C0005C0, C_IO, 0x5C0005E0, C_IO,
 0x5FE00060, C_IBC, 0x5FE00260, C_IBC, 0x5FE00A60, C_IBC,
 0x60000000, C_NO, 0x60000400, C_NO, 0x60004000, C_NO, 0x60004400, C_NO,
 0x60008000, C_FE, 0x6000A000, C_FE, 0x6000C000, C_NO,
 0x6000E000, C_RV, 0x6000F000, C_RV,
 0x68000000, C_JP, 0x68004000, C_JP, 0x68008000, C_JP, 0x6800C000, C_JP,
 0x73E00000, C_IBC, 0x73E00020, C_IBC,
 0x73E00600, C_IBC, 0x70000620, C_IO,  0x73E00640, C_IBC, 0x73E00660, C_IBC,
 0x73E00680, C_IBC, 0x73E006A0, C_IBC, 0x73E006C0, C_IBC, 0x73E006E0, C_IBC,
 0x70000700, C_IO,  0x70000720, C_IO,  0x70000740, C_IO,  0x70000780, C_IO,
 0x70000780, C_IO,  0x700007A0, C_IO,  0x700007C0, C_IO,  0x700007E0, C_IO,
 0x701F0E00, C_IAC, 0x701F0F00, C_IAC,
 0x80000000, C_MR, 0x84000000, C_MR, 0x88000000, C_MR, 0x8C000000, C_MR,
 0x90000000, C_MR, 0x94000000, C_MR, 0x98000000, C_MR, 0x9C000000, C_MR,
 0xA0000000, C_MR, 0xA4000000, C_MR, 0xA8000000, C_MR, 0xAC000000, C_MR,
 0xB0000000, C_MR, 0xB4000000, C_MR, 0xB8000000, C_MR, 0xBC000000, C_MR,
 0xC0000000, C_BR, 0xC4000000, C_BR, 0xC8000000, C_BR, 0xCC000000, C_BR,
 0xD0000000, C_BR, 0xD4000000, C_BR, 0xD8000000, C_BR, 0xDC000000, C_BR,
 0xE0000000, C_BR, 0xE4000000, C_BR, 0xE8000000, C_BR, 0xEC000000, C_BR,
 0xF0000000, C_BR, 0xF4000000, C_BR, 0xF8000000, C_BR, 0xFC000000, C_BR,
 M32, 0
 };

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       values to decode
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        return  =       if >= 0, error code
                        if < 0, number of extra bytes retired
*/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
uint32 c, sc, rdx;
t_stat r;
DEVICE *dptr;

if (uptr == NULL) uptr = &cpu_unit;                     /* anon = CPU */
else if (uptr != &cpu_unit) return SCPE_ARG;            /* CPU only */
dptr = find_dev_from_unit (uptr);                       /* find dev */
if (dptr == NULL) return SCPE_IERR;
if (sw & SWMASK ('D')) rdx = 10;                        /* get radix */
else if (sw & SWMASK ('O')) rdx = 8;
else if (sw & SWMASK ('H')) rdx = 16;
else rdx = dptr->dradix;

if (sw & SWMASK ('A')) {                                /* ASCII? */
    sc = (uint32) (addr & 0x7) * 8;                     /* shift count */
    c = (uint32) (val[0] >> sc) & 0x7F;
    fprintf (of, (c < 0x20)? "<%02X>": "%c", c);
    return 0;
    }
if (sw & SWMASK ('B')) {                                /* byte? */
    sc = (uint32) (addr & 0x7) * 8;                     /* shift count */
    c = (uint32) (val[0] >> sc) & M8;
    fprintf (of, "%02X", c);
    return 0;
    }
if (sw & SWMASK ('W')) {                                /* word? */
    sc = (uint32) (addr & 0x6) * 8;                     /* shift count */
    c = (uint32) (val[0] >> sc) & M16;
    fprintf (of, "%04X", c);
    return -1;
    }
if (sw & SWMASK ('L')) {                                /* long? */
    if (addr & 4) c = (uint32) (val[0] >> 32) & M32;
    else c = (uint32) val[0] & M32;
    fprintf (of, "%08X", c);
    return -3;
    }
if (sw & SWMASK ('C')) {                                /* char format? */
    for (sc = 0; sc < 64; sc = sc + 8) {                /* print string */
        c = (uint32) (val[0] >> sc) & 0x7F;
        fprintf (of, (c < 0x20)? "<%02X>": "%c", c);
        }
    return -7;                                          /* return # chars */
    }
if (sw & SWMASK ('M')) {                                /* inst format? */
    if (addr & 4) c = (uint32) (val[0] >> 32) & M32;
    else c = (uint32) val[0] & M32;
    r = fprint_sym_m (of, addr, c);                     /* decode inst */
    if (r <= 0) return r;
    }

fprint_val (of, val[0], rdx, 64, PV_RZRO);
return -7;
}

/* Symbolic decode for -m

   Inputs:
        of      =       output stream
        addr    =       current PC
        inst    =       instruction to decode
   Outputs:
        return  =       if >= 0, error code
                        if < 0, number of extra bytes retired (-3)
*/

t_stat fprint_sym_m (FILE *of, t_addr addr, uint32 inst)
{
uint32 i, j, k, fl, ra, rb, rc, md, bd, jd, lit8, any;
t_stat r;

if ((r = fprint_pal_hwre (of, inst)) < 0) return r;     /* PAL instruction? */
for (i = 0; opval[i] != M32; i = i + 2) {               /* loop thru ops */
    fl = opval[i + 1];                                  /* flags */
    j = fl & CL_CLASS;                                  /* get class */
    k = i >> 1;
    if (((opval[i] & masks[j]) == (inst & masks[j])) && /* match? */
        ((j != CL_NO) || (fl & PAL_MASK (pal_type)))) {
        ra = I_GETRA (inst);                            /* all fields */
        rb = I_GETRB (inst);
        rc = I_GETRC (inst);
        lit8 = I_GETLIT8 (inst);
        md = I_GETMDSP (inst);
        bd = I_GETBDSP (inst);
        jd = inst & 0x3FFF;
        any = 0;
        fprintf (of, "%s", opcode[k]);                  /* opcode */
        if (fl & FL_RA)                                 /* ra? */
            any = fprintf (of, " R%d", ra);
        if (fl & FL_BDP) {                              /* branch? */
            addr = (addr + (SEXT_BDSP (bd) << 2) + 4) & M64;
            any = fprintf (of, (any? ",": " "));
            fprint_val (of, addr, 16, 64, PV_LEFT);
            }
        else if (fl & FL_MDP) {                         /* mem ref? */
            if ((fl & FL_RBI) && (rb != 31))
                any = fprintf (of, (any? ",%X(R%d)": " %X(R%d)"), md, rb);
            else any = fprintf (of, (any? ",%X": " %X"), md);
            }
        else if (fl & FL_RB) {                          /* rb? */
            if (fl & FL_RBI)
                any = fprintf (of, (any? ",(R%d)": " (R%d)"), rb);
            else if ((fl & FL_LIT) && (inst & I_ILIT))
                any = fprintf (of, (any? ",#%X": " #%X"), lit8);
            else any = fprintf (of, (any? ",R%d": " R%d"), rb);
            }
        if ((fl & FL_JDP) && jd)                        /* jmp? */
            any = fprintf (of, (any? ",%X": " %X"), jd);
        else if (fl & FL_RC)                            /* rc? */
            any = fprintf (of, (any? ",R%d": " R%d"), rc);
        return -3;
        }                                               /* end if */
    }                                                   /* end for */
return SCPE_ARG;
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

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
t_value num;
uint32 i, sc, rdx;
t_stat r;
DEVICE *dptr;

if (uptr == NULL) uptr = &cpu_unit;                     /* anon = CPU */
else if (uptr != &cpu_unit) return SCPE_ARG;            /* CPU only */
dptr = find_dev_from_unit (uptr);                       /* find dev */
if (dptr == NULL) return SCPE_IERR;
if (sw & SWMASK ('D')) rdx = 10;                        /* get radix */
else if (sw & SWMASK ('O')) rdx = 8;
else if (sw & SWMASK ('H')) rdx = 16;
else rdx = dptr->dradix;

if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    sc = (uint32) (addr & 0x7) * 8;                     /* shift count */
    val[0] = (val[0] & ~(((t_uint64) M8) << sc)) |
        (((t_uint64) cptr[0]) << sc);
    return 0;
    }
if (sw & SWMASK ('B')) {                                /* byte? */
    num = get_uint (cptr, rdx, M8, &r);                 /* get byte */
    if (r != SCPE_OK) return SCPE_ARG;
    sc = (uint32) (addr & 0x7) * 8;                     /* shift count */
    val[0] = (val[0] & ~(((t_uint64) M8) << sc)) |
        (num << sc);
    return 0;
    }
if (sw & SWMASK ('W')) {                                /* word? */
    num = get_uint (cptr, rdx, M16, &r);                /* get word */
    if (r != SCPE_OK) return SCPE_ARG;
    sc = (uint32) (addr & 0x6) * 8;                     /* shift count */
    val[0] = (val[0] & ~(((t_uint64) M16) << sc)) |
        (num << sc);
    return -1;
    }
if (sw & SWMASK ('L')) {                                /* longword? */
    num = get_uint (cptr, rdx, M32, &r);                /* get longword */
    if (r != SCPE_OK) return SCPE_ARG;
    sc = (uint32) (addr & 0x4) * 8;                     /* shift count */
    val[0] = (val[0] & ~(((t_uint64) M32) << sc)) |
        (num << sc);
    return -3;
    }
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* ASCII chars? */
    if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    for (i = 0; i < 8; i++) {
        if (cptr[i] == 0) break;
        sc = i * 8;
        val[0] = (val[0] & ~(((t_uint64) M8) << sc)) |
            (((t_uint64) cptr[i]) << sc);
        }
    return -7;
    }

if ((addr & 3) == 0) {                                  /* aligned only */
    r = parse_sym_m (cptr, addr, &num);                 /* try to parse inst */
    if (r <= 0) {                                       /* ok? */
        sc = (uint32) (addr & 0x4) * 8;                 /* shift count */
        val[0] = (val[0] & ~(((t_uint64) M32) << sc)) |
            (num << sc);
            return -3;
        }
    }

val[0] = get_uint (cptr, rdx, M64, &r);                 /* get number */
if (r != SCPE_OK) return r;
return -7;
}

/* Symbolic input

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        *val    =       pointer to output values
   Outputs:
        status  =       > 0   error code
                        <= 0  -number of extra words
*/

t_stat parse_sym_m (CONST char *cptr, t_addr addr, t_value *inst)
{
t_uint64 bra, df, db;
uint32 i, k, lit8, fl;
int32 reg;
t_stat r;
CONST char *tptr;
char gbuf[CBUFSIZE];

if ((r = parse_pal_hwre (cptr, inst)) < 0) return r;    /* PAL hardware? */
cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
for (i = 0; opcode[i] != NULL; i++) {                   /* loop thru opcodes */
    if (strcmp (opcode[i], gbuf) == 0) {                /* string match? */
        k = i << 1;                                     /* index to opval */
        fl = opval[k + 1];                              /* get flags */
        if (((fl & CL_CLASS) != CL_NO) ||               /* not PAL or */
            (fl & PAL_MASK (pal_type))) break;          /* PAL type match? */
        }
    }
if (opcode[i] == NULL) return SCPE_ARG;
*inst = opval[k];                                       /* save base op */

if (fl & FL_RA) {                                       /* need Ra? */
    cptr = get_glyph (cptr, gbuf, ',');                 /* get reg */
    if ((reg = parse_reg (gbuf)) < 0) return SCPE_ARG;
    *inst = *inst | (reg << I_V_RA);
    }
if (fl & FL_BDP) {                                      /* need branch disp? */
    cptr = get_glyph (cptr, gbuf, 0);
    bra = get_uint (gbuf, 16, M64, &r);
    if ((r != SCPE_OK) || (bra & 3)) return SCPE_ARG;
    df = ((bra - (addr + 4)) >> 2) & I_M_BDSP;
    db = ((addr + 4 - bra) >> 2) & I_M_BDSP;
    if (bra == ((addr + 4 + (SEXT_BDSP (df) << 2)) & M64))
        *inst = *inst | (uint32) df;
    else if (bra == ((addr + 4 + (SEXT_BDSP (db) << 2)) & M64))
        *inst = *inst | (uint32) db;
    else return SCPE_ARG;
    }
else if (fl & FL_MDP) {                                 /* need mem disp? */
    cptr = get_glyph (cptr, gbuf, 0);
    df = strtotv (gbuf, &tptr, 16);
    if ((gbuf == tptr) || (df > I_M_MDSP)) return SCPE_ARG;
    *inst = *inst | (uint32) df;
    if (*tptr == '(') {
        tptr = get_glyph (tptr + 1, gbuf, ')');
        if ((reg = parse_reg (gbuf)) < 0) return SCPE_ARG;
        *inst = *inst | (reg << I_V_RB);
        }
    else *inst = *inst | (31 << I_V_RB);
    if (*tptr != 0) return SCPE_ARG;
    }
else if (fl & FL_RBI) {                                 /* indexed? */
    cptr = get_glyph (cptr, gbuf, ',');
    if (gbuf[0] != '(') return SCPE_ARG;
    tptr = get_glyph (gbuf + 1, gbuf, ')');
    if ((reg = parse_reg (gbuf)) < 0) return SCPE_ARG;
    *inst = *inst | (reg << I_V_RB);
    if (*tptr != 0) return SCPE_ARG;
    }
else if (fl & FL_RB) {
    cptr = get_glyph (cptr, gbuf, ',');                 /* get reg/lit */
    if ((gbuf[0] == '#') && (fl & FL_LIT)) {            /* literal? */
        lit8 = (uint32) get_uint (gbuf + 1, 16, I_M_LIT8, &r);
        if (r != SCPE_OK) return r;
        *inst = *inst | I_ILIT | (lit8 << I_V_LIT8);
        }
    else {                                              /* rb */
        if ((reg = parse_reg (gbuf)) < 0) return SCPE_ARG;
        *inst = *inst | (reg << I_V_RB);
        }
    }
if (fl & FL_JDP) {                                      /* jmp? */
    cptr = get_glyph (cptr, gbuf, 0);                   /* get disp */
    df = get_uint (gbuf, 16, 0x3FFF, &r);
    if (r != SCPE_OK) return r;
    *inst = *inst | df;
    }
else if (fl & FL_RC) {                                  /* rc? */
    cptr = get_glyph (cptr, gbuf, ',');                 /* get reg */
    if ((reg = parse_reg (gbuf)) < 0) return SCPE_ARG;
    *inst = *inst | (reg << I_V_RC);
    }

if (*cptr != 0) return SCPE_ARG;                        /* any leftovers? */
return -3;
}

/* Parse a register */

int32 parse_reg (CONST char *cptr)
{
t_stat r;
int32 reg;

if ((*cptr == 'R') || (*cptr == 'r') ||
    (*cptr == 'F') || (*cptr == 'f')) cptr++;
reg = (int32) get_uint (cptr, 10, 31, &r);
if (r != SCPE_OK) return -1;
return reg;
}

