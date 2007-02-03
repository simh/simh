/* pdp8_sys.c: PDP-8 simulator interface

   Copyright (c) 1993-2006, Robert M Supnik

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

   15-Dec-06    RMS     Added TA8E support, IOT disambiguation
   30-Oct-06    RMS     Added infinite loop stop
   18-Oct-06    RMS     Re-ordered device list
   17-Oct-03    RMS     Added TSC8-75, TD8E support, DECtape off reel message
   25-Apr-03    RMS     Revised for extended file support
   30-Dec-01    RMS     Revised for new TTX
   26-Nov-01    RMS     Added RL8A support
   17-Sep-01    RMS     Removed multiconsole support
   16-Sep-01    RMS     Added TSS/8 packed char support, added KL8A support
   27-May-01    RMS     Added multiconsole support
   18-Mar-01    RMS     Added DF32 support
   14-Mar-01    RMS     Added extension detection of RIM binary tapes
   15-Feb-01    RMS     Added DECtape support
   30-Oct-00    RMS     Added support for examine to file
   27-Oct-98    RMS     V2.4 load interface
   10-Apr-98    RMS     Added RIM loader support
   17-Feb-97    RMS     Fixed bug in handling of bin loader fields
*/

#include "pdp8_defs.h"
#include <ctype.h>

extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern DEVICE tsc_dev;
extern DEVICE ptr_dev, ptp_dev;
extern DEVICE tti_dev, tto_dev;
extern DEVICE clk_dev, lpt_dev;
extern DEVICE rk_dev, rl_dev;
extern DEVICE rx_dev;
extern DEVICE df_dev, rf_dev;
extern DEVICE dt_dev, td_dev;
extern DEVICE mt_dev, ct_dev;
extern DEVICE ttix_dev, ttox_dev;
extern REG cpu_reg[];
extern uint16 M[];
extern int32 sim_switches;

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             maximum number of words for examine/deposit
   sim_devices          array of pointers to simulated devices
   sim_consoles         array of pointers to consoles (if more than one)
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "PDP-8";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 4;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &tsc_dev,
    &clk_dev,
    &ptr_dev,
    &ptp_dev,
    &tti_dev,
    &tto_dev,
    &ttix_dev,
    &ttox_dev,
    &lpt_dev,
    &rk_dev,
    &rl_dev,
    &rx_dev,
    &df_dev,
    &rf_dev,
    &dt_dev,
    &td_dev,
    &mt_dev,
    &ct_dev,
    NULL
    };

const char *sim_stop_messages[] = {
    "Unknown error",
    "Unimplemented instruction",
    "HALT instruction",
    "Breakpoint",
    "Non-standard device number",
    "DECtape off reel",
    "Infinite loop"
    };

/* Ambiguous device list - these devices have overlapped IOT codes */

DEVICE *amb_dev[] = {
    &rl_dev,
    &ct_dev,
    &td_dev,
    NULL
    };

#define AMB_RL      (1 << 12)
#define AMB_CT      (2 << 12)
#define AMB_TD      (3 << 12)

/* Binary loader

   Two loader formats are supported: RIM loader (-r) and BIN (-b) loader.

   RIM loader format consists of alternating pairs of addresses and 12-bit
   words.  It can only operate in field 0 and is not checksummed.

   BIN loader format consists of a string of 12-bit words (made up from
   7-bit characters) between leader and trailer (200).  The last word on
   tape is the checksum.  A word with the "link" bit set is a new origin;
   a character > 0200 indicates a change of field.
*/

t_stat sim_load (FILE *fileref, char *cptr, char *fnam, int flag)
{
int32 rubout, word, low, high, csum, newf, state, i;
uint32 origin, field;

if ((*cptr != 0) || (flag != 0)) return SCPE_ARG;
rubout = state = field = newf = origin = csum = 0;
if ((sim_switches & SWMASK ('R')) ||                    /* RIM format? */
    (match_ext (fnam, "RIM") && !(sim_switches & SWMASK ('B')))) {
    while ((i = getc (fileref)) != EOF) {
        switch (state) {

        case 0:                                         /* leader */
            if ((i != 0) && (i < 0200)) state = 1;
            high = i;
            break;

        case 1:                                         /* low byte */
            word = (high << 6) | i;                     /* form word */
            if (word > 07777) origin = word & 07777;
            else M[origin] = word;
            state = 2;
            break;

        case 2:                                         /* high byte */
            if (i >= 0200) return SCPE_OK;              /* end of tape? */
            high = i;                                   /* save high */
            state = 1;
            break;
            }                                           /* end switch */
        }                                               /* end while */
    }                                                   /* end if */

else {
    while ((i = getc (fileref)) != EOF) {               /* BIN format */
        if (rubout) {
            rubout = 0;
            continue;
            }
        if (i == 0377) {
            rubout = 1;
            continue;
            }
        if (i > 0200) {
            newf = (i & 070) << 9;
            continue;
            }
        switch (state) {

        case 0:                                         /* leader */
            if ((i != 0) && (i != 0200)) state = 1;
            high = i;                                   /* save as high */
            break;

        case 1:                                         /* low byte */
            low = i;
            state = 2;
            break;

        case 2:                                         /* high with test */
            word = (high << 6) | low;
            if (i == 0200) {                            /* end of tape? */
                if ((csum - word) & 07777) return SCPE_CSUM;
                return SCPE_OK;
                }
            csum = csum + low + high;
            if (word >= 010000) origin = word & 07777;
            else {
                if ((field | origin) >= MEMSIZE) 
                    return SCPE_NXM;
                M[field | origin] = word & 07777;
                origin = (origin + 1) & 07777;
                }
            field = newf;
            high = i;
            state = 1;
            break;
            }                                           /* end switch */
        }                                               /* end while */
    }                                                   /* end else */
return SCPE_FMT;                                        /* eof? error */
}

/* Symbol tables */

#define I_V_FL          18                              /* flag start */
#define I_M_FL          07                              /* flag mask */
#define I_V_NPN         0                               /* no operand */
#define I_V_FLD         1                               /* field change */
#define I_V_MRF         2                               /* mem ref */
#define I_V_IOT         3                               /* general IOT */
#define I_V_OP1         4                               /* operate 1 */
#define I_V_OP2         5                               /* operate 2 */
#define I_V_OP3         6                               /* operate 3 */
#define I_V_IOA         7                               /* ambiguous IOT */
#define I_NPN           (I_V_NPN << I_V_FL)
#define I_FLD           (I_V_FLD << I_V_FL)
#define I_MRF           (I_V_MRF << I_V_FL)
#define I_IOT           (I_V_IOT << I_V_FL)
#define I_OP1           (I_V_OP1 << I_V_FL)
#define I_OP2           (I_V_OP2 << I_V_FL)
#define I_OP3           (I_V_OP3 << I_V_FL)
#define I_IOA           (I_V_IOA << I_V_FL)

static const int32 masks[] = {
 07777, 07707, 07000, 07000,
 07416, 07571, 017457, 077777,
 };

/* Ambiguous device mnemonics must precede default mnemonics */

static const char *opcode[] = {
 "SKON", "ION", "IOF", "SRQ",                           /* std IOTs */
 "GTF", "RTF", "SGT", "CAF",
 "RPE", "RSF", "RRB", "RFC", "RFC RRB",                 /* reader/punch */
 "PCE", "PSF", "PCF", "PPC", "PLS",
 "KCF", "KSF", "KCC", "KRS", "KIE", "KRB",              /* console */
 "TLF", "TSF", "TCF", "TPC", "SPI", "TLS",
 "SBE", "SPL", "CAL",                                   /* power fail */
 "CLEI", "CLDI", "CLSC", "CLLE", "CLCL", "CLSK",        /* clock */
 "CINT", "RDF", "RIF", "RIB",                           /* mem mmgt */
 "RMF", "SINT", "CUF", "SUF",
 "RLDC", "RLSD", "RLMA", "RLCA",                        /* RL - ambiguous */
 "RLCB", "RLSA", "RLWC",
 "RRER", "RRWC", "RRCA", "RRCB",
 "RRSA", "RRSI", "RLSE",
 "KCLR", "KSDR", "KSEN", "KSBF",                        /* CT - ambiguous */
 "KLSA", "KSAF", "KGOA", "KRSB",
 "SDSS", "SDST", "SDSQ",                                /* TD - ambiguous */
 "SDLC", "SDLD", "SDRC", "SDRD",
 "ADCL", "ADLM", "ADST", "ADRB",                        /* A/D */
 "ADSK", "ADSE", "ADLE", "ADRS",
 "DCMA", "DMAR", "DMAW",                                /* DF/RF */
 "DCIM", "DSAC", "DIML", "DIMA",
 "DCEA",         "DEAL", "DEAC",
 "DFSE", "DFSC", "DISK", "DMAC",
 "DCXA", "DXAL", "DXAC",
 "PSKF", "PCLF", "PSKE",                                /* LPT */
 "PSTB", "PSIE", "PCLF PSTB", "PCIE",
 "LWCR", "CWCR", "LCAR",                                /* MT */
 "CCAR", "LCMR", "LFGR", "LDBR",
 "RWCR", "CLT", "RCAR",
 "RMSR", "RCMR", "RFSR", "RDBR",
 "SKEF", "SKCB", "SKJD", "SKTR", "CLF",
 "DSKP", "DCLR", "DLAG",                                /* RK */
 "DLCA", "DRST", "DLDC", "DMAN",
 "LCD", "XDR", "STR",                                   /* RX */
 "SER", "SDN", "INTR", "INIT",
 "DTRA", "DTCA", "DTXA", "DTLA",                        /* DT */
 "DTSF", "DTRB", "DTLB",
 "ETDS", "ESKP", "ECTF", "ECDF",                        /* TSC75 */
 "ERTB", "ESME", "ERIOT", "ETEN",

 "CDF", "CIF", "CIF CDF",
 "AND", "TAD", "ISZ", "DCA", "JMS", "JMP", "IOT",
 "NOP", "NOP2", "NOP3", "SWAB", "SWBA",
 "STL", "GLK", "STA", "LAS", "CIA",
 "BSW", "RAL", "RTL", "RAR", "RTR", "RAL RAR", "RTL RTR",
 "SKP", "SNL", "SZL",
 "SZA", "SNA", "SZA SNL", "SNA SZL",
 "SMA", "SPA", "SMA SNL", "SPA SZL",
 "SMA SZA", "SPA SNA", "SMA SZA SNL", "SPA SNA SZL",
 "SCL", "MUY", "DVI", "NMI", "SHL", "ASR", "LSR",
 "SCA", "SCA SCL", "SCA MUY", "SCA DVI",
 "SCA NMI", "SCA SHL", "SCA ASR", "SCA LSR",
 "ACS", "MUY", "DVI", "NMI", "SHL", "ASR", "LSR",
 "SCA", "DAD", "DST", "SWBA",
 "DPSZ", "DPIC", "DCIM", "SAM",
 "CLA", "CLL", "CMA", "CML", "IAC",                     /* encode only */
 "CLA", "OAS", "HLT",
 "CLA", "MQA", "MQL",
 NULL, NULL, NULL, NULL,                                /* decode only */
 NULL
 };
 
static const int32 opc_val[] = {
 06000+I_NPN, 06001+I_NPN, 06002+I_NPN, 06003+I_NPN,
 06004+I_NPN, 06005+I_NPN, 06006+I_NPN, 06007+I_NPN,
 06010+I_NPN, 06011+I_NPN, 06012+I_NPN, 06014+I_NPN, 06016+I_NPN,
 06020+I_NPN, 06021+I_NPN, 06022+I_NPN, 06024+I_NPN, 06026+I_NPN,
 06030+I_NPN, 06031+I_NPN, 06032+I_NPN, 06034+I_NPN, 06035+I_NPN, 06036+I_NPN,
 06040+I_NPN, 06041+I_NPN, 06042+I_NPN, 06044+I_NPN, 06045+I_NPN, 06046+I_NPN,
 06101+I_NPN, 06102+I_NPN, 06103+I_NPN,
 06131+I_NPN, 06132+I_NPN, 06133+I_NPN, 06135+I_NPN, 06136+I_NPN, 06137+I_NPN,
 06204+I_NPN, 06214+I_NPN, 06224+I_NPN, 06234+I_NPN,
 06244+I_NPN, 06254+I_NPN, 06264+I_NPN, 06274+I_NPN,
 06600+I_IOA+AMB_RL, 06601+I_IOA+AMB_RL, 06602+I_IOA+AMB_RL, 06603+I_IOA+AMB_RL,
 06604+I_IOA+AMB_RL, 06605+I_IOA+AMB_RL, 06607+I_IOA+AMB_RL,
 06610+I_IOA+AMB_RL, 06611+I_IOA+AMB_RL, 06612+I_IOA+AMB_RL, 06613+I_IOA+AMB_RL,
 06614+I_IOA+AMB_RL, 06615+I_IOA+AMB_RL, 06617+I_IOA+AMB_RL,
 06700+I_IOA+AMB_CT, 06701+I_IOA+AMB_CT, 06702+I_IOA+AMB_CT, 06703+I_IOA+AMB_CT,
 06704+I_IOA+AMB_CT, 06705+I_IOA+AMB_CT, 06706+I_IOA+AMB_CT, 06707+I_IOA+AMB_CT,
 06771+I_IOA+AMB_TD, 06772+I_IOA+AMB_TD, 06773+I_IOA+AMB_TD,
 06774+I_IOA+AMB_TD, 06775+I_IOA+AMB_TD, 06776+I_IOA+AMB_TD, 06777+I_IOA+AMB_TD,
 06530+I_NPN, 06531+I_NPN, 06532+I_NPN, 06533+I_NPN,    /* AD */
 06534+I_NPN, 06535+I_NPN, 06536+I_NPN, 06537+I_NPN,
 06601+I_NPN, 06603+I_NPN, 06605+I_NPN,                 /* DF/RF */
 06611+I_NPN, 06612+I_NPN, 06615+I_NPN, 06616+I_NPN,
 06611+I_NPN,              06615+I_NPN, 06616+I_NPN,
 06621+I_NPN, 06622+I_NPN, 06623+I_NPN, 06626+I_NPN,
 06641+I_NPN, 06643+I_NPN, 06645+I_NPN,
 06661+I_NPN, 06662+I_NPN, 06663+I_NPN,                 /* LPT */
 06664+I_NPN, 06665+I_NPN, 06666+I_NPN, 06667+I_NPN,
 06701+I_NPN, 06702+I_NPN, 06703+I_NPN,                 /* MT */
 06704+I_NPN, 06705+I_NPN, 06706+I_NPN, 06707+I_NPN,
 06711+I_NPN, 06712+I_NPN, 06713+I_NPN,
 06714+I_NPN, 06715+I_NPN, 06716+I_NPN, 06717+I_NPN,
 06721+I_NPN, 06722+I_NPN, 06723+I_NPN, 06724+I_NPN, 06725+I_NPN,
 06741+I_NPN, 06742+I_NPN, 06743+I_NPN,                 /* RK */
 06744+I_NPN, 06745+I_NPN, 06746+I_NPN, 06747+I_NPN,
 06751+I_NPN, 06752+I_NPN, 06753+I_NPN,                 /* RX */
 06754+I_NPN, 06755+I_NPN, 06756+I_NPN, 06757+I_NPN,
 06761+I_NPN, 06762+I_NPN, 06764+I_NPN, 06766+I_NPN,    /* DT */
 06771+I_NPN, 06772+I_NPN, 06774+I_NPN,
 06360+I_NPN, 06361+I_NPN, 06362+I_NPN, 06363+I_NPN,    /* TSC */
 06364+I_NPN, 06365+I_NPN, 06366+I_NPN, 06367+I_NPN,

 06201+I_FLD, 06202+I_FLD, 06203+I_FLD,
 00000+I_MRF, 01000+I_MRF, 02000+I_MRF, 03000+I_MRF,
 04000+I_MRF, 05000+I_MRF, 06000+I_IOT,
 07000+I_NPN, 07400+I_NPN, 07401+I_NPN, 07431+I_NPN, 07447+I_NPN,
 07120+I_NPN, 07204+I_NPN, 07240+I_NPN, 07604+I_NPN, 07041+I_NPN,
 07002+I_OP1, 07004+I_OP1, 07006+I_OP1,
 07010+I_OP1, 07012+I_OP1, 07014+I_OP1, 07016+I_OP1,
 07410+I_OP2, 07420+I_OP2, 07430+I_OP2,
 07440+I_OP2, 07450+I_OP2, 07460+I_OP2, 07470+I_OP2,
 07500+I_OP2, 07510+I_OP2, 07520+I_OP2, 07530+I_OP2,
 07540+I_OP2, 07550+I_OP2, 07560+I_OP2, 07570+I_OP2,
 07403+I_OP3, 07405+I_OP3, 07407+I_OP3,
 07411+I_OP3, 07413+I_OP3, 07415+I_OP3, 07417+I_OP3,
 07441+I_OP3, 07443+I_OP3, 07445+I_OP3, 07447+I_OP3,
 07451+I_OP3, 07453+I_OP3, 07455+I_OP3, 07457+I_OP3,
 017403+I_OP3, 017405+I_OP3, 0174017+I_OP3,
 017411+I_OP3, 017413+I_OP3, 017415+I_OP3, 017417+I_OP3,
 017441+I_OP3, 017443+I_OP3, 017445+I_OP3, 017447+I_OP3,
 017451+I_OP3, 017453+I_OP3, 017455+I_OP3, 017457+I_OP3,
 07200+I_OP1, 07100+I_OP1, 07040+I_OP1, 07020+I_OP1, 07001+I_OP1,
 07600+I_OP2, 07404+I_OP2, 07402+I_OP2,
 07601+I_OP3, 07501+I_OP3, 07421+I_OP3,
 07000+I_OP1, 07400+I_OP2, 07401+I_OP3, 017401+I_OP3,
 -1
 };

/* Operate decode

   Inputs:
        *of     =       output stream
        inst    =       mask bits
        class   =       instruction class code
        sp      =       space needed?
   Outputs:
        status  =       space needed
*/

int32 fprint_opr (FILE *of, int32 inst, int32 class, int32 sp)
{
int32 i, j;

for (i = 0; opc_val[i] >= 0; i++) {                     /* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;                /* get class */
    if ((j == class) && (opc_val[i] & inst)) {          /* same class? */
        inst = inst & ~opc_val[i];                      /* mask bit set? */
        fprintf (of, (sp? " %s": "%s"), opcode[i]);
        sp = 1;
        }
    }
return sp;
}

/* Symbolic decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       pointer to data
        *uptr   =       pointer to unit 
        sw      =       switches
   Outputs:
        return  =       status code
*/

#define FMTASC(x) ((x) < 040)? "<%03o>": "%c", (x)
#define SIXTOASC(x) (((x) >= 040)? (x): (x) + 0100)
#define TSSTOASC(x) ((x) + 040)

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 cflag, i, j, sp, inst, disp, opc;
extern int32 emode;

cflag = (uptr == NULL) || (uptr == &cpu_unit);
inst = val[0];
if (sw & SWMASK ('A')) {                                /* ASCII? */
    if (inst > 0377) return SCPE_ARG;
    fprintf (of, FMTASC (inst & 0177));
    return SCPE_OK;
    }
if (sw & SWMASK ('C')) {                                /* characters? */
    fprintf (of, "%c", SIXTOASC ((inst >> 6) & 077));
    fprintf (of, "%c", SIXTOASC (inst & 077));
    return SCPE_OK;
    }
if (sw & SWMASK ('T')) {                                /* TSS8 packed? */
    fprintf (of, "%c", TSSTOASC ((inst >> 6) & 077));
    fprintf (of, "%c", TSSTOASC (inst & 077));
    return SCPE_OK;
    }
if (!(sw & SWMASK ('M'))) return SCPE_ARG;

/* Instruction decode */

opc = (inst >> 9) & 07;                                 /* get major opcode */
if (opc == 07)                                          /* operate? */
    inst = inst | ((emode & 1) << 12);                  /* include EAE mode */
if (opc == 06) {                                        /* IOT? */
    DEVICE *dptr;
    DIB *dibp;
    uint32 dno = (inst >> 3) & 077;
    for (i = 0; (dptr = amb_dev[i]) != NULL; i++) {     /* check amb devices */
        if ((dptr->ctxt == NULL) ||                     /* no DIB or */
            (dptr->flags & DEV_DIS)) continue;          /* disabled? skip */
        dibp = (DIB *) dptr->ctxt;                      /* get DIB */
        if ((dno >= dibp->dev) ||                       /* IOT for this dev? */
            (dno < (dibp->dev + dibp->num))) {
            inst = inst | ((i + 1) << 12);              /* disambiguate */
            break;                                      /* done */
            }
        }
    }
        
for (i = 0; opc_val[i] >= 0; i++) {                     /* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;                /* get class */
    if ((opc_val[i] & 077777) == (inst & masks[j])) {   /* match? */

        switch (j) {                                    /* case on class */

        case I_V_NPN: case I_V_IOA:                     /* no operands */
            fprintf (of, "%s", opcode[i]);              /* opcode */
            break;

        case I_V_FLD:                                   /* field change */
            fprintf (of, "%s %-o", opcode[i], (inst >> 3) & 07);
            break;

        case I_V_MRF:                                   /* mem ref */
            disp = inst & 0177;                         /* displacement */
            fprintf (of, "%s%s", opcode[i], ((inst & 00400)? " I ": " "));
            if (inst & 0200) {                          /* current page? */
                if (cflag) fprintf (of, "%-o", (addr & 07600) | disp);
                else fprintf (of, "C %-o", disp);
                }
            else fprintf (of, "%-o", disp);             /* page zero */
            break;

        case I_V_IOT:                                   /* IOT */
            fprintf (of, "%s %-o", opcode[i], inst & 0777);
            break;

        case I_V_OP1:                                   /* operate group 1 */
            sp = fprint_opr (of, inst & 0361, j, 0);
            if (opcode[i]) fprintf (of, (sp? " %s": "%s"), opcode[i]);
            break;

        case I_V_OP2:                                   /* operate group 2 */
            if (opcode[i]) fprintf (of, "%s", opcode[i]); /* skips */
            fprint_opr (of, inst & 0206, j, opcode[i] != NULL);
            break;      

        case I_V_OP3:                                   /* operate group 3 */
            sp = fprint_opr (of, inst & 0320, j, 0);
            if (opcode[i]) fprintf (of, (sp? " %s": "%s"), opcode[i]);
            break;
            }                                           /* end case */

        return SCPE_OK;
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
        status  =       error status
*/

t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 cflag, d, i, j, k;
t_stat r;
char gbuf[CBUFSIZE];

cflag = (uptr == NULL) || (uptr == &cpu_unit);
while (isspace (*cptr)) cptr++;                         /* absorb spaces */
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    val[0] = (t_value) cptr[0] | 0200;
    return SCPE_OK;
    }
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* sixbit string? */
    if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    val[0] = (((t_value) cptr[0] & 077) << 6) |
              ((t_value) cptr[1] & 077);
    return SCPE_OK;
    }
if ((sw & SWMASK ('T')) || ((*cptr == '"') && cptr++)) { /* TSS8 string? */
    if (cptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    val[0] = (((t_value) (cptr[0] - 040) & 077) << 6) |
              ((t_value) (cptr[1] - 040) & 077);
    return SCPE_OK;
    }

/* Instruction parse */

cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL) return SCPE_ARG;
val[0] = opc_val[i] & 07777;                            /* get value */
j = (opc_val[i] >> I_V_FL) & I_M_FL;                    /* get class */

switch (j) {                                            /* case on class */

    case I_V_IOT:                                       /* IOT */
        cptr = get_glyph (cptr, gbuf, 0);               /* get dev+pulse */
        d = get_uint (gbuf, 8, 0777, &r);
        if (r != SCPE_OK) return SCPE_ARG;
        val[0] = val[0] | d;
        break;

    case I_V_FLD:                                       /* field */
        for (cptr = get_glyph (cptr, gbuf, 0); gbuf[0] != 0;
            cptr = get_glyph (cptr, gbuf, 0)) {
            for (i = 0; (opcode[i] != NULL) &&
                        (strcmp (opcode[i], gbuf) != 0) ; i++) ;
            if (opcode[i] != NULL) {
                k = (opc_val[i] >> I_V_FL) & I_M_FL;
                if (k != j) return SCPE_ARG;
                val[0] = val[0] | (opc_val[i] & 07777);
                }
            else {
                d = get_uint (gbuf, 8, 07, &r);
                if (r != SCPE_OK) return SCPE_ARG;
                val[0] = val[0] | (d << 3);
                break;
                }
            }
        break;

    case I_V_MRF:                                       /* mem ref */
        cptr = get_glyph (cptr, gbuf, 0);               /* get next field */
        if (strcmp (gbuf, "I") == 0) {                  /* indirect? */
            val[0] = val[0] | 0400;
            cptr = get_glyph (cptr, gbuf, 0);
            }
        if ((k = (strcmp (gbuf, "C") == 0)) || (strcmp (gbuf, "Z") == 0)) {
            cptr = get_glyph (cptr, gbuf, 0);
            d = get_uint (gbuf, 8, 0177, &r);
            if (r != SCPE_OK) return SCPE_ARG;
            val[0] = val[0] | d | (k? 0200: 0);
            }
        else {
            d = get_uint (gbuf, 8, 07777, &r);
            if (r != SCPE_OK) return SCPE_ARG;
            if (d <= 0177) val[0] = val[0] | d;
            else if (cflag && (((addr ^ d) & 07600) == 0))
                val[0] = val[0] | (d & 0177) | 0200;
            else return SCPE_ARG;
            }
        break;

    case I_V_OP1: case I_V_OP2: case I_V_OP3:           /* operates */
    case I_V_NPN: case I_V_IOA:
        for (cptr = get_glyph (cptr, gbuf, 0); gbuf[0] != 0;
            cptr = get_glyph (cptr, gbuf, 0)) {
            for (i = 0; (opcode[i] != NULL) &&
                        (strcmp (opcode[i], gbuf) != 0) ; i++) ;
            k = opc_val[i] & 07777;
            if ((opcode[i] == NULL) || (((k ^ val[0]) & 07000) != 0))
                return SCPE_ARG;
            val[0] = val[0] | k;
            }
        break;
        }                                               /* end case */

if (*cptr != 0) return SCPE_ARG;                        /* junk at end? */
return SCPE_OK;
}
