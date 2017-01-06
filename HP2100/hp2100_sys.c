/* hp2100_sys.c: HP 2100 simulator interface

   Copyright (c) 1993-2016, Robert M. Supnik

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

   13-May-16    JDB     Modified for revised SCP API function parameter types
   19-Jun-15    JDB     Conditionally use Fprintf function for version 4.x and on
   18-Jun-15    JDB     Added cast to int for isspace parameter
   24-Dec-14    JDB     Added casts to t_addr and t_value for 64-bit compatibility
                        Made local routines static
   05-Feb-13    JDB     Added hp_fprint_stopped to handle HLT instruction message
   18-Mar-13    JDB     Moved CPU state variable declarations to hp2100_cpu.h
   09-May-12    JDB     Quieted warnings for assignments in conditional expressions
   10-Feb-12    JDB     Deprecated DEVNO in favor of SC
                        Added hp_setsc, hp_showsc functions to support SC modifier
   15-Dec-11    JDB     Added DA and dummy DC devices
   29-Oct-10    JDB     DMA channels renamed from 0,1 to 1,2 to match documentation
   26-Oct-10    JDB     Changed DIB access for revised signal model
   03-Sep-08    JDB     Fixed IAK instruction dual-use mnemonic display
   07-Aug-08    JDB     Moved hp_setdev, hp_showdev from hp2100_cpu.c
                        Changed sim_load to use WritePW instead of direct M[] access
   18-Jun-08    JDB     Added PIF device
   17-Jun-08    JDB     Moved fmt_char() function from hp2100_baci.c
   26-May-08    JDB     Added MPX device
   24-Apr-08    JDB     Changed fprint_sym to handle step with irq pending
   07-Dec-07    JDB     Added BACI device
   27-Nov-07    JDB     Added RTE OS/VMA/EMA mnemonics
   21-Dec-06    JDB     Added "fwanxm" external for sim_load check
   19-Nov-04    JDB     Added STOP_OFFLINE, STOP_PWROFF messages
   25-Sep-04    JDB     Added memory protect device
                        Fixed display of CCA/CCB/CCE instructions
   01-Jun-04    RMS     Added latent 13037 support
   19-Apr-04    RMS     Recognize SFS x,C and SFC x,C
   22-Mar-02    RMS     Revised for dynamically allocated memory
   14-Feb-02    RMS     Added DMS instructions
   04-Feb-02    RMS     Fixed bugs in alter/skip display and parsing
   01-Feb-02    RMS     Added terminal multiplexor support
   16-Jan-02    RMS     Added additional device support
   17-Sep-01    RMS     Removed multiconsole support
   27-May-01    RMS     Added multiconsole support
   14-Mar-01    RMS     Revised load/dump interface (again)
   30-Oct-00    RMS     Added examine to file support
   15-Oct-00    RMS     Added dynamic device number support
   27-Oct-98    RMS     V2.4 load interface
*/


#include <ctype.h>
#include "hp2100_defs.h"
#include "hp2100_cpu.h"


#if (SIM_MAJOR >= 4)
  #define fprintf       Fprintf
  #define fputs(_s,_f)  Fprintf (_f, "%s", _s)
  #define fputc(_c,_f)  Fprintf (_f, "%c", _c)
#endif


extern DEVICE mp_dev;
extern DEVICE dma1_dev, dma2_dev;
extern DEVICE ptr_dev, ptp_dev;
extern DEVICE tty_dev, clk_dev;
extern DEVICE lps_dev;
extern DEVICE lpt_dev;
extern DEVICE baci_dev;
extern DEVICE mpx_dev;
extern DEVICE mtd_dev, mtc_dev;
extern DEVICE msd_dev, msc_dev;
extern DEVICE dpd_dev, dpc_dev;
extern DEVICE dqd_dev, dqc_dev;
extern DEVICE drd_dev, drc_dev;
extern DEVICE ds_dev;
extern DEVICE muxl_dev, muxu_dev, muxc_dev;
extern DEVICE ipli_dev, iplo_dev;
extern DEVICE pif_dev;
extern DEVICE da_dev, dc_dev;

/* SCP data structures and interface routines

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             maximum number of words for examine/deposit
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

char sim_name[] = "HP 2100";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 3;

DEVICE *sim_devices[] = {
    &cpu_dev,
    &mp_dev,
    &dma1_dev, &dma2_dev,
    &ptr_dev,
    &ptp_dev,
    &tty_dev,
    &clk_dev,
    &lps_dev,
    &lpt_dev,
    &baci_dev,
    &mpx_dev,
    &dpd_dev, &dpc_dev,
    &dqd_dev, &dqc_dev,
    &drd_dev, &drc_dev,
    &ds_dev,
    &mtd_dev, &mtc_dev,
    &msd_dev, &msc_dev,
    &muxl_dev, &muxu_dev, &muxc_dev,
    &ipli_dev, &iplo_dev,
    &pif_dev,
    &da_dev, &dc_dev,
    NULL
    };

const char *sim_stop_messages[] = {
    "Unknown error",
    "Unimplemented instruction",
    "Non-existent I/O device",
    "HALT instruction",
    "Breakpoint",
    "Indirect address loop",
    "Indirect address interrupt (should not happen!)",
    "No connection on interprocessor link",
    "Device/unit offline",
    "Device/unit powered off"
    };


/* Print additional information for simulator stops.

   The HP 21xx/1000 halt instruction ("HLT") opcode includes select code and
   device flag hold/clear bit fields.  In practice, these are not used to affect
   the device interface; rather, they communicate to the operator the
   significance of the particular halt encountered.

   Under simulation, the halt opcode must be communicated to the user as part of
   the stop message.  To so do, we define a sim_vm_fprint_stopped handler that
   is called for all VM stops.  When called for a STOP_HALT, the halt message
   has been printed, and we add the opcode value in the T register before
   returning TRUE, so that SCP will add the program counter value.  For example:

     HALT instruction 102077, P: 00101 (NOP)

   Reasons other than STOP_HALT need no additional information.

   Implementation notes:

    1. The octal halt instruction will always be of the form 10x0xx.  We take
       advantage of this to request 19 bits printed with leading spaces.  This
       adds a leading space to separate the value from the message.
*/

t_bool hp_fprint_stopped (FILE *st, t_stat reason)
{
if (reason == STOP_HALT)
    fprint_val (st, TR, 8, 19, PV_RSPC);

return TRUE;
}


/* Binary loader

   The binary loader consists of blocks preceded and trailed by zero frames.
   A block consists of 16b words (punched big endian), as follows:

        count'xxx
        origin
        word 0
        :
        word count-1
        checksum

   The checksum includes the origin but not the count.
*/

static int32 fgetw (FILE *fileref)
{
int c1, c2;

if ((c1 = fgetc (fileref)) == EOF) return -1;
if ((c2 = fgetc (fileref)) == EOF) return -1;
return ((c1 & 0377) << 8) | (c2 & 0377);
}

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
int32 origin, csum, zerocnt, count, word, i;

if ((*cptr != 0) || (flag != 0)) return SCPE_ARG;
for (zerocnt = 1;; zerocnt = -10) {                     /* block loop */
    for (;; zerocnt++) {                                /* skip 0's */
        if ((count = fgetc (fileref)) == EOF) return SCPE_OK;
        else if (count) break;
        else if (zerocnt == 0) return SCPE_OK;
        }
    if (fgetc (fileref) == EOF) return SCPE_FMT;
    if ((origin = fgetw (fileref)) < 0) return SCPE_FMT;
    csum = origin;                                      /* seed checksum */
    for (i = 0; i < count; i++) {                       /* get data words */
        if ((word = fgetw (fileref)) < 0) return SCPE_FMT;
        WritePW (origin, word);
        origin = origin + 1;
        csum = csum + word;
        }
    if ((word = fgetw (fileref)) < 0) return SCPE_FMT;
    if ((word ^ csum) & DMASK) return SCPE_CSUM;
    }
}

/* Symbol tables */

#define I_V_FL          16                              /* flag start */
#define I_M_FL          017                             /* flag mask */
#define I_V_NPN         0                               /* no operand */
#define I_V_NPC         1                               /* no operand + C */
#define I_V_MRF         2                               /* mem ref */
#define I_V_ASH         3                               /* alter/skip, shift */
#define I_V_ESH         4                               /* extended shift */
#define I_V_EMR         5                               /* extended mem ref */
#define I_V_IO1         6                               /* I/O + HC */
#define I_V_IO2         7                               /* I/O only */
#define I_V_EGZ         010                             /* ext grp, 1 op + 0 */
#define I_V_EG2         011                             /* ext grp, 2 op */
#define I_V_ALT         012                             /* alternate use instr */
#define I_NPN           (I_V_NPN << I_V_FL)
#define I_NPC           (I_V_NPC << I_V_FL)
#define I_MRF           (I_V_MRF << I_V_FL)
#define I_ASH           (I_V_ASH << I_V_FL)
#define I_ESH           (I_V_ESH << I_V_FL)
#define I_EMR           (I_V_EMR << I_V_FL)
#define I_IO1           (I_V_IO1 << I_V_FL)
#define I_IO2           (I_V_IO2 << I_V_FL)
#define I_EGZ           (I_V_EGZ << I_V_FL)
#define I_EG2           (I_V_EG2 << I_V_FL)
#define I_ALT           (I_V_ALT << I_V_FL)

static const int32 masks[] = {
 0177777, 0176777, 0074000, 0170000,
 0177760, 0177777, 0176700, 0177700,
 0177777, 0177777, 0177777
 };

static const char *opcode[] = {

/* These mnemonics are used by debug printouts, so put them first. */

 "$LIBR", "$LIBX", ".TICK", ".TNAM",                /* RTE-6/VM OS firmware */
 ".STIO", ".FNW",  ".IRT",  ".LLS",
 ".SIP",  ".YLD",  ".CPM",  ".ETEQ",
 ".ENTN", "$OTST", ".ENTC", ".DSPI",
 "$DCPC", "$MPV",  "$DEV",  "$TBG",                 /* alternates for dual-use */

 ".PMAP", "$LOC",  "$VTST",/* --- */                /* RTE-6/VM VMA firmware */
/* ---      ---      ---      --- */
 ".IMAP", ".IMAR", ".JMAP", ".JMAR",
 ".LPXR", ".LPX",  ".LBPR", ".LBP",

 ".EMIO", "MMAP",  "$ETST",/* --- */                /* RTE-IV EMA firmware */
/* ---      ---      ---      --- */
/* ---      ---      ---      --- */
/* ---      ---      --- */ ".EMAP",

/* Regular mnemonics. */

 "NOP", "NOP", "AND", "JSB",
 "XOR", "JMP", "IOR", "ISZ",
 "ADA", "ADB" ,"CPA", "CPB",
 "LDA", "LDB", "STA", "STB",
 "DIAG", "ASL", "LSL", "TIMER",
 "RRL", "ASR", "LSR", "RRR",
 "MPY", "DIV", "DLD", "DST",
 "FAD", "FSB", "FMP", "FDV",
 "FIX", "FLT",
 "STO", "CLO", "SOC", "SOS",
 "HLT", "STF", "CLF",
 "SFC", "SFS", "MIA", "MIB",
 "LIA", "LIB", "OTA", "OTB",
 "STC", "CLC",
 "SYA", "USA", "PAA", "PBA",
               "XMA",
 "XLA", "XSA", "XCA", "LFA",
 "RSA", "RVA",
               "MBI", "MBF",
 "MBW", "MWI", "MWF", "MWW",
 "SYB", "USB", "PAB", "PBB",
 "SSM", "JRS",
 "XMM", "XMS", "XMB",
 "XLB", "XSB", "XCB", "LFB",
 "RSB", "RVB", "DJP", "DJS",
 "SJP", "SJS", "UJP", "UJS",
 "SAX", "SBX", "CAX", "CBX",
 "LAX", "LBX", "STX",
 "CXA", "CXB", "LDX",
 "ADX", "XAX", "XBX",
 "SAY", "SBY", "CAY", "CBY",
 "LAY", "LBY", "STY",
 "CYA", "CYB", "LDY",
 "ADY", "XAY", "XBY",
 "ISX", "DSX", "JLY", "LBT",
 "SBT", "MBT", "CBT", "SBT",
 "ISY", "DSY", "JPY", "SBS",
 "CBS", "TBS", "CMW", "MVW",
 NULL,                                                  /* decode only */
 NULL
 };

static const int32 opc_val[] = {
 0105340+I_NPN, 0105341+I_NPN, 0105342+I_NPN, 0105343+I_NPN,    /* RTE-6/VM OS */
 0105344+I_NPN, 0105345+I_NPN, 0105346+I_NPN, 0105347+I_NPN,
 0105350+I_NPN, 0105351+I_NPN, 0105352+I_NPN, 0105353+I_NPN,
 0105354+I_ALT, 0105355+I_ALT, 0105356+I_ALT, 0105357+I_ALT,
 0105354+I_NPN, 0105355+I_NPN, 0105356+I_NPN, 0105357+I_NPN,    /* alternates */

 0105240+I_ALT, 0105241+I_ALT, 0105242+I_ALT, /*   ---     */   /* RTE-6/VM VMA */
/*    ---            ---            ---            ---     */
 0105250+I_NPN, 0105251+I_NPN, 0105252+I_NPN, 0105253+I_NPN,
 0105254+I_NPN, 0105255+I_NPN, 0105256+I_NPN, 0105257+I_ALT,

 0105240+I_NPN, 0105241+I_NPN, 0105242+I_NPN,                   /* RTE-IV EMA */
/*    ---            ---            ---            ---     */
/*    ---            ---            ---            ---     */
/*    ---            ---            ---    */ 0105257+I_NPN,

 0000000+I_NPN, 0002000+I_NPN, 0010000+I_MRF, 0014000+I_MRF,
 0020000+I_MRF, 0024000+I_MRF, 0030000+I_MRF, 0034000+I_MRF,
 0040000+I_MRF, 0044000+I_MRF, 0050000+I_MRF, 0054000+I_MRF,
 0060000+I_MRF, 0064000+I_MRF, 0070000+I_MRF, 0074000+I_MRF,
 0100000+I_NPN, 0100020+I_ESH, 0100040+I_ESH, 0100060+I_NPN,
 0100100+I_ESH, 0101020+I_ESH, 0101040+I_ESH, 0101100+I_ESH,
 0100200+I_EMR, 0100400+I_EMR, 0104200+I_EMR, 0104400+I_EMR,
 0105000+I_EMR, 0105020+I_EMR, 0105040+I_EMR, 0105060+I_EMR,
 0105100+I_NPN, 0105120+I_NPN,
 0102101+I_NPN, 0103101+I_NPN, 0102201+I_NPC, 0102301+I_NPC,
 0102000+I_IO1, 0102100+I_IO2, 0103100+I_IO2,
 0102200+I_IO1, 0102300+I_IO1, 0102400+I_IO1, 0106400+I_IO1,
 0102500+I_IO1, 0106500+I_IO1, 0102600+I_IO1, 0106600+I_IO1,
 0102700+I_IO1, 0106700+I_IO1,
 0101710+I_NPN, 0101711+I_NPN, 0101712+I_NPN, 0101713+I_NPN,
                               0101722+I_NPN,
 0101724+I_EMR, 0101725+I_EMR, 0101726+I_EMR, 0101727+I_NPN,
 0101730+I_NPN, 0101731+I_NPN,
                               0105702+I_NPN, 0105703+I_NPN,
 0105704+I_NPN, 0105705+I_NPN, 0105706+I_NPN, 0105707+I_NPN,
 0105710+I_NPN, 0105711+I_NPN, 0105712+I_NPN, 0105713+I_NPN,
 0105714+I_EMR, 0105715+I_EG2,
 0105720+I_NPN, 0105721+I_NPN, 0105722+I_NPN,
 0105724+I_EMR, 0105725+I_EMR, 0105726+I_EMR, 0105727+I_NPN,
 0105730+I_NPN, 0105731+I_NPN, 0105732+I_EMR, 0105733+I_EMR,
 0105734+I_EMR, 0105735+I_EMR, 0105736+I_EMR, 0105737+I_EMR,
 0101740+I_EMR, 0105740+I_EMR, 0101741+I_NPN, 0105741+I_NPN,
 0101742+I_EMR, 0105742+I_EMR, 0105743+I_EMR,
 0101744+I_NPN, 0105744+I_NPN, 0105745+I_EMR,
 0105746+I_EMR, 0101747+I_NPN, 0105747+I_NPN,
 0101750+I_EMR, 0105750+I_EMR, 0101751+I_NPN, 0105751+I_NPN,
 0101752+I_EMR, 0105752+I_EMR, 0105753+I_EMR,
 0101754+I_NPN, 0105754+I_NPN, 0105755+I_EMR,
 0105756+I_EMR, 0101757+I_NPN, 0105757+I_NPN,
 0105760+I_NPN, 0105761+I_NPN, 0105762+I_EMR, 0105763+I_NPN,
 0105764+I_NPN, 0105765+I_EGZ, 0105766+I_EGZ, 0105767+I_NPN,
 0105770+I_NPN, 0105771+I_NPN, 0105772+I_EMR, 0105773+I_EG2,
 0105774+I_EG2, 0105775+I_EG2, 0105776+I_EGZ, 0105777+I_EGZ,
 0000000+I_ASH,                                         /* decode only */
 -1
 };

/* Decode tables for shift and alter/skip groups */

static const char *stab[] = {
 "ALS", "ARS", "RAL", "RAR", "ALR", "ERA", "ELA", "ALF",
 "BLS", "BRS", "RBL", "RBR", "BLR", "ERB", "ELB", "BLF",
 "CLA", "CMA", "CCA", "CLB", "CMB", "CCB",
 "SEZ", "CLE", "CLE", "CME", "CCE",
 "SSA", "SSB", "SLA", "SLB",
 "ALS", "ARS", "RAL", "RAR", "ALR", "ERA", "ELA", "ALF",
 "BLS", "BRS", "RBL", "RBR", "BLR", "ERB", "ELB", "BLF",
 "INA", "INB", "SZA", "SZB", "RSS",
 NULL
 };

static const int32 mtab[] = {
 0007700, 0007700, 0007700, 0007700, 0007700, 0007700, 0007700, 0007700,
 0007700, 0007700, 0007700, 0007700, 0007700, 0007700, 0007700, 0007700,
 0007400, 0007400, 0007400, 0007400, 0007400, 0007400,
 0002040, 0002040, 0002300, 0002300, 0002300,
 0006020, 0006020, 0004010, 0004010,
 0006027, 0006027, 0006027, 0006027, 0006027, 0006027, 0006027, 0006027,
 0006027, 0006027, 0006027, 0006027, 0006027, 0006027, 0006027, 0006027,
 0006004, 0006004, 0006002, 0006002, 0002001,
 0
 };

static const int32 vtab[] = {
 0001000, 0001100, 0001200, 0001300, 0001400, 0001500, 0001600, 0001700,
 0005000, 0005100, 0005200, 0005300, 0005400, 0005500, 0005600, 0005700,
 0002400, 0003000, 0003400, 0006400, 0007000, 0007400,
 0002040, 0000040, 0002100, 0002200, 0002300,
 0002020, 0006020, 0000010, 0004010,
 0000020, 0000021, 0000022, 0000023, 0000024, 0000025, 0000026, 0000027,
 0004020, 0004021, 0004022, 0004023, 0004024, 0004025, 0004026, 0004027,
 0002004, 0006004, 0002002, 0006002, 0002001,
 -1
 };

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

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 cflag, cm, i, j, inst, disp;
uint32 irq;

cflag = (uptr == NULL) || (uptr == &cpu_unit);
inst = (int32) val[0];
if (sw & SWMASK ('A')) {                                /* ASCII? */
    if (inst > 0377) return SCPE_ARG;
    fprintf (of, FMTASC (inst & 0177));
    return SCPE_OK;
    }
if (sw & SWMASK ('C')) {                                /* characters? */
    fprintf (of, FMTASC ((inst >> 8) & 0177));
    fprintf (of, FMTASC (inst & 0177));
    return SCPE_OK;
    }
if (!(sw & SWMASK ('M'))) return SCPE_ARG;

/* If we are being called as a result of a VM stop to display the next
   instruction to be executed, check to see if an interrupt is pending and not
   deferred.  If so, then display the interrupt source and the trap cell
   instruction as the instruction to be executed, rather than the instruction at
   the current PC.
*/

if (sw & SIM_SW_STOP) {                                 /* simulator stop? */
    irq = calc_int ();                                  /* check interrupt */

    if (irq && (!ion_defer || !calc_defer())) {         /* pending interrupt and not deferred? */
        addr = irq;                                     /* set display address to trap cell */
        val[0] = inst = ReadIO (irq, SMAP);             /* load trap cell instruction */
        val[1] = ReadIO (irq + 1, SMAP);                /*   might be multi-word */
        val[2] = ReadIO (irq + 2, SMAP);                /*   although it's unlikely */
        fprintf (of, "IAK %2o: ", irq);                 /* report acknowledged interrupt */
        }
    }

for (i = 0; opc_val[i] >= 0; i++) {                     /* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;                /* get class */
    if ((opc_val[i] & DMASK) == (inst & masks[j])) {    /* match? */
        switch (j) {                                    /* case on class */

        case I_V_NPN:                                   /* no operands */
            fprintf (of, "%s", opcode[i]);              /* opcode */
            break;

        case I_V_NPC:                                   /* no operands + C */
            fprintf (of, "%s", opcode[i]);
            if (inst & I_HC) fprintf (of, " C");
            break;

        case I_V_MRF:                                   /* mem ref */
            disp = inst & I_DISP;                       /* displacement */
            fprintf (of, "%s ", opcode[i]);             /* opcode */
            if (inst & I_CP) {                          /* current page? */
                if (cflag)
                    fprintf (of, "%-o", ((uint32) addr & I_PAGENO) | disp);
                else
                    fprintf (of, "C %-o", disp);
                }
            else fprintf (of, "%-o", disp);             /* page zero */
            if (inst & I_IA) fprintf (of, ",I");
            break;

        case I_V_ASH:                                   /* shift, alter-skip */
            cm = FALSE;
            for (i = 0; mtab[i] != 0; i++) {
                if ((inst & mtab[i]) == vtab[i]) {
                    inst = inst & ~(vtab[i] & 01777);
                    if (cm) fprintf (of, ",");
                    cm = TRUE;
                    fprintf (of, "%s", stab[i]);
                    }
                }
            if (!cm) return SCPE_ARG;                   /* nothing decoded? */
            break;

        case I_V_ESH:                                   /* extended shift */
            disp = inst & 017;                          /* shift count */
            if (disp == 0) disp = 16;
            fprintf (of, "%s %d", opcode[i], disp);
            break;

        case I_V_EMR:                                   /* extended mem ref */
            fprintf (of, "%s %-o", opcode[i], (uint32) val[1] & VAMASK);
            if (val[1] & I_IA) fprintf (of, ",I");
            return -1;                                  /* extra word */

        case I_V_IO1:                                   /* IOT with H/C */
            fprintf (of, "%s %-o", opcode[i], inst & I_DEVMASK);
            if (inst & I_HC) fprintf (of, ",C");
            break;

        case I_V_IO2:                                   /* IOT */
            fprintf (of, "%s %-o", opcode[i], inst & I_DEVMASK);
            break;

        case I_V_EGZ:                                   /* ext grp 1 op + 0 */
            fprintf (of, "%s %-o", opcode[i], (uint32) val[1] & VAMASK);
            if (val[1] & I_IA) fprintf (of, ",I");
            return -2;                                  /* extra words */

        case I_V_EG2:                                   /* ext grp 2 op */
            fprintf (of, "%s %-o", opcode[i], (uint32) val[1] & VAMASK);
            if (val[1] & I_IA) fprintf (of, ",I");
            fprintf (of, " %-o", (uint32) val[2] & VAMASK);
            if (val[2] & I_IA) fprintf (of, ",I");
            return -2;                                  /* extra words */

        case I_V_ALT:                                   /* alternate use instr */
            if ((inst >= 0105354) &&
                (inst <= 0105357) &&                    /* RTE-6/VM OS range? */
                (addr >= 2) &&
                (addr <= 077))                          /* in trap cell? */
                continue;                               /* use alternate mnemonic */

            else if ((inst >= 0105240) &&               /* RTE-6/VM VMA range? */
                     (inst <= 0105257) &&
                     (cpu_unit.flags & UNIT_EMA))       /* EMA enabled? */
                continue;                               /* use EMA mnemonics */

            else
                fprintf (of, "%s", opcode[i]);          /* print opcode */
            break;
            }

        return SCPE_OK;
        }                                               /* end if */
    }                                                   /* end for */
return SCPE_ARG;
}

/* Get address with indirection

   Inputs:
        *cptr   =       pointer to input string
   Outputs:
        val     =       address
                        -1 if error
*/

static int32 get_addr (CONST char *cptr)
{
int32 d;
t_stat r;
char gbuf[CBUFSIZE];

cptr = get_glyph (cptr, gbuf, ',');                     /* get next field */
d = (int32) get_uint (gbuf, 8, VAMASK, &r);             /* construe as addr */
if (r != SCPE_OK) return -1;
if (*cptr != 0) {                                       /* more? */
    cptr = get_glyph (cptr, gbuf, 0);                   /* look for indirect */
    if (*cptr != 0) return -1;                          /* should be done */
    if (strcmp (gbuf, "I")) return -1;                  /* I? */
    d = d | I_IA;
    }
return d;
}

/* Symbolic input

   Inputs:
        *iptr   =       pointer to input string
        addr    =       current PC
        *uptr   =       pointer to unit
        *val    =       pointer to output values
        sw      =       switches
   Outputs:
        status  =       error status
*/

t_stat parse_sym (CONST char *iptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 cflag, d, i, j, k, clef, tbits;
t_stat r, ret;
CONST char *cptr;
char gbuf[CBUFSIZE];

cflag = (uptr == NULL) || (uptr == &cpu_unit);
while (isspace ((int) *iptr)) iptr++;                   /* absorb spaces */
if ((sw & SWMASK ('A')) || ((*iptr == '\'') && iptr++)) { /* ASCII char? */
    if (iptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    val[0] = (t_value) iptr[0] & 0177;
    return SCPE_OK;
    }
if ((sw & SWMASK ('C')) || ((*iptr == '"') && iptr++)) { /* char string? */
    if (iptr[0] == 0) return SCPE_ARG;                  /* must have 1 char */
    val[0] = (((t_value) iptr[0] & 0177) << 8) |
              ((t_value) iptr[1] & 0177);
    return SCPE_OK;
    }

ret = SCPE_OK;
cptr = get_glyph (iptr, gbuf, 0);                       /* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i]) {                                        /* found opcode? */
    val[0] = opc_val[i] & DMASK;                        /* get value */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;                /* get class */
    switch (j) {                                        /* case on class */

    case I_V_NPN:                                       /* no operand */
        break;

    case I_V_NPC:                                       /* no operand + C */
        if (*cptr != 0) {
            cptr = get_glyph (cptr, gbuf, 0);
            if (strcmp (gbuf, "C")) return SCPE_ARG;
            val[0] = val[0] | I_HC;
            }
        break;

    case I_V_MRF:                                       /* mem ref */
        cptr = get_glyph (cptr, gbuf, 0);               /* get next field */
        k = strcmp (gbuf, "C");
        if (k == 0) {                                   /* C specified? */
            val[0] = val[0] | I_CP;
            cptr = get_glyph (cptr, gbuf, 0);
            }
        else {
            k = strcmp (gbuf, "Z");
            if (k == 0)                                 /* Z specified? */
                cptr = get_glyph (cptr, gbuf, ',');
            }
        if ((d = get_addr (gbuf)) < 0) return SCPE_ARG;
        if ((d & VAMASK) <= I_DISP) val[0] = val[0] | d;
        else if (cflag && !k && (((addr ^ d) & I_PAGENO) == 0))
            val[0] = val[0] | (d & (I_IA | I_DISP)) | I_CP;
        else return SCPE_ARG;
        break;

    case I_V_ESH:                                       /* extended shift */
        cptr = get_glyph (cptr, gbuf, 0);
        d = (int32) get_uint (gbuf, 10, 16, &r);
        if ((r != SCPE_OK) || (d == 0)) return SCPE_ARG;
        val[0] = val[0] | (d & 017);
        break;

    case I_V_EMR:                                       /* extended mem ref */
        cptr = get_glyph (cptr, gbuf, 0);               /* get next field */
        if ((d = get_addr (gbuf)) < 0) return SCPE_ARG;
        val[1] = d;
        ret = -1;
        break;

    case I_V_IO1:                                       /* IOT + optional C */
        cptr = get_glyph (cptr, gbuf, ',');             /* get device */
        d = (int32) get_uint (gbuf, 8, I_DEVMASK, &r);
        if (r != SCPE_OK) return SCPE_ARG;
        val[0] = val[0] | d;
        if (*cptr != 0) {
            cptr = get_glyph (cptr, gbuf, 0);
            if (strcmp (gbuf, "C")) return SCPE_ARG;
            val[0] = val[0] | I_HC;
            }
        break;

    case I_V_IO2:                                       /* IOT */
        cptr = get_glyph (cptr, gbuf, 0);               /* get device */
        d = (int32) get_uint (gbuf, 8, I_DEVMASK, &r);
        if (r != SCPE_OK) return SCPE_ARG;
        val[0] = val[0] | d;
        break;

    case I_V_EGZ:                                       /* ext grp 1 op + 0 */
        cptr = get_glyph (cptr, gbuf, 0);               /* get next field */
        if ((d = get_addr (gbuf)) < 0) return SCPE_ARG;
        val[1] = d;
        val[2] = 0;
        ret = -2;
        break;

    case I_V_EG2:                                       /* ext grp 2 op */
        cptr = get_glyph (cptr, gbuf, 0);               /* get next field */
        if ((d = get_addr (gbuf)) < 0) return SCPE_ARG;
        cptr = get_glyph (cptr, gbuf, 0);               /* get next field */
        if ((k = get_addr (gbuf)) < 0) return SCPE_ARG;
        val[1] = d;
        val[2] = k;
        ret = -2;
        break;
        }                                               /* end case */

    if (*cptr != 0) return SCPE_ARG;                    /* junk at end? */
    return ret;
    }                                                   /* end if opcode */

/* Shift or alter-skip

   Each opcode is matched by a mask, specifiying the bits affected, and
   the value, specifying the value.  As opcodes are processed, the mask
   values are used to specify which fields have already been filled in.

   The mask has two subfields, the type bits (A/B and A/S), and the field
   bits.  The type bits, once specified by any instruction, must be
   consistent in all other instructions.  The mask bits assure that no
   field is filled in twice.

   Two special cases:

   1. The dual shift field in shift requires checking how much of the
      target word has been filled in before assigning the shift value.
      To implement this, shifts are listed twice is the decode table.
      If the current subopcode is a shift in the first part of the table
      (entries 0..15), and CLE has been seen or the first shift field is
      filled in, the code forces a mismatch.  The glyph will match in
      the second part of the table.

   2. CLE processing must be deferred until the instruction can be
      classified as shift or alter-skip, since it has two different
      bit values in the two classes.  To implement this, CLE seen is
      recorded as a flag and processed after all other subopcodes.
*/

clef = FALSE;
tbits = 0;
val[0] = 0;
for (cptr = get_glyph (iptr, gbuf, ','); gbuf[0] != 0;
     cptr = get_glyph (cptr, gbuf, ',')) {              /* loop thru glyphs */
    if (strcmp (gbuf, "CLE") == 0) {                    /* CLE? */
        if (clef) return SCPE_ARG;                      /* already seen? */
        clef = TRUE;                                    /* set flag */
        continue;
        }
    for (i = 0; stab[i] != NULL; i++) {                 /* find subopcode */
        if ((strcmp (gbuf, stab[i]) == 0) &&
            ((i >= 16) || (!clef && ((val[0] & 001710) == 0)))) break;
        }
    if (stab[i] == NULL) return SCPE_ARG;
    if (tbits & mtab[i] & (I_AB | I_ASKP) & (vtab[i] ^ val[0]))
        return SCPE_ARG;
    if (tbits & mtab[i] & ~(I_AB | I_ASKP)) return SCPE_ARG;
    tbits = tbits | mtab[i];                            /* fill type+mask */
    val[0] = val[0] | vtab[i];                          /* fill value */
    }
if (clef) {                                             /* CLE seen? */
    if (val[0] & I_ASKP) {                              /* alter-skip? */
        if (tbits & 0100) return SCPE_ARG;              /* already filled in? */
        else val[0] = val[0] | 0100;
        }
    else val[0] = val[0] | 040;                         /* fill in shift */
    }
return ret;
}


/* Format a character into a printable string.

   Control characters are translated to readable strings.  Printable characters
   retain their original form but are enclosed in single quotes.  Characters
   outside of the ASCII range are represented as escaped octal values.
*/

const char *fmt_char (uint8 ch)
{
static const char *const ctl [] = { "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL",
                                    "BS",  "HT",  "LF",  "VT",  "FF",  "CR",  "SO",  "SI",
                                    "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB",
                                    "CAN", "EM",  "SUB", "ESC", "FS",  "GS",  "RS",  "US" };
static char rep [5];

if (ch <= '\037')                                       /* ASCII control character? */
    return ctl [ch];                                    /* return string representation */

else if (ch == '\177')                                  /* ASCII delete? */
    return "DEL";                                       /* return string representation */

else if (ch > '\177') {                                 /* beyond printable range? */
    sprintf (rep, "\\%03o", ch);                        /* format value */
    return rep;                                         /* return escaped octal code */
    }

else {                                                  /* printable character */
    rep [0] = '\'';                                     /* form string */
    rep [1] = ch;                                       /*   containing character */
    rep [2] = '\'';
    rep [3] = '\0';
    return rep;                                         /* return quoted character */
    }
}


/* Set select code */

t_stat hp_setsc (UNIT *uptr, int32 num, CONST char *cptr, void *desc)
{
DEVICE *dptr = (DEVICE *) desc;
DIB *dibptr;
int32 i, newdev;
t_stat r;

if (cptr == NULL)
    return SCPE_ARG;

if ((desc == NULL) || (num > 1))
    return SCPE_IERR;

dibptr = (DIB *) dptr->ctxt;

if (dibptr == NULL)
    return SCPE_IERR;

newdev = (int32) get_uint (cptr, 8, I_DEVMASK - num, &r);

if (r != SCPE_OK)
    return r;

if (newdev < VARDEV)
    return SCPE_ARG;

for (i = 0; i <= num; i++, dibptr++)
    dibptr->select_code = newdev + i;

return SCPE_OK;
}


/* Show select code */

t_stat hp_showsc (FILE *st, UNIT *uptr, int32 num, CONST void *desc)
{
const DEVICE *dptr = (const DEVICE *) desc;
DIB *dibptr;
int32 i;

if ((desc == NULL) || (num > 1))
    return SCPE_IERR;

dibptr = (DIB *) dptr->ctxt;

if (dibptr == NULL)
    return SCPE_IERR;

fprintf (st, "select code=%o", dibptr->select_code);

for (i = 1; i <= num; i++)
    fprintf (st, "/%o", dibptr->select_code + i);

return SCPE_OK;
}


/* Set device number */

t_stat hp_setdev (UNIT *uptr, int32 num, CONST char *cptr, void *desc)
{
return hp_setsc (uptr, num, cptr, desc);
}


/* Show device number */

t_stat hp_showdev (FILE *st, UNIT *uptr, int32 num, CONST void *desc)
{
t_stat result;

result = hp_showsc (st, uptr, num, desc);

if (result == SCPE_OK)
    fputc ('\n', st);

return result;
}
