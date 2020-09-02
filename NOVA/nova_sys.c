/* nova_sys.c: NOVA simulator interface

   Copyright (c) 1993-2017, Robert M. Supnik

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

   09-Mar-17    RMS     Fixed missing break in loader (COVERITY)
                        Fixed overlook case in address parse (COVERITY)
   25-Mar-12    RMS     Fixed declaration (Mark Pizzolato)
   04-Jul-07    BKR     DEC's IOF/ION changed to DG's INTDS/INTEN mnemonic,
                        Fixed QTY/ADCV device name
                        RDSW changed to DDG's READS mnemonic,
                        fixed/enhanced 'load' command for DG-compatible binary tape format
   26-Mar-04    RMS     Fixed warning with -std=c99
   14-Jan-04    BKR     Added support for QTY and ALM
   04-Jan-04    RMS     Fixed 64b issues found by VMS 8.1
   24-Nov-03    CEO     Added symbolic support for LEF instruction
   17-Sep-01    RMS     Removed multiconsole support
   31-May-01    RMS     Added multiconsole support
   14-Mar-01    RMS     Revised load/dump interface (again)
   22-Dec-00    RMS     Added second terminal support
   10-Dec-00    RMS     Added Eclipse support
   08-Dec-00    BKR     Added plotter support
   30-Oct-00    RMS     Added support for examine to file
   15-Oct-00    RMS     Added stack, byte, trap instructions
   14-Apr-99    RMS     Changed t_addr to unsigned
   27-Oct-98    RMS     V2.4 load interface
   24-Sep-97    RMS     Fixed bug in device name table (Charles Owen)
*/

#include "nova_defs.h"
#include <ctype.h>

extern DEVICE cpu_dev;
extern UNIT cpu_unit;
extern DEVICE ptr_dev;
extern DEVICE ptp_dev;
extern DEVICE plt_dev;
extern DEVICE tti_dev;
extern DEVICE tto_dev;
extern DEVICE tti1_dev;
extern DEVICE tto1_dev;
extern DEVICE clk_dev;
extern DEVICE lpt_dev;
extern DEVICE dkp_dev;
extern DEVICE dsk_dev;
extern DEVICE mta_dev;
extern DEVICE qty_dev;
extern DEVICE alm_dev;
extern REG cpu_reg[];
extern uint16 M[];
extern int32 saved_PC;
extern int32 AMASK;

#if defined (ECLIPSE)

extern DEVICE map_dev;
extern DEVICE fpu_dev;
extern DEVICE pit_dev;
extern int32 Usermap;
extern int32 MapStat;

#endif

/* SCP data structures

   sim_name             simulator name string
   sim_PC               pointer to saved PC register descriptor
   sim_emax             number of words needed for examine
   sim_devices          array of pointers to simulated devices
   sim_stop_messages    array of pointers to stop messages
   sim_load             binary loader
*/

#if defined (ECLIPSE)
char sim_name[] = "ECLIPSE";
#else
char sim_name[] = "NOVA";
#endif

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 4;

DEVICE *sim_devices[] = {
    &cpu_dev,
#if defined (ECLIPSE)
    &map_dev,
    &fpu_dev,
    &pit_dev,
#endif
    &ptr_dev,
    &ptp_dev,
    &tti_dev,
    &tto_dev,
    &tti1_dev,
    &tto1_dev,
    &clk_dev,
    &plt_dev,
    &lpt_dev,
    &dsk_dev,
    &dkp_dev,
    &mta_dev,
    &qty_dev,
    &alm_dev,
    NULL
    };

const char *sim_stop_messages[SCPE_BASE] = {
    "Unknown error",
    "Unknown I/O instruction",
    "HALT instruction",
    "Breakpoint",
    "Nested indirect address limit exceeded",
    "Nested indirect interrupt or trap address limit exceeded",
    "Read breakpoint",
    "Write breakpoint"
    };

/* Binary loader

   Loader format consists of blocks, optionally preceded, separated, and
   followed by zeroes.  Each block consists of:

        lo_count
        hi_count
        lo_origin
        hi_origin
        lo_checksum
        hi_checksum
        lo_data byte    ---
        hi_data byte     |
        :                > -count words
        lo_data byte     |
        hi_data byte    ---

   If the word count is [0,-20], then the block is normal data.
   If the word count is [-21,-n], then the block is repeated data.
   If the word count is 1, the block is the start address.
   If the word count is >1, the block is an error block.

Notes:
    'start' block terminates loading.
    'start' block starting address 1B0 = do not auto-start, 0B0 = auto-start.
    'start' block starting address is saved in 'save_PC' so a "continue"
      should start the program.

    specify -i switch ignores checksum errors


internal state machine:

    0,1 get byte count (low and high), ignore leader bytes (<000>)
    2,3 get origin
    4,5 get checksum
    6,7 process data block
    8   process 'ignore' (error) block
*/

t_stat sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
int32 data, csum, count, state, i;
int32 origin;
int pos;
int block_start;
int done;

if ((*cptr != 0) || (flag != 0))
    return ( SCPE_ARG ) ;
state = 0;
block_start = -1 ;
done  = 0 ;
for ( pos = 0 ; (! done) && ((i=getc(fileref)) != EOF) ; ++pos )
    {
    i &= 0x00FF ;                                       /* (insure no sign extension) */
    switch (state) {
        case 0:                                         /* leader */
            count = i;
            state = (count != 0);
        if ( state )
        block_start = pos ;
            break;
        case 1:                                         /* high count */
            csum = count = (i << 8) | count ;
            state = 2;
            break;
        case 2:                                         /* low origin */
            origin = i;
            state = 3;
            break;
        case 3:                                         /* high origin */
            origin = (i << 8) | origin;
            csum = csum + origin;
            state = 4;
            break;
        case 4:                                         /* low checksum */
            csum = csum + i;
            state = 5;
            break;
        case 5:                                         /* high checksum */
            csum = (csum + (i << 8)) & 0xFFFF;
            if (count == 1)
                {
                /*  'start' block  */
                /*  do any auto-start check or inhibit check  */
                saved_PC = (origin & 077777) ;              /*  0B0 = auto-start program    */
                                                            /*  1B0 = do not auto start */
                state = 0 ;                                 /*  indicate okay state */
                done = 1 ;                                  /*  we're done! */
                if ( ! (origin & 0x8000) )
                    {
                    sim_printf( "auto start @ %05o \n", (origin & 0x7FFF) ) ;
                    }
                break;
                }
            if ( ((count & 0x8000) == 0) && (count > 1))
            {
                /*  'ignore' block  */
                state = 8;
                break;
            }

            /*  'data' or 'repeat' block  */
            count = 0200000 - count ;
            if ( count <= 020 )
            {
                /*  'data' block  */
                state = 6;
                break;
            }
            /*  'repeat' block (multiple data)  */

            if (count > 020) {                           /* large block */
                for (count = count - 1; count > 1; count--) {
                    if (origin >= AMASK /* MEMSIZE? */)
                        {
                        return ( SCPE_NXM );
                        }
                    M[origin] = data;
                    origin = origin + 1;
                    }
                state = 0 ;
                }
            state = 0;
            break;
        case 6:                                         /* low data  */
            data  = i;
            state = 7;
            break;
        case 7:                                         /* high data */
            data = (i << 8) | data;
            csum = (csum + data) & 0xFFFF;

            if (origin >= AMASK)
                return SCPE_NXM;
            M[origin] = data;
            origin = origin + 1;
            count = count - 1;
            if (count == 0) {
                if ( csum )
                    {
                    sim_printf( "checksum error: block start at %d [0x%x] \n", block_start, block_start ) ;
                    sim_printf( "calculated: 0%o [0x%4x]\n", csum, csum ) ;
                    if ( ! (sim_switches & SWMASK('I')) )
                        return SCPE_CSUM;
                    }
                state = 0;
                break;
                }
            state = 6;
            break;
        case 8:                                         /* error (ignore) block */
            if (i == 0377)                              /*  (wait for 'RUBOUT' char)  */
                state = 0;
            break;
            }                                           /* end switch */
        }                                               /* end while */

/* Ok to find end of tape between blocks or in error state */

return ( ((state == 0) || (state == 8)) ? SCPE_OK : SCPE_FMT ) ;
}


/* Symbol tables */

#define I_V_FL          18                              /* flag bits */
#define I_M_FL          037                             /* flag width */
#define I_V_NPN         000                             /* no operands */
#define I_V_R           001                             /* reg */
#define I_V_D           002                             /* device */
#define I_V_RD          003                             /* reg,device */
#define I_V_M           004                             /* mem addr */
#define I_V_RM          005                             /* reg,mem addr */
#define I_V_RR          006                             /* operate */
#define I_V_BY          007                             /* Nova byte pointer */
#define I_V_2AC         010                             /* reg,reg */
#define I_V_RSI         011                             /* reg,short imm */
#define I_V_LI          012                             /* long imm */
#define I_V_RLI         013                             /* reg,long imm */
#define I_V_LM          014                             /* long mem addr */
#define I_V_RLM         015                             /* reg,long mem addr */
#define I_V_FRM         016                             /* flt reg,long mem addr */
#define I_V_FST         017                             /* flt long mem, status */
#define I_V_XP          020                             /* XOP */
#define I_NPN           (I_V_NPN << I_V_FL)
#define I_R             (I_V_R << I_V_FL)
#define I_D             (I_V_D << I_V_FL)
#define I_RD            (I_V_RD << I_V_FL)
#define I_M             (I_V_M << I_V_FL)
#define I_RM            (I_V_RM << I_V_FL)
#define I_RR            (I_V_RR << I_V_FL)
#define I_BY            (I_V_BY << I_V_FL)
#define I_2AC           (I_V_2AC << I_V_FL)
#define I_RSI           (I_V_RSI << I_V_FL)
#define I_LI            (I_V_LI << I_V_FL)
#define I_RLI           (I_V_RLI << I_V_FL)
#define I_LM            (I_V_LM << I_V_FL)
#define I_RLM           (I_V_RLM << I_V_FL)
#define I_FRM           (I_V_FRM << I_V_FL)
#define I_FST           (I_V_FST << I_V_FL)
#define I_XP            (I_V_XP << I_V_FL)

static const int32 masks[] = {
 0177777, 0163777, 0177700, 0163700,
 0174000, 0160000, 0103770, 0163477,
 0103777, 0103777, 0177777, 0163777,
 0176377, 0162377, 0103777, 0163777,
 0100077
 };

static const char *opcode[] = {
 "JMP", "JSR", "ISZ", "DSZ",
 "LDA", "STA",
#if defined (ECLIPSE)
 "ADI", "SBI", "DAD", "DSB",
 "IOR", "XOR", "ANC", "XCH",
 "SGT", "SGE", "LSH", "DLSH",
 "HXL", "HXR", "DHXL", "DHXR",
 "BTO", "BTZ", "SBZ", "SZBO",
 "LOB", "LRB", "COB", "LDB",
 "STB", "PSH", "POP",
 "LMP", "SYC",
 "PSHR", "POPB", "BAM", "POPJ",
         "RTN", "BLM", "DIVX",
 "MUL", "MULS", "DIV", "DIVS",
 "SAVE", "RSTR",
 "XOP",
 "FAS", "FAD", "FSS", "FSD",
 "FMS", "FMD", "FDS", "FDD",
 "FAMS", "FAMD", "FSMS", "FSMD",
 "FMMS", "FMMD", "FDMS", "FDMD",
 "FLDS", "FLDD", "FSTS", "FSTD",
 "FLAS", "FLMD", "FFAS", "FFMD",
 "FNOM", "FRH", "FAB", "FNEG",
 "FSCAL", "FEXP", "FINT", "FHLV",
 "FNS", "FSA", "FSEQ", "FSNE",
 "FSLT", "FSGE", "FSLE", "FSGT",
 "FSNM", "FSND", "FSNU", "FSNUD",
 "FSNO", "FSNOD", "FSNUO", "FSNER",
 "FSST", "FLST",
 "FTE", "FTD", "FCLE",
 "FPSH", "FPOP",
 "FCMP", "FMOV",
 "CMV", "CMP", "CTR", "CMT",
 "EJMP", "EJSR", "EISZ", "EDSZ",
         "ELDA", "ESTA", "ELEF",
 "ELDB", "ESTB", "DSPA",
 "PSHJ", "CLM", "SNB",
 "MSP", "XCT", "HLV",
 "IORI", "XORI", "ANDI", "ADDI",
#endif
 "COM", "COMZ", "COMO", "COMC",
 "COML", "COMZL", "COMOL", "COMCL",
 "COMR", "COMZR", "COMOR", "COMCR",
 "COMS", "COMZS", "COMOS", "COMCS",
 "COM#", "COMZ#", "COMO#", "COMC#",
 "COML#", "COMZL#", "COMOL#", "COMCL#",
 "COMR#", "COMZR#", "COMOR#", "COMCR#",
 "COMS#", "COMZS#", "COMOS#", "COMCS#",
 "NEG", "NEGZ", "NEGO", "NEGC",
 "NEGL", "NEGZL", "NEGOL", "NEGCL",
 "NEGR", "NEGZR", "NEGOR", "NEGCR",
 "NEGS", "NEGZS", "NEGOS", "NEGCS",
 "NEG#", "NEGZ#", "NEGO#", "NEGC#",
 "NEGL#", "NEGZL#", "NEGOL#", "NEGCL#",
 "NEGR#", "NEGZR#", "NEGOR#", "NEGCR#",
 "NEGS#", "NEGZS#", "NEGOS#", "NEGCS#",
 "MOV", "MOVZ", "MOVO", "MOVC",
 "MOVL", "MOVZL", "MOVOL", "MOVCL",
 "MOVR", "MOVZR", "MOVOR", "MOVCR",
 "MOVS", "MOVZS", "MOVOS", "MOVCS",
 "MOV#", "MOVZ#", "MOVO#", "MOVC#",
 "MOVL#", "MOVZL#", "MOVOL#", "MOVCL#",
 "MOVR#", "MOVZR#", "MOVOR#", "MOVCR#",
 "MOVS#", "MOVZS#", "MOVOS#", "MOVCS#",
 "INC", "INCZ", "INCO", "INCC",
 "INCL", "INCZL", "INCOL", "INCCL",
 "INCR", "INCZR", "INCOR", "INCCR",
 "INCS", "INCZS", "INCOS", "INCCS",
 "INC#", "INCZ#", "INCO#", "INCC#",
 "INCL#", "INCZL#", "INCOL#", "INCCL#",
 "INCR#", "INCZR#", "INCOR#", "INCCR#",
 "INCS#", "INCZS#", "INCOS#", "INCCS#",
 "ADC", "ADCZ", "ADCO", "ADCC",
 "ADCL", "ADCZL", "ADCOL", "ADCCL",
 "ADCR", "ADCZR", "ADCOR", "ADCCR",
 "ADCS", "ADCZS", "ADCOS", "ADCCS",
 "ADC#", "ADCZ#", "ADCO#", "ADCC#",
 "ADCL#", "ADCZL#", "ADCOL#", "ADCCL#",
 "ADCR#", "ADCZR#", "ADCOR#", "ADCCR#",
 "ADCS#", "ADCZS#", "ADCOS#", "ADCCS#",
 "SUB", "SUBZ", "SUBO", "SUBC",
 "SUBL", "SUBZL", "SUBOL", "SUBCL",
 "SUBR", "SUBZR", "SUBOR", "SUBCR",
 "SUBS", "SUBZS", "SUBOS", "SUBCS",
 "SUB#", "SUBZ#", "SUBO#", "SUBC#",
 "SUBL#", "SUBZL#", "SUBOL#", "SUBCL#",
 "SUBR#", "SUBZR#", "SUBOR#", "SUBCR#",
 "SUBS#", "SUBZS#", "SUBOS#", "SUBCS#",
 "ADD", "ADDZ", "ADDO", "ADDC",
 "ADDL", "ADDZL", "ADDOL", "ADDCL",
 "ADDR", "ADDZR", "ADDOR", "ADDCR",
 "ADDS", "ADDZS", "ADDOS", "ADDCS",
 "ADD#", "ADDZ#", "ADDO#", "ADDC#",
 "ADDL#", "ADDZL#", "ADDOL#", "ADDCL#",
 "ADDR#", "ADDZR#", "ADDOR#", "ADDCR#",
 "ADDS#", "ADDZS#", "ADDOS#", "ADDCS#",
 "AND", "ANDZ", "ANDO", "ANDC",
 "ANDL", "ANDZL", "ANDOL", "ANDCL",
 "ANDR", "ANDZR", "ANDOR", "ANDCR",
 "ANDS", "ANDZS", "ANDOS", "ANDCS",
 "AND#", "ANDZ#", "ANDO#", "ANDC#",
 "ANDL#", "ANDZL#", "ANDOL#", "ANDCL#",
 "ANDR#", "ANDZR#", "ANDOR#", "ANDCR#",
 "ANDS#", "ANDZS#", "ANDOS#", "ANDCS#",
 "INTEN", "INTDS",
 "READS", "INTA", "MSKO", "IORST", "HALT",
#if !defined (ECLIPSE)
 "MUL", "DIV", "MULS", "DIVS",
 "PSHA", "POPA", "SAV", "RET",
 "MTSP", "MTFP", "MFSP", "MFFP",
 "LDB", "STB",
#endif
 "NIO", "NIOS", "NIOC", "NIOP",
 "DIA", "DIAS", "DIAC", "DIAP",
 "DOA", "DOAS", "DOAC", "DOAP",
 "DIB", "DIBS", "DIBC", "DIBP",
 "DOB", "DOBS", "DOBC", "DOBP",
 "DIC", "DICS", "DICC", "DICP",
 "DOC", "DOCS", "DOCC", "DOCP",
 "SKPBN", "SKPBZ", "SKPDN", "SKPDZ",
#if defined (ECLIPSE)
  "LEF", "LEF", "LEF", "LEF",
#endif
 NULL
 };

static const int32 opc_val[] = {
 0000000+I_M, 0004000+I_M, 0010000+I_M, 0014000+I_M,
 0020000+I_RM, 0040000+I_RM,
#if defined (ECLIPSE)
 0100010+I_RSI, 0100110+I_RSI, 0100210+I_2AC, 0100310+I_2AC,
 0100410+I_2AC, 0100510+I_2AC, 0100610+I_2AC, 0100710+I_2AC,
 0101010+I_2AC, 0101110+I_2AC, 0101210+I_RSI, 0101310+I_RSI,
 0101410+I_RSI, 0101510+I_RSI, 0101610+I_RSI, 0101710+I_RSI,
 0102010+I_2AC, 0102110+I_2AC, 0102210+I_2AC, 0102310+I_2AC,
 0102410+I_2AC, 0102510+I_2AC, 0102610+I_2AC, 0102710+I_2AC,
 0103010+I_2AC, 0103110+I_2AC, 0103210+I_2AC,
 0113410+I_NPN, 0103510+I_2AC,
 0103710+I_NPN, 0107710+I_NPN, 0113710+I_NPN, 0117710+I_NPN,
                0127710+I_NPN, 0133710+I_NPN, 0137710+I_NPN,
 0143710+I_NPN, 0147710+I_NPN, 0153710+I_NPN, 0157710+I_NPN,
 0163710+I_LI, 0167710+I_NPN,
 0100030+I_XP,
 0100050+I_2AC, 0100150+I_2AC, 0100250+I_2AC, 0100350+I_2AC,
 0100450+I_2AC, 0100550+I_2AC, 0100650+I_2AC, 0100750+I_2AC,
 0101050+I_FRM, 0101150+I_FRM, 0101250+I_FRM, 0101350+I_FRM,
 0101450+I_FRM, 0101550+I_FRM, 0101650+I_FRM, 0101750+I_FRM,
 0102050+I_FRM, 0102150+I_FRM, 0102250+I_FRM, 0102350+I_FRM,
 0102450+I_2AC, 0102550+I_FRM, 0102650+I_2AC, 0102750+I_FRM,
 0103050+I_R, 0123050+I_R, 0143050+I_R, 0163050+I_R,
 0103150+I_R, 0123150+I_R, 0143150+I_R, 0163150+I_R,
 0103250+I_NPN, 0107250+I_NPN, 0113250+I_NPN, 0117250+I_NPN,
 0123250+I_NPN, 0127250+I_NPN, 0133250+I_NPN, 0137250+I_NPN,
 0143250+I_NPN, 0147250+I_NPN, 0153250+I_NPN, 0157250+I_NPN,
 0163250+I_NPN, 0167250+I_NPN, 0173250+I_NPN, 0177250+I_NPN,
 0103350+I_FST, 0123350+I_FST,
 0143350+I_NPN, 0147350+I_NPN, 0153350+I_NPN,
 0163350+I_NPN, 0167350+I_NPN,
 0103450+I_2AC, 0103550+I_2AC,
 0153650+I_NPN, 0157650+I_NPN, 0163650+I_NPN, 0167650+I_NPN,
 0102070+I_LM, 0106070+I_LM, 0112070+I_LM, 0116070+I_LM,
                0122070+I_RLM, 0142070+I_RLM, 0162070+I_RLM,
 0102170+I_RLM, 0122170+I_RLM, 0142170+I_RLM,
 0102270+I_LM,  0102370+I_2AC, 0102770+I_2AC,
 0103370+I_R, 0123370+I_R, 0143370+I_R,
 0103770+I_RLI, 0123770+I_RLI, 0143770+I_RLI, 0163770+I_RLI, 
#endif
 0100000+I_RR, 0100020+I_RR, 0100040+I_RR, 0100060+I_RR,
 0100100+I_RR, 0100120+I_RR, 0100140+I_RR, 0100160+I_RR,
 0100200+I_RR, 0100220+I_RR, 0100240+I_RR, 0100260+I_RR,
 0100300+I_RR, 0100320+I_RR, 0100340+I_RR, 0100360+I_RR,
 0100010+I_RR, 0100030+I_RR, 0100050+I_RR, 0100070+I_RR,
 0100110+I_RR, 0100130+I_RR, 0100150+I_RR, 0100170+I_RR,
 0100210+I_RR, 0100230+I_RR, 0100250+I_RR, 0100270+I_RR,
 0100310+I_RR, 0100330+I_RR, 0100350+I_RR, 0100370+I_RR,
 0100400+I_RR, 0100420+I_RR, 0100440+I_RR, 0100460+I_RR,
 0100500+I_RR, 0100520+I_RR, 0100540+I_RR, 0100560+I_RR,
 0100600+I_RR, 0100620+I_RR, 0100640+I_RR, 0100660+I_RR,
 0100700+I_RR, 0100720+I_RR, 0100740+I_RR, 0100760+I_RR,
 0100410+I_RR, 0100430+I_RR, 0100450+I_RR, 0100470+I_RR,
 0100510+I_RR, 0100530+I_RR, 0100550+I_RR, 0100570+I_RR,
 0100610+I_RR, 0100630+I_RR, 0100650+I_RR, 0100670+I_RR,
 0100710+I_RR, 0100730+I_RR, 0100750+I_RR, 0100770+I_RR,
 0101000+I_RR, 0101020+I_RR, 0101040+I_RR, 0101060+I_RR,
 0101100+I_RR, 0101120+I_RR, 0101140+I_RR, 0101160+I_RR,
 0101200+I_RR, 0101220+I_RR, 0101240+I_RR, 0101260+I_RR,
 0101300+I_RR, 0101320+I_RR, 0101340+I_RR, 0101360+I_RR,
 0101010+I_RR, 0101030+I_RR, 0101050+I_RR, 0101070+I_RR,
 0101110+I_RR, 0101130+I_RR, 0101150+I_RR, 0101170+I_RR,
 0101210+I_RR, 0101230+I_RR, 0101250+I_RR, 0101270+I_RR,
 0101310+I_RR, 0101330+I_RR, 0101350+I_RR, 0101370+I_RR,
 0101400+I_RR, 0101420+I_RR, 0101440+I_RR, 0101460+I_RR,
 0101500+I_RR, 0101520+I_RR, 0101540+I_RR, 0101560+I_RR,
 0101600+I_RR, 0101620+I_RR, 0101640+I_RR, 0101660+I_RR,
 0101700+I_RR, 0101720+I_RR, 0101740+I_RR, 0101760+I_RR,
 0101410+I_RR, 0101430+I_RR, 0101450+I_RR, 0101470+I_RR,
 0101510+I_RR, 0101530+I_RR, 0101550+I_RR, 0101570+I_RR,
 0101610+I_RR, 0101630+I_RR, 0101650+I_RR, 0101670+I_RR,
 0101710+I_RR, 0101730+I_RR, 0101750+I_RR, 0101770+I_RR,
 0102000+I_RR, 0102020+I_RR, 0102040+I_RR, 0102060+I_RR,
 0102100+I_RR, 0102120+I_RR, 0102140+I_RR, 0102160+I_RR,
 0102200+I_RR, 0102220+I_RR, 0102240+I_RR, 0102260+I_RR,
 0102300+I_RR, 0102320+I_RR, 0102340+I_RR, 0102360+I_RR,
 0102010+I_RR, 0102030+I_RR, 0102050+I_RR, 0102070+I_RR,
 0102110+I_RR, 0102130+I_RR, 0102150+I_RR, 0102170+I_RR,
 0102210+I_RR, 0102230+I_RR, 0102250+I_RR, 0102270+I_RR,
 0102310+I_RR, 0102330+I_RR, 0102350+I_RR, 0102370+I_RR,
 0102400+I_RR, 0102420+I_RR, 0102440+I_RR, 0102460+I_RR,
 0102500+I_RR, 0102520+I_RR, 0102540+I_RR, 0102560+I_RR,
 0102600+I_RR, 0102620+I_RR, 0102640+I_RR, 0102660+I_RR,
 0102700+I_RR, 0102720+I_RR, 0102740+I_RR, 0102760+I_RR,
 0102410+I_RR, 0102430+I_RR, 0102450+I_RR, 0102470+I_RR,
 0102510+I_RR, 0102530+I_RR, 0102550+I_RR, 0102570+I_RR,
 0102610+I_RR, 0102630+I_RR, 0102650+I_RR, 0102670+I_RR,
 0102710+I_RR, 0102730+I_RR, 0102750+I_RR, 0102770+I_RR,
 0103000+I_RR, 0103020+I_RR, 0103040+I_RR, 0103060+I_RR,
 0103100+I_RR, 0103120+I_RR, 0103140+I_RR, 0103160+I_RR,
 0103200+I_RR, 0103220+I_RR, 0103240+I_RR, 0103260+I_RR,
 0103300+I_RR, 0103320+I_RR, 0103340+I_RR, 0103360+I_RR,
 0103010+I_RR, 0103030+I_RR, 0103050+I_RR, 0103070+I_RR,
 0103110+I_RR, 0103130+I_RR, 0103150+I_RR, 0103170+I_RR,
 0103210+I_RR, 0103230+I_RR, 0103250+I_RR, 0103270+I_RR,
 0103310+I_RR, 0103330+I_RR, 0103350+I_RR, 0103370+I_RR,
 0103400+I_RR, 0103420+I_RR, 0103440+I_RR, 0103460+I_RR,
 0103500+I_RR, 0103520+I_RR, 0103540+I_RR, 0103560+I_RR,
 0103600+I_RR, 0103620+I_RR, 0103640+I_RR, 0103660+I_RR,
 0103700+I_RR, 0103720+I_RR, 0103740+I_RR, 0103760+I_RR,
 0103410+I_RR, 0103430+I_RR, 0103450+I_RR, 0103470+I_RR,
 0103510+I_RR, 0103530+I_RR, 0103550+I_RR, 0103570+I_RR,
 0103610+I_RR, 0103630+I_RR, 0103650+I_RR, 0103670+I_RR,
 0103710+I_RR, 0103730+I_RR, 0103750+I_RR, 0103770+I_RR,
 0060177+I_NPN, 0060277+I_NPN,
 0060477+I_R, 0061477+I_R, 0062077+I_R, 0062677+I_NPN, 0063077+I_NPN,
#if !defined (ECLIPSE)
 0073301+I_NPN, 0073101+I_NPN, 0077201+I_NPN, 0077001+I_NPN,
 0061401+I_R, 0061601+I_R, 0062401+I_NPN, 0062601+I_NPN,
 0061001+I_R, 0060001+I_R, 0061201+I_R, 0060201+I_R,
 0060401+I_BY, 0062001+I_BY,
#endif
 0060000+I_RD, 0060100+I_RD, 0060200+I_RD, 0060300+I_RD,
 0060400+I_RD, 0060500+I_RD, 0060600+I_RD, 0060700+I_RD,
 0061000+I_RD, 0061100+I_RD, 0061200+I_RD, 0061300+I_RD,
 0061400+I_RD, 0061500+I_RD, 0061600+I_RD, 0061700+I_RD,
 0062000+I_RD, 0062100+I_RD, 0062200+I_RD, 0062300+I_RD,
 0062400+I_RD, 0062500+I_RD, 0062600+I_RD, 0062700+I_RD,
 0063000+I_RD, 0063100+I_RD, 0063200+I_RD, 0063300+I_RD,
 0063400+I_D, 0063500+I_D, 0063600+I_D, 0063700+I_D,
#if defined (ECLIPSE)
 0064000+I_D, 0070000+I_D, 0074000+I_D, 0076000+I_D,
#endif
 -1
 };
 
static const char *skip[] = {
 "SKP", "SZC", "SNC", "SZR", "SNR", "SEZ", "SBN",
 NULL
 };

static const char *device[] = {
#if defined (ECLIPSE)
 "ERCC", "MAP",
#endif
 "TTI", "TTO", "PTR", "PTP", "RTC", "PLT", "CDR", "LPT",
 "DSK", "MTA", "DCM", "QTY" /* "ADCV" */, "DKP", "CAS",
 "TTI1", "TTO1", "CPU",
 NULL
 };

static const int32 dev_val[] = {
#if defined (ECLIPSE)
 002, 003,
#endif
 010, 011, 012, 013, 014, 015, 016, 017,
 020, 022, 024, 030, 033, 034, 
 050, 051, 077,
 -1
 };

/* Address decode

   Inputs:
        *of     =       output stream
        addr    =       current PC
        ind     =       indirect flag
        mode    =       addressing mode
        disp    =       displacement
        ext     =       true if extended address
        cflag   =       true if decoding for CPU
   Outputs:
        return  =       error code
*/
t_stat fprint_addr (FILE *of, t_addr addr, int32 ind, int32 mode,
    int32 disp, t_bool ext, int32 cflag)
{
int32 dsign, dmax;

if (ext)                                                /* get max disp */
    dmax = AMASK + 1;
else dmax = I_M_DISP + 1;
dsign = dmax >> 1;                                      /* get disp sign */
if (ind)                                                /* indirect? */
    fprintf (of, "@");
switch (mode & 03) {                                    /* mode */

    case 0:                                             /* absolute */
        fprintf (of, "%-o", disp);
        break;

    case 1:                                             /* PC rel */
        if (disp & dsign) {
            if (cflag)
                fprintf (of, "%-o", (addr - (dmax - disp)) & AMASK);
            else fprintf (of, ".-%-o", dmax - disp);
            }
        else {
            if (cflag)
                fprintf (of, "%-o", (addr + disp) & AMASK);
            else fprintf (of, ".+%-o", disp);
            }
        break;

    case 2:                                             /* AC2 rel */
        if (disp & dsign)
            fprintf (of, "-%-o,2", dmax - disp);
        else fprintf (of, "%-o,2", disp);
        break;

    case 3:                                             /* AC3 rel */
        if (disp & dsign)
            fprintf (of, "-%-o,3", dmax - disp);
        else fprintf (of, "%-o,3", disp);
        break;
        }                                               /* end switch */

return SCPE_OK;
}

/* Symbolic output

   Inputs:
        *of     =       output stream
        addr    =       current PC
        *val    =       pointer to values
        *uptr   =       pointer to unit
        sw      =       switches
   Outputs:
        status  =       error code
*/

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
    UNIT *uptr, int32 sw)
{
int32 cflag, i, j, c1, c2, inst, inst1, dv, src, dst, skp;
int32 ind, mode, disp, dev;
int32 byac, extind, extdisp, xop;

cflag = (uptr == NULL) || (uptr == &cpu_unit);
c1 =  ((int32) val[0] >> 8) & 0177;
c2 = (int32) val[0] & 0177;
if (sw & SWMASK ('A')) {                                /* ASCII? */
    fprintf (of, (c2 < 040)? "<%03o>": "%c", c2);
    return SCPE_OK;
    }
if (sw & SWMASK ('C')) {                                /* character? */
    fprintf (of, (c1 < 040)? "<%03o>": "%c", c1);
    fprintf (of, (c2 < 040)? "<%03o>": "%c", c2);
    return SCPE_OK;
    }
if (!(sw & SWMASK ('M')))                               /* mnemonic? */
    return SCPE_ARG;

/* Instruction decode */

inst = (int32) val[0];
inst1 = (int32) val[1];
for (i = 0; opc_val[i] >= 0; i++) {                     /* loop thru ops */
    j = (opc_val[i] >> I_V_FL) & I_M_FL;                /* get class */
    if ((opc_val[i] & 0177777) == (inst & masks[j])) {  /* match? */
        src = I_GETSRC (inst);                          /* opr fields */
        dst = I_GETDST (inst);
        skp = I_GETSKP (inst);
        ind = inst & I_IND;                             /* mem ref fields */
        mode = I_GETMODE (inst);
        disp = I_GETDISP (inst);
        dev = I_GETDEV (inst);                          /* IOT fields */
        byac = I_GETPULSE (inst);                       /* byte fields */
        xop = I_GETXOP (inst);                          /* XOP fields */
        extind = inst1 & A_IND;                         /* extended fields */
        extdisp = inst1 & AMASK;
        for (dv = 0; (dev_val[dv] >= 0) && (dev_val[dv] != dev); dv++) ;

        switch (j) {                                    /* switch on class */

        case I_V_NPN:                                   /* no operands */
            fprintf (of, "%s", opcode[i]);              /* opcode */
            break;

        case I_V_R:                                     /* reg only */
            fprintf (of, "%s %-o", opcode[i], dst);
            break;

        case I_V_D:                                     /* dev only */
#if defined (ECLIPSE)
            if (Usermap && (MapStat & 0100)) {          /* the evil LEF mode */
                fprintf (of, "LEF %-o,", dst);
                fprint_addr (of, addr, ind, mode, disp, FALSE, cflag);
                break;
                }
#endif
            if (dev_val[dv] >= 0)
                fprintf (of, "%s %s", opcode[i], device[dv]);
            else fprintf (of, "%s %-o", opcode[i], dev);
            break;

        case I_V_RD:                                    /* reg, dev */
            if (dev_val[dv] >= 0)
                fprintf (of, "%s %-o,%s", opcode[i], dst, device[dv]);
            else fprintf (of, "%s %-o,%-o", opcode[i], dst, dev);
            break;

        case I_V_M:                                     /* addr only */
            fprintf (of, "%s ", opcode[i]);
            fprint_addr (of, addr, ind, mode, disp, FALSE, cflag);
            break;

        case I_V_RM:                                    /* reg, addr */
            fprintf (of, "%s %-o,", opcode[i], dst);
            fprint_addr (of, addr, ind, mode, disp, FALSE, cflag);
            break;

        case I_V_RR:                                    /* operate */
            fprintf (of, "%s %-o,%-o", opcode[i], src, dst);
            if (skp)
                fprintf (of, ",%s", skip[skp-1]);
            break;

        case I_V_BY:                                    /* byte */
            fprintf (of, "%s %-o,%-o", opcode[i], byac, dst);
            break;

        case I_V_2AC:                                   /* reg, reg */
            fprintf (of, "%s %-o,%-o", opcode[i], src, dst);
            break;

        case I_V_RSI:                                   /* reg, short imm */
            fprintf (of, "%s %-o,%-o", opcode[i], src + 1, dst);
            break;

        case I_V_LI:                                    /* long imm */
            fprintf (of, "%s %-o", opcode[i], inst1);
            return -1;

        case I_V_RLI:                                   /* reg, long imm */
            fprintf (of, "%s %-o,%-o", opcode[i], inst1, dst);
            return -1;

        case I_V_LM:                                    /* long addr */
            fprintf (of, "%s ", opcode[i]);
            fprint_addr (of, addr, extind, mode, extdisp, TRUE, cflag);
            return -1;

        case I_V_RLM:                                   /* reg, long addr */
            fprintf (of, "%s %-o,", opcode[i], dst);
            fprint_addr (of, addr, extind, mode, extdisp, TRUE, cflag);
            return -1;

        case I_V_FRM:                                   /* flt reg, long addr */
            fprintf (of, "%s %-o,", opcode[i], dst);
            fprint_addr (of, addr, extind, src, extdisp, TRUE, cflag);
            return -1;

        case I_V_FST:                                   /* flt status */
            fprintf (of, "%s ", opcode[i]);
            fprint_addr (of, addr, extind, dst, extdisp, AMASK + 1, cflag);
            return -1;

        case I_V_XP:                                    /* XOP */
            fprintf (of, "%s %-o,%-o,%-o", opcode[i], src, dst, xop);
            break;                                      /* end case */

        default:
            fprintf (of, "??? [%-o]", inst);
            break;
            }
        return SCPE_OK;
        }                                               /* end if */
    }                                                   /* end for */
return SCPE_ARG;
}

/* Address parse

   Inputs:
        *cptr   =       pointer to input string
        addr    =       current PC
        ext     =       extended address
        cflag   =       true if parsing for CPU
        val[3]  =       output array
   Outputs:
        optr    =       pointer to next char in input string
                        NULL if error
*/

#define A_FL    001                                     /* CPU flag */
#define A_NX    002                                     /* index seen */
#define A_PER   004                                     /* period seen */
#define A_NUM   010                                     /* number seen */
#define A_SI    020                                     /* sign seen */
#define A_MI    040                                     /* - seen */

CONST char *get_addr (CONST char *cptr, t_addr addr, t_bool ext, int32 cflag, int32 *val)
{
int32 d, x, pflag;
t_stat r;
char gbuf[CBUFSIZE];
int32 dmax, dsign;

if (ext)                                                /* get max disp */
    dmax = AMASK + 1;
else dmax = I_M_DISP + 1;
dsign = dmax >> 1;                                      /* get disp sign */
val[0] = 0;                                             /* no indirect */
val[1] = 0;                                             /* PC rel */
val[2] = 0;                                             /* no addr */

pflag = cflag & A_FL;                                   /* isolate flag */
if (*cptr == '@') {                                     /* indirect? */
    val[0] = 1;
    cptr++;
    }           
if (*cptr == '.') {                                     /* relative? */
    pflag = pflag | A_PER;
    x = 1;                                              /* "index" is PC */
    d = 0;                                              /* default disp is 0 */
    cptr++;
    }
if (*cptr == '+') {                                     /* + sign? */
    pflag = pflag | A_SI;
    cptr++;
    }
else if (*cptr == '-') {                                /* - sign? */
    pflag = pflag | A_MI | A_SI;
    cptr++;
    }   
if (*cptr != 0) {                                       /* number? */
    cptr = get_glyph (cptr, gbuf, ',');                 /* get glyph */
    d = (int32) get_uint (gbuf, 8, AMASK, &r);
    if (r != SCPE_OK)
        return NULL;
    pflag = pflag | A_NUM;
    }
if (*cptr != 0) {                                       /* index? */
    cptr = get_glyph (cptr, gbuf, 0);                   /* get glyph */
    x = (int32) get_uint (gbuf, 8, I_M_DST, &r);
    if ((r != SCPE_OK) || (x < 2))
        return NULL;
    pflag = pflag | A_NX;
    }

switch (pflag) {                                        /* case on flags */

    case A_NUM: case A_NUM+A_SI:                        /* ~CPU, (+)num */
        if (d < dmax)
            val[2] = d;
        else return NULL;
        break;

    case A_NUM+A_FL: case A_NUM+A_SI+A_FL:              /* CPU, (+)num */
        if (d < dmax)
            val[2] = d;
        else if (((d >= (((int32) addr - dsign) & AMASK)) &&
                  (d < (((int32) addr + dsign) & AMASK))) ||
                  (d >= ((int32) addr + (-dsign & AMASK)))) {
            val[1] = 1;                                 /* PC rel */
            val[2] = (d - addr) & (dmax - 1);
            }
        else return NULL;
        break;

    case A_PER: case A_PER+A_FL:                        /* . */
    case A_PER+A_SI+A_NUM: case A_PER+A_SI+A_NUM+A_FL:  /* . + num */
    case A_PER+A_SI+A_MI+A_NUM:                         /* . - num */
    case A_PER+A_SI+A_MI+A_NUM+A_FL:
    case A_NX+A_NUM: case A_NX+A_NUM+A_FL:              /* num, ndx */
    case A_NX+A_SI+A_NUM: case A_NX+A_SI+A_NUM+A_FL:    /* +num, ndx */
    case A_NX+A_SI+A_MI+A_NUM:                          /* -num, ndx */
    case A_NX+A_SI+A_MI+A_NUM+A_FL:
        val[1] = x;                                     /* set mode */
        if (((pflag & A_MI) == 0) && (d < dsign))
            val[2] = d;
        else if ((pflag & A_MI) && (d <= dsign))
            val[2] = (dmax - d);
        else return NULL;
        break;

    default:
        return NULL;
        }                                               /* end case */

return cptr;
}

/* Parse two registers 

   Inputs:
        *cptr   =       input string
        term    =       second terminating character
        val     =       output array
   Outputs:
        optr    =       pointer to next char in input string
                        NULL if error
*/

CONST char *get_2reg (CONST char *cptr, char term, int32 *val)
{
char gbuf[CBUFSIZE];
t_stat r;

cptr = get_glyph (cptr, gbuf, ',');                     /* get register */
val[0] = (int32) get_uint (gbuf, 8, I_M_SRC, &r);
if (r != SCPE_OK)
    return NULL;
cptr = get_glyph (cptr, gbuf, term);                    /* get register */
val[1] = (int32) get_uint (gbuf, 8, I_M_DST, &r);
if (r != SCPE_OK)
    return NULL;
return cptr;
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

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32 cflag, d, i, j, amd[3];
t_stat r, rtn;
char gbuf[CBUFSIZE];

cflag = (uptr == NULL) || (uptr == &cpu_unit);
while (isspace (*cptr)) cptr++;                         /* absorb spaces */
if ((sw & SWMASK ('A')) || ((*cptr == '\'') && cptr++)) { /* ASCII char? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    val[0] = (t_value) cptr[0];
    return SCPE_OK;
    }
if ((sw & SWMASK ('C')) || ((*cptr == '"') && cptr++)) { /* ASCII string? */
    if (cptr[0] == 0)                                   /* must have 1 char */
        return SCPE_ARG;
    val[0] = ((t_value) cptr[0] << 8) + (t_value) cptr[1];
    return SCPE_OK;
    }

/* Instruction parse */

rtn = SCPE_OK;                                          /* assume 1 word */
cptr = get_glyph (cptr, gbuf, 0);                       /* get opcode */
for (i = 0; (opcode[i] != NULL) && (strcmp (opcode[i], gbuf) != 0) ; i++) ;
if (opcode[i] == NULL)
    return SCPE_ARG;
val[0] = opc_val[i] & 0177777;                          /* get value */
j = (opc_val[i] >> I_V_FL) & I_M_FL;                    /* get class */

switch (j) {                                            /* case on class */

    case I_V_NPN:                                       /* no operand */
        break;

    case I_V_R:                                         /* IOT reg */
        cptr = get_glyph (cptr, gbuf, 0);               /* get register */
        d = (int32) get_uint (gbuf, 8, I_M_DST, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        val[0] = val[0] | (d << I_V_DST);               /* put in place */
        break;

    case I_V_RD:                                        /* IOT reg,dev */
        cptr = get_glyph (cptr, gbuf, ',');             /* get register */
        d = (int32) get_uint (gbuf, 8, I_M_DST, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        val[0] = val[0] | (d << I_V_DST);               /* put in place */
    case I_V_D:                                         /* IOT dev */
        cptr = get_glyph (cptr, gbuf, 0);               /* get device */
        for (i = 0; (device[i] != NULL) &&
                    (strcmp (device[i], gbuf) != 0); i++);
        if (device[i] != NULL)
            val[0] = val[0] | dev_val[i];
        else {
            d = (int32) get_uint (gbuf, 8, I_M_DEV, &r);
            if (r != SCPE_OK)
                return SCPE_ARG;
            val[0] = val[0] | (d << I_V_DEV);
            }
        break;

    case I_V_RM:                                        /* reg, addr */
        cptr = get_glyph (cptr, gbuf, ',');             /* get register */
        d = (int32) get_uint (gbuf, 8, I_M_DST, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        val[0] = val[0] | (d << I_V_DST);               /* put in place */
    case I_V_M:                                         /* addr */
        cptr = get_addr (cptr, addr, FALSE, cflag, amd);
        if (cptr == NULL)
            return SCPE_ARG;
        val[0] = val[0] | (amd[0] << I_V_IND) | (amd[1] << I_V_MODE) | amd[2];
        break;

    case I_V_RR:                                        /* operate */
        cptr = get_2reg (cptr, ',', amd);               /* get 2 reg */
        if (cptr == NULL)
            return SCPE_ARG;
        val[0] = val[0] | (amd[0] << I_V_SRC) | (amd[1] << I_V_DST);
        if (*cptr != 0) {                               /* skip? */
            cptr = get_glyph (cptr, gbuf, 0);           /* get skip */
            for (i = 0; (skip[i] != NULL) &&
                (strcmp (skip[i], gbuf) != 0); i++) ;
            if (skip[i] == NULL)
                return SCPE_ARG;
            val[0] = val[0] | (i + 1);
            }                                           /* end if */
        break;

    case I_V_BY:                                        /* byte */
        cptr = get_2reg (cptr, 0, amd);                 /* get 2 reg */
        if (cptr == NULL)
            return SCPE_ARG;
        val[0] = val[0] | (amd[0] << I_V_PULSE) | (amd[1] << I_V_DST);
        break;

    case I_V_2AC:                                       /* reg, reg */
        cptr = get_2reg (cptr, 0, amd);                 /* get 2 reg */
        if (cptr == NULL)
            return SCPE_ARG;
        val[0] = val[0] | (amd[0] << I_V_SRC) | (amd[1] << I_V_DST);
        break;

    case I_V_RSI:                                       /* reg, short imm */
        cptr = get_glyph (cptr, gbuf, ',');             /* get immediate */
        d = (int32) get_uint (gbuf, 8, I_M_SRC + 1, &r);
        if ((d == 0) || (r != SCPE_OK))
            return SCPE_ARG;
        val[0] = val[0] | ((d - 1) << I_V_SRC);         /* put in place */
        cptr = get_glyph (cptr, gbuf, 0);               /* get register */
        d = (int32) get_uint (gbuf, 8, I_M_DST, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        val[0] = val[0] | (d << I_V_DST);               /* put in place */
        break;

    case I_V_RLI:                                       /* reg, long imm */
        cptr = get_glyph (cptr, gbuf, ',');             /* get immediate */
        val[1] = (int32) get_uint (gbuf, 8, DMASK, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        cptr = get_glyph (cptr, gbuf, 0);               /* get register */
        d = (int32) get_uint (gbuf, 8, I_M_DST, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        val[0] = val[0] | (d << I_V_DST);               /* put in place */
        rtn = -1;
        break;

    case I_V_LI:                                        /* long imm */
        cptr = get_glyph (cptr, gbuf, 0);               /* get immediate */
        val[1] = (int32) get_uint (gbuf, 8, DMASK, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        rtn = -1;
        break;

    case I_V_RLM:                                       /* reg, long mem */
        cptr = get_glyph (cptr, gbuf, ',');             /* get register */
        d = (int32) get_uint (gbuf, 8, I_M_DST, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        val[0] = val[0] | (d << I_V_DST);               /* put in place */
    case I_V_LM:                                        /* long mem */
        cptr = get_addr (cptr, addr, TRUE, cflag, amd);
        if (cptr == NULL)
            return SCPE_ARG;
        val[0] = val[0] | (amd[1] << I_V_MODE);
        val[1] = (amd[0] << A_V_IND) | amd[2];
        rtn = -1;
        break;

    case I_V_FRM:                                       /* flt reg, long mem */
        cptr = get_glyph (cptr, gbuf, ',');             /* get register */
        d = (int32) get_uint (gbuf, 8, I_M_DST, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        val[0] = val[0] | (d << I_V_DST);               /* put in place */
        cptr = get_addr (cptr, addr, TRUE, cflag, amd);
        if (cptr == NULL)
            return SCPE_ARG;
        val[0] = val[0] | (amd[1] << I_V_SRC);
        val[1] = (amd[0] << A_V_IND) | amd[2];
        rtn = -1;
        break;

    case I_V_FST:                                       /* flt status */
        cptr = get_addr (cptr, addr, TRUE, cflag, amd);
        if (cptr == NULL)
            return SCPE_ARG;
        val[0] = val[0] | (amd[1] << I_V_DST);
        val[1] = (amd[0] << A_V_IND) | amd[2];
        rtn = -1;
        break;

    case I_V_XP:                                        /* XOP */
        cptr = get_2reg (cptr, ',', amd);               /* get 2 reg */
        if (cptr == NULL)
            return SCPE_ARG;
        val[0] = val[0] | (amd[0] << I_V_SRC) | (amd[1] << I_V_DST);    
        cptr = get_glyph (cptr, gbuf, 0);               /* get argument */
        d = (int32) get_uint (gbuf, 8, I_M_XOP, &r);
        if (r != SCPE_OK)
            return SCPE_ARG;
        val[0] = val[0] | (d << I_V_XOP);
        break;
        }                                               /* end case */

if (*cptr != 0)                                         /* any leftovers? */
    return SCPE_ARG;
return rtn;
}
