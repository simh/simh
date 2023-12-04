/* sel32_cpu.c: Sel 32 CPU simulator

   Copyright (c) 2018-2022, James C. Bevier
   Portions provided by Richard Cornwell, Geert Rolf and other SIMH contributers

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
   JAMES C. BEVIER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "sel32_defs.h"

/* Concept 32 PSD Mode Trap/Interrupt Priorities */
/* Relative|Logical |Int Vect|TCW |IOCD|Description */
/* Priority|Priority|Location|Addr|Addr             */
/*   -                 080              Power Fail Safe Trap */
/*   -                 084              Power On Trap */
/*   -                 088              Memory Parity Trap */
/*   -                 08C              Nonpresent Memory Trap */
/*   -                 090              Undefined Instruction Trap */
/*   -                 094              Privilege Violation Trap */
/*   -                 098              Supervisor Call Trap (SVC) */
/*   -                 09C              Machine Check Trap */
/*   -                 0A0              System Check Trap */
/*   -                 0A4              Map Fault Trap */
/*   -                 0A8              CALM or Undefined IPU Instruction Trap */
/*   -                 0AC              Signal CPU or Signal IPU Trap */
/*   -                 0B0              Address Specification Trap */
/*   -                 0B4              Console Attention Trap */
/*   -                 0B8              Privlege Mode Halt Trap */
/*   -                 0BC              Arithmetic Exception Trap */
/*   -                 0C0              Cache Error Trap (V9 Only) */
/*   -                 0C4              Demand Page Fault Trap (V6&V9 Only) */
/*                                                                */
/*   0        00       100              External/software Interrupt 0 */
/*   1        01       104              External/software Interrupt 1 */
/*   2        02       108              External/software Interrupt 2 */
/*   3        03       10C              External/software Interrupt 3 */
/*   4        04       110    704  700  I/O Channel 0 interrupt */  
/*   5        05       114    70C  708  I/O Channel 1 interrupt */  
/*   6        06       118    714  710  I/O Channel 2 interrupt */  
/*   7        07       11C    71C  718  I/O Channel 3 interrupt */  
/*   8        08       120    724  720  I/O Channel 4 interrupt */  
/*   9        09       124    72C  728  I/O Channel 5 interrupt */  
/*   A        0A       128    734  730  I/O Channel 6 interrupt */  
/*   B        0B       12C    73C  738  I/O Channel 7 interrupt */  
/*   C        0C       130    744  740  I/O Channel 8 interrupt */  
/*   D        0D       134    74C  748  I/O Channel 9 interrupt */  
/*   E        0E       138    754  750  I/O Channel A interrupt */  
/*   F        0F       13C    75C  758  I/O Channel B interrupt */  
/*  10        10       140    764  760  I/O Channel C interrupt */  
/*  11        11       144    76C  768  I/O Channel D interrupt */  
/*  12        12       148    774  770  I/O Channel E interrupt */  
/*  13        13       14c    77C  778  I/O Channel F interrupt */  
/*  14        14       150              External/Software Interrupt */
/*  15        15       154              External/Software Interrupt */
/*  16        16       158              External/Software Interrupt */
/*  17        17       15C              External/Software Interrupt */
/*  18        18       160              Real-Time Clock Interrupt */
/*  19        19       164              External/Software Interrupt */
/*  1A        1A       1A8              External/Software Interrupt */
/*  1B        1B       1AC              External/Software Interrupt */
/*  1C        1C       1B0              External/Software Interrupt */
/* THRU      THRU     THRU                        THRU              */ 
/*  6C        6C       2B0              External/Software Interrupt */
/*  6D        6D       2B4              External/Software Interrupt */
/*  6E        6E       2B8              External/Software Interrupt */
/*  6F        6F       2BC              Interval Timer Interrupt */

/* IVL ------------> ICB   Trap/Interrupt Vector Location points to Interrupt Context Block */
/*                   Wd 0 - Old PSD Word 1  points to return location */
/*                   Wd 1 - Old PSD Word 2 */
/*                   Wd 2 - New PSD Word 1  points to first instruction of service routine */
/*                   Wd 3 - New PSD Word 2 */
/*                   Wd 4 - CPU Status word at time of interrupt/trap */
/*                   Wd 5 - N/U For Traps/Interrupts */

/* IVL ------------> ICB   XIO Interrupt Vector Location */
/*                   Wd 0 - Old PSD Word 1  points to return location */
/*                   Wd 1 - Old PSD Word 2 */
/*                   Wd 2 - New PSD Word 1  points to first instruction of service routine */
/*                   Wd 3 - New PSD Word 2 */
/*                   Wd 4 - Input/Output Command List Address (IOCL) for the Class F I/O CHannel */
/*                   Wd 5 - 24 bit real address of the channel status word */

/* CPU registers, map cache, spad, and other variables */
int             cpu_index;                  /* Current CPU running */
uint32          PSD[2];                     /* the PC for the instruction */
#define PSD1 PSD[0]                         /* word 1 of PSD */
#define PSD2 PSD[1]                         /* word 2 of PSD */
uint32          M[MAXMEMSIZE] = { 0 };      /* Memory */
uint32          GPR[8];                     /* General Purpose Registers */
uint32          BR[8];                      /* Base registers */
uint32          PC;                         /* Program counter */
uint32          CC;                         /* Condition codes, bits 1-4 of PSD1 */
uint32          CPUSTATUS;                  /* cpu status word */
uint32          TRAPSTATUS;                 /* trap status word */
uint32          SPAD[256];                  /* Scratch pad memory */
uint32          INTS[128];                  /* Interrupt status flags */
uint32          pad[16];                    /* In case of wrong access */
uint32          CMCR;                       /* Cache Memory Control Register */
uint32          SMCR;                       /* Shared Memory Control Register */
uint32          CMSMC;                      /* V9 Cache/Shadow Memory Configuration */
uint32          CSMCW;                      /* CPU Shadow Memory Configuration Word */
uint32          ISMCW;                      /* IPU Shadow Memory Configuration Word */
uint32          CCW;                        /* Computer Configuration Word */
uint32          CSW = 0;                    /* Console switches going to 0x780 */
uint32          BOOTR[8] = {0};             /* Boot registers settings */
/* CPU mapping cache entries */
/* 32/55 has none */
/* 32/7x has 32 8KW maps per task */
/* Concept 32/27 has 256 2KW maps per task */
/* Concept 32/X7 has 2048 2KW maps per task */
uint32          MAPC[1024];                 /* maps are 16bit entries on word bountries */
uint32          dummy=0;
uint32          pfault;                     /* page # of fault from read/write */
uint32          BPIX=0;                     /* # pages loaded for O/S */
uint32          CPIXPL=0;                   /* highest page loaded for User */
uint32          CPIX=0;                     /* CPIX user MPL offset */
uint32          HIWM=0;                     /* max maps loaded so far */
uint32          MODES=0;                    /* Operating modes, bits 0, 5, 6, 7 of PSD1 */
uint32          TLB[2048];                  /* Translated addresses for each map entry */
/* bits 0-4 are bits 0-4 from map entry */
/* bit 0 valid */
/* bit 1 p1 write access if set */
/* bit 2 p2 write access if set */
/* bit 3 p3 write access if set MM - memory modify */
/* bit 4 p4 write access if set MA - memory accessed */
/* bit 5 hit bit means entry is setup, even if not valid map */
/* if hit bit is set and entry not valid, we will do a page fault */
/* bit 6 dirty bit, set when written to, page update required */
/* bits 8-18 has map reg contents for this page (Map << 13) */
/* bit 19-31 is zero for page offset of zero */

uint32          dummy2=0;
uint8           wait4int = 0;               /* waiting for interrupt if set */
int32           irq_auto = 0;               /* auto reset interrupt processing flag */

/* define traps */
uint32          TRAPME = 0;                 /* trap to be executed */
uint32          attention_trap = 0;         /* set when trap is requested */

uint32          RDYQIN;                     /* fifo input index */
uint32          RDYQOUT;                    /* fifo output index */
uint32          RDYQ[128];                  /* channel ready queue */
uint8           waitqcnt = 0;               /* # instructions before start */

struct InstHistory
{
    uint32   opsd1;                         /* original PSD1 */
    uint32   opsd2;                         /* original PSD2 */
    uint32   npsd1;                         /* new PSD1 after instruction */
    uint32   npsd2;                         /* new PSD2 after instruction */
    uint32   oir;                           /* the instruction itself */
    uint32   modes;                         /* current cpu mode bits */
    uint32   reg[16];                       /* regs/bregs for operation */
};

/* forward definitions */
t_stat cpu_ex(t_value * vptr, t_addr addr, UNIT * uptr, int32 sw);
t_stat cpu_dep(t_value val, t_addr addr, UNIT * uptr, int32 sw);
t_stat cpu_reset(DEVICE * dptr);
t_stat cpu_set_size(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
t_stat cpu_show_hist(FILE * st, UNIT * uptr, int32 val, CONST void *desc);
t_stat cpu_set_hist(UNIT * uptr, int32 val, CONST char *cptr, void *desc);
uint32 cpu_cmd(UNIT * uptr, uint16 cmd, uint16 dev);
t_stat cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *cpu_description (DEVICE *dptr);
t_stat RealAddr(uint32 addr, uint32 *realaddr, uint32 *prot, uint32 access);
t_stat load_maps(uint32 thepsd[2], uint32 lmap);
t_stat read_instruction(uint32 thepsd[2], uint32 *instr);
t_stat Mem_read(uint32 addr, uint32 *data);
t_stat Mem_write(uint32 addr, uint32 *data);

/* external definitions */
extern t_stat checkxio(uint16 addr, uint32 *status);    /* XIO check in chan.c */
extern t_stat startxio(uint16 addr, uint32 *status);    /* XIO start in chan.c */
extern t_stat testxio(uint16 addr, uint32 *status);     /* XIO test in chan.c */
extern t_stat stopxio(uint16 addr, uint32 *status);     /* XIO stop in chan.c */
extern t_stat rschnlxio(uint16 addr, uint32 *status);   /* reset channel XIO */
extern t_stat haltxio(uint16 addr, uint32 *status);     /* halt XIO */
extern t_stat grabxio(uint16 addr, uint32 *status);     /* grab XIO n/u */
extern t_stat rsctlxio(uint16 addr, uint32 *status);    /* reset controller XIO */
extern t_stat chan_set_devs();                          /* set up the defined devices on the simulator */
extern uint32 scan_chan(uint32 *ilev);                  /* go scan for I/O int pending */
extern uint32 cont_chan(uint16 chsa);                   /* continue channel program */
extern uint16 loading;                                  /* set when doing IPL */
extern int fprint_inst(FILE *of, uint32 val, int32 sw); /* instruction print function */
extern int irq_pend;                                    /* go scan for pending interrupt */
extern void rtc_setup(uint32 ss, uint32 level);         /* tell rtc to start/stop */
extern void itm_setup(uint32 ss, uint32 level);         /* tell itm to start/stop */
extern int32 itm_rdwr(uint32 cmd, int32 cnt, uint32 level); /* read/write the interval timer */
extern int16 post_csw(CHANP *chp, uint32 rstat);
extern DIB  *dib_chan[MAX_CHAN];

/* floating point subroutines definitions */
extern uint32   s_fixw(uint32 val, uint32 *cc);
extern uint32   s_fltw(uint32 val, uint32 *cc);
extern t_uint64 s_fixd(t_uint64 val, uint32 *cc);
extern t_uint64 s_fltd(t_uint64 val, uint32 *cc);
extern uint32   s_nor(uint32 reg, uint32 *exp);
extern t_uint64 s_nord(t_uint64 reg, uint32 *exp);
extern uint32   s_adfw(uint32 reg, uint32 mem, uint32 *cc);
extern uint32   s_sufw(uint32 reg, uint32 mem, uint32 *cc);
extern t_uint64 s_adfd(t_uint64 reg, t_uint64 mem, uint32 *cc);
extern t_uint64 s_sufd(t_uint64 reg, t_uint64 mem, uint32 *cc);
extern uint32   s_mpfw(uint32 reg, uint32 mem, uint32 *cc);
extern uint32   s_dvfw(uint32 reg, uint32 mem, uint32 *cc);
extern t_uint64 s_mpfd(t_uint64 reg, t_uint64 mem, uint32 *cc);
extern t_uint64 s_dvfd(t_uint64 reg, t_uint64 mem, uint32 *cc);
extern uint32   s_normfw(uint32 mem, uint32 *cc);
extern t_uint64 s_normfd(t_uint64 mem, uint32 *cc);

/* History information */
int32           hst_p = 0;                  /* History pointer */
int32           hst_lnt = 0;                /* History length */
struct InstHistory *hst = NULL;             /* History stack */

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT  cpu_unit =
    /* Unit data layout for CPU */
/*  { UDATA(rtc_srv, UNIT_BINK | MODEL(MODEL_27) | MEMAMOUNT(0),
 *  MAXMEMSIZE ), 120 }; */
    {
    NULL,       /* UNIT *next */             /* next active */
    NULL,       /* t_stat (*action) */       /* action routine */
    NULL,       /* char *filename */         /* open file name */
    NULL,       /* FILE *fileref */          /* file reference */
    NULL,       /* void *filebuf */          /* memory buffer */
    0,          /* uint32 hwmark */          /* high water mark */
    0,          /* int32 time */             /* time out */
    UNIT_IDLE|UNIT_FIX|UNIT_BINK|MODEL(MODEL_27)|MEMAMOUNT(4), /* flags */
    0,          /* uint32 dynflags */        /* dynamic flags */
    0x800000,   /* t_addr capac */           /* capacity */
    0,          /* t_addr pos */             /* file position */
    0,          /* void (*io_flush) */       /* io flush routine */
    0,          /* uint32 iostarttime */     /* I/O start time */
    0,          /* int32 buf */              /* buffer */
    80,         /* int32 wait */             /* wait */
};

REG cpu_reg[] = {
    {HRDATAD(PC, PC, 24, "Program Counter"), REG_FIT},
    {BRDATAD(PSD, PSD, 16, 32, 2, "Program Status Doubleword"), REG_FIT},
    {BRDATAD(GPR, GPR, 16, 32, 8, "Index registers"), REG_FIT},
    {BRDATAD(BR, BR, 16, 32, 8, "Base registers"), REG_FIT},
    {BRDATAD(BOOTR, BOOTR, 16, 32, 8, "Boot registers"), REG_FIT},
    {BRDATAD(SPAD, SPAD, 16, 32, 256, "CPU Scratchpad memory"), REG_FIT},
    {BRDATAD(MAPC, MAPC, 16, 32, 1024, "CPU map cache"), REG_FIT},
    {BRDATAD(TLB, TLB, 16, 32, 2048, "CPU Translation Lookaside Buffer"), REG_FIT},
    {HRDATAD(HIWM, HIWM, 32, "Max Maps Loaded"), REG_FIT},
    {HRDATAD(BPIX, BPIX, 32, "# Maps Loaded for O/S"), REG_FIT},
    {HRDATAD(CPIXPL, CPIXPL, 32, "Maximum Map # Loaded for User"), REG_FIT},
    {HRDATAD(CPIX, CPIX, 32, "Current CPIX user MPL offset"), REG_FIT},
    {HRDATAD(CPUSTATUS, CPUSTATUS, 32, "CPU Status Word"), REG_FIT},
    {HRDATAD(TRAPSTATUS, TRAPSTATUS, 32, "TRAP Status Word"), REG_FIT},
    {HRDATAD(CC, CC, 32, "Condition Codes"), REG_FIT},
    {HRDATAD(MODES, MODES, 32, "Mode bits"), REG_FIT},
    {BRDATAD(INTS, INTS, 16, 32, 128, "Interrupt Status"), REG_FIT},
    {HRDATAD(CMCR, CMCR, 32, "Cache Memory Control Register"), REG_FIT},
    {HRDATAD(SMCR, SMCR, 32, "Shared Memory Control Register"), REG_FIT},
    {HRDATAD(CMSMC, CMSMC, 32, "V9 Cache/Shadow Memory Configuration Word"), REG_FIT},
    {HRDATAD(CSMCW, CSMCW, 32, "V9 CPU Shadow Memory Configuration Word"), REG_FIT},
    {HRDATAD(ISMCW, ISMCW, 32, "V9 IPU Shadow Memory Configuration Word"), REG_FIT},
    {HRDATAD(CCW, CCW, 32, "Computer Configuration Word"), REG_FIT},
    {HRDATAD(CSW, CSW, 32, "Console Switches"), REG_FIT},
    {BRDATAD(RDYQ, RDYQ, 16, 32, 128, "Channel Program Completon Status"), REG_FIT},
    {HRDATAD(RDYQIN, RDYQIN, 32, "RDYQ input index"), REG_FIT},
    {HRDATAD(RDYQOUT, RDYQOUT, 32, "RDYQ output index"), REG_FIT},
    {NULL}
};

/* Modifier table layout (MTAB) - only extended entries have disp, reg, or flags */
MTAB cpu_mod[] = {
    {
    /* MTAB table layout for cpu type */
    /* {UNIT_MODEL, MODEL(MODEL_55), "32/55", "32/55", NULL, NULL, NULL, "Concept 32/55"}, */
    UNIT_MODEL,          /* uint32 mask */          /* mask */
    MODEL(MODEL_55),     /* uint32 match */         /* match */
    "32/55",             /* cchar  *pstring */      /* print string */
    "32/55",             /* cchar  *mstring */      /* match string */
    NULL,                /* t_stat (*valid) */      /* validation routine */
    NULL,                /* t_stat (*disp)  */      /* display routine */
    NULL,                /* void *desc      */      /* value desc, REG* if MTAB_VAL, int* if not */
    "Concept 32/55",     /* cchar *help     */      /* help string */
    },
    {UNIT_MODEL, MODEL(MODEL_75), "32/75", "32/75", NULL, NULL, NULL, "Concept 32/75"},
    {UNIT_MODEL, MODEL(MODEL_27), "32/27", "32/27", NULL, NULL, NULL, "Concept 32/27"},
    {UNIT_MODEL, MODEL(MODEL_67), "32/67", "32/67", NULL, NULL, NULL, "Concept 32/67"},
    {UNIT_MODEL, MODEL(MODEL_87), "32/87", "32/87", NULL, NULL, NULL, "Concept 32/87"},
    {UNIT_MODEL, MODEL(MODEL_97), "32/97", "32/97", NULL, NULL, NULL, "Concept 32/97"},
    {UNIT_MODEL, MODEL(MODEL_V6), "V6", "V6", NULL, NULL, NULL, "Concept V6"},
    {UNIT_MODEL, MODEL(MODEL_V9), "V9", "V9", NULL, NULL, NULL, "Concept V9"},
    {
    /* MTAB table layout for cpu memory size */
    /* {UNIT_MSIZE, MEMAMOUNT(0), "128K", "128K", &cpu_set_size}, */
    UNIT_MSIZE,          /* uint32 mask */          /* mask */
    MEMAMOUNT(0),        /* uint32 match */         /* match */
    NULL,                /* cchar  *pstring */      /* print string */
    "128K",              /* cchar  *mstring */      /* match string */
    &cpu_set_size,       /* t_stat (*valid) */      /* validation routine */
    NULL,                /* t_stat (*disp)  */      /* display routine */
    NULL,                /* void *desc      */      /* value desc, REG* if MTAB_VAL, int* if not */
    NULL,                /* cchar *help     */      /* help string */
    },
    {UNIT_MSIZE, MEMAMOUNT(1),   NULL, "256K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(2),   NULL, "512K", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(3),   NULL,   "1M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(4),   NULL,   "2M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(5),   NULL,   "3M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(6),   NULL,   "4M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(7),   NULL,   "6M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(8),   NULL,   "8M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(9),   NULL,  "12M", &cpu_set_size},
    {UNIT_MSIZE, MEMAMOUNT(10),  NULL,  "16M", &cpu_set_size},
    {MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle},
    {MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL},
    {MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_SHP, 0, "HISTORY", "HISTORY",
        &cpu_set_hist, &cpu_show_hist},
    {0}
};

/* CPU device descriptor */
DEVICE cpu_dev = {
    /* "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 24, 1, 8, 32,
    &cpu_ex, &cpu_dep, &cpu_reset, NULL, NULL, NULL,
    NULL, DEV_DEBUG, 0, dev_debug,
    NULL, NULL, &cpu_help, NULL, NULL, &cpu_description */
    "CPU",               /* cchar *name */          /* device name */
    &cpu_unit,           /* UNIT *units */          /* unit array */
    cpu_reg,             /* REG *registers */       /* register array */
    cpu_mod,             /* MTAB *modifiers */      /* modifier array */
    1,                   /* uint32 numunits */      /* number of units */
    16,                  /* uint32 aradix */        /* address radix */
    32,                  /* uint32 awidth */        /* address width */
    1,                   /* uint32 aincr */         /* address increment */
    16,                  /* uint32 dradix */        /* data radix */
    8,                   /* uint32 dwidth */        /* data width */
    &cpu_ex,             /* t_stat (*examine) */    /* examine routine */
    &cpu_dep,            /* t_stat (*deposit) */    /* deposit routine */
    &cpu_reset,          /* t_stat (*reset) */      /* reset routine */
    NULL,                /* t_stat (*boot) */       /* boot routine */
    NULL,                /* t_stat (*attach) */     /* attach routine */
    NULL,                /* t_stat (*detach) */     /* detach routine */
    NULL,                /* void *ctxt */           /* (context) device information block pointer */
    DEV_DEBUG,           /* uint32 flags */         /* device flags */
    0,                   /* uint32 dctrl */         /* debug control flags */
    dev_debug,           /* DEBTAB *debflags */     /* debug flag name array */
    NULL,                /* t_stat (*msize) */      /* memory size change routine */
    NULL,                /* char *lname */          /* logical device name */
    &cpu_help,           /* t_stat (*help) */       /* help function */
    NULL,                /* t_stat (*attach_help) *//* attach help function */
    NULL,                /* void *help_ctx */       /* Context available to help routines */
    &cpu_description,    /* cchar *(*description) *//* Device description */
    NULL,                /* BRKTYPTB *brk_types */  /* Breakpoint types */
};

/* CPU Instruction decode flags */
#define INV     0x0000      /* Instruction is invalid */
#define HLF     0x0001      /* Half word instruction */
#define ADR     0x0002      /* Normal addressing mode */
#define IMM     0x0004      /* Immediate mode */
#define WRD     0x0008      /* Word addressing, no index */
#define SCC     0x0010      /* Sets CC */
#define RR      0x0020      /* Read source register */
#define R1      0x0040      /* Read destination register */
#define RB      0x0080      /* Read base register into dest */
#define SD      0x0100      /* Stores into destination register */
#define RNX     0x0200      /* Reads memory without sign extend */
#define RM      0x0400      /* Reads memory */
#define SM      0x0800      /* Stores memory */
#define DBL     0x1000      /* Double word operation */
#define SB      0x2000      /* Store Base register */
#define BT      0x4000      /* Branch taken, no PC incr */
#define SF      0x8000      /* Special flag */

int nobase_mode[] = {
   /*    00            04             08             0C  */
   /*    00            ANR,           ORR,           EOR */ 
         HLF,        SCC|R1|RR|SD|HLF, SCC|R1|RR|SD|HLF, SCC|R1|RR|SD|HLF, 

   /*    10            14             18             1C */
   /*    CAR,          CMR,           SBR            ZBR */
         HLF,          HLF,           HLF,           HLF,

   /*    20            24             28             2C  */
   /*    ABR           TBR            REG            TRR  */
         HLF,          HLF,           HLF,           HLF, 

   /*    30            34             38             3C */
   /*    CALM          LA             ADR            SUR */
       HLF,            SD|ADR,        HLF,           HLF,

   /*    40            44             48             4C  */ 
   /*    MPR           DVR                             */
      SCC|SD|HLF,      HLF,           HLF|INV,       HLF|INV, 

   /*    50            54             58             5C */
   /*                                                 */
        HLF|INV,       HLF|INV,       HLF|INV,       HLF|INV,

   /*    60            64             68             6C   */
   /*    NOR           NORD           SCZ            SRA  */
         HLF,          HLF,           HLF,           HLF, 

   /*    70            74             78             7C */
   /*    SRL           SRC            SRAD           SRLD */ 
         HLF,          HLF,           HLF,           HLF,

   /*    80            84             88             8C   */
   /*    LEAR          ANM            ORM            EOM  */
       SD|ADR,  SD|RR|RNX|ADR,  SD|RR|RNX|ADR,  SD|RR|RNX|ADR,  

   /*    90            94             98             9C */
   /*    CAM           CMM            SBM            ZBM  */ 
      SCC|RR|RM|ADR,   RR|RM|ADR,     ADR,           ADR,

   /*    A0            A4             A8             AC  */
   /*    ABM           TBM            EXM            L    */
         ADR,          ADR,           ADR,        SCC|SD|RM|ADR,

   /*    B0            B4             B8             BC */
   /*    LM            LN             ADM            SUM  */ 
     SCC|SD|RM|ADR,    SCC|SD|RM|ADR,  SD|RR|RM|ADR,  SD|RR|RM|ADR,

   /*    C0            C4             C8             CC    */
   /*    MPM           DVM            IMM            LF  */
     SCC|SD|RM|ADR,    RM|ADR,        IMM,           ADR, 

   /*    D0            D4             D8             DC */
   /*    LEA           ST             STM            STF */ 
     SD|ADR,           RR|SM|ADR,     RR|SM|ADR,     ADR,  

   /*    E0            E4             E8             EC   */
   /*    ADF           MPF            ARM            BCT  */
     ADR,           ADR,      SM|RR|RNX|ADR,  ADR, 

   /*    F0            F4             F8             FC */
   /*    BCF           BI             MISC           IO */ 
        ADR,           RR|SD|WRD,     ADR,           IMM,  
};

int base_mode[] = {
   /* 00        04            08         0C      */
   /* 00        AND,          OR,        EOR  */
     HLF,      SCC|R1|RR|SD|HLF,     SCC|R1|RR|SD|HLF,     SCC|R1|RR|SD|HLF,  

   /* 10        14           18        1C  */
   /* SACZ      CMR         xBR         SRx */
     HLF,       HLF,        HLF,        HLF,

   /* 20        24            28         2C   */
   /* SRxD      SRC          REG        TRR      */
     HLF,       HLF,         HLF,       HLF,    

   /* 30        34          38           3C */
   /*           LA          FLRop        SUR */
    INV,        INV,       HLF,      HLF,

   /* 40        44            48         4C     */
   /*                                        */
      INV,      INV,        INV,       INV, 

    /* 50       54          58            5C */
   /*  LA       BASE        BASE          CALLM */ 
    SD|ADR,     SM|ADR,     SB|ADR,    RM|ADR,

   /* 60        64            68         6C     */
   /*                                         */
      INV,      INV,         INV,      INV,   

   /* 70       74           78           7C */
   /*                                          */ 
    INV,     INV,        INV,        INV,  

   /* LEAR      ANM          ORM        EOM   */
   /* 80        84            88         8C   */
    SD|ADR,    SD|RR|RNX|ADR, SD|RR|RNX|ADR, SD|RR|RNX|ADR, 

   /* CAM       CMM           SBM        ZBM  */ 
   /* 90        94            98         9C   */
    SCC|RR|RM|ADR, RR|RM|ADR,   ADR,     ADR,

   /* A0        A4            A8         AC   */
   /* ABM       TBM           EXM        L    */
      ADR,      ADR,          ADR,    SCC|SD|RM|ADR,

   /* B0        B4            B8         BC   */
   /* LM        LN            ADM        SUM  */ 
   SCC|SD|RM|ADR,   SCC|SD|RM|ADR,      SD|RR|RM|ADR,     SD|RR|RM|ADR,

   /* C0        C4            C8         CC   */
   /* MPM       DVM           IMM        LF   */
    SCC|SD|RM|ADR,  RM|ADR,       IMM,       ADR, 

   /*  D0       D4            D8         DC */
   /*  LEA      ST            STM        STFBR */ 
     INV,       RR|SM|ADR,    RR|SM|ADR,    ADR,  

   /* E0        E4            E8         EC     */
   /* ADF       MPF           ARM        BCT      */
    ADR,     ADR,     SM|RR|RNX|ADR,    ADR, 

   /* F0        F4            F8         FC */
  /*  BCF       BI            MISC       IO */ 
    ADR,        RR|SD|WRD,    ADR,       IMM,  
};

/* Map image descriptor 32/77 */
/* |--------------------------------------| */
/* |0|1|2|3 4 5 6|7 8  9 10 11 12 13 14 15| */
/* |N|V|P|  n/u  | 9 bit map block entry  | */
/* |U| | |       |      32kb/block        | */
/* |             |  32 8kb maps per task  | */
/* |             |   1 mb address space   | */
/* |--------------------------------------| */

/* Map image descriptor 32/27 */
/* |--------------------------------------| */
/* |0|1|2|3|4|5 6 7 8  9 10 11 12 13 14 15| */
/* |V|P|P|P|P|    11 bit map block entry  | */
/* | |1|2|3|4|           8kb/block        | */
/* |         |    256 8kb maps per task   | */
/* |         |      2 mb address space    | */
/* |--------------------------------------| */

/* Map image descriptor  32/67, 32/87, 32/97 */
/* |--------------------------------------| */
/* |0|1|2|3|4|5 6 7 8  9 10 11 12 13 14 15| */
/* |V|P|P|P|P|    11 bit map block entry  | */
/* | |1|2|3|4|           2kb/block        | */
/* |         |    2048 8kb maps per task  | */
/* |         |      16 mb address space   | */
/* |--------------------------------------| */
/* BIT 0 = 0    Invalid map block (page) entry */
/*       = 1    Valid map block (page) entry */
/*     1 = 0    000-7ff of 8kb page is not write protected */
/*       = 1    000-7ff of 8kb page is write protected */
/*     2 = 0    800-fff of 8kb page is not write protected */
/*       = 1    800-fff of 8kb page is write protected */
/*     3 = 0    1000-17ff of 8kb page is not write protected */
/*       = 1    1000-17ff of 8kb page is write protected */
/*     4 = 0    1800-1fff of 8kb page is not write protected */
/*       = 1    1800-1fff of 8kb page is write protected */
/*  5-15 =      11 most significant bits of the 24 bit real address for page */

/* Map image descriptor V6 & V9 */
/* |--------------------------------------| */
/* |0|1|2|3|4|5 6 7 8  9 10 11 12 13 14 15| */
/* |V|P|P|M|M|    11 bit map block entry  | */
/* | |1|2|M|A|           2kb/map          | */
/* |         |    2048 8kb maps per task  | */
/* |         |      16 mb address space   | */
/* |--------------------------------------| */
/* BIT 0 = 0    Invalid map block (page) entry */
/*       = 1    Valid map block (page) entry */
/* */
/* PSD 1 BIT 0 -  Map Bit 1 - Map Bit 2 - Access state */
/* Priv Bits with ECO for Access Protection change */
/*     0              0           0     No access allowed to page */
/*     0              0           1     No access allowed to page */
/*     0              1           0     Read/Write/Execute access */
/*     0              1           1     Read/Execute access only */
/*O/S*/
/*     1              0           0     Read/Write/Execute access */
/*     1              0           1     Read/Execute access only */
/*     1              1           0     Read/Write/Execute access */
/*     1              1           1     Read/Execute access only */
/* Priv Bits without ECO for Access Protection change */
/*     0              0           0     No access allowed to page */
/*     0              0           1     Read/Execute access only */
/*     0              1           0     Read//Execute access only */
/*     0              1           1     Read/Write/Execute access */
/*O/S*/
/*     1              0           0     Read/Write/Execute only */
/*     1              0           1     Read/Execute access only */
/*     1              1           0     Read/Write/Execute access */
/*     1              1           1     Read/Write/Execute access */
/* */
/* BIT 3 = 0    (MM) A first write (modify) to the map block (page) has not occurred */
/*       = 1    (MM) A first write (modify) to the map block (page) has occurred */
/* BIT 4 = 0    (MA) A first read or write (access) to the map block (page) has not occurred */
/*       = 1    (MA) A first read or write (access) to the map block (page) has occurred */
/*  5-15 =      11 most significant bits of the 24 bit real address for page */

/* Note */
/* If a map is valid, a MAP (page) hit occurs and logical to physical translation occures */
/* If the map is not valid, a demand MAP (page) fault occures and the faulting page is provided */
/* P1 and P2 are used with Bit 0 of PSD to define the access rights */
/* A privilege violation trap occurres if access it denied */
/* Bits 5-15 contain the 11 most-significant bits of the physical address */
/* MSD 0 page limit is used to verify access to O/S pages */
/* CPIXPL page limit is used to verify access to user pages and page faults */
/* CPIX CPIX of user MPL offset */
/* Access to pages outside the limit registers results in a map fault */

#define MAX32       32      /* 32/77 map limit */
#define MAX256      256     /* 32/27 and 32/87 map limit */
#define MAX2048     2048    /* 32/67, V6, and V9 map limit */

/* set up the map registers for the current task in the cpu */
/* the PSD bpix and cpix are used to setup the maps */
/* return non-zero if mapping error */
/* if lmap set, always load maps on 67, 97, V6, and V7 */
/* The RMW and WMW macros are used to read/write memory words */
/* RMW(addr) or WMW(addr, data) where addr is a byte alligned word address */
/* The RMR and WMR macros are used to read/write the MAPC cache registers */
/* RMR(addr) or WMR(addr, data) where addr is a half word alligned address */
/* We will only get here if the retain maps bit is not set in PSD word 2 */
t_stat load_maps(uint32 thepsd[2], uint32 lmap)
{
    uint32 num, sdc, spc, onlyos=0;
    uint32 mpl, cpixmsdl, bpixmsdl, msdl, midl;
    uint32 cpix, bpix, i, j, map, osmsdl, osmidl;
    uint32 MAXMAP = MAX2048;                        /* default to 2048 maps */

    sim_debug(DEBUG_TRAP, &cpu_dev,
        "Load Maps Entry PSD %08x %08x STATUS %08x lmap %1x CPU Mode %2x\n",
        thepsd[0], thepsd[1], CPUSTATUS, lmap, CPU_MODEL);

    /* process 32/7X computers */
    if (CPU_MODEL < MODEL_27) {
        MAXMAP = MAX32;                             /* 32 maps for 32/77 */
        /* 32/7x machine, 8KW maps 32 maps total */
        MODES &= ~BASEBIT;                          /* no basemode on 7x */
        if ((thepsd[1] & 0xc0000000) == 0)          /* mapped mode? */
            return ALLOK;                           /* no, all OK, no mapping required */

        /* we are mapped, so load the maps for this task into the cpu map cache */
        cpix = (thepsd[1]) & 0x3ff8;                /* get cpix 12 bit offset from psd wd 2 */
        bpix = (thepsd[1] >> 16) & 0x3ff8;          /* get bpix 12 bit offset from psd wd 2 */
        num = 0;                                    /* working map number */

        /* master process list is in 0x83 of spad for 7x */
        mpl = SPAD[0x83];                           /* get mpl from spad address */

        /* diags want the mpl entries checked to make sure valid dbl wowrd address */
        if (mpl & 0x7) {                            /* test for double word address */
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "load_maps MPL not on double word boundry %06x\n", mpl);
            TRAPSTATUS |= BIT20;                    /* set bit 20 of trap status */
            return MAPFLT;                          /* not dbl bound, map fault error */
        }

        /* check if valid real address */
        if ((mpl == 0) || !MEM_ADDR_OK(mpl & MASK24)) {  /* see if in memory */
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "load_maps MEM SIZE7 %06x mpl %06x invalid\n",
                MEMSIZE, mpl);
            TRAPSTATUS |= BIT18;                    /* set bit 18 of trap status */
            return MAPFLT;                          /* no, map fault error */
        }

        /* mpl is ok, get the msdl for given cpix */
        cpixmsdl = RMW(mpl+cpix);                   /* get msdl from mpl for given cpix */

        /* if bit zero of mpl entry is set, use bpix first to load maps */
        if (cpixmsdl & BIT0) {

            /* load bpix maps first */
            bpixmsdl = RMW(mpl+bpix);               /* get bpix msdl word address */

            /* check for valid bpix msdl addr */
            if (!MEM_ADDR_OK(bpixmsdl & MASK24)) {  /* see if in memory */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "load_maps MEM SIZE8 %06x bpix msdl %08x invalid\n",
                    MEMSIZE, bpixmsdl);
                return NPMEM;                       /* no, none present memory error */
            }

            sdc = (bpixmsdl >> 24) & 0x3f;          /* get 6 bit segment description count */
            msdl = bpixmsdl & MASK24;               /* get 24 bit real address of msdl */
            /* check for valid msdl addr */
            if (!MEM_ADDR_OK(msdl & MASK24)) {      /* see if in memory */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "load_maps MEM SIZE9 %06x msdl %08x invalid\n", MEMSIZE, msdl);
                return NPMEM;                       /* no, none present memory error */
            }

            /* process all of the msdl's */
            for (i = 0; i < sdc; i++) {             /* loop through the msd's */
                spc = (RMW(msdl+i) >> 24) & 0xff;   /* get segment page count from msdl */
                midl = RMW(msdl+i) & MASK24;        /* get 24 bit real word address of midl */

                /* check for valid midl addr */
                if (!MEM_ADDR_OK(midl & MASK24)) {  /* see if in memory */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "load_maps MEM SIZEa %06x midl %08x invalid\n", MEMSIZE, midl);
                    return NPMEM;                   /* no, none present memory error */
                }

                for (j = 0; j < spc; j++, num++) {  /* loop throught the midl's */
                    uint32 pad = RMW(midl+(j<<1));  /* get page descriptor address */
                    if (num >= MAXMAP) {
                        TRAPSTATUS |= BIT5;         /* set bit 5 of trap status */
                        return MAPFLT;              /* map loading overflow, map fault error */
                    }
                    /* load 16 bit map descriptors */
                    map = RMH(pad);                 /* get 16 bit map entries */
                    WMR((num<<1), map);             /* store the map reg contents into cache */
                }
            }
        }

        /* now load cpix maps */
        /* check for valid cpix msdl addr */
        if (MEM_ADDR_OK(cpixmsdl & MASK24)) {       /* see if in memory */
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "load_maps MEM SIZEb %06x cpix msdl %08x invalid\n", MEMSIZE, cpixmsdl);
            return NPMEM;                           /* no, none present memory error */
        }

        sdc = (cpixmsdl >> 24) & 0x3f;              /* get 6 bit segment description count */
        msdl = cpixmsdl & 0xffffff;                 /* get 24 bit real address of msdl */
        /* check for valid msdl addr */
        if (!MEM_ADDR_OK(msdl & MASK24)) {          /* see if in memory */
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "load_maps MEM SIZEc %06x msdl %08x invalid\n", MEMSIZE, msdl);
            return NPMEM;                           /* no, none present memory error */
        }

        /* process all of the msdl's */
        for (i = 0; i < sdc; i++) {
            spc = (RMW(msdl+i) >> 24) & 0xff;       /* get segment page count from msdl */
            midl = RMW(msdl+i) & MASK24;            /* get 24 bit real word address of midl */

            /* check for valid midl addr */
            if (!MEM_ADDR_OK(midl & MASK24)) {      /* see if in memory */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "load_maps MEM SIZEd %06x midl %08x invalid\n", MEMSIZE, midl);
                return NPMEM;                       /* no, none present memory error */
            }

            for (j = 0; j < spc; j++, num++) {      /* loop through the midl's */
                uint32 pad = RMW(midl+(j<<1));      /* get page descriptor address */
                if (num >= MAXMAP) {
                    TRAPSTATUS |= (BIT16|BIT9);     /* set bit 5 of trap status */
                    return MAPFLT;                  /* map loading overflow, map fault error */
                }
                /* load 16 bit map descriptors */
                map = RMH(pad);                     /* get 16 bit map entries */
                WMR((num<<1), map);                 /* store the map reg unmodified into cache */
            }
        }
        /* if none loaded, map fault */
        if (num == 0) {
            TRAPSTATUS |= (BIT16|BIT9);             /* set bit 5 of trap status */
            return MAPFLT;                          /* attempt to load 0 maps, map fault error */
        }
        /* clear the rest of the previously used maps */
        for (i = num; i < HIWM; i++)                /* zero any remaining entries */
            WMR((i<<1), 0);                         /* clear the map entry to make not valid */
        HIWM = num;                                 /* set new high water mark */
        return ALLOK;                               /* all cache is loaded, return OK */
    }
    /****************** END-OF-32/7X-MAPPING ********************/

    /* process a 32/27, 32/67, 32/87, 32/97, V6, or V9 here with 2KW (8kb) maps */
    /* 32/27 & 32/87 have 256 maps. Others have 2048 maps */
    /* 32/27 & 32/87 must have all maps preallocated and loaded */ 
    /* 32/67 & 32/97 must load O/S maps and have user preallocated maps loaded on access */
    /* V6 and V9 must load O/S maps and have user maps allocated and loaded on access */

    /* See if any mapping to take place */
    if ((MODES & MAPMODE) == 0)                     /* mapped mode? */
        return ALLOK;                               /* no, all OK, no mapping required */

    /* set maximum maps for 32/27 and 32/87 processors */
    if ((CPU_MODEL == MODEL_27) || (CPU_MODEL == MODEL_87))
        MAXMAP = MAX256;                            /* only 256 2KW (8kb) maps */

    /* we are mapped, so load the map definitions */
    cpix = thepsd[1] & 0x3ff8;                      /* get cpix 11 bit offset from psd wd 2 */
    num = 0;                                        /* no maps loaded yet */

    /* master process list is in 0xf3 of spad for concept machines */
    mpl = SPAD[0xf3];                               /* get mpl from spad address */

    /* diags want the mpl entries checked to make sure valid dbl word address */
    if (mpl & 0x7) {                                /* test for double word address */
        sim_debug(DEBUG_TRAP, &cpu_dev,
            "load_maps MPL not on double word boundry %06x\n", mpl);
        if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
            TRAPSTATUS |= BIT6;                     /* set bit 6 of trap status */
        else
            TRAPSTATUS |= BIT20;                    /* set bit 20 of trap status */
        return MAPFLT;                              /* no, map fault error */
    }

    /* check if valid real address */
    mpl &= MASK24;                                  /* clean mpl address */
    if (!MEM_ADDR_OK(mpl)) {                        /* see if in our real memory */
        sim_debug(DEBUG_TRAP, &cpu_dev,
            "load_maps MEM SIZE1 %06x mpl %06x invalid\n", MEMSIZE, mpl);
npmem:
        BPIX = 0;                                   /* no os maps loaded */
        CPIXPL = 0;                                 /* no user pages */
        CPIX = cpix;                                /* save user CPIX */
        if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
            TRAPSTATUS |= BIT1;                     /* set bit 1 of trap status */
        } else
            TRAPSTATUS |= BIT10;                    /* set bit 8 of trap status */
        return NPMEM;                               /* non present memory error */
    }

    /* output O/S and User MPL entries */
    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "#MEMORY %06x MPL %06x MPL[0] %08x %06x MPL[%04x] %08x %06x\n",
        MEMSIZE, mpl, RMW(mpl), RMW(mpl+4), cpix,
        RMW(cpix+mpl), RMW(cpix+mpl+4));
    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "MEMORY2 %06x BPIX %04x cpix %04x CPIX %04x CPIXPL %04x HIWM %04x\n",
        MEMSIZE, BPIX, cpix, CPIX, CPIXPL, HIWM);

    /* load the users regs first or the O/S.  Verify the User MPL entry too. */
    /* If bit zero of cpix mpl entry is set, use msd entry 0 first to load maps */
    /* Then load the user maps after the O/S */
    /* If the cpix is zero, then only load the O/S. */
    /* This test must be made to allow sysgen to run with a zero cpix */
    /* If bit 0 of MPL[0] is 0, load the O/S maps. */
    /* Do not load O/S if bit 0 of O/S MPL[0] is set.  It is set by the */
    /* swapper on MPX startup */

    /* mpl is valid, get msdls for O/S and User */
    osmidl = RMW(mpl);                              /* get O/S map count & retain flag from MPL[0] */
    osmsdl = RMW(mpl+4);                            /* get msdl pointer for OS from MPL[1] */
    midl = RMW(mpl+cpix);                           /* get midl entry for given user cpix */
    msdl = RMW(mpl+cpix+4);                         /* get mpl entry wd 1 for given cpix */
    spc = osmidl & MASK16;                          /* get 16 bit O/S segment description count */

    /* see if we are to only load the O/S */
    if (cpix == 0) { 
        CPIX = cpix;                                /* save CPIX */
        onlyos = 1;                                 /* flag to only load O/S, nothing else */
        if (osmidl & BIT0) {                        /* see if the O/S retain bit 0 is on */
            return ALLOK;                           /* O/S retain bit is set, no mapping required */
        }

loados: /* merge point for loading O/S first */
        /* to be followed by user maps */
        /* the retain bit is not set so load the O/S */
        if (spc > MAXMAP) {
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "load_maps bad O/S page count %04x, map fault\n", spc);
nomaps:
            /* Bad map load count specified. */
            BPIX = 0;                               /* no os maps loaded */
            CPIXPL = 0;                             /* no user pages */
            CPIX = cpix;                            /* save CPIX */
            HIWM = 0;                               /* reset high water mark */
            if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                TRAPSTATUS |= (BIT5|BIT9);          /* set bit 5/9 of trap status */
            else
                TRAPSTATUS |= BIT16;                /* set bit 16 of trap status */
            return MAPFLT;                          /* map loading overflow, map fault error */
        }

        /* we have a valid count, load the O/S map list address */
        osmsdl &= MASK24;                           /* get 24 bit real address from mpl 0 wd2 */
        if (!MEM_ADDR_OK(osmsdl)) {                 /* see if address is within our memory */
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "load_maps MEM SIZE2 %06x os page list address %06x invalid\n",
                MEMSIZE, osmsdl);
            goto npmem;                             /* non present memory trap */
        }

        /* load the O/S maps */
        for (j = 0; j < spc; j++, num++) {          /* copy maps from msdl to map cache */
            uint32 pad = osmsdl+(j<<1);             /* get page descriptor address */

            /* see if map overflow */
            if (num >= MAXMAP) {
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "load_maps O/S page count overflow %04x, map fault\n", num);
                goto nomaps;                        /* map overflow, map fault trap */
            }
            if (!MEM_ADDR_OK(pad)) {                /* see if address is within our memory */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "load_maps MEM SIZE3 %06x os page address %06x invalid\n",
                    MEMSIZE, pad);
                goto npmem;                         /* non present memeory trap */
            }
            /* load 16 bit map descriptors */
            map = RMH(pad);                         /* get page descriptor from memory */

            /* for valid maps translate the map number to a real address */
            /* put this address in the TLB for later translation */
            /* copy the map status bits too and set hit bit in the TLB */
            if (map & 0x8000) {                     /* see if map is valid */
                TLB[num] = (((map & 0x7ff) << 13) | ((map << 16) & 0xf8000000));
                TLB[num] |= 0x04000000;             /* set HIT bit for non lmap */
                WMR((num<<1), map);                 /* store the map unmodified into cache */
            } else {
                TLB[num] = 0;                       /* clear the TLB for non valid maps */
            }
        }
        BPIX = num;                                 /* save the # maps loaded in O/S */
        CPIXPL = 0;                                 /* no user pages */

        if (!onlyos)                                /* see if only O/S to be loaded */
            goto loaduser;                          /* no, go load the user maps */

        for (i = BPIX; i < MAXMAP; i++)             /* zero any remaining entries */
            TLB[i] = 0;                             /* clear look aside buffer */

        /* Only the O/S is to be loaded, finish up & return */
        HIWM = num;                                 /* set new high water mark */
        return ALLOK;                               /* all cache is loaded, return OK */
    }

    /* The csect is not zero here, so see what we have to do */
    /* See if O/S is to be loaded first because user MPL entry has BIT 0 set */
    if (midl & BIT0) {
        /* the user wants the O/S to load first, if the O/S retain bit set? */
        if (osmidl & BIT0) {                        /* see if the O/S retain bit 0 is on */
            num = spc;                              /* yes, set the number of O/S maps loaded */
            BPIX = spc;                             /* save the # maps in O/S */
            goto loaduser;                          /* load user map only or after O/S */
        }

        /* no retain bit, load user maps only, or after O/S */
        /* validate O/S map count */
        if (spc > MAXMAP) {
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "load_maps bad O/S page count %04x, map fault\n", spc);
            goto nomaps;                            /* we have error, make way out */
        }

        /* see if any O/S maps to load */ 
        if (spc == 0) {
            BPIX = 0;                               /* no os maps loaded */
            /* O/S page count is zero, so just load the user */
            goto loaduser;                          /* load user map only or after O/S */
        }
        /* user wants to have O/S loaded first */
        onlyos = 0;                                 /* return to loaduser after loading O/S */
        goto loados;                                /* go load the O/S */
    }

    /* the user wants to only load the user maps, no O/S maps are to be retained */
    BPIX = 0;                                       /* clear O/S loaded page count */
    num = 0;                                        /* nothing loaded yet */
    /****************** END-OF-O/S-MAPPING ********************/

loaduser:
    spc = midl & MASK16;                            /* get 16 bit User page count */

    /* see if O/S has already loaded the MAXMAPS */
    /* if borrow bit is on and cpix count is zero, return ok */
    /* if borrow bit is on and cpix count not zero, map overflow error */
    if (BPIX == MAXMAP) {
        HIWM = num;                                 /* set new high water mark */
        CPIXPL = 0;                                 /* no user pages */
        if ((midl & BIT0) && (spc == 0)) {          /* see if the user had borrow bit on */
            sim_debug(DEBUG_CMD, &cpu_dev,
                "load_maps @loaduser num %04x BPIX loaded %04x load done\n", num, BPIX);
            return ALLOK;                           /* all cache is loaded, return OK */
        }
        else {
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "load_maps map overflow BPIX %04x count %04x, map fault\n", BPIX, spc);
            goto nomaps;                            /* we have error, make way out */
        }
    }

    /* the O/S has been loaded if requested or retained, so now load the user */
    msdl &= MASK24;                                 /* get 24 bit real word address of msdl */

    /* This test fails cn.mmm diag at test 46, subtest 2 with unexpected error */
    /* Do this test if we are a LMAP instruction and not a 32/27 or 32/87 */
    if (lmap && !MEM_ADDR_OK(msdl)) {               /* see if address is within our memory */
        sim_debug(DEBUG_TRAP, &cpu_dev,
            "load_maps MEM SIZE4 %06x user page list address %06x invalid\n",
            MEMSIZE, msdl);
        if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
            TRAPSTATUS |= BIT1;                     /* set bit 1 of trap status */
        } else
            TRAPSTATUS |= BIT28;                    /* set bit 28 of trap status */
        return NPMEM;                               /* non present memory error */
    }

    /* We have a valid user MPL[cpix] address, msdl */
    spc = midl & MASK16;                            /* get 16 bit User page count */

    /* it is OK here to have no O/S maps loaded, num can be 0 */
    if ((spc > MAXMAP) || ((spc+BPIX) > MAXMAP)) {
        sim_debug(DEBUG_TRAP, &cpu_dev,
            "load_maps bad User page count %04x num %04x bpix %04x, map fault\n",
            spc, num, BPIX);
        /* Bad map load count specified. */
        BPIX = 0;                                   /* no os maps loaded */
        CPIXPL = 0;                                 /* no user pages */
        CPIX = cpix;                                /* save CPIX */
        HIWM = 0;                                   /* reset high water mark */
        if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
            TRAPSTATUS |= (BIT5|BIT9);              /* set bit 5/9 of trap status */
        else
            TRAPSTATUS |= BIT16;                    /* set bit 16 of trap status */
        return MAPFLT;                              /* map overflow fault error */
    }
    CPIX = cpix;                                    /* save user MPL offset (cpix) */
    CPIXPL = spc;                                   /* save user map load count */

    /* Load maps for 32/27 aand 32/87 */
    if ((CPU_MODEL == MODEL_27) || (CPU_MODEL == MODEL_87)) {

        sim_debug(DEBUG_CMD, &cpu_dev,
            "load_maps Processing 32/27 & 32/87 Model# %02x\n", CPU_MODEL);

        /* handle non virtual page loading or diag LMAP instruction */
        /* do 32/27 and 32/87 that force load all maps */
        /* now load user maps specified by the cpix value */
        for (j = 0; j < spc; j++, num++) {          /* copy maps from midl to map cache */
            uint32 pad = msdl+(j<<1);               /* get page descriptor address */

            /* see if map overflow */
            if (num >= MAXMAP) {
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "load_maps User page count overflow %04x, map fault\n", num);
                TRAPSTATUS |= BIT16;                /* set bit 16 of trap status */
                TRAPSTATUS |= (BIT5|BIT9);          /* set bit 5 of trap status */
                goto nomaps;                        /* map overflow, map fault trap */
            }
            if (!MEM_ADDR_OK(pad)) {                /* see if address is within our memory */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "load_maps MEM SIZE5 %06x User page address %06x invalid\n",
                    MEMSIZE, pad);
                goto npmem;                         /* non present memeory trap */
            }
            /* load 16 bit map descriptors */
            map = RMH(pad);                         /* get page descriptor from memory */

            /* for valid maps translate the map number to a real address */
            /* put this address in the TLB for later translation */
            /* copy the map status bits too and set hit bit in the TLB */
            /* leaving out diags in next statment fails test3/1 of vm.mmm */
            if ((map & 0x8000)) {                   /* see if map is valid */
                TLB[num] = (((map & 0x7ff) << 13) | ((map << 16) & 0xf8000000));
                TLB[num] |= 0x04000000;             /* set HIT bit on */
            } else
                TLB[num] = 0;                       /* clear the TLB for non valid maps */
            WMR((num<<1), map);                     /* store map unmodified into cache */
        }
        if (num == 0) {                             /* see if any maps loaded */
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "load_maps1 No maps loaded %04x, map fault\n", num);
            goto nomaps;                            /* return map fault error */
        }

        /* All maps are now loaded, finish up & return */
        for (i = num; i < MAXMAP; i++)              /* zero any remaining TLB entries */
            TLB[i] = 0;                             /* clear look aside buffer */

        HIWM = num;                                 /* set new high water mark */
        return ALLOK;                               /* all cache is loaded, return OK */
    }
    /**************END-OF-NON-VIRTUAL-USER-MAPPING-FOR-27-87************/
   
    sim_debug(DEBUG_CMD, &cpu_dev,
        "load_maps Processing 32/67 & 32/97 Model# %02x\n", CPU_MODEL);

    /* handle load on memory access case for 67, 97, V6 & V9 */
    /* now clear TLB & maps specified by the cpix value */
    for (j = 0; j < spc; j++, num++) {              /* clear maps in map cache */
        uint32 pad = msdl+(j<<1);                   /* get page descriptor address */

        /* if this is a LPSDCM instruction, just clear the TLB entry */
        if (!lmap) {
            /* load 16 bit map descriptors */
            map = RMH(pad);                         /* get page descriptor from memory */
            TLB[num] = 0;                           /* clear the TLB for non valid maps */
            if ((num < 0x20) || (num > (spc+BPIX) - 0x10))
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "UserV pad %06x=%04x map #%4x, %04x, map2 %08x, TLB %08x, MAPC %08x\n",
                    pad, map, num, map, (((map << 16) & 0xf8000000)|(map & 0x7ff)<<13)|0x04000000,
                    TLB[num], MAPC[num/2]);
            continue;                               /* just clear the TLBs */
        }

        /* only do the following tests for LMAP instruction, not LPSDCM */
        /* see if map overflow */
        if (num >= MAXMAP) {
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "load_maps User page count overflow %04x, map fault\n", num);
            if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                TRAPSTATUS |= (BIT5|BIT9);          /* set bit 5/9 of trap status */
            else
                TRAPSTATUS |= BIT16;                /* set bit 16 of trap status */
            goto nomaps;                            /* map overflow, map fault trap */
        }

        if (!MEM_ADDR_OK(pad)) {                    /* see if address is within our memory */
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "load_maps MEM SIZE6 %06x User page address %06x non present\n",
                MEMSIZE, pad);
            goto npmem;                             /* non present memeory trap */
        }

        /* load 16 bit map descriptors */
        map = RMH(pad);                             /* get page descriptor from memory */

        if (lmap) {
            TLB[num] = (((map & 0x7ff) << 13) | ((map << 16) & 0xf8000000));
            TLB[num] |= 0x04000000;                 /* set HIT bit for lmap */
            /* removing this store of map fails test 2 on 97 */
            WMR((num<<1), map);                     /* store the map unmodified into cache */
        }

        if ((num < 0x20) || (num > (spc+BPIX) - 0x10))
            sim_debug(DEBUG_DETAIL, &cpu_dev,
                "UserV2 pad %06x=%04x map #%4x, %04x, map2 %08x, TLB %08x, MAPC %08x\n",
                pad, map, num, map, (((map << 16) & 0xf8000000)|(map & 0x7ff)<<13)|0x04000000,
                TLB[num], MAPC[num/2]);
    }

    if (num == 0) {                                 /* see if any maps loaded */
        sim_debug(DEBUG_TRAP, &cpu_dev,
            "load_maps2 No maps loaded %04x, map fault\n", num);
        goto nomaps;
    }

    /* All maps are now loaded, finish up & return */
    /* removing this code causes diag to stop at 17/0 for 97 in cn.mmm */
    for (i = num; i < MAXMAP; i++)                  /* zero any remaining entries */
        TLB[i] = 0;                                 /* clear look aside buffer */

    HIWM = num;                                     /* set new high water mark */
    return ALLOK;                                   /* all cache is loaded, return OK */
    /****************** END-OF-VIRTUAL-USER-MAP-LOADING ********************/
}

/*
 * Return the real memory address from the logical address
 * Also return the protection status, 1 if write protected address.
 * For 67, 97, V6, & V9 return all protection bits.
 * Addr is a byte address.
 */
t_stat RealAddr(uint32 addr, uint32 *realaddr, uint32 *prot, uint32 access)
{
    uint32 word, index, map, raddr, mpl, offset;
    uint32  nix, msdl, mix;

    *prot = 0;      /* show unprotected memory as default */
                    /* unmapped mode is unprotected */

    /*****************START-7X-ADDRESS-PROCESSING****************/
    /* see what machine we have */
    if (CPU_MODEL < MODEL_27) {
        /* 32/7x machine with 8KW maps */
        if (MODES & EXTDBIT)
            word = addr & 0xfffff;                  /* get 20 bit logical word address */
        else
            word = addr & 0x7ffff;                  /* get 19 bit logical word address */
        if ((MODES & MAPMODE) == 0) {
            /* check if valid real address */
            if (!MEM_ADDR_OK(word)) {               /* see if address is within our memory */
                return NPMEM;                       /* no, none present memory error */
            }
            *realaddr = word;                       /* return the real address */
            return ALLOK;                           /* all OK, return instruction */
        }

        /* we are mapped, so calculate real address from map information */
        /* 32/7x machine, 8KW maps */
        index = word >> 15;                         /* get 4 or 5 bit value */
        map = RMR((index<<1));                      /* read the map reg cache contents */

        /* see if map is valid */
        if ((map & 0x4000) == 0)
            /* map is invalid, so return map fault error */
            return MAPFLT;                          /* map fault error */

        /* required map is valid, get 9 bit address and merge with 15 bit page offset */
        word = ((map & 0x1ff) << 15) | (word & 0x7fff);
        /* check if valid real address */
        if (!MEM_ADDR_OK(word))                     /* see if address is within our memory */
            return NPMEM;                           /* no, none present memory error */
        if ((MODES & PRIVBIT) == 0) {               /* see if we are in unprivileged mode */
            if (map & 0x2000)                       /* check if protect bit is set in map entry */
                *prot = 1;                          /* return memory write protection status */
        }
        *realaddr = word;                           /* return the real address */
        return ALLOK;                               /* all OK, return instruction */
    }
    /*****************END-OF-7X-ADDRESS-PROCESSING****************/

    /* Everyone else has 2KW maps */
    /* do common processing */
    /* diag wants the address to be 19 or 24 bit, use masked address */
    if (MODES & (BASEBIT | EXTDBIT))
        word = addr & 0xffffff;                     /* get 24 bit address */
    else
        word = addr & 0x7ffff;                      /* get 19 bit address */

    if ((MODES & MAPMODE) == 0) {
        /* we are in unmapped mode, check if valid real address */
        if (!MEM_ADDR_OK(word)) {                   /* see if address is within our memory */
            if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
                if (access == MEM_RD)
                    TRAPSTATUS |= BIT1;             /* set bit 1 of trap status */
                if (access == MEM_WR)
                    TRAPSTATUS |= BIT2;             /* set bit 2 of trap status */
            } else {
                TRAPSTATUS |= BIT10;                /* set bit 10 of trap status */
            }
            return NPMEM;                           /* no, none present memory error */
        }
        *realaddr = word;                           /* return the real address */
        return ALLOK;                               /* all OK, return instruction */
    }

    mpl = SPAD[0xf3] & MASK24;                      /* get 24 bit dbl wd mpl from spad address */

    /* did not get expected machine check trap for */
    /* 27, 87, 67 in test 37/0 if code removed */
    /* unexpected machine check trap for 67 in test 37/0 cn.mmm */
    /* now check the O/S midl pointer for being valid */
    /* we may want to delay checking until we actually use it */
    if (!MEM_ADDR_OK((RMW(mpl+4) & MASK24))) {      /* check OS midl */
        sim_debug(DEBUG_TRAP, &cpu_dev,
            "RealAddr Non Present Memory O/S msdl MPL %06x MPL[1] %06x\n",
            mpl, RMW(mpl+4));

        if ((CPU_MODEL == MODEL_27) || (CPU_MODEL == MODEL_87)) {
            // 32/27, 32/87 want MACHINECHK for test 37/1 in CN.MMM
            TRAPSTATUS |= BIT10;                    /* set bit 10 of trap status */
            return MACHINECHK_TRAP;                 /* diags want machine check error */
        } else
        if ((CPU_MODEL == MODEL_67) || (CPU_MODEL == MODEL_V6)) {
            TRAPSTATUS |= BIT10;                    /* set bit 10 of trap status */
            return MAPFLT;                          /* map fault error */
        }
        else
        if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
            // V9 & 32/97 wants MACHINECHK for test 37/1 in CN.MMM & VM.MMM
            TRAPSTATUS |= (BIT7|BIT9);              /* set bit 7 of trap status */
            TRAPSTATUS |= BIT28;                    /* set bit 28 of trap status */
            return MACHINECHK_TRAP;                 /* diags want machine check error */
        }
    }

    /* we are mapped, so calculate real address from map information */
    /* get 11 bit page number from address bits 8-18 */
    index = (word >> 13) & 0x7ff;                   /* get 11 bit page value */
    offset = word & 0x1fff;                         /* get 13 bit page offset */

    /* make sure map index is valid */
      if (index >= (BPIX + CPIXPL)) {
            sim_debug(DEBUG_TRAP, &cpu_dev,
            "RealAddr %06x word %06x loadmap gets mapfault index %04x B(%x)+C(%x) %04x\n",
            word, addr, index, BPIX, CPIXPL, BPIX+CPIXPL);
        if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
            TRAPSTATUS |= (BIT5|BIT9);              /* set bit 5/9 of trap status */
        else
            TRAPSTATUS |= BIT16;                    /* set bit 16 of trap status */
        return MAPFLT;                              /* map fault error */
    }

    /* continue processing non virtual machines here 32/27 & 32/87 */
    /* at this point all maps have been force loaded in load_maps */
    /* just do the conversion from logical to real address */
    if ((CPU_MODEL == MODEL_27) || (CPU_MODEL == MODEL_87)) {
        map = RMR((index<<1));                      /* read the map reg cache contents */
        raddr = TLB[index];                         /* get the base address & bits */

        if (!MEM_ADDR_OK(RMW(mpl+CPIX+4) & MASK24)) {  /* check user midl */
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "RealAddr 27 & 87 map fault index %04x B+C %04x map %04x TLB %08x\n",
                index, BPIX+CPIXPL, map, TLB[index]);
            // 32/27 & 32/87 want MACHINECHK for test 37/1 in CN.MMM
            TRAPSTATUS |= BIT10;                    /* set bit 10 of trap status */
            return MACHINECHK_TRAP;                 /* diags want machine check error */
        }

        if (((map & 0x8000) == 0) || ((raddr & BIT0) == 0)) {   /* see if valid map */
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "RealAddr loadmap 0a map fault index %04x B+C %04x map %04x TLB %08x\n",
                index, BPIX+CPIXPL, map, TLB[index]);
            TRAPSTATUS |= BIT10;                    /* set bit 10 of trap status */
            return MAPFLT;                          /* no, map fault error */
        }

        // needed for 32/27 & 32/87
        /* check if valid real address */
        if (!MEM_ADDR_OK(raddr & MASK24)) {         /* see if address is within our memory */
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "RealAddr loadmap 0c non present memory fault addr %06x raddr %08x index %04x\n",
                word, raddr, index);
            TRAPSTATUS |= BIT28;                    /* set bit 28 of trap status */
            return NPMEM;                           /* no, none present memory error */
        }
        word = (raddr & 0xffe000) | offset;         /* combine real addr and offset */
        *realaddr = word;                           /* return the real address */
        if (MODES & PRIVBIT)                        /* all OK if privledged */
            return ALLOK;                           /* all OK, return instruction */

        /* get user protection status of map */
        offset = (word >> 11) & 0x3;                /* see which 1/4 page we are in */
        if ((BIT1 >> offset) & raddr) {             /* is 1/4 page write protected */
            *prot = 1;                              /* return memory write protection status */
        }
        sim_debug(DEBUG_DETAIL, &cpu_dev,
            "RealAddrRa address %08x, TLB %08x MAPC[%03x] %08x wprot %02x prot %02x\n",
            word, TLB[index], index/2, MAPC[index/2], (word>>11)&3, *prot);
        return ALLOK;                               /* all OK, return instruction */
    }
    /*****************END-OF-27-87-ADDRESS-PROCESSING****************/

    /* handle 32/67, 32/97 and V6 & V9 here */
    /* Concept 32 machine, 2KW maps */
    /* the index is less than B+C, so setup to get a map */
    if (TLB[index] & 0x04000000) {                  /* is HIT bit on in TLB */
        /* handle HIT bit already on in TLB here */
        /* diags wants a NPMEM error if physical addr is exceeded */
        index &= 0x7ff;                             /* map # */
        raddr = TLB[index];                         /* get the base address & bits */
        /* check if valid real address */
        if (!MEM_ADDR_OK(raddr & MASK24)) {         /* see if address is within our memory */
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "RealAddr loadmap 2a non present memory fault addr %08x raddr %08x index %04x\n",
                addr, raddr, index);
            if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
                if (access == MEM_RD)
                    TRAPSTATUS |= BIT1;             /* set bit 1 of trap status */
                else
                if (access == MEM_WR)
                    TRAPSTATUS |= BIT2;             /* set bit 2 of trap status */
            } else
                TRAPSTATUS |= BIT28;                /* set bit 28 of trap status */
            return NPMEM;                           /* none present memory error */
        }
        map = RMR((index<<1));                      /* read the map reg contents */
        word = (raddr & 0xffe000) | offset;         /* combine map and offset */
        *realaddr = word;                           /* return the real address */

        /* handle 32/67 & 32/97 protection here */
        if (CPU_MODEL < MODEL_V6) {
            /* process 32/67 & 32/97 load map on access */
            /* handle 32/67 & 32/97 */
            if (MODES & PRIVBIT)                    /* all OK if privledged */
                return ALLOK;                       /* all OK, return instruction */

            /* get protection status of map */
            offset = (word >> 11) & 0x3;            /* see which 1/4 page we are in */
            if ((BIT1 >> offset) & raddr) {         /* is 1/4 page write protected */
                *prot = 1;                          /* return memory write protection status */
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev,
                "RealAddrR address %08x, TLB %08x MAPC[%03x] %08x wprot %02x prot %02x\n",
                word, TLB[index], index/2, MAPC[index/2], (word>>11)&3, *prot);
            return ALLOK;                           /* all OK, return instruction */
        }

        /* handle valid V6 & V9 HIT bit on */
        /* get protection status of map */
        offset = ((map >> 12) & 0x6);               /* get bits p1 & p2 from map into bits 13 & 14 */
        if (MODES & PRIVBIT)                        /* all access if privledged */
            *prot = offset | 0x8;                   /* set priv bit */ 
        else
            *prot = offset;                         /* return memory write protection status */

        sim_debug(DEBUG_DETAIL, &cpu_dev,
            "RealAddrX address %06x, TLB %06x MAPC[%03x] %08x wprot %02x prot %02x\n",
            word, TLB[index], index/2, MAPC[index/2], (word>>11)&3, *prot);
        return ALLOK;                               /* all OK, return instruction */
    }

    /* Hit bit is off in TLB, so lets go get some maps */
    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "$MEMORY %06x HIT MPL %06x MPL[0] %08x %06x MPL[%04x] %08x %06x\n",
        MEMSIZE, mpl, RMW(mpl), RMW(mpl+4), CPIX, RMW(CPIX+mpl), RMW(CPIX+mpl+4));

    /* check user msdl address now that we are going to access it */
    msdl = RMW(mpl+CPIX+4);                         /* get msdl entry for given CPIX */
    if (!MEM_ADDR_OK(msdl & MASK24)) {              /* check user midl */
        sim_debug(DEBUG_TRAP, &cpu_dev,
            "RealAddr User CPIX Non Present Memory User msdl %06x CPIX %04x\n",
            msdl, CPIX);

        if (CPU_MODEL == MODEL_67) {
            /* test 37/0 wants MAPFLT trap for 67 */
            TRAPSTATUS |= BIT28;                    /* set bit 28 of trap status */
            return MAPFLT;                          /* map fault error on memory access */
        } else
        if (CPU_MODEL == MODEL_97) {
            // 32/97 wants MAPFLT for test 37/1 in CN.MMM
            TRAPSTATUS |= BIT12;                    /* set bit 12 of trap status */
            TRAPSTATUS |= (BIT7|BIT9);              /* set bit 7/9 of trap status */
            TRAPSTATUS |= BIT10;                    /* set bit 10 of trap status */
            return MAPFLT;                          /* no, map fault error */
        } else
        if (CPU_MODEL == MODEL_V6) {
            // V6 wants MAPFLT for test 37/1 in CN.MMM & VM.MMM */
            TRAPSTATUS |= BIT28;                    /* set bit 28 of trap status */
            /* OK for V6 */
            return MAPFLT;                          /* map fault error */
        }
        else
        if (CPU_MODEL == MODEL_V9) {
            /* V9 wants MAPFLT for test 37/1 in CN.MMM & VM.MMM */
            /* V9 fails test 46/subtest 2 with "did not get expected map trap */
            TRAPSTATUS |= BIT12;                    /* set bit 12 of trap status */
            TRAPSTATUS |= (BIT7|BIT9);              /* set bit 7 of trap status */
            TRAPSTATUS |= BIT10;                    /* set bit 10 of trap status */
            return MAPFLT;                          /* map fault error */
        }
    }

    /* get os or user msdl, index < BPIX; os, else user */
    if (index < BPIX)
        msdl = RMW(mpl+4);                          /* get mpl entry wd 1 for os */
    else
        /* check user msdl address now that we are going to access it */
        msdl = RMW(mpl+CPIX+4);                     /* get mpl entry wd 1 for given cpix */

    /* HIT bit is off, we must load the map entries from memory */
    /* turn on the map hit flag if valid and set maps real base addr */
    nix = index & 0x7ff;                            /* map # or mapc index */
    word = (TLB[nix] & 0xffe000) | offset;          /* combine map and offset */
    if (index < BPIX)
        mix = nix;                                  /* get map index in memory */
    else
        mix = nix-BPIX;                             /* get map index in memory */
    map = RMH(msdl+(mix<<1));                       /* map content from memory */      
    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "Addr %06x RealAddr %06x Map0[%04x] HIT %04x TLB[%3x] %08x MAPC[%03x] %08x\n",
        addr, word, mix, map, nix, TLB[nix], nix/2, MAPC[nix/2]);

    /* process HIT bit off V6 & V9 here */
    if ((map & 0x8000) == 0) {
        *realaddr = word;                           /* return the real address */
        /* for V6 & V9 handle demand paging */
        if (CPU_MODEL >= MODEL_V6) {
            /* map is not valid, so we have map fault */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "AddrMa %06x RealAddr %06x Map0 MISS %04x, TLB[%3x] %08x MAPC[%03x] %08x\n",
                addr, word, map, nix, TLB[nix], nix/2, MAPC[nix/2]);
            /* do a demand page request for the required page */
            pfault = nix;                           /* save page number */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "Mem_write Daddr2 %06x page %04x demand page bits set TLB %08x map %04x\n",
                addr, nix, TLB[nix], map);
            return DMDPG;                           /* demand page request */
        }
        /* handle 67 & 97 map invalid here */
        if (CPU_MODEL == MODEL_97) {
            if (access == MEM_RD)
                TRAPSTATUS |= BIT1;                 /* set bit 1 of trap status */
            else
            if (access == MEM_WR)
                TRAPSTATUS |= BIT2;                 /* set bit 2 of trap status */
        } else
            TRAPSTATUS |= BIT28;                    /* set bit 28  of trap status */
        return MAPFLT;                              /* map fault error */
    }

    /* map is valid, process it */
    TLB[nix] = ((map & 0x7ff) << 13) | ((map << 16) & 0xf8000000) | 0x04000000;
    word = (TLB[nix] & 0xffe000) | offset;          /* combine map and offset */
    WMR((nix<<1), map);                             /* store the map reg contents into MAPC cache */
    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "RealAddrm RMH %04x mix %04x TLB[%04x] %08x B+C %04x RMR[nix] %04x\n",
        map, mix, nix, TLB[nix], BPIX+CPIXPL, RMR(nix<<1));

    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "Addr1c %06x RealAddr %06x Map1[%04x] HIT %04x, TLB[%3x] %08x MAPC[%03x] %08x RMR %04x\n",
        addr, word, mix, map, nix, TLB[nix], nix/2, MAPC[nix/2], RMR(nix<<1));

    *realaddr = word;                               /* return the real address */
    raddr = TLB[nix];                               /* get the base address & bits */

    if ((CPU_MODEL == MODEL_67) || (CPU_MODEL == MODEL_97)) {
        /* get protection status of map */
        if ((MODES & PRIVBIT) == 0) {               /* OK if privledged */
            /* otherwise check protection bit */
            offset = (word >> 11) & 0x3;            /* see which 1/4 page we are in */
            if ((BIT1 >> offset) & raddr)           /* is 1/4 page write protected */
                *prot = 1;                          /* return memory write protection status */
        }
    } else {
        /* get protection status of map */
        offset = ((map >> 12) & 0x6);               /* get bits p1 & p2 from map into bits 13 & 14 */
        if (MODES & PRIVBIT)                        /* all access if privledged */
            *prot = offset | 0x8;                   /* set priv bit */ 
        else
            *prot = offset;                         /* return memory write protection status */
    }

    /* now do the other halfword of the memory map pair */
    /* calc index of previous or next halfword */
    if ((mix & 1) == 0) {
        mix += 1;                                   /* we are at lf hw, so do next hw */
        nix += 1;                                   /* point to next map in MAPC */
        /* This is really a firmware error where the CPU loads the */
        /* right halfword map information even though it exceeds */
        /* the allowed map count.  It does not hurt anything, so OK */
        /* 32/67 & V6 allow loading the extra rt hw map entry */
        if ((nix == BPIX) || (nix > (BPIX+CPIXPL)))
            return ALLOK;                           /* no other map is valid, we are done */
    } else
    if ((mix & 1) == 1) {
        if (nix == BPIX)
            return ALLOK;                           /* no other map is valid, we are done */
        mix -= 1;                                   /* we are at rt hw, so backup hw */
        nix -= 1;                                   /* point to last map in MAPC */
    }

    sim_debug(DEBUG_DETAIL, &cpu_dev,
        "RealAddrp mix %04x nix %04x TLB[%04x] %08x B+C %04x RMR[nix] %04x\n",
         mix, nix, nix, TLB[nix], BPIX+CPIXPL, RMR(nix<<1));

    /* allow the excess map entry to be loaded, even though bad */
    if (nix <= (BPIX+CPIXPL)) {                     /* needs to be a mapped reg */
        sim_debug(DEBUG_DETAIL, &cpu_dev,
            "Addr1d BPIX %03x CPIXPL %03x RealAddr %06x TLB[%3x] %08x MAPC[%03x] %08x RMR %04x\n",
            BPIX, CPIXPL, word, nix, TLB[nix], nix/2, MAPC[nix/2], RMR(nix<<1));

        /* mix & nix has other map correct index */
        if ((TLB[nix] & 0x04000000) == 0) {         /* is HIT bit already on */
            /* hit not on, so load the map */
            /* allow the excess map entry to be loaded, even though bad */
            if (nix <= (BPIX+CPIXPL)) {             /* needs to be a mapped reg */
                map = RMH(msdl+(mix<<1));           /* map content from memory */      
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "Addr2a %06x MapX[%04x] HIT %04x, TLB[%3x] %08x MAPC[%03x] %08x\n",
                    addr, mix, map, nix, TLB[nix], nix/2, MAPC[nix/2]);

                if (map & 0x8000) {                 /* must be valid to load */
                    /* setting access bit fails test 15/0 in vm.mmm diag */
                    TLB[nix] = ((map & 0x7ff) << 13) | ((map << 16) & 0xf8000000) | 0x04000000;
                    word = (TLB[nix] & 0xffe000);   /* combine map and offset */
                    WMR((nix<<1), map);             /* store the map reg contents into MAPC cache */
                    sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "Addr2b %06x RealAddr %06x Map2[%04x] HIT %04x, TLB[%3x] %08x MAPC[%03x] %08x\n",
                        addr, word, mix, map, nix, TLB[nix], nix/2, MAPC[nix/2]);
                }
            }
        }
    }
    /*****************END-OF-V6-V9-ADDRESS-HIT-PROCESSING****************/
    /*****************END-OF-67-97-ADDRESS-HIT-PROCESSING****************/
    return ALLOK;                                   /* all OK, return instruction */
}

/* fetch the current instruction from the PC address */
t_stat read_instruction(uint32 thepsd[2], uint32 *instr)
{
    uint32 status, addr;

    if (CPU_MODEL < MODEL_27) {
        /* 32/7x machine with 8KW maps */
        /* instruction must be in first 512KB of address space */
        addr = thepsd[0] & 0x7fffc;                 /* get 19 bit logical word address */
    } else {
        /* 32/27, 32/67, 32/87, 32/97 2KW maps */
        /* Concept 32 machine, 2KW maps */
        if (thepsd[0] & BASEBIT) {                  /* bit 6 is base mode? */
            addr = thepsd[0] & 0xfffffc;            /* get 24 bit address */
        }
        else
            addr = thepsd[0] & 0x7fffc;             /* get 19 bit address */
    }

    /* go read the memory location */
    status = Mem_read(addr, instr);
    if ((status == MAPFLT) && (TRAPSTATUS == BIT1)) {
        /* if map fault on read, change code to read instruction */
        TRAPSTATUS &= ~BIT1;                        /* clear error on read memory */
        TRAPSTATUS |= BIT0;                         /* set error on instruction read */
    } else
    if (status == DMDPG)
        pfault |= 0x80000000;                       /* set instruction fetch paging error */
    sim_debug(DEBUG_DETAIL, &cpu_dev, "read_instr status %02x @ %06x\n", status, addr);
    return status;                                  /* return ALLOK or ERROR status */
}

/*
 * Read a full word from memory
 * Return error type if failure, ALLOK if
 * success.  Addr is logical byte address.
 */
t_stat Mem_read(uint32 addr, uint32 *data)
{
    uint32 status, realaddr, prot, page, map, mix, nix, msdl, mpl, nmap;

    status = RealAddr(addr, &realaddr, &prot, MEM_RD);  /* convert address to real physical address */

    if (status == ALLOK) {
        *data = RMW(realaddr);                      /* valid address, get physical address contents */
        if (((CPU_MODEL >= MODEL_V6) || (CPU_MODEL == MODEL_97) ||
            (CPU_MODEL == MODEL_67)) && (MODES & MAPMODE)) {

            page = (addr >> 13) & 0x7ff;            /* get 11 bit value */
            if (CPU_MODEL >= MODEL_V6) {
                /* check for v6 & v9 if we have read access */
                switch (prot & 0x0e) {
                case 0x0: case 0x2:
                    /* O/S or user has no read/execute access, do protection violation */
                    sim_debug(DEBUG_EXP, &cpu_dev,
                        "Mem_readA protect error @ %06x prot %02x modes %08x page %04x\n",
                        addr, prot, MODES, page);
                    if (CPU_MODEL == MODEL_V9)
                        TRAPSTATUS |= BIT2;         /* set bit 2 of trap status */
                    else
                        TRAPSTATUS &= ~BIT12;       /* clear bit 12 of trap status */
                    return MPVIOL;                  /* return memory protection violation */
                case 0x4: case 0x6: case 0x8: case 0xa: case 0xc: case 0xe:
                    /* O/S or user has read/execute access, no protection violation */
                    sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "Mem_readB protect is ok @ %06x prot %02x modes %08x page %04x\n",
                        addr, prot, MODES, page);
                }
                mpl = SPAD[0xf3];                   /* get mpl from spad address */
                nix = page & 0x7ff;                 /* map # or mapc index */
                if (page < BPIX) {
                    mix = nix;                      /* get map index in memory */
                    msdl = RMW(mpl+4);              /* get mpl entry for o/s */
                } else {
                    mix = nix-BPIX;                 /* get map index in memory */
                    msdl = RMW(mpl+CPIX+4);         /* get mpl entry for given cpix */
                }
                nmap = RMH(msdl+(mix<<1));          /* map content from memory */      
                map = RMR((page<<1));               /* read the map reg contents */
                /* if I remove this test, we fail at test 14/0 */
                if (((map & 0x800) == 0)) {
                    map |= 0x800;                   /* set the accessed bit in the map cache entry */
                    WMR((page<<1), map);            /* store the map reg contents into cache */
                    TLB[page] |= 0x0c000000;        /* set the accessed bit in TLB too */
                    WMH(msdl+(mix<<1), map);        /* save modified map with access bit set */
                    sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "Mem_read Yaddr %06x page %04x set access bit TLB %08x map %04x nmap %04x\n",
                        addr, page, TLB[page], map, nmap);
                }
            }
            /* everybody else has read access */
        }
        sim_debug(DEBUG_DETAIL, &cpu_dev,
            "Mem_read addr %06x realaddr %06x data %08x prot %02x\n",
            addr, realaddr, *data, prot);
    } else {
        /* RealAddr returned an error */
        sim_debug(DEBUG_EXP, &cpu_dev,
            "Mem_read error addr %06x realaddr %06x data %08x prot %02x status %04x\n",
            addr, realaddr, *data, prot, status);
        if (status == NPMEM) {                      /* operand nonpresent memory error */
            if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
                TRAPSTATUS |= BIT1;                 /* set bit 1 of trap status */
            } else
                TRAPSTATUS |= BIT10;                /* set bit 10 of trap status */
        }
        if (status == MAPFLT) {
            if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                TRAPSTATUS |= (BIT12|BIT16);        /* set bit 12 of trap status */
            else
                TRAPSTATUS |= BIT10;                /* set bit 10 of trap status */
        }
        sim_debug(DEBUG_EXP, &cpu_dev, "Mem_read MISS %02x @ %06x TRAPSTATUS %08x\n",
            status, addr, TRAPSTATUS);
    }
    return status;                                  /* return ALLOK or ERROR status */
}

/*
 * Write a full word to memory, checking protection
 * and alignment restrictions. Return 1 if failure, 0 if
 * success.  Addr is logical byte address, data is 32bit word
 */
t_stat Mem_write(uint32 addr, uint32 *data)
{
    uint32 status, realaddr=0, prot=0, raddr, page, nmap, msdl, mpl, map, nix, mix;

    status = RealAddr(addr, &realaddr, &prot, MEM_WR);  /* convert address to real physical address */

    if (prot) {
        sim_debug(DEBUG_DETAIL, &cpu_dev, "Mem_write addr %.8x realaddr %.8x data %.8x prot %02x\n",
            addr, realaddr, *data, prot);
    }

    if (status == ALLOK) {
        if (((CPU_MODEL >= MODEL_V6) || (CPU_MODEL == MODEL_97) ||
            (CPU_MODEL == MODEL_67)) && (MODES & MAPMODE)) {
            page = (addr >> 13) & 0x7ff;            /* get 11 bit value */
            if (CPU_MODEL >= MODEL_V6) {
                /* check for v6 & v9 if we have write access */
                switch (prot &0x0e) {
                case 0x0: case 0x2: case 0x6: case 0xa: case 0xe:
                    /* O/S or user has read/execute access, do protection violation */
                    sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "Mem_writeA protect error @ %06x prot %02x modes %08x\n",
                        addr, prot, MODES);
                    if (CPU_MODEL == MODEL_V9)
                        TRAPSTATUS |= BIT1;         /* set bit 1 of trap status */
                    else
                        TRAPSTATUS |= BIT12;        /* set bit 12 of trap status */
                    return MPVIOL;                  /* return memory protection violation */
                case 0x4: case 0x8: case 0xc:
                    /* O/S or user has write access, no protection violation */
                    sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "Mem_writeB protect is ok @ %06x prot %02x modes %08x\n",
                        addr, prot, MODES);
                }
                map = RMR((page<<1));               /* read the map reg contents */
                raddr = TLB[page];                  /* get the base address & bits */
                nix = page & 0x7ff;                 /* map # or mapc index */
                mpl = SPAD[0xf3];                   /* get mpl from spad address */
                if (page < BPIX) {
                    mix = nix;                      /* get map index in memory */
                    msdl = RMW(mpl+4);              /* get mpl entry for o/s */
                } else {
                    mix = nix-BPIX;                 /* get map index in memory */
                    msdl = RMW(mpl+CPIX+4);         /* get mpl entry for given cpix */
                }
                nmap = RMH(msdl+(mix<<1));          /* map content from memory */      
                if ((nmap & 0x1000) == 0) {
                    nmap |= 0x1800;                 /* set the modify/accessed bit in the map cache entry */
                    WMR((page<<1), nmap);           /* store the map reg contents into cache */
                    TLB[page] |= 0x18000000;        /* set the modify/accessed bits in TLB too */
                    WMH((msdl+(mix << 1)), nmap);   /* save modified map with access bit set */
                    sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "Mem_write Waddr %06x page %04x set access bit TLB %08x map %04x nmap %04x raddr %08x\n",
                        addr, page, TLB[page], map, nmap, raddr);
                }
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "Mem_write Xaddr %06x page %04x MA bits set TLB %08x map %04x prot %04x modes %04x\n",
                    addr, page, TLB[page], map, prot, MODES);
            } else {
                if (prot) {                         /* check for write protected memory */
                    sim_debug(DEBUG_EXP, &cpu_dev,
                        "Mem_writeB 32/67 protect error @ %06x prot %02x page %04x\n",
                        addr, prot, page);
                    if (CPU_MODEL == MODEL_97)
                        TRAPSTATUS |= BIT1;         /* set bit 1 of trap status */
                    else
                        TRAPSTATUS |= BIT12;        /* set bit 12 of trap status */
                    return MPVIOL;                  /* return memory protection violation */
                }
            }
            /* everything else has write access */
        } else {
            if (prot) {                             /* check for write protected memory */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "Mem_writeC protect error @ %06x prot %02x\n", addr, prot);
                TRAPSTATUS |= BIT12;                /* set bit 12 of trap status */
                return MPVIOL;                      /* return memory protection violation */
            }
        }
        WMW(realaddr, *data);                       /* valid address, put physical address contents */
    } else {
        /* RealAddr returned an error */
        sim_debug(DEBUG_TRAP, &cpu_dev,
            "Mem_write error addr %.8x realaddr %.8x data %.8x prot %02x status %04x\n",
            addr, realaddr, *data, prot, status);
        if (status == NPMEM) {                      /* operand nonpresent memory error */
            if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
                TRAPSTATUS |= BIT2;                 /* set bit 2 of trap status */
            } else
                TRAPSTATUS |= BIT10;                /* set bit 10 of trap status */
        }
        if (status == MAPFLT) {
            if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                TRAPSTATUS |= (BIT12|BIT16);        /* set bit 12 of trap status */
            else
                TRAPSTATUS |= BIT10;                /* set bit 10 of trap status */
        }
        sim_debug(DEBUG_TRAP, &cpu_dev,
            "Mem_write error %02x @ %06x TRAPSTATUS %08x pfaualt %04x\n",
            status, addr, TRAPSTATUS, pfault);
    }
    return status;                                  /* return ALLOK or ERROR */
}

/* function to set the CCs in PSD1 */
/* ovr is setting for CC1 */
void set_CCs(uint32 value, int ovr)
{
    PSD1 &= 0x87FFFFFE;                             /* clear the old CC's */
    if (ovr)
        CC = CC1BIT;                                /* CC1 value */
    else
        CC = 0;                                     /* CC1 off */
    if (value & FSIGN)
        CC |= CC3BIT;                               /* CC3 for neg */
    else if (value == 0)
        CC |= CC4BIT;                               /* CC4 for zero */
    else 
        CC |= CC2BIT;                               /* CC2 for greater than zero */
    PSD1 |= (CC & 0x78000000);                      /* update the CC's in the PSD */
}

/* retain these values across calls to sim_instr */
uint32  skipinstr = 0;                              /* Skip test for interrupt on this instruction */
uint32  drop_nop = 0;                               /* Set if right hw instruction is a nop */
uint32  OIR=0;                                      /* Original Instruction register */
uint32  OPSD1=0;                                    /* Original PSD1 */
uint32  OPSD2=0;                                    /* Original PSD2 */
uint32  TPSD[2];                                    /* Temp PSD */

/* Opcode definitions */
/* called from simulator */
t_stat sim_instr(void) {
    t_stat              reason = 0;                 /* reason for stopping */
    t_uint64            dest = 0;                   /* Holds destination/source register */
    t_uint64            source = 0;                 /* Holds source or memory data */
    t_uint64            td;                         /* Temporary */
    t_int64             int64a;                     /* temp int */
    t_int64             int64b;                     /* temp int */
    t_int64             int64c;                     /* temp int */
    uint32              addr;                       /* Holds address of last access */
    uint32              temp;                       /* General holding place for stuff */
    uint32              IR;                         /* Instruction register */
    uint32              i_flags=0;                  /* Instruction description flags from table */
    uint32              t;                          /* Temporary */
    uint32              temp2;                      /* Temporary */
    uint32              bc=0;                       /* Temporary bit count */
    uint16              opr;                        /* Top half of Instruction register */
    uint16              OP;                         /* Six bit instruction opcode */
    uint16              chan;                       /* I/O channel address */
    uint16              lchan;                      /* Logical I/O channel address */
    uint16              suba;                       /* I/O subaddress */
    uint16              lchsa;                      /* logical I/O channel & subaddress */
    uint16              rchsa;                      /* real I/O channel & subaddress */
    uint8               FC;                         /* Current F&C bits */
    uint8               EXM_EXR=0;                  /* PC Increment for EXM/EXR instructions */
    uint8               BM, MM, BK;                 /* basemode, mapped mode, blocked mode */
    uint32              reg;                        /* GPR or Base register bits 6-8 */
    uint32              sreg;                       /* Source reg in from bits 9-11 reg-reg instructions */
    uint32              ix = 0;                     /* index register */
    uint32              dbl;                        /* Double word */
    uint32              ovr=0;                      /* Overflow flag */
//FORSTEP    uint32              stopnext = 0;      /* Stop on next instruction */
    uint32              int_icb;                    /* interrupt context block address */
    uint32              rstatus;                    /* temp return status */
    int32               int32a;                     /* temp int */
    int32               int32b;                     /* temp int */
    int32               int32c;                     /* temp int */
//  uint32              uint32a;                    /* temp uint */
//  uint32              uint32b;                    /* temp uint */
//  uint32              uint32c;                    /* temp uint */

//#define MPXTEST
//#define LOOK_MAP_05272021
#ifdef MPXTEST
    int32               ii;                         /* temp int */
#endif

wait_loop:
    while (reason == 0) {                           /* loop until halted */

        // wait_loop:
        if (sim_interval <= 0) {                    /* event queue? */
            reason = sim_process_event();           /* process */
            if (reason != SCPE_OK) {
                if (reason == SCPE_STEP) {
                    sim_debug(DEBUG_EXP, &cpu_dev,
                        "Process Event step reason %08x interval %08x\n",
                        reason, sim_interval);
                    return reason;
                    break;
                }
                else {
                    sim_debug(DEBUG_EXP, &cpu_dev,
                        "Process Event other reason %08x interval %08x\n",
                        reason, sim_interval);
                    return reason;
                    break;                          /* process */
                }
            }
        }

        if (sim_brk_summ)
            sim_debug(DEBUG_EXP, &cpu_dev, "Process Event sim_brk_summ = %08x\n",
               sim_brk_summ);
        PC = PSD1 & 0xfffffe;                       /* get 24 bit addr from PSD1 */
        /* stop simulator if user break requested */
        if (sim_brk_summ && sim_brk_test(PC, SWMASK('E'))) {
            reason = STOP_IBKPT;
//          reason = SCPE_STEP;
            sim_debug(DEBUG_EXP, &cpu_dev, "Process Event test reason %08x interval %08x\n",
               reason, sim_interval);
            sim_interval= 0;                        /* count down */
            break;
        }

        sim_interval--;                             /* count down */

        if (drop_nop) {                             /* need to drop a nop? */
            drop_nop = 0;                           /* we dropped the nop */
            sim_debug(DEBUG_EXP, &cpu_dev,
                "CPU Drop NOP PSD1 %08x\n", PSD1);
        }

        if (skipinstr) {                            /* need to skip interrupt test? */
            skipinstr = 0;                          /* skip only once */
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "CPU Skip instruction PSD %08x %08x irq_pend %d wait4int %d irq_auto %x\n",
                PSD1, PSD2, irq_pend, wait4int, irq_auto);
            goto skipi;                             /* skip int test */
        }

        if (waitqcnt > 0) {                         /* test for UTX delay */
            waitqcnt--;                             /* wait b4 ints */
            if (waitqcnt == 0)
                irq_pend = 1;                       /* start scanning interrupts again */
        }

        /* we are booting the system, so see if boot channel prog is completed */
        if (loading) {
            uint32 il;
            uint32 chsa  = scan_chan(&il);          /* go scan for load complete pending */
            if (chsa != 0) {                        /* see if a boot channel/subaddress were returned */
                /* take interrupt, store the PSD, fetch new PSD */
                PSD1 = TPSD[0];                     /* PSD1 from location 0 */
                PSD2 = TPSD[1];                     /* PSD2 from location 4 */
                CC = PSD1 & 0x78000000;             /* extract bits 1-4 from PSD1 */
                MODES = PSD1 & 0x87000000;          /* extract bits 0, 5, 6, 7 from PSD 1 */
                CPUSTATUS &= ~0x87000000;           /* reset bits in CPUSTATUS */
                CPUSTATUS |= MODES;                 /* now insert into CPUSTATUS */
                sim_debug(DEBUG_IRQ, &cpu_dev, "Boot Loading PSD1 %.8x PSD2 %.8x\n", PSD1, PSD2);

                /* set interrupt blocking state in CPUSTATUS */
                CPUSTATUS |= BIT24;                 /* set blocked state in cpu status, bit 24 too */
                MODES |= BLKMODE;                   /* set blocked in mode to0 */
                PSD2 &= ~RETMBIT;                   /* turn off retain map bit in PSD2 */
                PSD2 &= ~RETBBIT;                   /* turn off retain block mode bit in PSD2 */

                SPAD[0xf5] = PSD2;                  /* save the current PSD2 */
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
                loading = 0;                        /* we are done loading */
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "Load Skipinstr %1x set loading PSD1 %08x PSD2 %08x CPUSTATUS %08x\n",
                    skipinstr, PSD1, PSD2, CPUSTATUS);
                goto skipi;                         /* skip int test */
            }
            goto wait_loop;                         /* continue waiting */
        }

        /* we get here when not booting */
        /* process any pending interrupts */
        if ((irq_pend || wait4int) && (irq_auto == 0)) {
            /* see if ints are pending */
            uint32 ilev;
            uint32 oldstatus = CPUSTATUS;           /* keep for retain blocking state */
            SPAD[0xf9] = CPUSTATUS;                 /* save the cpu status in SPAD */

            int_icb = scan_chan(&ilev);             /* no, go scan for I/O int pending */
            if (int_icb != 0) {                     /* was ICB returned for an I/O or interrupt */
                uint32 il = ilev;                   /* get the interrupt level */
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "<|>Normal int return icb %06x level %02x irq_pend %1x wait4int %1x\n",
                    int_icb, il, irq_pend, wait4int);

                /* take interrupt, store the PSD, fetch new PSD */
                bc = PSD2 & 0x3ff8;                 /* get copy of cpix */
                M[int_icb>>2] = PSD1&0xfffffffe;    /* store PSD 1 */
                M[(int_icb>>2)+1] = PSD2;           /* store PSD 2 */
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "<|>Normal int cpix %04x OPSD1 %08x OPSD2 %08x\n",
                    bc, PSD1, PSD2);
#ifdef DUMP_REGS
                for (ix=0; ix<8; ix+=2) {
                    sim_debug(DEBUG_IRQ, &cpu_dev,
                        "<|> GPR[%d] %.8x GPR[%d] %.8x\n", ix, GPR[ix], ix+1, GPR[ix+1]);
                }
#endif
                PSD1 = M[(int_icb>>2)+2];           /* get new PSD 1 */
                PSD2 = (M[(int_icb>>2)+3] & ~0x3fff) | bc;  /* get new PSD 2 w/old cpix */

                /* I/O status DW address will be in WD 6 */
                /* set new map mode and interrupt blocking state in CPUSTATUS */
                CC = PSD1 & 0x78000000;             /* extract bits 1-4 from PSD1 */
                MODES = PSD1 & 0x87000000;          /* extract bits 0, 5, 6, 7 from PSD 1 */
                CPUSTATUS &= ~0x87000080;           /* reset bits in CPUSTATUS */
                CPUSTATUS |= MODES;                 /* now insert into CPUSTATUS */
                if (PSD2 & MAPBIT) {
                    CPUSTATUS |= BIT8;              /* set bit 8 of cpu status to mapped */
                    MODES |= MAPMODE;               /* set mapped mode */
                } else {
                    CPUSTATUS &= ~BIT8;             /* reset bit 8 of cpu status */
                    MODES &= ~MAPMODE;              /* reset set mapped mode */
                }

                if ((PSD2 & RETBBIT) == 0) {        /* is it retain blocking state, bit 48 set */
                    /* retain blocking state is off, use bit 49 to set new blocking state */
                    if (PSD2 & SETBBIT) {           /* no, is it set blocking state bit 49 set*/
                        /* new blocking state is blocked when bits 48=0 & bit 49=1 */
                        CPUSTATUS |= BIT24;         /* yes, set blk state in cpu status bit 24 */
                        MODES |= BLKMODE;           /* set blocked mode */

                        /* This test fixed the hangs on terminal input for diags & UTX! */
                        t = SPAD[il+0x80];          /* get spad entry for interrupt */
                        /* Class F I/O spec says to reset active interrupt if user's */
                        /* interrupt service routine runs with interrupts blocked */
                        if (((t & 0x0f800000) == 0x0f000000) || /* if class F clear interrupt */
                            ((t & 0x0000ffff) == 0x00007f06) || /* RT Clock */
                            ((t & 0x0f00ffff) == 0x03007f04)) { /* Interval timer */
                            /* if this is F class I/O interrupt, clear the active level */
                            /* SPAD entries for interrupts begin at 0x80 */
                            if ((irq_auto) != 0) {
                                sim_debug(DEBUG_IRQ, &cpu_dev,
                                    "<|>Auto-reset irq_auto NOT zero %x INTS[%02x] %08x SPAD[%02x] %08x\n",
                                    irq_auto, il, INTS[il], il+0x80, SPAD[il+0x80]);
                            }
                            irq_auto = il;          /* show processing in blocked mode */
                            sim_debug(DEBUG_IRQ, &cpu_dev,
                                "<|>Auto-reset interrupt INTS[%02x] %08x SPAD[%02x] %08x simi %02x\n",
                                il, INTS[il], il+0x80, SPAD[il+0x80], sim_interval);
#define LEAVE_ACTIVE
#ifndef LEAVE_ACTIVE
/*AIR*/                         INTS[irq_auto] &= ~INTS_ACT;  /* deactivate specified int level */
/*AIR*/                         SPAD[irq_auto+0x80] &= ~SINT_ACT; /* deactivate in SPAD too */
                                irq_auto = 0;
#endif
                        }
                    } else {
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                            "<|>RUN ACTIVE interrupt INTS[%02x] %08x SPAD[%02x] %08x\n",
                            il, INTS[il], il+0x80, SPAD[il+0x80]);
                        CPUSTATUS &= ~BIT24;        /* no, reset blk state in cpu status bit 24 */
                        MODES &= ~BLKMODE;          /* reset blocked mode */
                    }
                } else {
                    /* handle retain blocking state */
                    PSD2 &= ~RETMBIT;               /* turn off retain bit in PSD2 */
                    /* set new blocking state in PSD2 */
                    PSD2 &= ~(SETBBIT|RETBBIT);     /* clear bit 48 & 49 to be unblocked */
                    MODES &= ~(BLKMODE|RETBLKM);    /* reset blocked & retain mode bits */
                    if (oldstatus & BIT24) {        /* see if old mode is blocked */
                        PSD2 |= SETBBIT;            /* set to blocked state */
                        MODES |= BLKMODE;           /* set blocked mode */
                    }
                }

                SPAD[0xf5] = PSD2;                  /* save the current PSD2 */
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "<|>Int %02x OPSD1 %08x OPSD2 %08x NPSD1 %08x NPSD2 %08x\n",
                    il, RMW(int_icb), RMW(int_icb+4), PSD1, PSD2);
                bc = RMW(int_icb+20) & 0xffffff;
                if (RMW(int_icb+16) == 0)
                    sim_debug(DEBUG_IRQ, &cpu_dev,
                        "<|>Int2 %02x ICBA %06x ICBA %06x IOCLA %06x\n", il, int_icb,
                        RMW(int_icb+16), RMW(int_icb+20));
                else
                    sim_debug(DEBUG_IRQ, &cpu_dev,
                        "<|>Int2 %02x ICBA %06x IOCLA %06x STAT %08x SW1 %08x SW2 %08x\n",
                        il, int_icb, RMW(int_icb+16), RMW(int_icb+20), RMW(bc), RMW(bc+4));
#ifdef DYNAMIC_DEBUG_01172021
                    cpu_dev.dctrl |= DEBUG_INST;    /* start instruction trace */
#endif
                wait4int = 0;                       /* wait is over for int */
                drop_nop = 0;                       /* no nop skipping */
                goto skipi;                         /* skip int test */
            }
        }

        /* see if in wait instruction */
        if (wait4int) {                             /* keep waiting */
            /* tell simh we will be waiting */
            sim_idle(TMR_RTC, 1);                   /* wait for clock tick */
            irq_pend = 1;                           /* start scanning interrupts again */
            goto wait_loop;                         /* continue waiting */
        }

        /* Check for external interrupt here */
        /* see if we have an attention request from console */
        if (!skipinstr && attention_trap) {
            TRAPME = attention_trap;                /* get trap number */
            attention_trap = 0;                     /* clear flag */
            sim_debug(DEBUG_XIO, &cpu_dev, "Attention TRAP %04x\n", TRAPME);
            goto newpsd;                            /* got process trap */
        }

skipi:
        i_flags = 0;                                /* do not update pc if MF or NPM */
        TRAPSTATUS = CPUSTATUS & 0x57;              /* clear all trap status except cpu type */

        /* check for breakpoint request */
        if (sim_brk_summ && sim_brk_test(PC, SWMASK('E'))) {
            reason = STOP_IBKPT;
            break;
        }

        /* fill IR from logical memory address */
        if ((TRAPME = read_instruction(PSD, &IR))) {
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "read_instr TRAPME %04x PSD %08x %08x i_flags %04x drop_nop %1x\n",
                TRAPME, PSD1, PSD2, i_flags, drop_nop);
            if ((CPU_MODEL <= MODEL_27) || (CPU_MODEL == MODEL_67) ||
                (CPU_MODEL == MODEL_87) || (CPU_MODEL == MODEL_97)) {
                if ((TRAPME == MAPFLT) || (TRAPME == NPMEM)) {
                    i_flags |= HLF;                 /* assume half word instr */
                    PSD1 &= ~BIT31;                 /* force off last right */
                    // fix for 32/67 test 32/3 in MMM diag
                    if ((CPU_MODEL == MODEL_27) || (CPU_MODEL == MODEL_67))
                        i_flags |= BT;              /* do not update pc if MF or NPM */
                    else
                        i_flags &= ~BT;             /* do not update pc if MF or NPM */
                }
            } else {
                if ((TRAPME == PRIVVIOL_TRAP) && (CPU_MODEL == MODEL_V9)) {
                    i_flags |= HLF;                 /* assume half word instr */
                    drop_nop = 0;
                    i_flags &= ~BT;                 /* no branch taken */
                    PSD1 &= ~BIT31;                 /* force off last right */
                }
            }
            sim_debug(DEBUG_TRAP, &cpu_dev,
                "read_instr2 TRAPME %04x PSD %08x %08x i_flags %04x drop_nop %1x\n",
                TRAPME, PSD1, PSD2, i_flags, drop_nop);
            goto newpsd;                            /* got process trap */
        }

        if (PSD1 & 2) {                             /* see if executing right half */
            /* we have a rt hw instruction */
            IR <<= 16;                              /* put instruction in left hw */
            if ((CPU_MODEL <= MODEL_27) || (CPU_MODEL == MODEL_87) ||
                    (CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
                drop_nop = 0;                       /* not dropping nop for these machines */
                goto exec;                          /* machine does not drop nop instructions */
            }
            /* We have 67 or V6 and have a rt hw instruction */
            if (IR == 0x00020000) {                 /* is this a NOP from rt hw? */
                PSD1 = (PSD1 + 2) | (((PSD1 & 2) >> 1) & 1);    /* skip this instruction */
                if (skipinstr)
                    sim_debug(DEBUG_IRQ, &cpu_dev,
                        "2Rt HW instruction skipinstr %1x is set PSD1 %08x PSD2 %08x CPUSTATUS %08x\n",
                        skipinstr, PSD1, PSD2, CPUSTATUS);
                goto skipi;                         /* go read next instruction */
            }
            if (skipinstr)
                sim_debug(DEBUG_IRQ, &cpu_dev,
                "3Rt HW instruction skipinstr %1x is set PSD1 %08x PSD2 %08x CPUSTATUS %08x\n",
                skipinstr, PSD1, PSD2, CPUSTATUS);
        } else {
            /* we have a left hw or fullword instruction */
            /* see if we can drop a rt hw nop instruction */
            OP = (IR >> 24) & 0xFC;                 /* this is a 32/67 or above, get OP */
            if ((CPU_MODEL <= MODEL_27) || (CPU_MODEL == MODEL_87) ||
                    (CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                goto exec;                          /* old machines did not drop nop instructions */
            if (PSD1 & BASEBIT)
                i_flags = base_mode[OP>>2];         /* set the BM instruction processing flags */
            else
                i_flags = nobase_mode[OP>>2];       /* set the NBM instruction processing flags */
            if ((i_flags & 0xf) == HLF) {           /* this is left HW instruction */
                if ((IR & 0xffff) == 0x0002) {      /* see if rt hw is a nop */
                    /* treat this as a fw instruction */
                    drop_nop = 1;                   /* we need to skip nop next time */
                    sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "CPU setting Drop NOP PSD1 %08x IR %08x\n", PSD1, IR);
                }
            }
        }

exec:
        /* temp saves for debugging */
        OIR = IR;                                   /* save the instruction */
        OPSD1 = PSD1;                               /* save the old PSD1 */
        OPSD2 = PSD2;                               /* save the old PSD2 */
        TRAPSTATUS = CPUSTATUS & 0x57;              /* clear all trap status except cpu type */

        /* Split instruction into pieces */
        PC = PSD1 & 0xfffffe;                       /* get 24 bit addr from PSD1 */
        sim_debug(DEBUG_DETAIL, &cpu_dev,
            "-----Instr @ PC %08x PSD1 %08x PSD2 %08x IR %08x drop_nop %x\n",
            PC, PSD1, PSD2, IR, drop_nop);

        /* Update history for this instruction */
        if (hst_lnt) {
            hst_p += 1;                             /* next history location */
            if (hst_p >= hst_lnt)                   /* check for wrap */
                hst_p = 0;                          /* start over at beginning */
            hst[hst_p].opsd1 = OPSD1;               /* set original psd1 */ 
            hst[hst_p].opsd2 = OPSD2;               /* set original psd2 */ 
            hst[hst_p].oir = OIR;                   /* set original instruction */ 
        }

        opr = (IR >> 16) & MASK16;                  /* use upper half of instruction */
        OP = (opr >> 8) & 0xFC;                     /* Get opcode (bits 0-5) left justified */
        FC =  ((IR & F_BIT) ? 0x4 : 0) | (IR & 3);  /* get F & C bits for addressing */
        reg = (opr >> 7) & 0x7;                     /* dest reg or xr on base mode */
        sreg = (opr >> 4) & 0x7;                    /* src reg for reg-reg instructions or BR instr */
        dbl = 0;                                    /* no doubleword instruction */
        ovr = 0;                                    /* no overflow or arithmetic exception either */
        dest = (t_uint64)IR;                        /* assume memory address specified */
        CC = PSD1 & 0x78000000;                     /* save CC's if any */
        MODES = PSD1 & 0x87000000;                  /* insert bits 0, 5, 6, 7 from PSD 1 */
        CPUSTATUS &= ~0x87000000;                   /* reset those bits in CPUSTATUS */
        CPUSTATUS |= MODES;                         /* now insert them into CPUSTATUS */
        if (PSD2 & MAPBIT) {
            CPUSTATUS |= BIT8;                      /* set bit 8 of cpu status to mapped */
            MODES |= MAPMODE;                       /* set mapped mode */
        } else {
            CPUSTATUS &= ~BIT8;                     /* reset bit 8 of cpu status */
            MODES &= ~MAPMODE;                      /* reset mapped mode */
        }

        if (MODES & BASEBIT) {
            i_flags = base_mode[OP>>2];             /* set the instruction processing flags */
            addr = IR & RMASK;                      /* get address offset from instruction */
            sim_debug(DEBUG_DETAIL, &cpu_dev,
                "Base OP %04x i_flags %04x addr %08x\n", OP, i_flags, addr);
            switch(i_flags & 0xf) {
            case HLF:
                source = GPR[sreg];                 /* get the src reg from instruction */
                break;
            case IMM:
                if (PC & 02) {                      /* if pc is on HW boundry, bad address */
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "ADDRSPEC1 OP %04x addr %08x\n", OP, addr);
                    goto newpsd;                    /* go execute the trap now */
                }
                break;
            case ADR:
                ix = (IR >> 20) & 7;                /* get index reg from instruction */
                if (ix != 0)
                    addr += (GPR[ix] & MASK24);     /* if not zero, add in reg contents */
            case WRD:
                if (PC & 02) {                      /* if pc is on HW boundry, bad address */
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "ADDRSPEC2 OP %04x addr %08x\n", OP, addr);
                    goto newpsd;                    /* go execute the trap now */
                }
                ix = (IR >> 16) & 7;                /* get base reg from instruction */
                if (ix != 0)     
                    addr += (BR[ix] & MASK24);      /* if not zero, add to base reg contents */
                FC = ((IR & F_BIT) ? 4 : 0);        /* get F bit from original instruction */
                FC |= addr & 3;                     /* set new C bits to address from orig or regs */
                addr &= MASK24;                     /* make pure 24 bit addr */
                break;
            case INV:
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
                break;
             }
        } else {
            i_flags = nobase_mode[OP>>2];           /* set the instruction processing flags */
            addr = IR & 0x7ffff;                    /* get 19 bit address from instruction */
            if (PC >= 0x80000) {
                TRAPME = MAPFAULT_TRAP;             /* Map Fault Trap */
                // DIAG add 97 for correct PSD address CN.MMM test 32, subtest 1 fails
                if ((CPU_MODEL <= MODEL_27) || (CPU_MODEL == MODEL_67) || 
                    (CPU_MODEL == MODEL_87) || (CPU_MODEL == MODEL_97)) {
                    // DIAG fix for 32/87 test 33/2, clear psd bit 31
                    if ((CPU_MODEL == MODEL_87))
                        PSD1 &= ~BIT31;             /* force off last right */
                    // DIAG fix 32/27 32/67 for diag MMM test 33/2
                    if ((CPU_MODEL <= MODEL_27) || (CPU_MODEL == MODEL_67))
                        i_flags |= BT;              /* do not update pc if MAPFAULT on 27 */
                    else
                        i_flags &= ~BT;             /* do not update pc if MF or NPM */
                    i_flags |= HLF;                 /* assume half word instr */
                }
                if ((CPU_MODEL <= MODEL_27)) {
                    /* 77, 27 rolls to zero, not 80000 */
                    PSD1 &= 0xff07ffff;             /* remove overflow bits */
                } else {
                    PSD1 &= 0xff0fffff;             /* leave overflow bit for trap addr */
                }
                sim_debug(DEBUG_TRAP, &cpu_dev,
            "PC over 80000 PC %08x Base OP %02x i_flags %04x addr %06x PSD %08x %08x\n",
                    PC, OP, i_flags, addr, PSD1, PSD2);
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                else
                    TRAPSTATUS |= BIT19;            /* set bit 19 of trap status */
                goto newpsd;                        /* handle trap */
            }
            sim_debug(DEBUG_DETAIL, &cpu_dev,
                "Non Based i_flags %04x addr %08x\n", i_flags, addr);
            /* non base mode instructions have bit 0 of the instruction set */
            /* for word length instructions and zero for halfword instructions */
            /* the LA (op=0x34) is the only exception.  So test for PC on a halfword */
            /* address and trap if word opcode is in right hw */
            /* if pc is on HW boundry, addr trap if bit zero set */
            if (PC & 02) {
                if ((OP == 0x34) || (OP & 0x80)) {
                    i_flags |= HLF;                 /* diags treats these as hw instructions */
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    goto newpsd;                    /* go execute the trap now */
                }
            }
            switch(i_flags & 0xf) {
            case HLF:                               /* halfword instruction */
                source = GPR[sreg];                 /* get the src reg contents */
                break;

            case IMM:       /* Immediate mode */
                if (PC & 02) {                      /* if pc is on HW boundry, bad address */
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "ADDRSPEC3 OP %04x addr %08x\n", OP, addr);
                    goto newpsd;                    /* go execute the trap now */
                }
                break;

            case ADR:       /* Normal addressing mode */
                ix = (IR >> 21) & 3;                /* get the index reg if specified */
                if (ix != 0) {
                    addr += GPR[ix];                /* if not zero, add in reg contents */
                    FC = ((IR & F_BIT) ? 4 : 0);    /* get F bit from original instruction */
                    FC |= addr & 3;                 /* set new C bits to address from orig or regs */
                }

                /* wart alert! */
                /* the lea instruction requires special handling for indirection. */
                /* Bits 0,1 are set to 1 in result addr if indirect bit is zero in */
                /* instruction.  Bits 0 & 1 are set to the last word */
                /* or instruction in the chain bits 0 & 1 if indirect bit set */
                  /* if IX == 00 => dest = IR */
                  /* if IX == 0x => dest = IR + reg */
                  /* if IX == Ix => dest = ind + reg */

                /* fall through */
            case WRD:       /* Word addressing, no index */
                bc = 0xC0000000;                    /* set bits 0, 1 for instruction if not indirect */
                t = IR;                             /* get current IR */
                addr &= MASK24;                     /* make pure 24 bit addr */
                while ((t & IND) != 0) {            /* process indirection */
                    if ((TRAPME = Mem_read(addr, &temp))) {   /* get the word from memory */
                        sim_debug(DEBUG_TRAP, &cpu_dev,
                            "case WRD Mem_read status %02x @ %08x OP %04x\n", TRAPME, addr, OP);
                        if (CPU_MODEL == MODEL_V9)  /* V9 wants bit0 set in pfault */
                            if (TRAPME == DMDPG)    /* demand page request */
                                pfault |= 0x80000000;   /* set instruction fetch paging error */
                        goto newpsd;                /* memory read error or map fault */
                    }
                    bc = temp & 0xC0000000;         /* save new bits 0, 1 from indirect location */
                    CC = (temp & 0x78000000);       /* save CC's from the last indirect word */
                    /* process new X, I, ADDR fields */
                    addr = temp & MASK19;           /* get just the addr */
                    ix = (temp >> 21) & 3;          /* get the index reg from indirect word */
                    if (ix != 0)
                        addr += (GPR[ix] & MASK19); /* add the register to the address */
                    /* if no F or C bits set, use original, else new */
                    if ((temp & F_BIT) || (addr & 3)) 
                        FC = ((temp & F_BIT) ? 0x4 : 0) | (addr & 3);
                    else {
                        addr |= (IR & F_BIT);       /* copy F bit from instruction */
                        addr |= (FC & 3);           /* copy in last C bits */
                    }
                    t = temp;                       /* go process next indirect location */
                    temp &= MASK19;                 /* go process next indirect location */
                    addr &= ~F_BIT;                 /* turn off F bit */
                }
                dest = (t_uint64)addr;              /* make into 64 bit variable */
                break;

            case INV:       /* Invalid instruction */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
                break;
            }
        }

        /* Read memory operand */
        if (i_flags & RM) { 
            if ((TRAPME = Mem_read(addr, &temp))) { /* get the word from memory */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "case RM Mem_read status %02x @ %08x\n", TRAPME, addr);
                // DIAG add 97 for correct PSD address CN.MMM test 32, subtest 1 fails
                if ((TRAPME == MAPFLT) || (TRAPME == NPMEM) || (TRAPME == MPVIOL))
                    PSD1 &= ~BIT31;                 /* force off last right */
                goto newpsd;                        /* memory read error or map fault */
            }
            source = (t_uint64)temp;                /* make into 64 bit value */
            switch(FC) {
            case 0:                                 /* word address, extend sign */
                source |= (source & MSIGN) ? D32LMASK : 0;
                break;
            case 1:                                 /* left hw */
                source >>= 16;                      /* move left hw to right hw*/
                /* Fall through */
            case 3:                                 /* right hw or right shifted left hw */
                source &= RMASK;                    /* use just the right hw */
                if (source & 0x8000) {              /* check sign of 16 bit value */
                    /* sign extend the value to leftmost 48 bits */
                    source = LMASK | (source & RMASK);  /* extend low 32 bits */
                    source |= (D32LMASK);           /* extend hi bits */
                }
                break;
            case 2:                                 /* double word address */
                if ((addr & 7) != 2) {              /* must be double word adddress */
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "ADDRSPEC4 case RM wd 1/3 Mem_read DW status %02x @ %08x src %08x\n",
                        TRAPME, addr, (uint32)source);
                    goto newpsd;                    /* go execute the trap now */
                }
                if ((TRAPME = Mem_read(addr+4, &temp))) {   /* get the 2nd word from memory */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "case RM wd 2 Mem_read status %02x @ %08x\n", TRAPME, addr+4);
                    goto newpsd;                    /* memory read error or map fault */
                }
                source = (source << 32) | (t_uint64)temp;   /* merge in the low order 32 bits */
                dbl = 1;                            /* double word instruction */
                break;
            case 4:                                 /* byte mode, byte 0 */
            case 5:                                 /* byte mode, byte 1 */
            case 6:                                 /* byte mode, byte 2 */
            case 7:                                 /* byte mode, byte 3 */
                source = (source >> (8*(7-FC))) & 0xff; /* right justify addressed byte */
                break;
           }
        }

        /* Read memory operand without doing sign extend for EOMX/ANMX/ORMX/ARMX */
        if (i_flags & RNX) { 
            if ((TRAPME = Mem_read(addr, &temp))) { /* get the word from memory */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "case RNX 2 Mem_read status %02x @ %08x\n", TRAPME, addr);
                goto newpsd;                        /* memory read error or map fault */
            }
            source = (t_uint64)temp;                /* make into 64 bit value */
            switch(FC) {
            case 0:                                 /* word address and no sign extend */
                source &= D32RMASK;                 /* just l/o 32 bits */
                break;
            case 1:                                 /* left hw */
                source >>= 16;                      /* move left hw to right hw*/
                /* Fall through */
            case 3:                                 /* right hw or right shifted left hw */
                source &= RMASK;                    /* use just the right hw */
                break;
            case 2:                                 /* double word address */
                if ((addr & 7) != 2) {              /* must be double word adddress */
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "ADDRSPEC5 OP %04x addr %08x\n", OP, addr);
                    goto newpsd;                    /* go execute the trap now */
                }
                if ((TRAPME = Mem_read(addr+4, &temp))) {   /* get the 2nd word from memory */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "case RNX wd 2 Mem_read status %02x @ %08x\n", TRAPME, addr+4);
                    goto newpsd;                    /* memory read error or map fault */
                }
                source = (source << 32) | (t_uint64)temp;   /* merge in the low order 32 bits */
                dbl = 1;                            /* double word instruction */
                break;
            case 4:                                 /* byte mode, byte 0 */
            case 5:                                 /* byte mode, byte 1 */
            case 6:                                 /* byte mode, byte 2 */
            case 7:                                 /* byte mode, byte 3 */
                source = (source >> (8*(7-FC))) & 0xff; /* right justify addressed byte */
                break;
           }
        }

        /* Read in if from register */
        if (i_flags & RR) {
            if (FC == 2 && (i_flags & HLF) == 0)    /* double dest? */
                dbl = 1;                            /* src must be dbl for dbl dest */
            dest = (t_uint64)GPR[reg];              /* get the register content */
            if (dbl) {                              /* is it double regs */
                if (reg & 1) {                      /* check for odd reg load */
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "ADDRSPEC6 OP %04x addr %08x\n", OP, addr);
                    goto newpsd;                    /* go execute the trap now */
                }
                /* merge the regs into the 64bit value */
                dest = (((t_uint64)dest) << 32) | ((t_uint64)GPR[reg+1]);
            } else {
                /* sign extend the data value */
                dest |= (dest & MSIGN) ? D32LMASK : 0;
            }
        }

        /* For Base mode */
        if (i_flags & RB) {
            dest = (t_uint64)BR[reg];               /* get base reg contents */
        }

        /* For register instructions */
        if (i_flags & R1) {
            source = (t_uint64)GPR[sreg];
            if (dbl) {
                if (sreg & 1) {
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "ADDRSPEC7 OP %04x addr %08x\n", OP, addr);
                    goto newpsd;                    /* go execute the trap now */
                }
                /* merge the regs into the 64bit value */
                source = (source << 32) | ((t_uint64)GPR[reg+1]);
            } else {
                /* sign extend the data value */
                source |= (source & MSIGN) ? ((t_uint64)MASK32) << 32: 0;
            }
        }

        /* process instruction op code */
        sim_debug(DEBUG_DETAIL, &cpu_dev,
            "PSD %08x %08x SW OP %04x IR %08x addr %08x\n",
            PSD1, PSD2, OP, IR, addr);

        /*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
        /* start processing the opcodes */
        /*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
        switch (OP>>2) {
        /*
         * For op-codes=00,04,08,0c,10,14,28,2c,38,3c,40,44,60,64,68
         */
        /* Reg - Reg instruction Format (16 bit) */
        /* |--------------------------------------| */
        /* |0 1 2 3 4 5|6 7 8 |9 10 11|12 13 14 15| */
        /* | Op Code   | DReg | SReg  | Aug Code  | */
        /* |--------------------------------------| */
        case 0x00>>2:       /* HLF - HLF */         /* CPU General operations */
            switch(opr & 0xF) {                     /* switch on aug code */
            case 0x0:   /* HALT */
#ifndef TEMP4DEBUG
                if ((MODES & PRIVBIT) == 0) {       /* must be privileged to halt */
                    TRAPME = PRIVVIOL_TRAP;         /* set the trap to take */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    else
                        TRAPSTATUS |= BIT19;        /* set bit 19 of trap status */
                    goto newpsd;                    /* Privlege violation trap */
                }
                if (CPUSTATUS & BIT23) {            /* Priv mode halt must be enabled */
                    TRAPME = PRIVHALT_TRAP;         /* set the trap to take */
                    goto newpsd;                    /* Privlege mode halt trap */
                }
#endif
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "\n[][][][][][][][][][] HALT [][][][][][][][][][]\n");
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "PSD1 %.8x PSD2 %.8x TRAPME %.4x CPUSTATUS %08x\n",
                    PSD1, PSD2, TRAPME, CPUSTATUS);
                for (ix=0; ix<8; ix+=2) {
                    sim_debug(DEBUG_EXP, &cpu_dev,
                        "GPR[%d] %.8x GPR[%d] %.8x\n", ix, GPR[ix], ix+1, GPR[ix+1]);
                }
                sim_debug(DEBUG_EXP, &cpu_dev,
                    "[][][][][][][][][][] HALT [][][][][][][][][][]\n");

                fprintf(stdout, "\r\n[][][][][][][][][][] HALT [][][][][][][][][][]\r\n");
                fprintf(stdout, "PSD1 %.8x PSD2 %.8x TRAPME %.4x CPUSTATUS %08x\r\n",
                    PSD1, PSD2, TRAPME, CPUSTATUS);
                for (ix=0; ix<8; ix+=2) {
                    fprintf(stdout, "GPR[%d] %.8x GPR[%d] %.8x\r\n",
                        ix, GPR[ix], ix+1, GPR[ix+1]);
                }
                if (MODES & BASEBIT) {              /* see if based */
                    for (ix=0; ix<8; ix+=2) {
                        fprintf(stdout, "BR[%d] %.8x BR[%d] %.8x\r\n",
                           ix, BR[ix], ix+1, BR[ix+1]);
                }
            }
            fprintf(stdout, "[][][][][][][][][][] HALT [][][][][][][][][][]\r\n");
/*TEST DIAG*/reason = STOP_HALT;                    /* do halt for now */
            break;

            case 0x1:   /* WAIT */
                if ((MODES & PRIVBIT) == 0) {       /* must be privileged to wait */
                    TRAPME = PRIVVIOL_TRAP;         /* set the trap to take */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    else
                        TRAPSTATUS |= BIT19;        /* set bit 19 of trap status */
                    goto newpsd;                    /* Privlege violation trap */
                }
                /* if interrupts are blocked, system check trap */
                if (CPUSTATUS & BIT24) {            /* status word bit 24 says blocked */
                    TRAPME = SYSTEMCHK_TRAP;        /* trap condition if F class */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT12;        /* set bit 0 of trap status */
                    else
                        TRAPSTATUS |= BIT20;        /* set bit 20 of trap status */
                    goto newpsd;                    /* system check trap */
                }
                if (wait4int == 0) {
                    time_t result = time(NULL);
                    sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "Starting WAIT mode %08x\n", (uint32)result);
                }
                wait4int = 1;                       /* show we are waiting for interrupt */
                /* tell simh we will be waiting */
                sim_idle(TMR_RTC, 0);               /* wait for next pending device event */
                irq_pend = 1;                       /* start scanning interrupts again */
                i_flags |= BT;                      /* keep PC from being incremented while waiting */
                break;
            case 0x2:   /* NOP */
                break;
            case 0x3:   /* LCS */
                /* get console switches from memory loc 0x780 */
                if ((TRAPME = Mem_read(0x780, &GPR[reg]))) /* get the word from memory */
                    goto newpsd;                    /* memory read error or map fault */
                set_CCs(GPR[reg], 0);               /* set the CC's, CC1 = 0 */
                break;
            case 0x4:   /* ES */
                if (reg & 1) {                      /* see if odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                /* reg is reg to extend sign into from reg+1 */
                GPR[reg] = (GPR[reg+1] & FSIGN) ? FMASK : 0;
                set_CCs(GPR[reg], 0);               /* set CCs, CC2 & CC3 */
                break;
            case 0x5:   /* RND */
                if (reg & 1) {                      /* see if odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                temp = GPR[reg];                    /* save the current contents of specified reg */
                t = (temp & FSIGN) != 0;            /* set flag for sign bit not set in temp value */
                bc = 1;
                t |= ((bc & FSIGN) != 0) ? 2 : 0;   /* ditto for the bit value */
                if (GPR[reg+1] & FSIGN) {           /* if sign of R+1 is set, incr R by 1 */
                    temp += bc;                     /* add the bit value to the reg */
                    /* if both signs are neg and result sign is positive, overflow */
                    /* if both signs are pos and result sign is negative, overflow */
                    if ((t == 3 && (temp & FSIGN) == 0) ||
                        (t == 0 && (temp & FSIGN) != 0)) {
                        ovr = 1;                    /* we have an overflow */
                    }
                    GPR[reg] = temp;                /* update the R value */
                } else
                    ovr = 0;
                set_CCs(temp, ovr);                 /* set the CC's, CC1 = ovr */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (ovr && (MODES & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                    goto newpsd;                    /* handle trap */
                }
                break;
            case 0x6:   /* BEI */
                if ((MODES & PRIVBIT) == 0) {       /* must be privileged to BEI */
                    TRAPME = PRIVVIOL_TRAP;         /* set the trap to take */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    else
                        TRAPSTATUS |= BIT19;        /* set bit 19 of trap status */
                    goto newpsd;                    /* Privlege violation trap */
                }
#ifdef DYNAMIC_DEBUG
TPSD[0] = PSD1;
TPSD[1] = PSD2;
#endif
                CPUSTATUS |= BIT24;                 /* into status word bit 24 too */
                PSD2 &= ~(SETBBIT|RETBBIT);         /* clear bit 48 & 49 */
                MODES &= ~(BLKMODE|RETBLKM);        /* reset blocked & retain mode bits */
                PSD2 |= SETBBIT;                    /* set to blocked state */
                MODES |= BLKMODE;                   /* set blocked mode */

                SPAD[0xf5] = PSD2;                  /* save the current PSD2 */
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
#ifdef DYNAMIC_DEBUG
sim_debug(DEBUG_IRQ, &cpu_dev,
    "BEI OPSD %.8x %.8x NPSD %.8x %.8x SPDF5 %.8x CPUSTATUS %08x\n",
        TPSD[0], TPSD[1], PSD1, PSD2, SPAD[0xf5], CPUSTATUS);
#endif
                break;

            case 0x7:   /* UEI */
                if ((MODES & PRIVBIT) == 0) {       /* must be privileged to UEI */
                    TRAPME = PRIVVIOL_TRAP;         /* set the trap to take */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    else
                        TRAPSTATUS |= BIT19;        /* set bit 19 of trap status */
                    goto newpsd;                    /* Privlege violation trap */
                }
#ifdef DYNAMIC_DEBUG
TPSD[0] = PSD1;
TPSD[1] = PSD2;
#endif
                if (CPUSTATUS & BIT24) {            /* see if old mode is blocked */
                    irq_pend = 1;                   /* start scanning interrupts again */
#ifdef LEAVE_ACTIVE
                    if (irq_auto) {
/*AIR*/                 INTS[irq_auto] &= ~INTS_ACT;    /* deactivate specified int level */
/*AIR*/                 SPAD[irq_auto+0x80] &= ~SINT_ACT; /* deactivate in SPAD too */
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                            "<|>IntX deactivate level %02x at UEI PSD1 %08x PSD2 %08x\n",
                            irq_auto, PSD1, PSD2);
/*AIR*/                 irq_auto = 0;               /* show done processing in blocked mode */
                    }
#endif
                }
                CPUSTATUS &= ~BIT24;                /* clear status word bit 24 */
                MODES &= ~(BLKMODE|RETBLKM);        /* reset blocked & retain mode bits */
                PSD2 &= ~(SETBBIT|RETBBIT);         /* clear bits 48 & 49 to be unblocked */
                SPAD[0xf5] = PSD2;                  /* save the current PSD2 */
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
#ifdef DYNAMIC_DEBUG
sim_debug(DEBUG_IRQ, &cpu_dev,
    "UEI OPSD %.8x %.8x NPSD %.8x %.8x SPDF5 %.8x CPUSTATUS %08x\n",
        TPSD[0], TPSD[1], PSD1, PSD2, SPAD[0xf5], CPUSTATUS);
#endif
                break;
            case 0x8:   /* EAE */
                PSD1 |= AEXPBIT;                    /* set the enable AEXP flag in PSD 1 */
                MODES |= AEXPBIT;                   /* enable arithmetic exception in modes & PSD 1 */
                CPUSTATUS |= AEXPBIT;               /* into status word too */
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
                break;
            case 0x9:   /* RDSTS */
                GPR[reg] = CPUSTATUS;               /* get CPU status word */
                break;
            case 0xA:   /* SIPU */                  /* ignore for now */
                sim_debug(DEBUG_CMD, &cpu_dev,
                    "SIPU CPUSTATUS %08x SPAD[0xf9] %08x\n", CPUSTATUS, SPAD[0xf9]);
                break;
            case 0xB:   /* RWCS */                  /* RWCS ignore for now */
                /* reg = specifies reg containing the ACS/WCS address */
                /* sreg = specifies the ACS/WCS address */
                /* if the WCS option is not present, address spec error */
                /* if the mem addr is not a DW, address spec error */
                /* If 0<-Rs<=fff and Rs bit 0=0, then PROM address */
                /* If 0<-Rs<=fff and Rs bit 0=1, then ACS address */
                /* if bit 20 set, WCS enables, else addr spec error */
                if ((CPUSTATUS & 0x00000800) == 0) {
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                /* Maybe TODO copy something from WCS */
                break;
            case 0xC:   /* WWCS */                  /* WWCS ignore for now */
                /* reg = specifies the logical address in memory that */
                /* is to receive the ACS/WCS contents */
                /* sreg = specifies the ACS/WCS address */
                /* bit 20 of cpu stat must be set=1 to to write to ACS or WCS */
                /* bit 21 of CPU stat must be 0 to write to ACS */
                /* if bit 20 set, WCS enables, else addr spec error */
                if ((CPUSTATUS & 0x00000800) == 0) {
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                /* Maybe TODO copy something to WCS */
                break;
            case 0xD:   /* SEA */
                if (MODES & BASEBIT)                /* see if based */
                    goto inv;                       /* invalid instruction in based mode */
                MODES |= EXTDBIT;                   /* set new extended flag (bit 5) in modes & PSD */
                PSD1 |= EXTDBIT;                    /* set the enable AEXP flag in PSD1 */
                CPUSTATUS |= EXTDBIT;               /* into status word too */
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
                break;
            case 0xE:   /* DAE */
                MODES &= ~AEXPBIT;                  /* disable arithmetic exception in modes & PSD */
                PSD1 &= ~AEXPBIT;                   /* disable AEXP flag in PSD */
                CPUSTATUS &= ~AEXPBIT;              /* into status word too */
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
                break;

            case 0xF:   /* CEA */
                if (MODES & BASEBIT)                /* see if based */
                    goto inv;                       /* invalid instruction in based mode */
                MODES &= ~EXTDBIT;                  /* disable extended mode in modes and PSD */
                PSD1 &= ~EXTDBIT;                   /* disable extended mode (bit 5) flag in PSD */
                CPUSTATUS &= ~EXTDBIT;              /* into status word too */
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
                break;
            }
            break;
        case 0x04>>2:       /* 0x04 RR|R1|SD|HLF - SD|HLF */ /* ANR, SMC, CMC, RPSWT */
            i_flags &=  ~SCC;                       /* make sure we do not set CC's for dest value */
            switch(opr & 0xF) {
            case 0x0:   /* ANR */
                dest &= source;                     /* just an and reg to reg */
                if (dest & MSIGN)
                    dest |= D32LMASK;               /* force upper word to all ones */
                i_flags |=  SCC;                    /* make sure we set CC's for dest value */
                break; 

            case 0xA:       /* CMC */               /* Cache Memory Control - Diag use only */
                if (CPU_MODEL == MODEL_87)
                    break;                          /* just ignore */
                if (CPU_MODEL < MODEL_67) {
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    goto newpsd;                    /* handle trap */
                }
                if (CPU_MODEL <= MODEL_V6) {
                    /* Cache memory control bit assignments for reg */
                    /* 0-22 reserved, must be zero */
                    /* 23 - Initialize Instruction Cache Bank 0 On = 1 Off = 0 */
                    /* 24 - Initialize Instruction Cache Bank 1 On = 1 Off = 0 */
                    /* 25 - Initialize Operand Cache Bank 0 On = 1 Off = 0 */
                    /* 26 - Initialize Operand Cache Bank 1 On = 1 Off = 0 */
                    /* 27 - Enable Instruction Cache Bank 0 On = 1 Off = 0 */
                    /* 28 - Enable Instruction Cache Bank 1 On = 1 Off = 0 */
                    /* 29 - Enable Operand Cache Bank 0 On = 1 Off = 0 */
                    /* 30 - Enable Operand Cache Bank 1 On = 1 Off = 0 */
                    /* 31 - Bypass Instruction Cache Bank 1 On = 1 Off = 0 */
                    sim_debug(DEBUG_EXP, &cpu_dev,
                        "CMC V6/67 GPR[%02x] = %04x CMCR = %08x CPU STATUS SPAD[f9] = %08x\r\n",
                        reg, GPR[reg], CMCR, SPAD[0xf9]);
                    CMCR = GPR[reg];                /* write reg bits 23-31 to cache memory controller */
                    i_flags &= ~SD;                 /* turn off store dest for this instruction */
                } else
                if (CPU_MODEL == MODEL_V9) {
                    sim_debug(DEBUG_EXP, &cpu_dev,
                        "CMC V9 GPR[%02x] = %08x CMCR = %08x CPU STATUS SPAD[f9] = %08x\r\n",
                        reg, GPR[reg], CMCR, SPAD[0xf9]);
                    CMCR = GPR[reg];                /* write reg bits 23-31 to cache memory controller */
                    i_flags &= ~SD;                 /* turn off store dest for this instruction */
                }
                break;

            case 0x7:       /* SMC */               /* Shared Memory Control - Diag use only */
                if (CPU_MODEL < MODEL_67) {
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    goto newpsd;                    /* handle trap */
                }
                /* Shared memory control bit assignments for reg */
                /*    0 - Reserved */
                /*    1 - Shared Memory Enabled (=1)/Disabled (=0) */
                /*  2-6 - Upper Bound of Shared Memory */
                /*    7 - Read & Lock Enabled (=1)/Disabled (=0) */
                /* 8-12 - Lower Bound of Shared Memory */
                /* 3-31 - Reserved and must be zero */
                sim_debug(DEBUG_CMD, &cpu_dev,
                    "SMC V6/67 GPR[%02x] = %08x SMCR = %08x CPU STATUS SPAD[f9] = %08x\n",
                    reg, GPR[reg], SMCR, SPAD[0xf9]);
                SMCR = GPR[reg];                    /* write reg bits 0-12 to shared memory controller */
                i_flags &= ~SD;                     /* turn off store dest for this instruction */
                break;

/* 67, 97, V6 Computer Configuration Word is copied when bit zero of Rd set to one (0x80000000) */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00|01|02 03 04 05 06|07|08 09 10 11 12|13 14 15|16|17|18|19|20 21|22|23 24 25 26|27|28|29|30|31| */
/* |  | S| Upper Bound  |RL| Lower Bound  |Reserved|4k|8k|SM|P2| Res |AP| Reserved  |I0|I1|D0|D1|BY| */
/* | 0| x| x  x  x  x  x| x| x  x  x  x  x| 0  0  0| x| x| x| x| 0  0| 0| 0  0  0  0| x| x| x| x| x| */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */
/* Bits:    0   Reserved */
/*          1   Shared Memory Enabled (=1)/Disabled (=0) */
/*        2-6   Upper Bound of Shared Memory */
/*          7   Read & Lock Enabled (=1)/Disabled (=0) */
/*       8-12   Lower Bound of Shared Memory */
/*      13-15   Reserved */
/*         16   4K WCS Option Present (=1)/Not Present (=0) */
/*         17   8K WCS Option Present (=1)/Not Present (=0) */
/*         18   Firmware Control Store Mode ROMSIM (=1)/PROM (=0) */
/*         19   IPU Present (=1)/Not Present (=0) */
/*      20-21   Reserved */
/*         22   Access Protection ECO Present (=0)/No Access Protection (=0) V6 & V9 */
/*      23-26   Reserved */
/*         27   Instruction Cache Bank 0 on (=1)/Off (=0) */
/*         28   Instruction Cache Bank 1 on (=1)/Off (=0) */
/*         29   Data Cache Bank 0 on (=1)/Off (=0) */
/*         30   Data Cache Bank 1 on (=1)/Off (=0) */
/*         31   Instruction Cache Enabled (=1)/Disabled (=0) */
/* */
/* V9 Computer Configuration Word when bit zero of Rd set to one (0x80000000) */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03|04 05 06 07|08 09 10 11|12 13 14 15|16|17|18|19|20|21|22|23|24 25 26 27|28 29 30 31| */
/* | CPU Bank1 | CPU Bank2 | IPU Bank0 | IPU Bank1 |M1|M2|C1|C2|P2|SM|AP|  | CPU FW Ver| CPU FW Rev| */
/* | x  x  x  x| x  x  x  x| x  x  x  x| x  x  x  x| x| x| x| x| x| x| x| x| x  x  x  x| x  x  x  x| */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */
/* Bits: 0-15   Cache/Shadow Unit Present (=1)/Not Present (=0) */
/*         16   MACC Present in CP1 (=1)/Not Present (=0) */
/*         17   MACC Present in CP2 (=1)/Not Present (=0) */
/*         18   CP1 Present (=1)/CP1 Not Present (=0) */
/*         19   CP2 Present (=1)/CP2 Not Present (=0) */
/*         20   IPU Present (=1)/Not Present (=0) */
/*         21   Shared Memory Present (=1)/Not Present (=0) */
/*         22   Access Protection ECO Present (=0)/No Access Protection (=0) V6 & V9 */
/*         23   Reserved */
/*      24-27   CPU Firmware Version */
/*      28-31   CPU Formware Revision Level */
/* */
/* V9 CPU Shadow Memory Configuration Word when bit one of Rd set to one (0x40000000) */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02|03 04 05 06 07|08 09 10 11 12 13 14 15|16 17 18 19 20 21 22 23|24 25 26 27 28 29 30 31| */
/* | SMU #  |   Not Used   | CPU Unit 1 Base Addr  | CPU Unit 2 Base Addr  | CPU Unit 3 Base Addr  | */
/* | x  x  x| 0  0  0  0  0| x  x  x  x  x  x  x  0| x  x  x  x  x  x  x  0| x  x  x  x  x  x  x  0| */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */
/* Bits:    0   Shadow Memory Unit 1 Present (=1)/Not Present (=0) */
/*          1   Shadow Memory Unit 2 Present (=1)/Not Present (=0) */
/*          2   Shadow Memory Unit 3 Present (=1)/Not Present (=0) */
/*        3-7   Not Used */
/*       8-14   Shadow Memory Unit 1 Base Address (bits 08-14 of address) */
/*         15   Always zero */
/*      16-22   Shadow Memory Unit 2 Base Address (bits 08-14 of address) */
/*         23   Always zero */
/*      24-30   Shadow Memory Unit 2 Base Address (bits 08-14 of address) */
/*         31   Always zero */
/* */
/* V9 IPU Shadow Memory Configuration Word when bit two of Rd set to one (0x20000000) */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02|03 04 05 06 07|08 09 10 11 12 13 14 15|16 17 18 19 20 21 22 23|24 25 26 27 28 29 30 31| */
/* | SMU #  |   Not Used   | IPU Unit 1 Base Addr  | IPU Unit 2 Base Addr  | IPU Unit 3 Base Addr  | */
/* | x  x  x| 0  0  0  0  0| x  x  x  x  x  x  x  0| x  x  x  x  x  x  x  0| x  x  x  x  x  x  x  0| */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */
/* Bits:    0   Shadow Memory Unit 1 Present (=1)/Not Present (=0) */
/*          1   Shadow Memory Unit 2 Present (=1)/Not Present (=0) */
/*          2   Shadow Memory Unit 3 Present (=1)/Not Present (=0) */
/*        3-7   Not Used */
/*       8-14   Shadow Memory Unit 1 Base Address (bits 08-14 of address) */
/*         15   Always zero */
/*      16-22   Shadow Memory Unit 2 Base Address (bits 08-14 of address) */
/*         23   Always zero */
/*      24-30   Shadow Memory Unit 2 Base Address (bits 08-14 of address) */
/*         31   Always zero */
/* */
/* When bit zero of Rd is zero, PSW word 2 is copies to Rd (0x00000000) */
/* */
            case 0xB:   /* RPSWT */                 /* Read Processor Status Word 2 (PSD2) */
                if ((GPR[reg] & 0x80000000) && (CPU_MODEL < MODEL_V9)) {
                    /* if bit 0 of reg set, return (default 0) CPU Configuration Word */
                    dest = CCW;                     /* no cache or shared memory */
                    /* make sure bit 19 is zero saying IPU not present */
                    dest &= ~0x00001000;            /* reset IPU bit for DIAGS */
                    /* bit 22 set for access ECO present */
                    dest |= 0x00000200;             /* set ECO bit for DIAGS */
                    /* Try setting cache on bits 27-31 */
                    dest |= 0x0000001f;             /* set SIM bit for DIAGS */
                } else
                if ((GPR[reg] & 0x80000000) && (CPU_MODEL == MODEL_V9)) {
                    /* if bit 0 of reg set, return Cache/Shadow Configuration Word */
                    CMSMC = 0xffff0000;             /* no CPU/IPU Cache/Shadow unit present */
                    CMSMC |= 0x00000000;            /* CPU Cache/Shadow unit present */
                    CMSMC |= 0x00000800;            /* bit 20, IPU not present */
                    CMSMC |= 0x00000200;            /* bit 22, Access Protection ECO present */
                    CMSMC |= 0x0000001f;            /* CPU Firmware Version 1/Rev level 0 */
                    dest = CMSMC;                   /* return starus */
                } else
                if ((GPR[reg] & 0x40000000) && (CPU_MODEL == MODEL_V9)) {
                    /* if bit 1 of reg set, return CPU Shadow Memory Configuration Word */
                    CSMCW = 0x00000000;             /* no Shadow unit present */
                    dest = CSMCW;                   /* return starus */
                } else
                if ((GPR[reg] & 0x20000000) && (CPU_MODEL ==  MODEL_V9)) {
                    /* if bit 2 of reg set, return Cache Memory Configuration Word */
                    ISMCW = 0x00000000;             /* no Shadow unit present */
                    dest = ISMCW;                   /* return starus */
                } else
                if ((GPR[reg] & BIT0) == 0x00000000) {
                    /* if bit 0 of reg not set, return PSD2 */
                    /* make sure bit 49 (block state is current state */
                    dest = SPAD[0xf5];              /* get PSD2 for user from SPAD 0xf5 */
                    dest &= ~(SETBBIT|RETBBIT);     /* clear bit 48 & 49 to be unblocked */
                    if (CPUSTATUS & BIT24) {        /* see if old mode is blocked */
                        dest |= SETBBIT;            /* set bit 49 for blocked */
                    }
                }
                break;

            case 0x08:      /* 0x0408 INV (Diag Illegal instruction) */
                /* HACK HACK HACK for DIAGS */
                if (CPU_MODEL <= MODEL_27) {        /* DIAG error for 32/27 only */
                    if ((PSD1 & 2) == 0)            /* if lf hw instruction */
                        i_flags |= HLF;             /* if nop in rt hw, bump pc a word */
                }
                /* drop through */
            default:        /* INV */               /* everything else is invalid instruction */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
                break;
            }
            break;

        case 0x08>>2:       /* 0x08 SCC|RR|R1|SD|HLF - */ /* ORR or ORRM */
            dest |= source;                         /* or the regs into dest reg */
            switch(opr & 0x0f) {
            case 0x8:                               /* this is ORRM op */
                 dest &= GPR[4];                    /* mask with reg 4 contents */
                 /* drop thru */
            case 0x0:                               /* this is ORR op */
                if (dest & MSIGN)                   /* see if we need to sign extend */
                    dest |= D32LMASK;               /* force upper word to all ones */
                break;
            default:    /* INV */                   /* everything else is invalid instruction */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
            }
            break;

        case 0x0C>>2:       /* 0x0c SCC|RR|R1|SD|HLF - SCC|SD|HLF */ /* EOR or EORM */
            dest ^= source;                         /* exclusive or the regs into dest reg */
            switch(opr & 0x0f) {
            case 0x8:                               /* this is EORM op */
                 dest &= GPR[4];                    /* mask with reg 4 contents */
                /* drop thru */
            case 0x0:                               /* this is EOR op */
                if (dest & MSIGN)                   /* see if we need to sign extend */
                    dest |= D32LMASK;               /* force upper word to all ones */
                break;
            default:    /* INV */                   /* everything else is invalid instruction */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
            }
            break;

        case 0x10>>2:       /* 0x10 HLF - HLF */    /* CAR or (basemode SACZ ) */
            if ((opr & 0xF) == 0) {                 /* see if CAR instruction */
                /* handle non basemode/basemode CAR instr */
                if ((int32)GPR[reg] < (int32)GPR[sreg])
                    CC = CC3BIT;                    /* Rd < Rs; negative */
                else
                if (GPR[reg] == GPR[sreg])
                    CC = CC4BIT;                    /* Rd == Rs; zero */
                else
                    CC = CC2BIT;                    /* Rd > Rs; positive */
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
            } else {
                if ((MODES & BASEBIT) == 0) {       /* if not basemode, error */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    goto newpsd;                    /* handle trap */
                }
                /* handle basemode SACZ instruction */
sacz:           /* non basemode SCZ enters here */
                temp = GPR[reg];                    /* get destination reg contents to shift */
                CC = 0;                             /* zero the CC's */
                t = 0;                              /* start with zero shift count */
                if (temp == 0) {
                    CC = CC4BIT;                    /* set CC4 showing dest is zero & cnt is zero too */
                }
#ifdef NOT_FOR_DIAG
                /* The doc says the reg is not shifted if bit 0 is set on entry. */
                /* diags says it does, so that is what we will do */
                /* set count to zero, but shift reg 1 left */
                else
                if (temp & BIT0) {
                    CC = 0;                         /* clear CC4 & set count to zero */
                }
#endif
                else
                if (temp != 0) {                    /* shift non zero values */
                    while ((temp & FSIGN) == 0) {   /* shift the reg until bit 0 is set */
                        temp <<= 1;                 /* shift left 1 bit */
                        t++;                        /* increment shift count */
                    }
                    temp <<= 1;                     /* shift the sign bit out */
                }
                GPR[reg] = temp;                    /* save the shifted values */
                GPR[sreg] = t;                      /* set the shift cnt into the src reg */
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
            }
            break;

        case 0x14>>2:       /* 0x14 HLF - HLF */    /* CMR compare masked with reg */
            if (opr & 0xf) {                        /* any subop not zero is error */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
            }
            temp = GPR[reg] ^ GPR[sreg];            /* exclusive or src and destination values */
            temp &= GPR[4];                         /* and with mask reg (GPR 4) */
            CC = 0;                                 /* set all CCs zero */
            if (temp == 0)                          /* if result is zero, set CC4 */
                CC = CC4BIT;                        /* set CC4 to show result 0 */
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's */
            PSD1 |= (CC & 0x78000000);              /* update the CC's in the PSD */
            break;

        case 0x18>>2:       /* 0x18 HLF - HLF */    /* SBR, (basemode ZBR, ABR, TBR */
            if (MODES & BASEBIT) {                  /* handle basemode ZBR, ABR, TBR */
                if ((opr & 0xC) == 0x0)             /* SBR instruction */
                    goto sbr;                       /* use nonbase SBR code */
                if ((opr & 0xC) == 0x4)             /* ZBR instruction */
                    goto zbr;                       /* use nonbase ZBR code */
                if ((opr & 0xC) == 0x8)             /* ABR instruction */
                    goto abr;                       /* use nonbase ABR code */
                if ((opr & 0xC) == 0xC)             /* TBR instruction */
                    goto tbr;                       /* use nonbase TBR code */
inv:
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                 if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */

            } else {                                /* handle non basemode SBR */
                if (opr & 0xc) {                    /* any subop not zero is error */
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    goto newpsd;                    /* handle trap */
                }
sbr:                                                /* handle basemode too */
                /* move the byte field bits 14-15 to bits 27-28 */
                /* or in the bit# from dest reg field bits 6-8 into bit 29-31 */
                bc = (((opr << 3) & 0x18) | reg);   /* get # bits to shift right */
                bc = BIT0 >> bc;                    /* make a bit mask of bit number */
                t = (PSD1 & 0x70000000) >> 1;       /* get old CC bits 1-3 into CCs 2-4*/
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                if (GPR[sreg] & bc)                 /* test the bit in src reg */
                    t |= CC1BIT;                    /* set CC1 to the bit value */
                GPR[sreg] |=  bc;                   /* set the bit in src reg */
                PSD1 |= t;                          /* update the CC's in the PSD */
            }
            break;

        case 0x1C>>2:       /* 0x1C HLF - HLF */    /* ZBR (basemode SRA, SRL, SLA, SLL) */
            if (MODES & BASEBIT) {                  /* handle basemode SRA, SRL, SLA, SLL */
                bc = opr & 0x1f;                    /* get bit shift count */
                if ((opr & 0x60) == 0x00) {         /* SRA instruction */
                    temp = GPR[reg];                /* get reg value to shift */
                    t = temp & FSIGN;               /* sign value */
                    for (ix=0; ix<bc; ix++) {
                        temp >>= 1;                 /* shift bit 0 right one bit */
                        temp |= t;                  /* restore original sign bit */
                    }
                    GPR[reg] = temp;                /* save the new value */
                    break;
                }
                if ((opr & 0x60) == 0x20) {         /* SRL instruction */
                    GPR[reg] >>= bc;                /* value to be output */
                    break;
                }
                if ((opr & 0x60) == 0x40) {         /* SLA instruction */
                    temp = GPR[reg];                /* get reg value to shift */
                    t = temp & FSIGN;               /* sign value */
                    ovr = 0;                        /* set ovr off */
                    for (ix=0; ix<bc; ix++) {
                        temp <<= 1;                 /* shift bit into sign position */
                        if ((temp & FSIGN) ^ t)     /* see if sign bit changed */
                            ovr = 1;                /* set arithmetic exception flag */
                    }
                    temp &= ~BIT0;                  /* clear sign bit */
                    temp |= t;                      /* restore original sign bit */
                    GPR[reg] = temp;                /* save the new value */
                    PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                    if (ovr)
                        PSD1 |= BIT1;               /* CC1 in PSD */
                    /* the arithmetic exception will be handled */
                    /* after instruction is completed */
                    /* check for arithmetic exception trap enabled */
                    if (ovr && (MODES & AEXPBIT)) {
                        TRAPME = AEXPCEPT_TRAP;     /* set the trap type */
                        goto newpsd;                /* go execute the trap now */
                    }
                    break;
                }
                if ((opr & 0x60) == 0x60) {         /* SLL instruction */
                    GPR[reg] <<= bc;                /* value to be output */
                    break;
                }
                break;
            } else {                                /* handle nonbase ZBR */
                if (opr & 0xc) {                    /* any subop not zero is error */
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    goto newpsd;                    /* handle trap */
                }
zbr:            /* handle basemode too */
                /* move the byte field bits 14-15 to bits 27-28 */
                /* or in the bit# from dest reg field bits 6-8 into bit 29-31 */
                bc = (((opr << 3) & 0x18) | reg);   /* get # bits to shift right */
                bc = BIT0 >> bc;                    /* make a bit mask of bit number */
                t = (PSD1 & 0x70000000) >> 1;       /* get old CC bits 1-3 into CCs 2-4*/
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                if (GPR[sreg] & bc)                 /* test the bit in src reg */
                    t |= CC1BIT;                    /* set CC1 to the bit value */
                GPR[sreg] &= ~bc;                   /* reset the bit in src reg */
                PSD1 |= t;                          /* update the CC's in the PSD */
            }
            break;

        case 0x20>>2:       /* 0x20 HLF - HLF */    /* ABR (basemode SRAD, SRLD, SLAD, SLLD) */
            if (MODES & BASEBIT) {                  /* handle basemode SRAD, SRLD, SLAD, SLLD */
                if (reg & 1) {                      /* see if odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                dest = (t_uint64)GPR[reg+1];        /* get low order reg value */
                dest |= (((t_uint64)GPR[reg]) << 32);   /* insert upper reg value */
                bc = opr & 0x1f;                    /* get bit shift count */
                source = dest & DMSIGN;             /* 64 bit sign value */
                switch (opr & 0x60) {
                case 0x00:                          /* SRAD instruction */
                    for (ix=0; ix<bc; ix++) {
                        dest >>= 1;                 /* shift bit 0 right one bit */
                        dest |= source;             /* restore original sign bit */
                    }
                    break;

                case 0x20:                          /* SRLD */
                    dest >>= bc;                    /* shift right #bits */
                    break;

                case 0x40:                          /* SLAD instruction */
                    ovr = 0;                        /* set ovr off */
                    for (ix=0; ix<bc; ix++) {
                        dest <<= 1;                 /* shift bit into sign position */
                        if ((dest & DMSIGN) ^ source)   /* see if sign bit changed */
                            ovr = 1;                /* set arithmetic exception flag */
                    }
                    dest &= ~DMSIGN;                /* clear sign bit */
                    dest |= source;                 /* restore original sign bit */
                    GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                    GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                    PSD1 &= 0x87FFFFFE;             /* clear the old CC's */
                    if (ovr)
                        PSD1 |= BIT1;               /* CC1 in PSD */
                    /* the arithmetic exception will be handled */
                    /* after instruction is completed */
                    /* check for arithmetic exception trap enabled */
                    if (ovr && (MODES & AEXPBIT)) {
                        TRAPME = AEXPCEPT_TRAP;     /* set the trap type */
                        goto newpsd;                /* go execute the trap now */
                    }
                    break;

                case 0x60:                          /* SLLD */
                    dest <<= bc;                    /* shift left #bits */
                    break;
                }
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                break;

            } else {                                /* handle nonbase mode ABR */
                if (opr & 0xc) {                    /* any subop not zero is error */
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    goto newpsd;                    /* handle trap */
                }
abr:                                                /* basemode ABR too */
                /* move the byte field bits 14-15 to bits 27-28 */
                /* or in the bit# from dest reg field bits 6-8 into bit 29-31 */
                bc = (((opr << 3) & 0x18) | reg);   /* get # bits to shift right */
                bc = BIT0 >> bc;                    /* make a bit mask of bit number */
                temp = GPR[sreg];                   /* get reg value to add bit to */
                t = (temp & FSIGN) != 0;            /* set flag for sign bit not set in temp value */
                t |= ((bc & FSIGN) != 0) ? 2 : 0;   /* ditto for the bit value */
                temp += bc;                         /* add the bit value to the reg */
                /* if both signs are neg and result sign is positive, overflow */
                /* if both signs are pos and result sign is negative, overflow */
                if ((t == 3 && (temp & FSIGN) == 0) ||
                    (t == 0 && (temp & FSIGN) != 0)) {
                    ovr = 1;                        /* we have an overflow */
                }
                GPR[sreg] = temp;                   /* save the new value */
                set_CCs(temp, ovr);                 /* set the CC's, CC1 = ovr */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (ovr && (MODES & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                    goto newpsd;                    /* handle trap */
                }
            }
            break;

        case 0x24>>2:       /* 0x24 HLF - HLF */    /* TBR (basemode SRC)  */
            if (MODES & BASEBIT) {                  /* handle SRC basemode */
                bc = opr & 0x1f;                    /* get bit shift count */
                temp = GPR[reg];                    /* get reg value to shift */
                if ((opr & 0x60) == 0x40) {         /* SLCBR instruction */
                    for (ix=0; ix<bc; ix++) {
                        t = temp & BIT0;            /* get sign bit status */
                        temp <<= 1;                 /* shift the bit out */
                        if (t)
                            temp |= 1;              /* the sign bit status */
                    }
                } else {                            /* this is SRCBR */
                    for (ix=0; ix<bc; ix++) {
                        t = temp & 1;               /* get bit 31 status */
                        temp >>= 1;                 /* shift the bit out */
                        if (t)
                            temp |= BIT0;           /* put in new sign bit */
                    }
                }
                GPR[reg] = temp;                    /* shift result */
            } else {                                /* handle TBR non basemode */
                if (opr & 0xc) {                    /* any subop not zero is error */
                    TRAPME = UNDEFINSTR_TRAP;       /* Undefined Instruction Trap */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    goto newpsd;                    /* handle trap */
                }
tbr:                                                /* handle basemode TBR too */
                /* move the byte field bits 14-15 to bits 27-28 */
                /* or in the bit# from dest reg field bits 6-8 into bit 29-31 */
                bc = (((opr << 3) & 0x18) | reg);   /* get # bits to shift right */
                bc = BIT0 >> bc;                    /* make a bit mask of bit number */
                t = (PSD1 & 0x70000000) >> 1;       /* get old CC bits 1-3 into CCs 2-4*/
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                if (GPR[sreg] & bc)                 /* test the bit in src reg */
                    t |= CC1BIT;                    /* set CC1 to the bit value */
                PSD1 |= t;                          /* update the CC's in the PSD */
            }
            break;

        case 0x28>>2:       /* 0x28 HLF - HLF */    /* Misc OP REG instructions */
            switch(opr & 0xF) {
            case 0x0:       /* TRSW */
                if (MODES & BASEBIT)
                    temp = 0x78FFFFFE;              /* bits 1-4 and 24 bit addr for based mode */
                else
                    temp = 0x7807FFFE;              /* bits 1-4 and 19 bit addr for non based mode */
                addr = GPR[reg];                    /* get reg value */
                /* we are returning to the addr in reg, set CC's from reg */
                /* update the PSD with new address from reg */
                PSD1 &= ~temp;                      /* clean the bits to be changed */
                PSD1 |= (addr & temp);              /* insert the CC's and address */
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "TRSW REG %01x PSD %08x %08x modes %08x temp %06x\n",
                    reg, PSD1, PSD2, MODES, temp);
                i_flags |= BT;                      /* we branched, so no PC update */
                break;

            case 0x2:       /* XCBR */              /* Exchange base registers */
                if ((MODES & BASEBIT) == 0)         /* see if nonbased */
                    goto inv;                       /* invalid instruction in nonbased mode */
                temp = BR[reg];                     /* get dest reg value */
                BR[reg] = BR[sreg];                 /* put source reg value int dest reg */
                BR[sreg] = temp;                    /* put dest reg value into src reg */
                break;

            case 0x4:       /* TCCR */              /* Transfer condition codes to GPR bits 28-31 */
                if ((MODES & BASEBIT) == 0)         /* see if nonbased */
                    goto inv;                       /* invalid instruction in nonbased mode */
                temp = CC >> 27;                    /* right justify CC's in reg */
                GPR[reg] = temp;                    /* put dest reg value into src reg */
                break;

            case 0x5:       /* TRCC */              /* Transfer GPR bits 28-31 to condition codes */
                if ((MODES & BASEBIT) == 0)         /* see if nonbased */
                    goto inv;                       /* invalid instruction in nonbased mode */
                PSD1 = ((PSD1 & 0x87fffffe)|((GPR[reg] & 0xf) << 27));  /* insert CCs from reg */
                break;

            case 0x8:       /* BSUB */              /* Procedure call */
                if ((MODES & BASEBIT) == 0)         /* see if nonbased */
                    goto inv;                       /* invalid instruction in nonbased mode */

                /* if Rd field is 0 (reg is b6-b8), this is a BSUB instruction */
                /* otherwise it is a CALL instruction (Rd != 0) */
                if (reg == 0) {
                    /* BSUB instruction */
                    uint32 cfp = BR[2];             /* get dword bounded frame pointer from BR2 */
                    if ((BR[2] & 0x7) != 0)  {
                        /* Fault, must be dw bounded address */
                        TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                        goto newpsd;                /* go execute the trap now */
                    }
                    cfp = BR[2] & 0x00fffff8;       /* clean the cfp address to 24 bit dw */

                    M[cfp>>2] = (PSD1 + 2) & 0x01fffffe; /* save AEXP bit and PC into frame */
                    M[(cfp>>2)+1] = 0x80000000;     /* show frame created by BSUB instr */
                    BR[1] = BR[sreg] & MASK24;      /* Rs reg to BR 1 */
                    PSD1 = (PSD1 & 0xff000000) | (BR[1] & MASK24); /* New PSD address */
                    BR[3] = GPR[0];                 /* GPR 0 to BR 3 (AP) */
                    BR[0] = cfp;                    /* set frame pointer from BR 2 into BR 0 */
                    i_flags |= BT;                  /* we changed the PC, so no PC update */
                } else
                {
                    /* CALL instruction */
                    /* get frame pointer from BR2-16 words & make it a dword addr */
                    uint32 cfp = ((BR[2]-0x40) & 0x00fffff8);

                    /* if cfp and cfp+15w are in different maps, then addr exception error */
                    if ((cfp & 0xffe000) != ((cfp+0x3f) & 0xffe000)) {
                        TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                        goto newpsd;                /* go execute the trap now */
                    }

                    temp = (PSD1+2) & 0x01fffffe;   /* save AEXP bit and PC from PSD1 in to frame */
                    if ((TRAPME = Mem_write(cfp, &temp))) { /* Save the PSD into memory */
                        goto newpsd;                /* memory write error or map fault */
                    }

                    temp = 0x00000000;              /* show frame created by CALL instr */
                    if ((TRAPME = Mem_write(cfp+4, &temp))) { /* Save zero into memory */
                        goto newpsd;                /* memory write error or map fault */
                    }

                    /* Save BR 0-7 to stack */
                    for (ix=0; ix<8; ix++) {
                        if ((TRAPME = Mem_write(cfp+(4*ix)+8, &BR[ix]))) { /* Save into memory */
                            goto newpsd;            /* memory write error or map fault */
                        }
                    }

                    /* save GPR 2-8 to stack */
                    for (ix=2; ix<8; ix++) {
                        if ((TRAPME = Mem_write(cfp+(4*ix)+32, &GPR[ix]))) { /* Save into memory */
                            goto newpsd;            /* memory write error or map fault */
                        }
                    }

                    /* keep bits 0-7 from old PSD */ 
                    PSD1 = (PSD1 & 0xff000000) | ((BR[sreg]) & MASK24); /* New PSD address */
                    BR[1] = BR[sreg];               /* Rs reg to BR 1 */
                    BR[3] = GPR[reg];               /* Rd to BR 3 (AP) */
                    BR[0] = cfp;                    /* set current frame pointer into BR[0] */
                    BR[2] = cfp;                    /* set current frame pointer into BR[2] */
                    i_flags |= BT;                  /* we changed the PC, so no PC update */
                }
                break;

            case 0xC:       /* TPCBR */             /* Transfer program Counter to Base Register */
                if ((MODES & BASEBIT) == 0)         /* see if nonbased */
                    goto inv;                       /* invalid instruction in nonbased mode */
                BR[reg] = PSD1 & 0xfffffe;          /* save PC from PSD1 into BR */
                break;

            case 0xE:       /* RETURN */            /* procedure return for basemode calls */
                if ((MODES & BASEBIT) == 0)         /* see if nonbased */
                    goto inv;                       /* invalid instruction in nonbased mode */
                t = BR[0];                          /* get frame pointer from BR[0] */
                if ((TRAPME = Mem_read(t+4, &temp)))   /* get the word from memory */
                    goto newpsd;                    /* memory read error or map fault */
                /* if Bit0 set, restore all saved regs, else restore only BRs */
                if ((temp & BIT0) == 0) {           /* see if GPRs are to be restored */
                    /* Bit 0 is not set, so restore all GPRs */
                    for (ix=2; ix<8; ix++)
                        if ((TRAPME = Mem_read(t+ix*4+32, &GPR[ix])))   /* get the word from memory */
                            goto newpsd;            /* memory read error or map fault */
                }
                for (ix=0; ix<8; ix++) {
                    if ((TRAPME = Mem_read(t+ix*4+8, &BR[ix])))   /* get the word from memory */
                        goto newpsd;                /* memory read error or map fault */
                }
                PSD1 &= ~0x1fffffe;                 /* leave everything except AEXP bit and PC */
                if ((TRAPME = Mem_read(t, &temp)))  /* get the word from memory */
                    goto newpsd;                    /* memory read error or map fault */
                PSD1 |= (temp & 0x01fffffe);        /* restore AEXP bit and PC from call frame */
                i_flags |= BT;                      /* we changed the PC, so no PC update */
                break;

            case 0x1:                               /* INV */
            case 0x3:                               /* INV */
            case 0x6:                               /* INV */
            case 0x7:                               /* INV */
            case 0x9:                               /* INV */
            case 0xA:                               /* INV */
            case 0xB:                               /* INV */
            case 0xD:                               /* INV */
            case 0xF:                               /* INV */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
                break;
            }
            break;

        case 0x2C>>2:       /* 0x2C HLF - HLF */    /* Reg-Reg instructions */
            temp = GPR[reg];                        /* reg contents specified by Rd */
            addr = GPR[sreg];                       /* reg contents specified by Rs */
            bc = 0;

            switch(opr & 0xF) {
            case 0x0:       /* TRR */               /* SCC|SD|R1 */
                temp = addr;                        /* set value to go to GPR[reg] */
                bc = 1;                             /* set CC's at end */
                break;

            case 0x1:       /* TRBR */              /* Transfer GPR to BR  */
                if ((MODES & BASEBIT) == 0)         /* see if nonbased */
                    goto inv;                       /* invalid instruction in nonbased mode */
                BR[reg] = GPR[sreg];                /* copy GPR to BR */
                break;

            case 0x2:       /* TBRR */              /* transfer BR to GPR */
                if ((MODES & BASEBIT) == 0)         /* see if nonbased */
                    goto inv;                       /* invalid instruction in nonbased mode */
                temp = BR[sreg];                    /* set base reg value */
                bc = 1;                             /* set CC's at end */
                break;

            case 0x3:       /* TRC */               /* Transfer register complement */
                temp = addr ^ FMASK;                /* complement Rs */
                bc = 1;                             /* set CC's at end */
                break;

            case 0x4:       /* TRN */               /* Transfer register negative */
                temp = NEGATE32(addr);              /* negate Rs value */
                if (temp == addr)                   /* overflow if nothing changed */
                    ovr = 1;                        /* set overflow flag */
                /* reset ovr if val == 0, not set for DIAGS */
                if ((temp == 0) & ovr)
                    ovr = 0;
                bc = 1;                             /* set the CC's */
                break;

            case 0x5:       /* XCR */               /* exchange registers Rd & Rs */
                GPR[sreg] = temp;                   /* Rd to Rs */
                set_CCs(temp, ovr);                 /* set the CC's from original Rd */
                temp = addr;                        /* save the Rs value to Rd reg */
                break;

            case 0x6:       /* INV */
                goto inv;
                break;

            case 0x7:       /* LMAP */              /* Load map reg - Diags only */
                if ((MODES & PRIVBIT) == 0) {       /* must be privileged */
                    TRAPME = PRIVVIOL_TRAP;         /* set the trap to take */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    else
                        TRAPSTATUS |= BIT19;        /* set bit 19 of trap status */
                    goto newpsd;                    /* handle trap */
                }
                /* cpu must be unmapped */
                if (MODES & MAPMODE) {              /* must be unmapped cpu */
                    TRAPME = MAPFAULT_TRAP;         /* Map Fault Trap */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT8;         /* set bit 8 of trap status */
                    else
                        TRAPSTATUS |= BIT19;        /* set bit 19 of trap status */
                    goto newpsd;                    /* handle trap */
                }
                {   /* load the cpu maps using diag psd */
                    uint32  DPSD[2];                /* the PC for the instruction */
                    /* get PSD pointed to by real addr in Rd (temp) */
                    DPSD[0] = RMW(temp);            /* get word one of psd */
                    DPSD[1] = RMW(temp+4);          /* get word two of psd */
                    sim_debug(DEBUG_CMD, &cpu_dev,
                        "LMAP PSD %08x %08x DPSD %08x %08x modes %08x temp %06x\n",
                        PSD1, PSD2, DPSD[0], DPSD[1], MODES, temp);
                    if ((DPSD[1] & MAPBIT) == 0)    /* if PSD2 is unmapped, treat as NOP */
                        goto skipit;
                    if (PSD2 & RETMBIT)             /* don't load maps if retain bit set */
                        goto skipit;
                    temp2 = MODES;                  /* save modes bits through load_maps call */
                    MODES = DPSD[0] & 0x87000000;   /* extract bits 0, 5, 6, 7 from PSD 1 */
                    MODES |= MAPMODE;               /* set mapped mode flag for load_maps call */
                    sim_debug(DEBUG_CMD, &cpu_dev,
                        "LMAP PSD %08x %08x DPSD %08x %08x modes %08x temp2 %08x\n",
                        PSD1, PSD2, DPSD[0], DPSD[1], MODES, temp2);
                    /* we need to load the new maps */
                    TRAPME = load_maps(DPSD, 1);    /* load maps for new PSD */
                    sim_debug(DEBUG_CMD, &cpu_dev,
                        "LMAP TRAPME %08x MAPC[8-c] %08x %08x %08x %08x %08x %08x\n",
                        TRAPME, MAPC[7], MAPC[8], MAPC[9], MAPC[10], MAPC[11], MAPC[12]);
                    MODES = temp2;                  /* restore modes flags */
                    if (TRAPME) {
                        /* DIAGS wants the cpix for the psd to be the requested one */
                        PSD2 = (PSD2 & 0xffffc000) | (DPSD[1] & 0x3ff8);
                        SPAD[0xf5] = PSD2;          /* save the current PSD2 */
                        goto newpsd;                /* handle trap */
                    }
                    goto skipit;
                    break;
                }
                break;

            case 0x8:       /* TRRM */              /* SCC|SD|R1 */
                temp = addr & GPR[4];               /* transfer reg-reg masked */
                bc = 1;                             /* set CC's at end */
                break;

            /* CPUSTATUS bits */
            /* Bits 0-19 reserved */
            /* Bit 20   =0 Write to writable control store is disabled */
            /*          =1 Write to writable control store is enabled */
            /* Bit 21   =0 Enable PROM mode */
            /*          =1 Enable Alterable Control Store Mode */
            /* Bit 22   =0 Enable High Speed Floating Point Accelerator */
            /*          =1 Disable High Speed Floating Point Accelerator */
            /* Bit 23   =0 Disable privileged mode halt trap */
            /*          =1 Enable privileged mode halt trap */
            /* Bit 24 is reserved */
            /* bit 25   =0 Disable software trap handling (enable automatic trap handling) */
            /*          =1 Enable software trap handling */
            /* Bits 26-31 reserved */
            case 0x9:       /* SETCPU */
                if ((MODES & PRIVBIT) == 0) {       /* must be privileged */
                    TRAPME = PRIVVIOL_TRAP;         /* set the trap to take */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    else
                        TRAPSTATUS |= BIT19;        /* set bit 19 of trap status */
                    goto newpsd;                    /* handle trap */
                }
                temp2 = CPUSTATUS;                  /* save original */
                /* bits 20-23 and bit 25 can change */
                CPUSTATUS &= 0xfffff0bf;            /* zero bits that can change */
                CPUSTATUS |= (temp & 0x0f40);       /* or in the new status bits */
                CPUSTATUS |= BIT22;                 /* HS Floating is set to off */
                /* make sure WCS is off and prom mode set to 0 (on) */ 
                CPUSTATUS &= ~(BIT20|BIT21);        /* make zero */
                sim_debug(DEBUG_CMD, &cpu_dev,
                    "SETCPU orig %08x user bits %08x New CPUSTATUS %08x SPAD[f9] %08x\n",
                    temp2, temp, CPUSTATUS, SPAD[0xf9]);
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
                break;

            case 0xA:       /* TMAPR */             /* Transfer map to Reg - Diags only */
                if ((MODES & PRIVBIT) == 0) {       /* must be privileged */
                    TRAPME = PRIVVIOL_TRAP;         /* set the trap to take */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    else
                        TRAPSTATUS |= BIT19;        /* set bit 19 of trap status */
                    goto newpsd;                    /* handle trap */
                }
                if (CPU_MODEL <= MODEL_27) {        /* 7X & 27 must be unmapped */
                    if (MODES & MAPMODE) {          /* must be unmapped cpu */
                        TRAPME = MAPFAULT_TRAP;     /* Map Fault Trap */
                        goto newpsd;                /* handle trap */
                    }
                }
                /* Rs has map number for even/odd pair loading */
                if (CPU_MODEL < MODEL_27) {
                    /* 32/77 with 32 map regs */
                    addr &= 0x1e;                   /* make 0-15 */
                    temp = MAPC[addr>>1];           /* get even/odd maps */
                } else
                if ((CPU_MODEL == MODEL_27) || (CPU_MODEL == MODEL_87)) {
                    /* 32/27 & 32/87 have 256 maps */
                    addr &= 0xfe;                   /* make 0-255 */
                    temp = MAPC[addr>>1];           /* get even/odd maps */
                } else {
                    /* 32/67, 32/97, V6 & V9 have 2048 maps demand paging */
                    addr &= 0x7ff;                  /* make 0-2047 */
                    temp = MAPC[addr>>1];           /* get even/odd maps */
                    if ((addr & 1) == 0)            /* if even reg, use left hw */
                        temp >>= 16;                /* move over reg value */
                    temp &= 0xffff;                 /* just 16 bits */
                    if (TLB[addr] & 0x04000000)     /* see if HIT bit set */
                        temp |= 0x80000000;         /* show hit BIT is set */
                    temp |= ((TLB[addr] & 0xf8000000) >> 16);  /* add in protect bits */
                    if ((addr < 0x26) || (addr > 0x7f8))
                        sim_debug(DEBUG_CMD, &cpu_dev,
                            "TMAPR #%4x val %08x TLB %08x RMR %04x MAPC %08x\n",
                            addr, temp, TLB[addr], RMR(addr<<1), MAPC[addr/2]);
                }
                GPR[reg] = temp;                    /* save the temp value to Rd reg */
                goto skipit;
                break;

            case 0xB:       /* TRCM */              /* Transfer register complemented masked */
                temp = (addr ^ FMASK) & GPR[4];     /* compliment & mask */
                bc = 1;                             /* set the CC's */
                break;

            case 0xC:       /* TRNM */              /* Transfer register negative masked */
                temp = NEGATE32(addr);              /* complement GPR[reg] */
                if (temp == addr)                   /* check for overflow */
                    ovr = 1;                        /* overflow */
                /* reset ovr if val == 0, not set for DIAGS */
                if ((temp == 0) & ovr)
                    ovr = 0;
                temp &= GPR[4];                     /* and with negative reg */
                bc = 1;                             /* set the CC's */
                break;

            case 0xD:       /* XCRM */              /* Exchange registers masked */
                addr &= GPR[4];                     /* and Rs with mask reg */
                temp &= GPR[4];                     /* and Rd with mask reg */
                GPR[sreg] = temp;                   /* Rs to get Rd masked value */
                set_CCs(temp, ovr);                 /* set the CC's from original Rd */
                temp = addr;                        /* save the Rs value to Rd reg */
                break;

            case 0xE:       /* TRSC */              /* transfer reg to SPAD */
                if ((MODES & PRIVBIT) == 0) {       /* must be privileged */
                    TRAPME = PRIVVIOL_TRAP;         /* set the trap to take */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    else
                        TRAPSTATUS |= BIT19;        /* set bit 19 of trap status */
                    goto newpsd;                    /* handle trap */
                }
                t = (GPR[reg] >> 16) & 0xff;        /* get SPAD address from Rd (6-8) */
                temp2 = SPAD[t];                    /* get old SPAD data */
                SPAD[t] = GPR[sreg];                /* store Rs into SPAD */
                break;

            case 0xF:       /* TSCR */              /* Transfer scratchpad to register */
                if ((MODES & PRIVBIT) == 0) {       /* must be privileged */
                    TRAPME = PRIVVIOL_TRAP;         /* set the trap to take */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    else
                        TRAPSTATUS |= BIT19;        /* set bit 19 of trap status */
                    goto newpsd;                    /* handle trap */
                }
                t = (GPR[sreg] >> 16) & 0xff;       /* get SPAD address from Rs (9-11) */
                temp = SPAD[t];                     /* get SPAD data into Rd (6-8) */
                break;
            }
            GPR[reg] = temp;                        /* save the temp value to Rd reg */
            if (bc)                                 /* set cc's if bc set */
                set_CCs(temp, ovr);                 /* set the CC's */
            /* the arithmetic exception will be handled */
            /* after instruction is completed */
            /* check for arithmetic exception trap enabled */
            if (ovr && (MODES & AEXPBIT)) {
                TRAPME = AEXPCEPT_TRAP;             /* set the trap type */
                goto newpsd;                        /* handle trap */
            }
skipit:        
            /* for retain, leave PSD2 alone */
            break;

        case 0x30>>2:       /* 0x30 */              /* CALM */
            /* Process CALM for 32/27 when in left hw, else invalid */
            if ((CPU_MODEL <= MODEL_87) && (CPU_MODEL != MODEL_67)) {
                uint32 oldstatus = CPUSTATUS;       /* keep for retain blocking state */
                /* DIAG error for 32/27 or 32/87 only */
                if ((PSD1 & 2) != 0)                /* is it lf hw instruction */
                    goto inv;                       /* invalid instr if in rt hw */
                addr = SPAD[0xf0];                  /* get trap table memory address from SPAD (def 80) */
                if ((addr == 0) || ((addr&MASK24) == MASK24)) {  /* see if secondary vector table set up */
                    TRAPME = ADDRSPEC_TRAP;         /* Not setup, error */
                    goto newpsd;                    /* program error */
                }
                addr = addr + (0x0A << 2);          /* addr has mem addr of CALM trap vector (def A8) */
                t = M[addr >> 2];                   /* get the ICB address from memory */
                if ((t == 0) || ((t&MASK24) == MASK24)) {   /* see if ICB set up */
                    TRAPME = ADDRSPEC_TRAP;         /* Not setup, error */
                    goto newpsd;                    /* program error */
                }
                bc = PSD2 & 0x3ff8;                 /* get copy of cpix */
                /* this will skip over rt hw instruction if any */
                PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);    /* bump pc by 1 wd */
                M[t>>2] = PSD1 & 0xfffffffe;        /* store PSD 1 + 1HW to point to next instruction */
                M[(t>>2)+1] = PSD2;                 /* store PSD 2 */
                PSD1 = M[(t>>2)+2];                 /* get new PSD 1 */
                PSD2 = (M[(t>>2)+3] & ~0x3fff) | bc;    /* get new PSD 2 w/old cpix */
                M[(t>>2)+4] = opr & 0x03FF;         /* store calm number in bits 6-15 */

                /* set the mode bits and CCs from the new PSD */
                CC = PSD1 & 0x78000000;             /* extract bits 1-4 from PSD1 */
                MODES = PSD1 & 0x87000000;          /* extract bits 0, 5, 6, 7 from PSD 1 */
                CPUSTATUS &= ~0x87000000;           /* reset bits in CPUSTATUS */
                CPUSTATUS |= MODES;                 /* now insert into CPUSTATUS */

                /* set new map mode and interrupt blocking state in CPUSTATUS */
                if (PSD2 & MAPBIT) {
                    CPUSTATUS |= BIT8;              /* set bit 8 of cpu status */
                    MODES |= MAPMODE;               /* set mapped mode */
                } else {
                    CPUSTATUS &= ~BIT8;             /* reset bit 8 of cpu status */
/*TRY_01072022*/    MODES &= ~MAPMODE;              /* reset mapped mode */
                }

                /* set interrupt blocking state */
                if ((PSD2 & RETBBIT) == 0) {        /* is it retain blocking state */
                    if (PSD2 & SETBBIT) {           /* no, is it set blocking state */
                        CPUSTATUS |= BIT24;         /* yes, set blk state in cpu status bit 24 */
                        MODES |= BLKMODE;           /* set blocked mode */
                    } else {
                        CPUSTATUS &= ~BIT24;        /* no, reset blk state in cpu status bit 24 */
                        MODES &= ~BLKMODE;          /* reset blocked mode */
                        irq_pend = 1;               /* start scanning interrupts again */
#ifdef LEAVE_ACTIVE
                        if (irq_auto) {
/*AIR*/                     INTS[irq_auto] &= ~INTS_ACT;  /* deactivate specified int level */
/*AIR*/                     SPAD[irq_auto+0x80] &= ~SINT_ACT; /* deactivate in SPAD too */
                            sim_debug(DEBUG_IRQ, &cpu_dev,
                                "<|>IntX deactivate level %02x at CALM PSD1 %08x\n",
                                irq_auto, PSD1);
/*AIR*/                     irq_auto = 0;           /* show done processing in blocked mode */
                        }
#endif
                    }
                } else {
                    /* handle retain blocking state */
                    PSD2 &= ~RETMBIT;               /* turn off retain bit in PSD2 */
                    /* set new blocking state in PSD2 */
                    PSD2 &= ~(SETBBIT|RETBBIT);     /* clear bit 48 & 49 to be unblocked */
                    MODES &= ~(BLKMODE|RETBLKM);    /* reset blocked & retain mode bits */
                    if (oldstatus & BIT24) {        /* see if old mode is blocked */
                        PSD2 |= SETBBIT;            /* set to blocked state */
                        MODES |= BLKMODE;           /* set blocked mode */
                    }
                }

                SPAD[0xf5] = PSD2;                  /* save the current PSD2 */
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
                TRAPME = 0;                         /* not to be processed as trap */
                goto newpsd;                        /* new psd loaded */
            } else {
//              fprintf(stderr, "got CALM trap\r\n");
                goto inv;                           /* invalid instr */
            }
            break;

        case 0x34>>2:       /* 0x34 SD|ADR - inv */ /* LA non-basemode */
            if (MODES & BASEBIT)                    /* see if based */
                goto inv;                           /* invalid instruction in based mode */
            if (MODES & EXTDBIT) {                  /* see if extended mode */
                dest = (t_uint64)(addr&MASK24);     /* just pure 24 bit address */
            } else {                                /* use bits 13-31 */
                dest = (t_uint64)((addr&0x7ffff) | ((FC & 4) << 17));   /* F bit to bit 12 */
            }
            break;

        case 0x38>>2:       /* 0x38 HLF - HLF */    /* REG - REG floating point */
            switch(opr & 0xF) {
            case 0x0:       /* ADR */
                temp = GPR[reg];                    /* reg contents specified by Rd */
                addr = GPR[sreg];                   /* reg contents specified by Rs */
                t = (temp & FSIGN) != 0;            /* set flag for sign bit not set in temp value */
                t |= ((addr & FSIGN) != 0) ? 2 : 0; /* ditto for the reg value */
                temp = temp + addr;                 /* add the values */
                /* if both signs are neg and result sign is positive, overflow */
                /* if both signs are pos and result sign is negative, overflow */
                if ((t == 3 && (temp & FSIGN) == 0) || (t == 0 && (temp & FSIGN) != 0)) {
                    ovr = 1;                        /* we have an overflow */
                }
                i_flags |= SF;                      /* special processing */
                break;

            case 0x1:       /* ADRFW */
            case 0x3:       /* SURFW */
                /* TODO not on 32/27 */
                temp = GPR[reg];                    /* reg contents specified by Rd */
                addr = GPR[sreg];                   /* reg contents specified by Rs */
                /* temp has Rd (GPR[reg]), addr has Rs (GPR[sreg]) */
                if ((opr & 0xF) == 0x3) {
                    addr = NEGATE32(addr);          /* subtract, so negate source */
                }
                temp2 = s_adfw(temp, addr, &CC);    /* do ADFW */
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "%s GPR[%d] %08x addr %08x result %08x CC %08x\n",
                    (opr&0xf)==3 ? "SURFW":"ADRFW",
                    reg, GPR[reg], GPR[sreg], temp2, CC);
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                if (CC & CC1BIT) {                  /* check for arithmetic exception */
                    ovr = 1;                        /* exception */
                    /* leave Rd & Rs unchanged if AEXPBIT is set */
                    if (MODES & AEXPBIT) {
                        TRAPME = AEXPCEPT_TRAP;     /* trap the system now */
                        goto newpsd;                /* process the trap */
                    }
                }
                /* AEXPBIT not set, so save the fixed return value */
                /* return result to destination reg */
                GPR[reg] = temp2;                   /* dest - reg contents specified by Rd */
                break;

            case 0x2:       /* MPRBR */
                /* TODO not on 32/27 */
                if ((MODES & BASEBIT) == 0)         /* see if nonbased */
                    goto inv;                       /* invalid instruction in nonbased mode */
                if (reg & 1) {
                    /* Spec fault if not even reg */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                temp = GPR[reg+1];                  /* get multiplicand */
                addr = GPR[sreg];                   /* multiplier */

                /* change value into a 64 bit value */
                dest = ((t_uint64)(addr & FMASK)) | ((addr & FSIGN) ? D32LMASK : 0);
                source = ((t_uint64)(temp & FMASK)) | ((temp & FSIGN) ? D32LMASK : 0);
                dest = dest * source;               /* do the multiply */
                i_flags |= (SD|SCC);                /* save dest reg and set CC's */
                dbl = 1;                            /* double reg save */
                break;

            case 0x4:       /* DVRFW */
                /* TODO not on 32/27 */
                temp = GPR[reg];                    /* reg contents specified by Rd */
                addr = GPR[sreg];                   /* reg contents specified by Rs */
                /* temp has Rd (GPR[reg]), addr has Rs (GPR[sreg]) */
                temp2 = (uint32)s_dvfw(temp, addr, &CC);    /* divide reg by sreg */
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "DVRFW GPR[%d] %08x src %08x result %08x\n",
                    reg, GPR[reg], addr, temp2);
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                if (CC & CC1BIT) {                  /* check for arithmetic exception */
                    ovr = 1;                        /* exception */
                    /* leave Rd & Rs unchanged if AEXPBIT is set */
                    if (MODES & AEXPBIT) {
                        TRAPME = AEXPCEPT_TRAP;     /* trap the system now */
                        goto newpsd;                /* process the trap */
                    }
                }
                /* AEXPBIT not set, so save the fixed return value */
                /* return result to destination reg */
                GPR[reg] = temp2;                   /* dest - reg contents specified by Rd */
                break;

            case 0x5:       /* FIXW */
                /* TODO not on 32/27 */
                /* convert from 32 bit float to 32 bit fixed */
                addr = GPR[sreg];                   /* reg contents specified by Rs */
                temp2 = s_fixw(addr, &CC);          /* do conversion */
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "FIXW GPR[%d] %08x result %08x\n",
                    sreg, GPR[sreg], temp2);
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                if (CC & CC1BIT) {                  /* check for arithmetic exception */
                    ovr = 1;                        /* exception */
                    /* leave Rd & Rs unchanged if AEXPBIT is set */
                    if (MODES & AEXPBIT) {
                        TRAPME = AEXPCEPT_TRAP;     /* trap the system now */
                        goto newpsd;                /* process the trap */
                    }
                }
                /* AEXPBIT not set, so save the fixed return value */
                /* return result to destination reg */
                GPR[reg] = temp2;                   /* dest - reg contents specified by Rd */
                break;                              /* go set CC's */

            case 0x6:       /* MPRFW */
                /* TODO not on 32/27 */
                temp = GPR[reg];                    /* reg contents specified by Rd */
                addr = GPR[sreg];                   /* reg contents specified by Rs */
                /* temp has Rd (GPR[reg]), addr has Rs (GPR[sreg]) */
                temp2 = s_mpfw(temp, addr, &CC);    /* mult reg by sreg */
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "MPRFW GPR[%d] %08x src %08x result %08x\n",
                    reg, GPR[reg], addr, temp2);
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                if (CC & CC1BIT) {                  /* check for arithmetic exception */
                    ovr = 1;                        /* exception */
                    /* leave Rd & Rs unchanged if AEXPBIT is set */
                    if (MODES & AEXPBIT) {
                        TRAPME = AEXPCEPT_TRAP;     /* trap the system now */
                        goto newpsd;                /* process the trap */
                    }
                }
                /* AEXPBIT not set, so save the fixed return value */
                /* return result to destination reg */
                GPR[reg] = temp2;                   /* dest - reg contents specified by Rd */
                break;

            case 0x7:       /* FLTW */
                /* TODO not on 32/27 */
                /* convert from 32 bit integer to 32 bit float */
                addr = GPR[sreg];                   /* reg contents specified by Rs */
                GPR[reg] = s_fltw(addr, &CC);       /* do conversion & set CC's */
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "FLTW GPR[%d] %08x result %08x\n",
                    sreg, GPR[sreg], GPR[reg]);
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                break;

            case 0x8:       /* ADRM */
                temp = GPR[reg];                    /* reg contents specified by Rd */
                addr = GPR[sreg];                   /* reg contents specified by Rs */
                t = (temp & FSIGN) != 0;            /* set flag for sign bit not set in temp value */
                t |= ((addr & FSIGN) != 0) ? 2 : 0; /* ditto for the reg value */
                temp = temp + addr;                 /* add the values */
                /* if both signs are neg and result sign is positive, overflow */
                /* if both signs are pos and result sign is negative, overflow */
                if ((t == 3 && (temp & FSIGN) == 0) ||
                    (t == 0 && (temp & FSIGN) != 0))
                    ovr = 1;                        /* we have an overflow */
                temp &= GPR[4];                     /* mask the destination reg */
                i_flags |= SF;                      /* special processing */
                break;

            case 0x9:       /* ADRFD */
            case 0xB:       /* SURFD */
                /* TODO not on 32/27 */
                if ((reg & 1) || (sreg & 1)) {      /* see if any odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                td = (((t_uint64)GPR[reg]) << 32);  /* get upper reg value */
                td |= (t_uint64)GPR[reg+1];         /* insert low order reg value */
                source = (((t_uint64)GPR[sreg]) << 32); /* get upper reg value */
                source |= (t_uint64)GPR[sreg+1];    /* insert low order reg value */
                if ((opr & 0xF) == 0xb) {
                    source = NEGATE32(source);      /* make negative for subtract */
                }
                dest = s_adfd(td, source, &CC);     /* do ADFD */

                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "%s GPR[%d] %08x %08x src %016llx result %016llx\n",
                    (opr&0xf)==8 ? "ADRFD":"SURFD", reg, GPR[reg], GPR[reg+1], source, dest);
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                if (CC & CC1BIT) {                  /* check for arithmetic exception */
                    ovr = 1;                        /* exception */
                    /* leave Rd & Rs unchanged if AEXPBIT is set */
                    if (MODES & AEXPBIT) {
                        TRAPME = AEXPCEPT_TRAP;     /* trap the system now */
                        goto newpsd;                /* process the trap */
                    }
                }
                /* AEXPBIT not set, so save the fixed return value */
                /* return result to destination reg */
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                break;

            case 0xA:       /* DVRBR */
                /* TODO not on 32/27 */
                if ((MODES & BASEBIT) == 0)         /* see if nonbased */
                    goto inv;                       /* invalid instruction in nonbased mode */
                if (reg & 1) {
                    /* Spec fault if not even reg */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                /* get Rs divisor value */
                source = (t_uint64)(GPR[sreg]) | ((GPR[sreg] & FSIGN) ? D32LMASK : 0);
                /* merge the dividend regs into the 64bit value */
                dest = (((t_uint64)GPR[reg]) << 32) | ((t_uint64)GPR[reg+1]);
                if (source == 0) {
                    goto doovr4;
                    break;
                }
                td = (t_int64)dest % (t_int64)source;   /* remainder */
                if (((td & DMSIGN) ^ (dest & DMSIGN)) != 0) /* Fix sign if needed */
                    td = NEGATE32(td);              /* dividend and remainder must be same sign */
                dest = (t_int64)dest / (t_int64)source; /* now do the divide */
                /* test for overflow */
                if ((dest & D32LMASK) != 0 && (dest & D32LMASK) != D32LMASK) {
doovr4:
                    ovr = 1;                        /* the quotient exceeds 31 bit, overflow */
                    /* the arithmetic exception will be handled */
                    /* after instruction is completed */
                    /* check for arithmetic exception trap enabled */
                    if (ovr && (MODES & AEXPBIT)) {
                        TRAPME = AEXPCEPT_TRAP;     /* set the trap type */
                    }
                    /* the original regs must be returned unchanged if aexp */
                    set_CCs(temp, ovr);             /* set the CC's */
                } else {
                    GPR[reg] = (uint32)(td & FMASK);    /* reg gets remainder, reg+1 quotient */
                    GPR[reg+1] = (uint32)(dest & FMASK);    /* store quotient in reg+1 */
                    set_CCs(GPR[reg+1], ovr);       /* set the CC's, CC1 = ovr */
                }
                break;

            case 0xC:       /* DVRFD */
                /* TODO not on 32/27 */
                if ((reg & 1) || (sreg & 1)) {      /* see if any odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                td = (((t_uint64)GPR[reg]) << 32);  /* get upper reg value */
                td |= (t_uint64)GPR[reg+1];         /* insert low order reg value */
                source = (((t_uint64)GPR[sreg]) << 32); /* get upper reg value */
                source |= (t_uint64)GPR[sreg+1];    /* insert low order reg value */
                dest = s_dvfd(td, source, &CC);     /* divide double values */
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "DVRFD GPR[%d] %08x %08x src %016llx result %016llx\n",
                    reg, GPR[reg], GPR[reg+1], source, dest);
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                if (CC & CC1BIT) {                  /* check for arithmetic exception */
                    ovr = 1;                        /* exception */
                    /* leave Rd & Rs unchanged if AEXPBIT is set */
                    if (MODES & AEXPBIT) {
                        TRAPME = AEXPCEPT_TRAP;     /* trap the system now */
                        goto newpsd;                /* process the trap */
                    }
                }
                /* AEXPBIT not set, so save the fixed return value */
                /* return result to destination reg */
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                break;

            case 0xD:       /* FIXD */
                /* dest - reg contents specified by Rd & Rd+1 */
                /* source - reg contents specified by Rs & Rs+1 */
                if (sreg & 1) {
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                /* merge the sregs into the 64bit value */
                source = (((t_uint64)GPR[sreg]) << 32) | ((t_uint64)GPR[sreg+1]);
                /* convert from 64 bit double to 64 bit int */
                dest = s_fixd(source, &CC);
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "FIXD GPR[%d] %08x %08x result %016llx\n",
                    sreg, GPR[sreg], GPR[sreg+1], dest);
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                if (CC & CC1BIT) {                  /* check for arithmetic exception */
                    ovr = 1;                        /* exception */
                    /* leave Rd & Rs unchanged if AEXPBIT is set */
                    if (MODES & AEXPBIT) {
                        TRAPME = AEXPCEPT_TRAP;     /* trap the system now */
                        goto newpsd;                /* process the trap */
                    }
                }
                /* AEXPBIT not set, so save the fixed return value */
                /* return result to destination reg */
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                break;

            case 0xE:       /* MPRFD */
                /* TODO not on 32/27 */
                if ((reg & 1) || (sreg & 1)) {      /* see if any odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                td = (((t_uint64)GPR[reg]) << 32);  /* get upper reg value */
                td |= (t_uint64)GPR[reg+1];         /* insert low order reg value */
                source = (((t_uint64)GPR[sreg]) << 32); /* get upper reg value */
                source |= (t_uint64)GPR[sreg+1];    /* insert low order reg value */
                dest = s_mpfd(td, source, &CC);     /* multiply double values */
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "MPRFD GPR[%d] %08x %08x src %016llx result %016llx\n",
                    reg, GPR[reg], GPR[reg+1], source, dest);
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                if (CC & CC1BIT) {                  /* check for arithmetic exception */
                    ovr = 1;                        /* exception */
                    /* leave Rd & Rs unchanged if AEXPBIT is set */
                    if (MODES & AEXPBIT) {
                        TRAPME = AEXPCEPT_TRAP;     /* trap the system now */
                        goto newpsd;                /* process the trap */
                    }
                }
                /* AEXPBIT not set, so save the fixed return value */
                /* return result to destination reg */
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                break;

            case 0xF:       /* FLTD */
                /* TODO not on 32/27 */
                /* convert from 64 bit integer to 64 bit float */
                if ((reg & 1) || (sreg & 1)) {      /* see if any odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                source = (((t_uint64)GPR[sreg]) << 32); /* get upper reg value */
                source |= (t_uint64)GPR[sreg+1];    /* insert low order reg value */
                dest = s_fltd(source, &CC);         /* do conversion & set CC's */
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "FLTD GPR[%d] %08x %08x result %016llx\n",
                    sreg, GPR[sreg], GPR[sreg+1], dest);
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                break;
            }
            if (i_flags & SF) {                     /* see if special processing */
                GPR[reg] = temp;                    /* temp has destination reg value */
                set_CCs(temp, ovr);                 /* set the CC's */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (ovr && (MODES & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                    goto newpsd;                    /* go execute the trap now */
                }
            }
            break;

        case 0x3C>>2:       /* 0x3C HLF - HLF */    /* SUR and SURM */
            temp = GPR[reg];                        /* get negative value to add */
            temp2 = GPR[sreg];                      /* get negative value to add */
            addr = NEGATE32(GPR[sreg]);             /* reg contents specified by Rs */
            switch(opr & 0xF) {
            case 0x0:       /* SUR */
                t = (temp & FSIGN) != 0;            /* set flag for sign bit not set in temp value */
                t |= ((addr & FSIGN) != 0) ? 2 : 0; /* ditto for the reg value */
                temp = temp + addr;                 /* add the values */
                /* if both signs are neg and result sign is positive, overflow */
                /* if both signs are pos and result sign is negative, overflow */
                if ((t == 3 && (temp & FSIGN) == 0) ||
                    (t == 0 && (temp & FSIGN) != 0))
                    ovr = 1;                        /* we have an overflow */
                if (addr == FSIGN)
                    ovr = 1;                        /* we have an overflow */
                break;

            case 0x8:       /* SURM */
                t = (temp & FSIGN) != 0;            /* set flag for sign bit not set in temp value */
                t |= ((addr & FSIGN) != 0) ? 2 : 0; /* ditto for the reg value */
                temp = temp + addr;                 /* add the values */
                /* if both signs are neg and result sign is positive, overflow */
                /* if both signs are pos and result sign is negative, overflow */
                if ((t == 3 && (temp & FSIGN) == 0) ||
                    (t == 0 && (temp & FSIGN) != 0))
                    ovr = 1;                        /* we have an overflow */
                temp &= GPR[4];                     /* mask the destination reg */
                if (addr == FSIGN)
                    ovr = 1;                        /* we have an overflow */
                break;
            default:
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
                break;
            }
            GPR[reg] = temp;                        /* save the result */
            set_CCs(temp, ovr);                     /* set CCs for result */
            /* the arithmetic exception will be handled */
            /* after instruction is completed */
            /* check for arithmetic exception trap enabled */
            if (ovr && (MODES & AEXPBIT)) {
                TRAPME = AEXPCEPT_TRAP;             /* set the trap type */
                goto newpsd;                        /* go execute the trap now */
            }
            break;

        case 0x40>>2:       /* 0x40 SCC|SD|HLF - INV */ /* MPR */
            if (MODES & BASEBIT) 
                goto inv;                           /* invalid instruction in basemode */
            if (reg & 1) {                          /* odd reg specified? */
                /* Spec fault */
                /* HACK HACK HACK for DIAGS */
                if (CPU_MODEL <= MODEL_27) {        /* DIAG error for 32/27 only */
                    if ((PSD1 & 2) == 0)            /* if lf hw instruction */
                        i_flags &= ~HLF;            /* if nop in rt hw, bump pc a word */
                    else
                        PSD1 &= ~3;                 /* fake out 32/27 diag error */
                }
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            if (opr & 0xf) {                        /* any subop not zero is error */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
            }
            temp = GPR[reg+1];                      /* get multiplicand */
            addr = GPR[sreg];                       /* multiplier */

            /* change immediate value into a 64 bit value */
            dest = ((t_uint64)(addr & FMASK)) | ((addr & FSIGN) ? D32LMASK : 0);
            source = ((t_uint64)(temp & FMASK)) | ((temp & FSIGN) ? D32LMASK : 0);
            dest = dest * source;                   /* do the multiply */
            dbl = 1;                                /* double reg save */
            break;

        case 0x44>>2:       /* 0x44 ADR - ADR */    /* DVR */
            /* sreg has Rs */
            if (reg & 1) {
                /* Spec fault */
                /* HACK HACK HACK for DIAGS */
                if (CPU_MODEL <= MODEL_27) {        /* DIAG error for 32/27 only */
                    if ((PSD1 & 2) == 0)            /* if lf hw instruction */
                        i_flags &= ~HLF;            /* if nop in rt hw, bump pc a word */
                    else
                        PSD1 &= ~3;                 /* fake out 32/27 diag error */
                }
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            if (opr & 0xf) {                        /* any subop not zero is error */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                goto newpsd;                        /* handle trap */
            }
            /* get Rs divisor value */
            source = (t_uint64)(GPR[sreg]) | ((GPR[sreg] & FSIGN) ? D32LMASK : 0);
            /* merge the dividend regs into the 64bit value */
            dest = (((t_uint64)GPR[reg]) << 32) | ((t_uint64)GPR[reg+1]);
            if (source == 0)
                goto doovr3;
            td = (t_int64)dest % (t_int64)source;   /* remainder */
            if (((td & DMSIGN) ^ (dest & DMSIGN)) != 0) /* Fix sign if needed */
                td = NEGATE32(td);                  /* dividend and remainder must be same sign */
            dest = (t_int64)dest / (t_int64)source; /* now do the divide */
            int64a = dest;
            if (int64a < 0)
                int64a = -int64a;
            if (int64a > 0x7fffffff)                /* if more than 31 bits, we have an error */
                goto doovr3;
            if (((dest & D32LMASK) != 0 && (dest & D32LMASK) != D32LMASK) ||
                (((dest & D32LMASK) == D32LMASK) && ((dest & D32RMASK) == 0))) {  /* test for overflow */
doovr3:
                dest = (((t_uint64)GPR[reg]) << 32);/* insert upper reg value */
                dest |= (t_uint64)GPR[reg+1];       /* get low order reg value */
                ovr = 1;                            /* the quotient exceeds 31 bit, overflow */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (ovr && (MODES & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                }
                /* the original regs must be returned unchanged if aexp */
                CC = CC1BIT;                        /* set ovr CC bit */
                if (dest == 0)
                    CC |= CC4BIT;                   /* dw is zero, so CC4 */
                else
                if (dest & DMSIGN)
                    CC |= CC3BIT;                   /* it is neg dw, so CC3  */
                else
                    CC |= CC2BIT;                   /* then dest > 0, so CC2 */
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's from PSD1 */
                PSD1 |= CC;                         /* update the CC's in the PSD */
            } else {
                GPR[reg] = (uint32)(td & FMASK);    /* reg gets remainder, reg+1 quotient */
                GPR[reg+1] = (uint32)(dest & FMASK);    /* store quotient in reg+1 */
                set_CCs(GPR[reg+1], ovr);           /* set the CC's, CC1 = ovr */
            }
            break;

        case 0x48>>2:       /* 0x48 INV - INV */    /* unused opcodes */
        case 0x4C>>2:       /* 0x4C INV - INV */    /* unused opcodes */
        default:
            TRAPME = UNDEFINSTR_TRAP;               /* Undefined Instruction Trap */
            if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                TRAPSTATUS |= BIT0;                 /* set bit 0 of trap status */
            goto newpsd;                            /* handle trap */
            break;

        case 0x50>>2:       /* 0x50 INV - SD|ADR */ /* LA basemode LABRM */
            if ((MODES & BASEBIT) == 0)             /* see if nonbased */
                goto inv;                           /* invalid instruction in nonbased mode */
            dest = (t_uint64)(addr&MASK24);         /* just pure 24 bit address */
            break;

        case 0x54>>2:       /* 0x54 SM|ADR - INV */ /* (basemode STWBR) */
            if ((MODES & BASEBIT) == 0)             /* see if nonbased */
                goto inv;                           /* invalid instruction in nonbased mode */
            if (FC != 0) {                          /* word address only */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "ADDRSPEC8 OP %04x addr %08x\n", OP, addr);
                goto newpsd;                        /* go execute the trap now */
            }
            dest = BR[reg];                         /* save the BR to memory */
            break;

        case 0x58>>2:       /* 0x58 SB|ADR - INV */ /* (basemode SUABR and LABR) */
            if ((MODES & BASEBIT) == 0)             /* see if nonbased */
                goto inv;                           /* invalid instruction in nonbased mode */
            if ((FC & 4) == 0) {                    /* see if SUABR F=0 0x5800 */
                dest = BR[reg] - addr;              /* subtract addr from the BR and store back to BR */
            } else {                                /* LABR if F=1  0x5808 */
                dest = addr;                        /* addr goes to specified BR */
            }
            break;

        case 0x5C>>2:       /* 0x5C RM|ADR - INV */  /* (basemode LWBR and BSUBM) */
            if ((MODES & BASEBIT) == 0)             /* see if nonbased */
                goto inv;                           /* invalid instruction in nonbased mode */
            if ((FC & 3) != 0) {                    /* word address only */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "ADDRSPEC9 OP %04x addr %08x\n", OP, addr);
                goto newpsd;                        /* go execute the trap now */
            }
            if ((FC & 0x4) == 0) {                  /* this is a LWBR 0x5C00 instruction */
                BR[reg] = (uint32)source;           /* load memory location into BR */
            } else
            {                                       /* this is a CALLM/BSUBM instruction */
                /* if Rd field is 0 (reg is b6-8), this is a BSUBM instruction */
                /* otherwise it is a CALLM instruction (Rd != 0) */
                if (reg == 0) {
                    /* BSUBM instruction */
                    uint32 cfp = BR[2];             /* get dword bounded frame pointer from BR2 */

                    if ((BR[2] & 0x7) != 0)  {
                        /* Fault, must be dw bounded address */
                        TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                        goto newpsd;                /* go execute the trap now */
                    }

                    temp = (PSD1+4) & 0x01fffffe;   /* save AEXP bit and PC from PSD1 into frame */
                    if ((TRAPME = Mem_write(cfp, &temp))) { /* Save the PSD into memory */
                        goto newpsd;                /* memory write error or map fault */
                    }

                    temp = 0x80000000;              /* show frame created by BSUBM instr */
                    if ((TRAPME = Mem_write(cfp+4, &temp))) { /* Save zero into memory */
                        goto newpsd;                /* memory write error or map fault */
                    }

                    temp = addr & 0xfffffe;         /* CALL memory address */
                    if ((temp & 0x3) != 0) {        /* check for word aligned */
                        /* Fault, must be word bounded address */
                        TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                        goto newpsd;                /* go execute the trap now */
                    }

                    if ((TRAPME = Mem_read(temp, &addr)))   /* get the word from memory */
                        goto newpsd;                /* memory read error or map fault */

                    BR[1] = addr;                   /* effective address contents to BR 1 */
                    /* keep bits 0-7 from old PSD */ 
                    PSD1 = ((PSD1 & 0xff000000) | (BR[1] & 0x01fffffe)); /* New PSD address */
                    BR[3] = GPR[0];                 /* GPR[0] to BR[3] (AP) */
                    BR[0] = cfp;                    /* set current frame pointer into BR[0] */
                    i_flags |= BT;                  /* we changed the PC, so no PC update */
                } else {
                    /* CALLM instruction */

                    /* get frame pointer from BR2 - 16 words & make it a dword addr */
                    uint32 cfp = ((BR[2]-0x40) & 0x00fffff8);

                    /* if cfp and cfp+15w are in different maps, then addr exception error */
                    if ((cfp & 0xffe000) != ((cfp+0x3f) & 0xffe000)) {
                        TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                        goto newpsd;                /* go execute the trap now */
                    }

                    temp = (PSD1+4) & 0x01fffffe;   /* save AEXP bit and PC from PSD1 in to frame */
                    if ((TRAPME = Mem_write(cfp, &temp))) { /* Save the PSD into memory */
                        goto newpsd;                /* memory write error or map fault */
                    }

                    temp = 0x00000000;              /* show frame created by CALL instr */
                    if ((TRAPME = Mem_write(cfp+4, &temp))) { /* Save zero into memory */
                        goto newpsd;                /* memory write error or map fault */
                    }

                    /* save the BRs 0-7 on stack */
                    for (ix=0; ix<8; ix++) {
                        if ((TRAPME = Mem_write(cfp+(4*ix)+8, &BR[ix]))) { /* Save into memory */
                            goto newpsd;            /* memory write error or map fault */
                        }
                    }

                    /* save GPRs 2-7 on stack */
                    for (ix=2; ix<8; ix++) {
                        if ((TRAPME = Mem_write(cfp+(4*ix)+32, &GPR[ix]))) { /* Save into memory */
                            goto newpsd;            /* memory write error or map fault */
                        }
                    }

                    temp = addr & 0xfffffe;         /* CALL memory address */
                    if ((temp & 0x3) != 0) {        /* check for word aligned */
                        /* Fault, must be word bounded address */
                        TRAPME = ADDRSPEC_TRAP;     /* bad address, error */
                        goto newpsd;                /* go execute the trap now */
                    }

                    if ((TRAPME = Mem_read(temp, &addr)))   /* get the word from memory */
                        goto newpsd;                /* memory read error or map fault */

                    BR[1] = addr;                   /* effective address contents to BR 1 */
                    /* keep bits 0-6 from old PSD */ 
                    PSD1 = (PSD1 & 0xff000000) | ((BR[1]) & 0x01fffffe); /* New PSD address */
                    BR[3] = GPR[reg];               /* Rd to BR 3 (AP) */
                    BR[0] = cfp;                    /* set current frame pointer into BR[0] */
                    BR[2] = cfp;                    /* set current frame pointer into BR[2] */
                    i_flags |= BT;                  /* we changed the PC, so no PC update */
                }
            }
            break;

        case 0x60>>2:       /* 0x60 HLF - INV */    /* NOR Rd,Rs */
            if ((MODES & BASEBIT)) {                /* only for nonbased mode */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
            }
            if (opr & 0xf) {                        /* any subop not zero is error */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
            }
            /* exponent must not be zero or all 1's */
            /* normalize the value Rd in GPR[reg] and put exponent into Rs GPR[sreg] */
            temp = s_nor(GPR[reg], &GPR[sreg]);
            sim_debug(DEBUG_DETAIL, &cpu_dev,
                "NOR GPR[%d] %08x result %08x exp %02x\n",
                reg, GPR[reg], temp, GPR[sreg]);
            GPR[reg] = temp;
            break;

        case 0x64>>2:       /* 0x64 SD|HLF - INV */ /* NORD */
            if ((MODES & BASEBIT)) {                /* only for nonbased mode */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
            }
            if (reg & 1) {                          /* see if odd reg specified */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            if (opr & 0xf) {                        /* any subop not zero is error */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
            }
            /* shift until upper 5 bits are neither 0 or all 1's */
            /* merge the GPR[reg] & GPR[reg+1] into a 64bit value */
            td = (((t_uint64)GPR[reg]) << 32) | ((t_uint64)GPR[reg+1]);
            /* normalize the value Rd in GPR[reg] and put exponent into Rs GPR[sreg] */
            dest = s_nord(td, &GPR[sreg]);
            sim_debug(DEBUG_DETAIL, &cpu_dev,
                "NORD GPR[%d] %08x %08x result %016llx exp %02x\n",
                reg, GPR[reg], GPR[reg+1], dest, GPR[sreg]);
            GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
            GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
            break;

        case 0x68>>2:       /* 0x68 HLF - INV */    /* non basemode SCZ */
            if (MODES & BASEBIT) 
                goto inv;                           /* invalid instruction */
            if (opr & 0xf) {                        /* any subop not zero is error */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
            }
            goto sacz;                              /* use basemode sacz instruction */

        case 0x6C>>2:       /* 0x6C HLF - INV */    /* non basemode SRA & SLA */
            if (MODES & BASEBIT) 
                goto inv;                           /* invalid instruction */
            bc = opr & 0x1f;                        /* get bit shift count */
            temp = GPR[reg];                        /* get reg value to shift */
            t = temp & FSIGN;                       /* sign value */
            if (opr & 0x0040) {                     /* is this SLA */
                ovr = 0;                            /* set ovr off */
                for (ix=0; ix<bc; ix++) {
                    temp <<= 1;                     /* shift bit into sign position */
                    if ((temp & FSIGN) ^ t)         /* see if sign bit changed */
                        ovr = 1;                    /* set arithmetic exception flag */
                }
                temp &= ~BIT0;                      /* clear sign bit */
                temp |= t;                          /* restore original sign bit */
                GPR[reg] = temp;                    /* save the new value */
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                if (ovr)
                    PSD1 |= BIT1;                   /* CC1 in PSD */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (ovr && (MODES & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                    goto newpsd;                    /* go execute the trap now */
                }
            } else {                                /* this is a SRA */
                for (ix=0; ix<bc; ix++) {
                    temp >>= 1;                     /* shift bit 0 right one bit */
                    temp |= t;                      /* restore original sign bit */
                }
                GPR[reg] = temp;                    /* save the new value */
            }
            break;

        case 0x70>>2:       /* 0x70 SD|HLF - INV */ /* non-basemode SRL & SLL */
            if (MODES & BASEBIT) 
                goto inv;                           /* invalid instruction in basemode */
            bc = opr & 0x1f;                        /* get bit shift count */
            if (opr & 0x0040)                       /* is this SLL, bit 9 set */
                GPR[reg] <<= bc;                    /* shift left #bits */
            else
                GPR[reg] >>= bc;                    /* shift right #bits */
            break;

        case 0x74>>2:       /* 0x74 SD|HLF - INV */ /* non-basemode SRC & SLC */
            if (MODES & BASEBIT) 
                goto inv;                           /* invalid instruction in basemode */
            bc = opr & 0x1f;                        /* get bit shift count */
            temp = GPR[reg];                        /* get reg value to shift */
            if (opr & 0x0040) {                     /* is this SLC, bit 9 set */
                for (ix=0; ix<bc; ix++) {
                    t = temp & BIT0;                /* get sign bit status */
                    temp <<= 1;                     /* shift the bit out */
                    if (t)
                        temp |= 1;                  /* the sign bit status */
                }
            } else {                                /* this is SRC, bit 9 not set */
                for (ix=0; ix<bc; ix++) {
                    t = temp & 1;                   /* get bit 31 status */
                    temp >>= 1;                     /* shift the bit out */
                    if (t)
                        temp |= BIT0;               /* put in new sign bit */
                }
            }
            GPR[reg] = temp;                        /* shift result */
            break;

        case 0x78>>2:       /* 0x78 HLF - INV */    /* non-basemode SRAD & SLAD */
            if (MODES & BASEBIT)                    /* Base mode? */
                goto inv;                           /* invalid instruction in basemode */
            if (reg & 1) {                          /* see if odd reg specified */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            bc = opr & 0x1f;                        /* get bit shift count */
            dest = (t_uint64)GPR[reg+1];            /* get low order reg value */
            dest |= (((t_uint64)GPR[reg]) << 32);   /* insert upper reg value */
            source = dest & DMSIGN;                 /* 64 bit sign value */
            if (opr & 0x0040) {                     /* is this SLAD */
                ovr = 0;                            /* set ovr off */
                for (ix=0; ix<bc; ix++) {
                    dest <<= 1;                     /* shift bit into sign position */
                    if ((dest & DMSIGN) ^ source)   /* see if sign bit changed */
                        ovr = 1;                    /* set arithmetic exception flag */
                }
                dest &= ~DMSIGN;                    /* clear sign bit */
                dest |= source;                     /* restore original sign bit */
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                if (ovr)
                    PSD1 |= BIT1;                   /* CC1 in PSD */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (ovr && (MODES & AEXPBIT)) {
                    TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                    goto newpsd;                    /* go execute the trap now */
                }
            } else {                                /* this is a SRAD */
                for (ix=0; ix<bc; ix++) {
                    dest >>= 1;                     /* shift bit 0 right one bit */
                    dest |= source;                 /* restore original sign bit */
                }
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
            }
            break;

        case 0x7C>>2:       /* 0x7C HLF - INV */    /* non-basemode SRLD & SLLD */
            if (MODES & BASEBIT) 
                goto inv;                           /* invalid instruction in basemode */
            if (reg & 1) {                          /* see if odd reg specified */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            dest = (t_uint64)GPR[reg+1];            /* get low order reg value */
            dest |= (((t_uint64)GPR[reg]) << 32);   /* insert upper reg value */
            bc = opr & 0x1f;                        /* get bit shift count */
            if (opr & 0x0040)                       /* is this SLL, bit 9 set */
                dest <<= bc;                        /* shift left #bits */
            else
                dest >>= bc;                        /* shift right #bits */
            GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
            GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
            break;

        case 0x80>>2:       /* 0x80 SD|ADR - SD|ADR */  /* LEAR */
            /* convert address to real physical address */
            TRAPME = RealAddr(addr, &temp, &t, MEM_RD);
            // diag allows any addr if mapped
            if (TRAPME != ALLOK) {
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "At LEAR with TRAPME %04x addr %08x\n", TRAPME, addr);
                goto newpsd;                        /* memory read error or map fault */
            }
            /* set access bit for mapped addresses */
            if ((CPU_MODEL >= MODEL_V6) && (MODES & MAPMODE)) {
                uint32 map, mix, nix, msdl, mpl, mmap;

                nix = (addr >> 13) & 0x7ff;         /* get 11 bit map value */
                /* check our access to the memory */
                switch (t & 0x0e) {
                case 0x0: case 0x2:
                    /* O/S or user has no read/execute access, do protection violation */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "LEAR readI protect error @ %06x prot %02x modes %08x page %04x\n",
                        addr, t, MODES, nix);
                    if (CPU_MODEL == MODEL_V9)
                        TRAPSTATUS |= BIT1;         /* set bit 1 of trap status */
                    else
                        TRAPSTATUS |= BIT12;        /* set bit 12 of trap status */
                    return MPVIOL;                  /* return memory protection violation */
                case 0x4: case 0x6: case 0x8: case 0xc: case 0xa: case 0xe:
                    /* O/S or user has read/execute access, no protection violation */
                    sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "LEAR readJ protect is ok @ %06x prot %02x modes %08x page %04x\n",
                        addr, t, MODES, nix);
                }
                /* we have read access, so go set the access bit in the map entry */
                mpl = SPAD[0xf3];                   /* get mpl from spad address */
                if (nix < BPIX) {
                    mix = nix;                      /* get map index in memory */
                    msdl = RMW(mpl+4);              /* get mpl entry for O/S */
                } else {
                    mix = nix-BPIX;                 /* get map index in memory */
                    msdl = RMW(mpl+CPIX+4);         /* get mpl entry for given CPIX */
                }
                mmap = RMH(msdl+(mix<<1));          /* map content from memory */      
                map = RMR((nix<<1));                /* read the map cache contents */
                if (((map & 0x800) == 0)) {         /* see if access bit is already on */
                    mmap |= 0x800;                  /* set the accessed bit in the map cache entry */
                    map |= 0x800;                   /* set the accessed bit in the memory map entry */
                    WMR((nix<<1), map);             /* store the map reg contents into cache */
                    TLB[nix] |= 0x0c000000;         /* set the accessed & hit bits in TLB too */
                    WMH(msdl+(mix<<1), mmap);       /* save modified memory map with access bit set */
                    sim_debug(DEBUG_EXP, &cpu_dev,
                        "LEAR Laddr %06x page %04x set access bit TLB %08x map %04x nmap %04x\n",
                        addr, nix, TLB[nix], map, mmap);
                }
            }

            /* OS code says F bit is not transferred, so just ignore it */
            /* DIAGS needs it, so put it back */
            if (FC & 4)                             /* see if F bit was set */
                temp |= 0x01000000;                 /* set bit 7 of address */
            dest = temp;                            /* put in dest to go out */
            break;

        case 0x84>>2:       /* 0x84 SD|RR|RNX|ADR - SD|RNX|ADR */ /* ANMx */
            td = dest & source;                     /* DO ANMX */
            CC = 0;
            switch(FC) {                            /* adjust for hw or bytes */
            case 4: case 5: case 6: case 7:         /* byte address */
                /* ANMB */
                td &= 0xff;                         /* mask out right most byte */
                dest &= 0xffffff00;                 /* make place for byte */
                if (td == 0)
                    CC |= CC4BIT;                   /* byte is zero, so CC4 */
                else
                    CC |= CC2BIT;                   /* then td > 0, so CC2 */
                break;
            case 1:                                 /* left halfword addr */
            case 3:                                 /* right halfword addr */
                /* ANMH */
                td &= RMASK;                        /* mask out right most 16 bits */
                dest &= LMASK;                      /* make place for halfword */
                if (td == 0)
                    CC |= CC4BIT;                   /* hw is zero, so CC4 */
                else
                    CC |= CC2BIT;                   /* then td > 0, so CC2 */
                break;
            case 0:                                 /* 32 bit word */
                /* ANMW */
                td &= D32RMASK;                     /* mask out right most 32 bits */
                dest = 0;                           /* make place for 64 bits */
                if (td == 0)
                    CC |= CC4BIT;                   /* word is zero, so CC4 */
                else
                if (td & 0x80000000)
                    CC |= CC3BIT;                   /* it is neg wd, so CC3  */
                else
                    CC |= CC2BIT;                   /* then td > 0, so CC2 */
                break;
            case 2:                                 /* 64 bit double */
                /* ANMD */
                dest = 0;                           /* make place for 64 bits */
                if (td == 0)
                    CC |= CC4BIT;                   /* dw is zero, so CC4 */
                else
                if (td & DMSIGN)
                    CC |= CC3BIT;                   /* it is neg dw, so CC3  */
                else
                    CC |= CC2BIT;                   /* then td > 0, so CC2 */
                break;
            }
            dest |= td;                             /* insert result into dest */
            if (FC != 2) {                          /* do not sign extend DW */
                if (dest & 0x80000000)              /* see if we need to sign extend */
                    dest |= D32LMASK;               /* force upper word to all ones */
            }
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's from PSD1 */
            PSD1 |= CC;                             /* update the CC's in the PSD */
            break;

        case 0x88>>2:       /* 0x88 SD|RR|RNX|ADR - SD|RNX|ADR */ /* ORMx */
            td = dest | source;                     /* DO ORMX */
meoa:       /* merge point for eor, and, or */
            CC = 0;
            switch(FC) {                            /* adjust for hw or bytes */
            case 4: case 5: case 6: case 7:         /* byte address */
                /* ORMB */
                td &= 0xff;                         /* mask out right most byte */
                dest &= 0xffffff00;                 /* make place for byte */
                dest |= td;                         /* insert result into dest */
                if (dest == 0)
                    CC |= CC4BIT;                   /* byte is zero, so CC4 */
                else
                if (dest & MSIGN) {
                    CC |= CC3BIT;                   /* assume negative */
                    dest |= D32LMASK;               /* force upper word to all ones */
                }
                else
                    CC |= CC2BIT;                   /* then td > 0, so CC2 */
                break;
            case 1:                                 /* left halfword addr */
            case 3:                                 /* right halfword addr */
                /* ORMH */
                td &= RMASK;                        /* mask out right most 16 bits */
                dest &= LMASK;                      /* make place for halfword */
                dest |= td;                         /* insert result into dest */
                if (dest == 0)
                    CC |= CC4BIT;                   /* byte is zero, so CC4 */
                else
                if (dest & MSIGN) {
                    CC |= CC3BIT;                   /* assume negative */
                    dest |= D32LMASK;               /* force upper word to all ones */
                }
                else
                    CC |= CC2BIT;                   /* then td > 0, so CC2 */
                break;
            case 0:                                 /* 32 bit word */
                /* ORMW */
                td &= D32RMASK;                     /* mask out right most 32 bits */
                dest = 0;                           /* make place for 64 bits */
                dest |= td;                         /* insert result into dest */
                if (dest == 0)
                    CC |= CC4BIT;                   /* byte is zero, so CC4 */
                else
                if (dest & MSIGN) {
                    CC |= CC3BIT;                   /* assume negative */
                    dest |= D32LMASK;               /* force upper word to all ones */
                }
                else
                    CC |= CC2BIT;                   /* then td > 0, so CC2 */
                break;
            case 2:                                 /* 64 bit double */
                /* ORMD */
                dest = 0;                           /* make place for 64 bits */
                dest |= td;                         /* insert result into dest */
                if (dest == 0)
                    CC |= CC4BIT;                   /* byte is zero, so CC4 */
                else
                if (dest & DMSIGN)
                    CC |= CC3BIT;                   /* assume negative */
                else
                    CC |= CC2BIT;                   /* then td > 0, so CC2 */
                break;
            }
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's from PSD1 */
            PSD1 |= CC;                             /* update the CC's in the PSD */
            break;

        case 0x8C>>2:       /* 0x8C  SD|RR|RNX|ADR - SD|RNX|ADR */  /* EOMx */
            /* must special handle because we are getting bit difference */
            /* for word, halfword, & byte zero the upper 32 bits of dest */
            /* Diags require CC's to be set on result value of byte, hw, wd, or dw */
            td = dest ^ source;                     /* DO EOMX */
            goto meoa;
            break;

        case 0x90>>2:       /* 0x90 SCC|RR|RM|ADR - RM|ADR */   /* CAMx */
            if (dbl == 0) {
                int32a = dest & D32RMASK;           /* mask out right most 32 bits */
                int32b = source & D32RMASK;         /* mask out right most 32 bits */
                int32c = int32a - int32b;           /* signed diff */
                td = int32c;
                if (int32a > int32b) dest = 1;
                else
                if (int32a == int32b) dest = 0;
                else dest = -1;
            } else {
                int64a = dest;                      /* mask out right most 32 bits */
                int64b = source;                    /* mask out right most 32 bits */
                int64c = int64a - int64b;           /* signed diff */
                td = int64c;
                if (int64a > int64b) dest = 1;
                else
                if (int64a == int64b) dest = 0;
                else dest = -1;
            }
            break;

        case 0x94>>2:       /* 0x94 RR|RM|ADR - RM|ADR */   /* CMMx */
            /* CMMD needs both regs to be masked with R4 */
            if (dbl) {
                /* we need to and both regs with R4 */
                t_uint64 nm = (((t_uint64)GPR[4]) << 32) | (((t_uint64)GPR[4]) & D32RMASK);
                td = dest;                          /* save dest */
                dest ^= source;
                dest &= nm;                         /* mask both regs with reg 4 contents */
            } else {
                td = dest;                          /* save dest */
                dest ^= source;                     /* <= 32 bits, so just do lower 32 bits */
                dest &= (((t_uint64)GPR[4]) & D32RMASK);    /* mask with reg 4 contents */
            }           
            CC = 0;
            if (dest == 0ll)
                CC |= CC4BIT;
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's from PSD1 */
            PSD1 |= CC;                             /* update the CC's in the PSD */
            break;

        case 0x98>>2:       /* 0x98 ADR - ADR */    /* SBM */
            if ((FC & 04) == 0)  {
                /* Fault, f-bit must be set for SBM instruction */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            if ((TRAPME = Mem_read(addr, &temp)))   /* get the word from memory */
                goto newpsd;                        /* memory read error or map fault */

            t = (PSD1 & 0x70000000) >> 1;           /* get old CC bits 1-3 into CCs 2-4*/
            /* use C bits and bits 6-8 (reg) to generate shift bit count */
            bc = ((FC & 3) << 3) | reg;             /* get # bits to shift right */
            bc = BIT0 >> bc;                        /* make a bit mask of bit number */
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's from PSD1 */
            if (temp & bc)                          /* test the bit in memory */
                t |= CC1BIT;                        /* set CC1 to the bit value */
            PSD1 |= t;                              /* update the CC's in the PSD */
            temp |= bc;                             /* set the bit in temp */
            if ((TRAPME = Mem_write(addr, &temp))) {  /* put word back into memory */
                goto newpsd;                        /* memory write error or map fault */
            }
            break;
                  
        case 0x9C>>2:       /* 0x9C ADR - ADR */    /* ZBM */
            if ((FC & 04) == 0)  {
                /* Fault, byte address not allowed */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            if ((TRAPME = Mem_read(addr, &temp)))   /* get the word from memory */
                goto newpsd;                        /* memory read error or map fault */

            t = (PSD1 & 0x70000000) >> 1;           /* get old CC bits 1-3 into CCs 2-4*/
            /* use C bits and bits 6-8 (reg) to generate shift bit count */
            bc = ((FC & 3) << 3) | reg;             /* get # bits to shift right */
            bc = BIT0 >> bc;                        /* make a bit mask of bit number */
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's from PSD1 */
            if (temp & bc)                          /* test the bit in memory */
                t |= CC1BIT;                        /* set CC1 to the bit value */
            PSD1 |= t;                              /* update the CC's in the PSD */
            temp &= ~bc;                            /* reset the bit in temp */
            if ((TRAPME = Mem_write(addr, &temp))) {  /* put word into memory */
                goto newpsd;                        /* memory write error or map fault */
            }
            break;

        case 0xA0>>2:       /* 0xA0 ADR - ADR */    /* ABM */
            if ((FC & 04) == 0)  {
                /* Fault, byte address not allowed */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            if ((TRAPME = Mem_read(addr, &temp)))   /* get the word from memory */
                goto newpsd;                        /* memory read error or map fault */

            /* use C bits and bits 6-8 (reg) to generate shift bit count */
            bc = ((FC & 3) << 3) | reg;             /* get # bits to shift right */
            bc = BIT0 >> bc;                        /* make a bit mask of bit number */
            t = (temp & FSIGN) != 0;                /* set flag for sign bit not set in temp value */
            t |= ((bc & FSIGN) != 0) ? 2 : 0;       /* ditto for the bit value */
            temp += bc;                             /* add the bit value to the reg */
            /* if both signs are neg and result sign is positive, overflow */
            /* if both signs are pos and result sign is negative, overflow */
            if ((t == 3 && (temp & FSIGN) == 0) ||
                (t == 0 && (temp & FSIGN) != 0)) {
                ovr = 1;                            /* we have an overflow */
            }
            set_CCs(temp, ovr);                     /* set the CC's, CC1 = ovr */
            if ((TRAPME = Mem_write(addr, &temp))) {    /* put word into memory */
                goto newpsd;                        /* memory write error or map fault */
            }
            /* the arithmetic exception will be handled */
            /* after instruction is completed */
            /* check for arithmetic exception trap enabled */
            if (ovr && (MODES & AEXPBIT)) {
                TRAPME = AEXPCEPT_TRAP;             /* set the trap type */
                goto newpsd;                        /* handle trap */
            }
            break;

        case 0xA4>>2:       /* 0xA4 ADR - ADR */    /* TBM */
            if ((FC & 04) == 0)  {
                /* Fault, byte address not allowed */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            if ((TRAPME = Mem_read(addr, &temp)))   /* get the word from memory */
                goto newpsd;                        /* memory read error or map fault */

            t = (PSD1 & 0x70000000) >> 1;           /* get old CC bits 1-3 into CCs 2-4*/
            /* use C bits and bits 6-8 (reg) to generate shift bit count */
            bc = ((FC & 3) << 3) | reg;             /* get # bits to shift right */
            bc = BIT0 >> bc;                        /* make a bit mask of bit number */
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's from PSD1 */
            if (temp & bc)                          /* test the bit in memory */
                t |= CC1BIT;                        /* set CC1 to the bit value */
            PSD1 |= t;                              /* update the CC's in the PSD */
            break;

        case 0xA8>>2:       /* 0xA8 RM|ADR - RM|ADR */ /* EXM */
            if ((FC & 04) != 0 || FC == 2) {        /* can not be byte or doubleword */
                /* Fault */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            if ((TRAPME = Mem_read(addr, &temp))) { /* get the word from memory */
                if (CPU_MODEL == MODEL_V9)          /* V9 wants bit0 set in pfault */
                    if (TRAPME == DMDPG)            /* demand page request */
                        pfault |= 0x80000000;       /* set instruction fetch paging error */
                goto newpsd;                        /* memory read error or map fault */
            }

            IR = temp;                              /* get instruction from memory */
            if (FC == 3)                            /* see if right halfword specified */
                IR <<= 16;                          /* move over the HW instruction */
#ifdef DIAG_SAYS_OK_TO_EXECUTE_ANOTHER_EXECUTE
            if ((IR & 0xFC7F0000) == 0xC8070000 ||  /* No EXR target */
                (IR & 0xFF800000) == 0xA8000000 ||  /* No EXM target */
                (IR & 0xFC000000) == 0x80000000) {
#else
            /* 32/67 diag says execute of execute is OK */
            if ((IR & 0xFC000000) == 0x80000000) {
#endif
                /* Fault, attempt to execute another EXR, EXRR, EXM, or LEAR  */
                goto inv;                           /* invalid instruction */
            }
            EXM_EXR = 4;                            /* set PC increment for EXM */

            OPSD1 &= 0x87FFFFFE;                    /* clear the old PSD CC's */
            OPSD1 |= PSD1 & 0x78000000;             /* update the CC's in the old PSD */
            /* TODO Update other history information for this instruction */
            if (hst_lnt) {
                hst[hst_p].opsd1 = OPSD1;           /* update the CC in opsd1 */
                hst[hst_p].npsd1 = PSD1;            /* save new psd1 */
                hst[hst_p].npsd2 = PSD2;            /* save new psd2 */
                hst[hst_p].modes = MODES;           /* save current mode bits */
                hst[hst_p].modes |= (CPUSTATUS & BIT24);    /* save blocking mode bit */
                for (ix=0; ix<8; ix++) {
                    hst[hst_p].reg[ix] = GPR[ix];   /* save reg */
                    hst[hst_p].reg[ix+8] = BR[ix];  /* save breg */
                }
            }

            /* DEBUG_INST support code */
            OPSD1 &= 0x87FFFFFE;                    /* clear the old CC's */
            OPSD1 |= PSD1 & 0x78000000;             /* update the CC's in the PSD */
            /* output mapped/unmapped */
            if (MODES & BASEBIT)
                BM = 'B';
            else
                BM = 'N';
            if (MODES & MAPMODE)
                MM = 'M';
            else
                MM = 'U';
            if (CPUSTATUS & BIT24)
                BK = 'B';
            else
                BK = 'U';
            sim_debug(DEBUG_INST, &cpu_dev, "%c%c%c %.8x %.8x %.8x ",
                BM, MM, BK, OPSD1, PSD2, OIR);
            if (cpu_dev.dctrl & DEBUG_INST)
                fprint_inst(sim_deb, OIR, 0);       /* display instruction */
            sim_debug(DEBUG_INST, &cpu_dev,
                "\n\tR0=%.8x R1=%.8x R2=%.8x R3=%.8x", GPR[0], GPR[1], GPR[2], GPR[3]);
            sim_debug(DEBUG_INST, &cpu_dev,
                " R4=%.8x R5=%.8x R6=%.8x R7=%.8x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
            goto exec;                              /* go execute the instruction */
            break;
 
        case 0xAC>>2:       /* 0xAC SCC|SD|RM|ADR - SCC|SD|RM|ADR */ /* Lx */
            dest = source;                          /* set value to load into reg */
            break;

        case 0xB0>>2:       /* 0xB0 SCC|SD|RM|ADR - SCC|SD|RM|ADR */ /* LMx */
            /* LMD needs both regs to be masked with R4 */
            if (dbl) {
                /* we need to and both regs with R4 */
                t_uint64 nm = (((t_uint64)GPR[4]) << 32) | (((t_uint64)GPR[4]) & D32RMASK);
                dest = source & nm;                 /* mask both regs with reg 4 contents */
            } else {
                dest = source;                      /* <= 32 bits, so just do lower 32 bits */
                dest &= (((t_uint64)GPR[4]) & D32RMASK);    /* mask with reg 4 contents */
                if (dest & 0x80000000)              /* see if we need to sign extend */
                    dest |= D32LMASK;               /* force upper word to all ones */
            }           
            break;
 
        case 0xB4>>2:       /* 0xB4 SCC|SD|RM|ADR - SCC|SD|RM|ADR */ /* LNx */
            dest = NEGATE32(source);                /* set the value to load into reg */
            td = dest;
            if (dest != 0 && (dest == source || dest == 0x80000000))
                ovr = 1;                            /* set arithmetic exception status */
            if (FC != 2) {                          /* do not sign extend DW */
                if (dest & 0x80000000)              /* see if we need to sign extend */
                    dest |= D32LMASK;               /* force upper word to all ones */
            }
            /* the arithmetic exception will be handled */
            /* after instruction is completed */
            /* check for arithmetic exception trap enabled */
            if (dest != 0 && ovr && (MODES & AEXPBIT)) {
                TRAPME = AEXPCEPT_TRAP;             /* set the trap type */
            }
            break;

        case 0xBC>>2:       /* 0xBC SD|RR|RM|ADR - SD|RR|RM|ADR */ /* SUMx */
            source = NEGATE32(source);
            /* Fall through */

        case 0xB8>>2:       /* 0xB8 SD|RR|RM|ADR - SD|RR|RM|ADR */ /* ADMx */
            ovr = 0;
            CC = 0;
            /* DIAG fixs */
            if (dbl == 0) {
                source &= D32RMASK;                 /* just 32 bits */
                dest &= D32RMASK;                   /* just 32 bits */
                t = (source & MSIGN) != 0;
                t |= ((dest & MSIGN) != 0) ? 2 : 0;
                td = dest + source;                 /* DO ADMx*/
                td &= D32RMASK;                     /* mask out right most 32 bits */
                dest = 0;                           /* make place for 64 bits */
                dest |= td;                         /* insert 32 bit result into dest */
                /* if both signs are neg and result sign is positive, overflow */
                /* if both signs are pos and result sign is negative, overflow */
                if (((t == 3) && ((dest & MSIGN) == 0)) || 
                    ((t == 0) && ((dest & MSIGN) != 0)))
                    ovr = 1;
                if ((td == 0) && ((source & MSIGN) == MSIGN) && ovr)
                    ovr = 0;                        /* Diags want 0 and no ovr on MSIGN - MSIGN */
                if (dest & MSIGN)
                    dest = (D32LMASK | dest);       /* sign extend */
                else
                    dest = (D32RMASK & dest);       /* zero fill */
                if (td == 0)
                    CC |= CC4BIT;                   /* word is zero, so CC4 */
                else
                if (td & 0x80000000)
                    CC |= CC3BIT;                   /* it is neg wd, so CC3  */
                else
                    CC |= CC2BIT;                   /* then td > 0, so CC2 */
            } else {
                /* ADMD */
                t = (source & DMSIGN) != 0;
                t |= ((dest & DMSIGN) != 0) ? 2 : 0;
                td = dest + source;                 /* get sum */
                dest = td;                          /* insert 64 bit result into dest */
                /* if both signs are neg and result sign is positive, overflow */
                /* if both signs are pos and result sign is negative, overflow */
                if (((t == 3) && ((dest & DMSIGN) == 0)) || 
                    ((t == 0) && ((dest & DMSIGN) != 0)))
                    ovr = 1;
                if (td == 0)
                    CC |= CC4BIT;                   /* word is zero, so CC4 */
                else
                if (td & DMSIGN)
                    CC |= CC3BIT;                   /* it is neg wd, so CC3  */
                else
                    CC |= CC2BIT;                   /* then td > 0, so CC2 */
            }
            if (ovr)
                CC |= CC1BIT;                       /* set overflow CC */
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's from PSD1 */
            PSD1 |= CC;                             /* update the CC's in the PSD */

            /* the arithmetic exception will be handled */
            /* after instruction is completed */
            /* check for arithmetic exception trap enabled */
            if (ovr && (MODES & AEXPBIT)) {
                TRAPME = AEXPCEPT_TRAP;             /* set the trap type */
            }
            break;

        case 0xC0>>2:       /* 0xC0 SCC|SD|RM|ADR - SCC|SD|RM|ADR */ /* MPMx */
            if (reg & 1) {                          /* see if odd reg specified */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            if (FC == 2) {                          /* must not be double word adddress */
                TRAPME = ADDRSPEC_TRAP;             /* bad address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            td = dest;
            dest = GPR[reg+1];                      /* get low order reg value */
            if (dest & MSIGN)
                dest = (D32LMASK | dest);           /* sign extend */
            dest = (t_uint64)((t_int64)dest * (t_int64)source);
            dbl = 1;
            break;

        case 0xC4>>2:       /* 0xC4 RM|ADR - RM|ADR */  /* DVMx */
            if (reg & 1) {                          /* see if odd reg specified */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            if (FC == 2) {                          /* must not be double word adddress */
                TRAPME = ADDRSPEC_TRAP;             /* bad address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            if (source == 0)
                goto doovr;                         /* we have div by zero */
            dest = (((t_uint64)GPR[reg]) << 32);    /* insert upper reg value */
            dest |= (t_uint64)GPR[reg+1];           /* get low order reg value */
            td = ((t_int64)dest % (t_int64)source); /* remainder */
            if (((td & DMSIGN) ^ (dest & DMSIGN)) != 0) /* Fix sign if needed */
                td = NEGATE32(td);                  /* dividend and remainder must be same sign */
            dest = (t_int64)dest / (t_int64)source; /* now do the divide */
            int64a = dest;
            if (int64a < 0)
                int64a = -int64a;
            if (int64a > 0x7fffffff)                /* if more than 31 bits, we have an error */
                goto doovr;
            if (((dest & D32LMASK) != 0 && (dest & D32LMASK) != D32LMASK) ||
                (((dest & D32LMASK) == D32LMASK) && ((dest & D32RMASK) == 0))) {  /* test for overflow */
doovr:
                dest = (((t_uint64)GPR[reg]) << 32);/* insert upper reg value */
                dest |= (t_uint64)GPR[reg+1];       /* get low order reg value */
                ovr = 1;                            /* the quotient exceeds 31 bit, overflow */
                /* the original regs must be returned unchanged if aexp */
                CC = CC1BIT;                        /* set ovr CC bit */
                if (dest == 0)
                    CC |= CC4BIT;                   /* dw is zero, so CC4 */
                else
                if (dest & DMSIGN)
                    CC |= CC3BIT;                   /* it is neg dw, so CC3  */
                else
                    CC |= CC2BIT;                   /* then dest > 0, so CC2 */
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's from PSD1 */
                PSD1 |= CC;                         /* update the CC's in the PSD */
                /* the arithmetic exception will be handled */
                /* after instruction is completed */
                /* check for arithmetic exception trap enabled */
                if (MODES & AEXPBIT)
                    TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
            } else {
                GPR[reg] = (uint32)(td & FMASK);    /* reg gets remainder, reg+1 quotient */
                GPR[reg+1] = (uint32)(dest & FMASK);    /* store quotient in reg+1 */
                set_CCs(GPR[reg+1], ovr);           /* set the CC's, CC1 = ovr */
            }
            break;

        case 0xC8>>2:       /* 0xC8 IMM - IMM */    /* Immedate */
            temp = GPR[reg];                        /* get reg contents */
            addr = IR & RMASK;                      /* sign extend 16 bit imm value from IR */
            if (addr & 0x8000)                      /* negative */
                addr |= LMASK;                      /* extend sign */

            switch(opr & 0xF) {                     /* switch on aug code */
            case 0x0:       /* LI */            /* SCC | SD */
                GPR[reg] = addr;                    /* put immediate value into reg */
                set_CCs(addr, ovr);                 /* set the CC's, CC1 = ovr */
                break;

            case 0x2:       /* SUI */
                addr = NEGATE32(addr);              /* just make value a negative add */
                /* drop through */
            case 0x1:       /* ADI */
               t = (temp & FSIGN) != 0;            /* set flag for sign bit not set in reg value */
               t |= ((addr & FSIGN) != 0) ? 2 : 0; /* ditto for the extended immediate value */
               temp = temp + addr;                 /* now add the numbers */
               /* if both signs are neg and result sign is positive, overflow */
               /* if both signs are pos and result sign is negative, overflow */
               if ((t == 3 && (temp & FSIGN) == 0) ||
                    (t == 0 && (temp & FSIGN) != 0))
                    ovr = 1;                        /* we have an overflow */
               GPR[reg] = temp;                    /* save the result */
               set_CCs(temp, ovr);                 /* set the CC's, CC1 = ovr */
               /* the arithmetic exception will be handled */
               /* after instruction is completed */
               /* check for arithmetic exception trap enabled */
               if (ovr && (MODES & AEXPBIT)) {
                   TRAPME = AEXPCEPT_TRAP;         /* set the trap type */
                   goto newpsd;                    /* go execute the trap now */
                }
                break;

            case 0x3:       /* MPI */
                if (reg & 1) {                      /* see if odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                /* change immediate value into a 64 bit value */
                source = ((t_uint64)(addr & FMASK)) | ((addr & FSIGN) ? D32LMASK : 0);
                temp = GPR[reg+1];                  /* get reg multiplier */
                dest = ((t_uint64)(temp & FMASK)) | ((temp & FSIGN) ? D32LMASK : 0);
                dest = dest * source;               /* do the multiply */
                i_flags |= (SD|SCC);                /* save regs and set CC's */
                dbl = 1;                            /* double reg save */
                break;

            case 0x4:       /* DVI */
                if (reg & 1) {                      /* see if odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                /* change immediate value into a 64 bit value */
                source = ((t_uint64)(addr & FMASK)) | ((addr & FSIGN) ? D32LMASK : 0);
                if (source == 0) {
                    goto doovr2;
                }
                dest = (((t_uint64)GPR[reg]) << 32);    /* get upper reg value */
                dest |= (t_uint64)GPR[reg+1];       /* insert low order reg value */
                td = ((t_int64)dest % (t_int64)source); /* remainder */
                /* fix double reg if neg remainder */
                if (((td & DMSIGN) ^ (dest & DMSIGN)) != 0) /* Fix sign if needed */
                    td = NEGATE32(td);              /* dividend and remainder must be same sign */
                dest = (t_int64)dest / (t_int64)source; /* now do the divide */
                int64a = dest;
                if (int64a < 0)
                    int64a = -int64a;
                if (int64a > 0x7fffffff)            /* if more than 31 bits, we have an error */
                    goto doovr2;
                if ((dest & D32LMASK) != 0 && (dest & D32LMASK) != D32LMASK) {  /* test for overflow */
doovr2:
                    dest = (((t_uint64)GPR[reg]) << 32);    /* get upper reg value */
                    dest |= (t_uint64)GPR[reg+1];   /* insert low order reg value */
                    ovr = 1;                        /* the quotient exceeds 31 bit, overflow */
                    /* the arithmetic exception will be handled */
                    /* after instruction is completed */
                    /* check for arithmetic exception trap enabled */
                    if (MODES & AEXPBIT)
                        TRAPME = AEXPCEPT_TRAP;     /* set the trap type */
                    /* the original regs must be returned unchanged if aexp */
                    /* put reg values back in dest for CC test */
                    CC = CC1BIT;                    /* set ovr CC bit */
                    if (dest == 0)
                        CC |= CC4BIT;               /* dw is zero, so CC4 */
                    else
                    if (dest & DMSIGN)
                        CC |= CC3BIT;               /* it is neg dw, so CC3  */
                    else
                        CC |= CC2BIT;               /* then dest > 0, so CC2 */
                    PSD1 &= 0x87FFFFFE;             /* clear the old CC's from PSD1 */
                    PSD1 |= CC;                     /* update the CC's in the PSD */
                } else {
                    GPR[reg] = (uint32)(td & FMASK);    /* reg gets remainder, reg+1 quotient */
                    GPR[reg+1] = (uint32)(dest & FMASK);    /* store quotient in reg+1 */
                    set_CCs(GPR[reg+1], ovr);       /* set the CC's, CC1 = ovr */
                }
                break;

            case 0x5:       /* CI */    /* SCC */
                temp = ((int)temp - (int)addr);     /* subtract imm value from reg value */
                set_CCs(temp, ovr);                 /* set the CC's, CC1 = ovr */
                break;

/* SVC instruction format C806 */
/* |-------+-------+-------+-------+-------+-------+-------+-------| */
/* |0 0 0 0 0 0|0 0 0|0 1 1|1 1 1 1|1 1 1 1|2 2 2 2 2 2 2 2 2 2 3 3| */
/* |0 1 2 3 4 5|6 7 8|8 0 1|2 3 4 5|6 7 8 9|0 1 2 3 4 5 6 7 8 9 0 1| */
/* |  Op Code  | N/U | N/U |  Aug  |SVC num|    SVC Call Number    | */
/* |1 1 0 0 1 0|0 0 0|0 0 0|0 1 1 0|x x x x|x x x x x x x x x x x x| */
/* |-------+-------+-------+-------+-------+-------+-------+-------| */
/* */
            case 0x6:       /* SVC  none - none */  /* Supervisor Call Trap */
            {
#ifdef MPXTEST  /* set to 1 for traceme in MPX to work */
                /* get current MPX task name */
                int j;
                char n[9];
                uint32 sq59 = M[0x930>>2];          /* get C.SQ59 headcell */
                uint32 dqe = M[0x8e8>>2];           /* get DQE of current task */

                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "SVC start sq59 %04x dqe %04x\n",sq59, dqe);
                if (sq59 != 0x930)
                    goto skipdqe2;                  /* not running on mpx, skip */
                for (j=0; j<8; j++) {               /* get the task name */
                    n[j] = (M[((dqe+0x18)>>2)+(j/4)] >> ((3-(j&7))*8)) & 0xff;
                    if (n[j] == 0)
                        n[j] = 0x20;
                }
                n[8] = 0;
skipdqe2:
#endif
                int32c = CPUSTATUS;                 /* keep for retain blocking state */
                addr = SPAD[0xf0];                  /* get trap table memory address from SPAD (def 80) */
                int32a = addr;
                if (addr == 0 || ((addr&MASK24) == MASK24)) {  /* see if secondary vector table set up */
                    TRAPME = ADDRSPEC_TRAP;         /* Not setup, error */
                    goto newpsd;                    /* program error */
                }
                addr = addr + (0x06 << 2);          /* addr has mem addr of SVC trap vector (def 98) */
                temp = M[addr >> 2];                /* get the secondary trap table address from memory */
                if (temp == 0 || ((temp&MASK24) == MASK24)) {  /* see if ICB set up */
                    TRAPME = ADDRSPEC_TRAP;         /* Not setup, error */
                    goto newpsd;                    /* program error */
                }
                temp2 = ((IR>>12) & 0x0f) << 2;     /* get SVC index from IR */
                t = M[(temp+temp2)>>2];             /* get secondary trap vector address ICB address */
                if (temp == 0 || ((temp&MASK24) == MASK24)) {  /* see if ICB set up */
                    TRAPME = ADDRSPEC_TRAP;         /* Not setup, error */
                    goto newpsd;                    /* program error */
                }
                bc = PSD2 & 0x3ff8;                 /* get copy of cpix */
                M[t>>2] = (PSD1+4) & 0xfffffffe;    /* store PSD 1 + 1W to point to next instruction */
                M[(t>>2)+1] = PSD2;                 /* store PSD 2 */
                PSD1 = M[(t>>2)+2];                 /* get new PSD 1 */
                PSD2 = (M[(t>>2)+3] & ~0x3ff8) | bc;    /* get new PSD 2 w/old cpix */
                M[(t>>2)+4] = IR&0xFFF;             /* store call number */
#ifdef MPXTEST  /* set to 1 for traceme to work */
                if (sq59 == 0x930) {                /* running on MPX? */
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "SVC %x,%x @ %.8x PSD %.8x %.8x SPAD PSD2 %x C.CURR %x LMN %8s\n",
                    temp2>>2, IR&0xFFF, OPSD1, PSD1, PSD2, SPAD[0xf5], dqe, n);
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "   R0=%.8x R1=%.8x R2=%.8x R3=%.8x\n", GPR[0], GPR[1], GPR[2], GPR[3]);
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "   R4=%.8x R5=%.8x R6=%.8x R7=%.8x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
                }
#if DYNAMIC_DEBUG
    if (((temp2>>2) == 1) && ((IR&0xFFF) == 0x03f)) {   /* SVC 1,3f */
        cpu_dev.dctrl |= DEBUG_INST;                /* start instruction trace */
    }
#endif
#else
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "SVC %x,%x @ %.8x PSD %.8x %.8x SPADF5 PSD2 %x CPUSTATUS %08x\n",
                    temp2>>2, IR&0xFFF, OPSD1, PSD1, PSD2, SPAD[0xf5], CPUSTATUS);
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "   R0=%.8x R1=%.8x R2=%.8x R3=%.8x\n", GPR[0], GPR[1], GPR[2], GPR[3]);
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "   R4=%.8x R5=%.8x R6=%.8x R7=%.8x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
#endif
#if DYNAMIC_DEBUG
    if (((temp2>>2) == 0) && ((IR&0xFFF) == 0xb01)) {   /* SVC 0,VOMM,1 */
        cpu_dev.dctrl |= DEBUG_INST;                /* start instruction trace */
    }
#endif
#ifdef DO_DYNAMIC_DEBUG
    if (((temp2>>2) == 0) && ((IR&0xFFF) == 0x303)) {   /* SVC 0,TAMM,1 */
        if (GPR[3] == 0x3a000)
            cpu_dev.dctrl |= DEBUG_INST;            /* start instruction trace */
    }
#endif
#ifdef DO_DYNAMIC_DEBUG
    if (((temp2>>2) == 2) && ((IR&0xFFF) == 0x028)) {   /* SVC 2,28 H.VOMM,9 */
        if (cpu_dev.dctrl & DEBUG_INST)
            cpu_dev.dctrl &= ~DEBUG_INST;           /* stop instruction trace */
        else
            cpu_dev.dctrl |= DEBUG_INST;            /* start instruction trace */
    }
#endif
#ifdef DO_DYNAMIC_DEBUG
    if (((temp2>>2) == 0) && ((IR&0xFFF) == 0xa11)) {   /* SVC 0,REMM,17 */
        cpu_dev.dctrl |= DEBUG_INST;                /* start instruction trace */
    }
#endif
#ifdef DO_DYNAMIC_DEBUG
    if (((temp2>>2) == 0) && ((IR&0xFFF) == 0x910)) {   /* SVC 0,REXS,16 */
        cpu_dev.dctrl |= DEBUG_INST;                /* start instruction trace */
    }
    if (((temp2>>2) == 0) && ((IR&0xFFF) == 0x925)) {   /* SVC 0,REXS,40 */
        cpu_dev.dctrl |= DEBUG_INST;                /* start instruction trace */
    }
#endif
                /* set the mode bits and CCs from the new PSD */
                CC = PSD1 & 0x78000000;             /* extract bits 1-4 from PSD1 */
                MODES = PSD1 & 0x87000000;          /* extract bits 0, 5, 6, 7 from PSD 1 */
                CPUSTATUS &= ~0x87000000;           /* reset bits in CPUSTATUS */
                CPUSTATUS |= MODES;                 /* now insert into CPUSTATUS */

                /* set new map mode and interrupt blocking state in CPUSTATUS */
                if (PSD2 & MAPBIT) {
                    CPUSTATUS |= BIT8;              /* set bit 8 of cpu status */
                    MODES |= MAPMODE;               /* set mapped mode */
                } else {
                    CPUSTATUS &= ~BIT8;             /* reset bit 8 of cpu status */
/*TRY_01072022*/    MODES &= ~MAPMODE;              /* reset mapped mode */
                }

                /* set interrupt blocking state */
                if ((PSD2 & RETBBIT) == 0) {        /* is it retain blocking state */
                    if (PSD2 & SETBBIT) {           /* no, is it set blocking state */
                        CPUSTATUS |= BIT24;         /* yes, set blk state in cpu status bit 24 */
                        MODES |= BLKMODE;           /* set blocked mode */
                    } else {
                        CPUSTATUS &= ~BIT24;        /* no, reset blk state in cpu status bit 24 */
                        MODES &= ~BLKMODE;          /* reset blocked mode */
                        irq_pend = 1;               /* start scanning interrupts again */
#ifdef LEAVE_ACTIVE
                        if (irq_auto) {
/*AIR*/                     INTS[irq_auto] &= ~INTS_ACT;  /* deactivate specified int level */
/*AIR*/                     SPAD[irq_auto+0x80] &= ~SINT_ACT; /* deactivate in SPAD too */
                            sim_debug(DEBUG_IRQ, &cpu_dev,
                                "<|>IntX deactivate level %02x at SVC #%2x PSD1 %08x\n",
                                irq_auto, temp2, PSD1);
/*AIR*/                     irq_auto = 0;           /* show done processing in blocked mode */
                        }
#endif
                    }
                } else {
                    /* handle retain blocking state */
                    PSD2 &= ~RETMBIT;               /* turn off retain bit in PSD2 */
                    /* set new blocking state in PSD2 */
                    PSD2 &= ~(SETBBIT|RETBBIT);     /* clear bit 48 & 49 to be unblocked */
                    MODES &= ~(BLKMODE|RETBLKM);    /* reset blocked & retain mode bits */
                    if (int32c & BIT24) {           /* see if old mode is blocked */
                        PSD2 |= SETBBIT;            /* set to blocked state */
                        MODES |= BLKMODE;           /* set blocked mode */
                    }
                }

                SPAD[0xf5] = PSD2;                  /* save the current PSD2 */
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
                TRAPME = 0;                         /* not to be processed as trap */
                goto newpsd;                        /* new psd loaded */
            }
            break;

            case 0x7:       /* EXR */
                IR = temp;                          /* get instruction to execute */
                /* if bit 30 set, instruction is in right hw, do EXRR */
                if (addr & 2)
                    IR <<= 16;                      /* move instruction to left HW */
#ifdef DIAG_SAYS_OK_TO_EXECUTE_ANOTHER_EXECUTE
                /* 32/67 diag says execute of execute is OK */
                if ((IR & 0xFC7F0000) == 0xC8070000 ||
                    (IR & 0xFF800000) == 0xA8000000) {
                    /* Fault, attempt to execute another EXR, EXRR, or EXM  */
                    goto inv;                       /* invalid instruction */
                }
#endif
                EXM_EXR = 4;                        /* set PC increment for EXR */
                OPSD1 &= 0x87FFFFFE;                /* clear the old CC's */
                OPSD1 |= PSD1 & 0x78000000;         /* update the CC's in the PSD */
                /* TODO Update other history information for this instruction */
                if (hst_lnt) {
                    hst[hst_p].opsd1 = OPSD1;       /* update the CC in opsd1 */
                    hst[hst_p].npsd1 = PSD1;        /* save new psd1 */
                    hst[hst_p].npsd2 = PSD2;        /* save new psd2 */
                    hst[hst_p].modes = MODES;       /* save current mode bits */
                    hst[hst_p].modes |= (CPUSTATUS & BIT24);    /* save blocking mode bit */
                    for (ix=0; ix<8; ix++) {
                        hst[hst_p].reg[ix] = GPR[ix];   /* save reg */
                        hst[hst_p].reg[ix+8] = BR[ix];  /* save breg */
                    }
                }
                /* DEBUG_INST support code */
                OPSD1 &= 0x87FFFFFE;                /* clear the old CC's */
                OPSD1 |= PSD1 & 0x78000000;         /* update the CC's in the PSD */
                /* output mapped/unmapped */
                if (MODES & BASEBIT)
                    BM = 'B';
                else
                    BM = 'N';
                if (MODES & MAPMODE)
                    MM = 'M';
                else
                    MM = 'U';
                if (CPUSTATUS & BIT24)
                    BK = 'B';
                else
                    BK = 'U';
                sim_debug(DEBUG_INST, &cpu_dev, "%c%c%c %.8x %.8x %.8x ",
                    BM, MM, BK, OPSD1, PSD2, OIR);
                if (cpu_dev.dctrl & DEBUG_INST) {
                    fprint_inst(sim_deb, OIR, 0);   /* display instruction */
                sim_debug(DEBUG_INST, &cpu_dev,
                    "\n\tR0=%.8x R1=%.8x R2=%.8x R3=%.8x", GPR[0], GPR[1], GPR[2], GPR[3]);
                sim_debug(DEBUG_INST, &cpu_dev,
                    " R4=%.8x R5=%.8x R6=%.8x R7=%.8x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
                if (MODES & BASEBIT) {
                    sim_debug(DEBUG_INST, &cpu_dev,
                        "\tB0=%.8x B1=%.8x B2=%.8x B3=%.8x", BR[0], BR[1], BR[2], BR[3]);
                    sim_debug(DEBUG_INST, &cpu_dev,
                        " B4=%.8x B5=%.8x B6=%.8x B7=%.8x\n", BR[4], BR[5], BR[6], BR[7]);
                }
                }
                goto exec;                          /* go execute the instruction */
                break;

            /* these instruction were never used by MPX, only diags */
            /* diags treat them as invalid halfword instructions */
            /* so set the HLF flag to get proper PC increment */
            case 0x8:                               /* SEM */
            case 0x9:                               /* LEM */
            case 0xA:                               /* CEMA */
            case 0xB:                               /* INV */
            case 0xC:                               /* INV */
            case 0xD:                               /* INV */
            case 0xE:                               /* INV */
            case 0xF:                               /* INV */
            default:
                goto inv;                           /* invalid instruction */
                break;
            }
            break;

        case 0xCC>>2:       /* 0xCC ADR - ADR */    /* LF */
            /* For machines with Base mode 0xCC08 stores base registers */
            if ((FC & 3) != 0) {                    /* must be word address */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            temp = addr & 0xffe000;                 /* get 11 bit map # */
            bc = addr & 0x20;                       /* bit 26 initial value */
            while (reg < 8) {
                if (bc != (addr & 0x20)) {          /* test for crossing file boundry */
                    if (CPU_MODEL < MODEL_27) {
                        TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                        goto newpsd;                /* go execute the trap now */
                    }
                }
                if (temp != (addr & 0xffe000)) {    /* test for crossing map boundry */
                    if (CPU_MODEL >= MODEL_V6) {
                        TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                        goto newpsd;                /* go execute the trap now */
                    }
                }
                if (FC & 0x4)                       /* LFBR? 0xCC08 */
                    TRAPME = Mem_read(addr, &BR[reg]);  /* read the base reg */
                else                                /* LF? 0xCC00 */
                    TRAPME = Mem_read(addr, &GPR[reg]); /* read the GPR reg */
                if (TRAPME)                         /* TRAPME has error */
                    goto newpsd;                    /* go execute the trap now */
                reg++;                              /* next reg to write */
                addr += 4;                          /* next addr */
            }
            break;

        case 0xD0>>2:       /* 0xD0 SD|ADR - INV */ /* LEA  none basemode only */
            if (MODES & BASEBIT) 
                goto inv;                           /* invalid instruction in basemode */
            /* bc has last bits 0,1 for indirect addr of both 1 for no indirection */
            addr &= 0x3fffffff;                     /* clear bits 0-1 */
            addr |= bc;                             /* insert bits 0,1 values into address */
            if (FC & 0x4)
                addr |= F_BIT;                      /* copy F bit from instruction */
            dest = (t_uint64)(addr);
            break;

        case 0xD4>>2:       /* 0xD4 RR|SM|ADR - RR|SM|ADR */ /* STx */
            break;

        case 0xD8>>2:       /* 0xD8 RR|SM|ADR - RR|SM|ADR */ /* STMx */
            /* STMD needs both regs to be masked with R4 */
            if (dbl) {
                /* we need to and both regs */
                t_uint64 nm = (((t_uint64)GPR[4]) << 32) | (((t_uint64)GPR[4]) & D32RMASK);
                dest &= nm;                         /* mask both regs with reg 4 contents */
            } else {
                dest &= (((t_uint64)GPR[4]) & D32RMASK);    /* mask with reg 4 contents */
            }           
            break;

        case 0xDC>>2:       /* 0xDC INV - ADR */    /* INV nonbasemode (STFx basemode) */
            /* DC00 STF */ /* DC08 STFBR */
            if ((FC & 0x4) && (CPU_MODEL <= MODEL_27))  {
                /* basemode undefined for 32/7x & 32/27 */
                TRAPME = UNDEFINSTR_TRAP;           /* Undefined Instruction Trap */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* handle trap */
            }
            /* For machines with Base mode 0xDC08 stores base registers */
            if ((FC & 3) != 0) {                    /* must be word address */
                TRAPME = ADDRSPEC_TRAP;             /* bad reg address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            bc = addr & 0x20;                       /* bit 26 initial value */
            temp = addr & 0xffe000;                 /* get 11 bit map # */
            while (reg < 8) {
                if (bc != (addr & 0x20)) {          /* test for crossing file boundry */
                    if (CPU_MODEL < MODEL_27) {
                        TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                        goto newpsd;                /* go execute the trap now */
                    }
                }
                if (temp != (addr & 0xffe000)) {    /* test for crossing map boundry */
                    if (CPU_MODEL >= MODEL_V6) {
                        TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                        goto newpsd;                /* go execute the trap now */
                    }
                }
                if (FC & 0x4)                       /* STFBR? */
                    TRAPME = Mem_write(addr, &BR[reg]);     /* store the base reg */
                else                                /* STF */
                    TRAPME = Mem_write(addr, &GPR[reg]);    /* store the GPR reg */
                if (TRAPME)                         /* TRAPME has error */
                    goto newpsd;                    /* go execute the trap now */
                reg++;                              /* next reg to write */
                addr += 4;                          /* next addr */
            }
            break;

        case 0xE0>>2:       /* 0xE0 ADR - ADR */    /* ADFx, SUFx */
                if ((TRAPME = Mem_read(addr, &temp))) { /* get the word from memory */
                    goto newpsd;                    /* memory read error or map fault */
                }
                source = (t_uint64)temp;            /* make into 64 bit value */
                if (FC & 2) {                       /* see if double word addr */
                    if ((TRAPME = Mem_read(addr+4, &temp))) {   /* get the 2nd word from memory */
                        goto newpsd;                /* memory read error or map fault */
                    }
                    source = (source << 32) | (t_uint64)temp;   /* merge in the low order 32 bits */
                    dbl = 1;                        /* double word instruction */
                } else {
                    source |= (source & MSIGN) ? D32LMASK : 0;
                    dbl = 0;                        /* not double wd */
                }
                PSD1 &= 0x87FFFFFE;                 /* clear the old CC's */
                CC = 0;                             /* clear the CC'ss */
                /* handle float or double add/sub instructions */
                if (dbl == 0) {
                    /* do ADFW or SUFW instructions */
                    temp2 = GPR[reg];               /* dest - reg contents specified by Rd */
                    addr = (uint32)(source & D32RMASK); /* get 32 bits from source memory */
                    if ((opr & 8) == 0) {           /* Was it SUFW? */
                        addr = NEGATE32(addr);      /* take negative for add */
                    }
                    temp = s_adfw(temp2, addr, &CC);    /* do ADFW */
                    sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "%s GPR[%d] %08x addr %08x result %08x CC %08x\n",
                        (opr&8) ? "ADFW":"SUFW", reg, GPR[reg], addr, temp, CC);
                    ovr = 0;
                    if (CC & CC1BIT)
                        ovr = 1;
                    PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                    /* check if we had an arithmetic exception on the last instruction*/
                    if (ovr && (MODES & AEXPBIT)) {
                        /* leave regs unchanged */
                        TRAPME = AEXPCEPT_TRAP;     /* trap the system now */
                        goto newpsd;                /* process the trap */
                    }
                    /* AEXP not enabled, so apply fix here */
                    /* return temp to destination reg */
                    GPR[reg] = temp;                /* dest - reg contents specified by Rd */
                } else {
                    /* handle ADFD or SUFD */
                    if (reg & 1) {                  /* see if odd reg specified */
                        TRAPME = ADDRSPEC_TRAP;     /* bad reg address, error */
                        goto newpsd;                /* go execute the trap now */
                    }
                    /* do ADFD or SUFD instructions */
                    td = (((t_uint64)GPR[reg]) << 32);  /* get upper reg value */
                    td |= (t_uint64)GPR[reg+1];     /* insert low order reg value */
                    /* source has 64 bit memory data */
                    if ((opr & 8) == 0) {           /* Was it SUFD? */
                        source = NEGATE32(source);  /* make negative for subtract */
                    }
                    dest = s_adfd(td, source, &CC); /* do ADFD */
                    sim_debug(DEBUG_DETAIL, &cpu_dev,
                        "%s GPR[%d] %08x %08x src %016llx result %016llx CC %08x\n",
                        (opr&8) ? "ADFD":"SUFD", reg, GPR[reg], GPR[reg+1], source, dest, CC);
                    ovr = 0;
                    if (CC & CC1BIT)                /* test for overflow detection */
                        ovr = 1;
                    PSD1 |= (CC & 0x78000000);      /* update the CC's in the PSD */
                    /* check if we had an arithmetic exception on the last instruction */
                    if (ovr && (MODES & AEXPBIT)) {
                        /* leave regs unchanged */
                        TRAPME = AEXPCEPT_TRAP;     /* trap the system now */
                        goto newpsd;                /* process the trap */
                    }
                    /* dest will be returned to destination regs */
                    /* if AEXP not enabled, apply fix here */
                    /* return dest to destination reg */
                    GPR[reg] = (uint32)((dest & D32LMASK) >> 32);   /* get upper reg value */
                    GPR[reg+1] = (uint32)(dest & D32RMASK); /* get lower reg value */
                }
                break;

        case 0xE4>>2:       /* 0xE4 ADR - ADR */    /* MPFx, DVFx */
            if ((TRAPME = Mem_read(addr, &temp))) { /* get the word from memory */
                goto newpsd;                        /* memory read error or map fault */
            }
            source = (t_uint64)temp;                /* make into 64 bit value */
            if (FC & 2) {                           /* see if double word addr */
                if ((TRAPME = Mem_read(addr+4, &temp))) {   /* get the 2nd word from memory */
                    goto newpsd;                    /* memory read error or map fault */
                }
                source = (source << 32) | (t_uint64)temp;   /* merge in the low order 32 bits */
                dbl = 1;                            /* double word instruction */
            } else {
                source |= (source & MSIGN) ? D32LMASK : 0;
                dbl = 0;                            /* not double wd */
            }
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's */
            CC = 0;                                 /* clear the CC'ss */
            /* handle float or double mul/div instructions */
            if (dbl == 0) {
                /* do MPFW or DVFW instructions */
                temp2 = GPR[reg];                   /* dest - reg contents specified by Rd */
                addr = (uint32)(source & D32RMASK); /* get 32 bits from source memory */
                if (opr & 8) {                      /* Was it MPFW? */
                    temp = s_mpfw(temp2, addr, &CC);    /* do MPFW */
                } else {
                    temp = (uint32)s_dvfw(temp2, addr, &CC);    /* do DVFW */
                }
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "%s GPR[%d] %08x addr %08x result %08x\n",
                    (opr&8) ? "MPFW":"DVFW", reg, GPR[reg], addr, temp);
                if (CC & CC1BIT)
                    ovr = 1;
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                /* check if we had an arithmetic exception on the last instruction*/
                if (ovr && (MODES & AEXPBIT)) {
                    /* leave regs unchanged */
                    TRAPME = AEXPCEPT_TRAP;         /* trap the system now */
                    goto newpsd;                    /* process the trap */
                }
                /* if AEXP not enabled, apply fix here */
                /* return temp to destination reg */
                GPR[reg] = temp;                    /* dest - reg contents specified by Rd */
            } else {
                /* handle MPFD or DVFD */
                if (reg & 1) {                      /* see if odd reg specified */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                /* do MPFD or DVFD instructions */
                td = (((t_uint64)GPR[reg]) << 32);  /* get upper reg value */
                td |= (t_uint64)GPR[reg+1];         /* insert low order reg value */
                /* source has 64 bit memory data */
                if (opr & 8) {                      /* Was it MPFD? */
                    dest = s_mpfd(td, source, &CC); /* do MPFD */
                } else {
                    dest = s_dvfd(td, source, &CC); /* do DVFD */
                }
                sim_debug(DEBUG_DETAIL, &cpu_dev,
                    "%s GPR[%d] %08x %08x src %016llx result %016llx\n",
                    (opr&8) ? "MPFD":"DVFD", reg, GPR[reg], GPR[reg+1], source, dest);
                if (CC & CC1BIT)                    /* test for overflow detection */
                    ovr = 1;
                PSD1 |= (CC & 0x78000000);          /* update the CC's in the PSD */
                /* check if we had an arithmetic exception on the last instruction*/
                if (ovr && (MODES & AEXPBIT)) {
                    /* leave regs unchanged */
                    TRAPME = AEXPCEPT_TRAP;         /* trap the system now */
                    goto newpsd;                    /* process the trap */
                }
                /* dest will be returned to destination regs */
                /* if AEXP not enabled, apply fix here */
                /* return dest to destination reg */
                GPR[reg] = (uint32)((dest & D32LMASK) >> 32);   /* get upper reg value */
                GPR[reg+1] = (uint32)(dest & D32RMASK); /* get lower reg value */
            }
            break;

        case 0xE8>>2:       /* 0xE8 SM|RR|RNX|ADR - SM|RM|ADR */ /* ARMx */
            ovr = 0;
            CC = 0;
            switch(FC) {                            /* adjust for hw or bytes */
            case 4: case 5: case 6: case 7:         /* byte address */
                /* ARMB */
                td = dest + source;                 /* DO ARMB */
                td &= 0xff;                         /* mask out right most byte */
                dest &= 0xffffff00;                 /* make place for byte */
                dest |= td;                         /* insert result into dest */
                if (td == 0)
                    CC |= CC4BIT;                   /* byte is zero, so CC4 */
                break;
            case 1:                                 /* left halfword addr */
            case 3:                                 /* right halfword addr */
                /* ARMH */
                td = dest + source;                 /* DO ARMH */
                td &= RMASK;                        /* mask out right most 16 bits */
                dest &= LMASK;                      /* make place for halfword */
                dest |= td;                         /* insert result into dest */
                if (td == 0)
                    CC |= CC4BIT;                   /* hw is zero, so CC4 */
                break;
            case 0:                                 /* 32 bit word */
                /* ARMW */
                /* dest and source are really 32 bit values */
                t = (source & MSIGN) != 0;
                t |= ((dest & MSIGN) != 0) ? 2 : 0;
                td = dest + source;                 /* DO ARMW */
                td &= D32RMASK;                     /* mask out right most 32 bits */
                dest = 0;                           /* make place for 64 bits */
                dest |= td;                         /* insert result into dest */
                /* if both signs are neg and result sign is positive, overflow */
                /* if both signs are pos and result sign is negative, overflow */
                if (((t == 3) && ((dest & MSIGN) == 0)) || 
                    ((t == 0) && ((dest & MSIGN) != 0)))
                    ovr = 1;
                if (dest & MSIGN)
                    dest = (D32LMASK | dest);       /* sign extend */
                else
                    dest = (D32RMASK & dest);       /* zero fill */
                if (td == 0)
                    CC |= CC4BIT;                   /* word is zero, so CC4 */
                else {
                    if (td & 0x80000000)
                        CC |= CC3BIT;               /* it is neg wd, so CC3  */
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                }
                break;
            case 2:                                 /* 64 bit double */
                /* ARMD */
                t = (source & DMSIGN) != 0;
                t |= ((dest & DMSIGN) != 0) ? 2 : 0;
                td = dest + source;                 /* DO ARMD */
                dest = td;                          /* insert result into dest */
                /* if both signs are neg and result sign is positive, overflow */
                /* if both signs are pos and result sign is negative, overflow */
                if (((t == 3) && ((dest & DMSIGN) == 0)) || 
                    ((t == 0) && ((dest & DMSIGN) != 0)))
                    ovr = 1;
                if (td == 0)
                    CC |= CC4BIT;                   /* dw is zero, so CC4 */
                else {
                    if (td & DMSIGN)
                        CC |= CC3BIT;               /* it is neg dw, so CC3  */
                    else
                        CC |= CC2BIT;               /* then td > 0, so CC2 */
                }
                break;
            }
            if (ovr)
                CC |= CC1BIT;                       /* set overflow CC */
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's from PSD1 */
            PSD1 |= CC;                             /* update the CC's in the PSD */
            /* the arithmetic exception will be handled */
            /* after instruction is completed */
            /* check for arithmetic exception trap enabled */
            if (ovr && (MODES & AEXPBIT)) {
                TRAPME = AEXPCEPT_TRAP;             /* set the trap type */
            }
            break;

        case 0xEC>>2:       /* 0xEC ADR - ADR */    /* Branch unconditional or Branch True */
            /* GOOF alert, the assembler sets bit 31 to 1 so this test will fail*/
            /* so just test for F bit and go on */
            /* if ((FC & 5) != 0) { */
            if ((FC & 4) != 0) {
                TRAPME = ADDRSPEC_TRAP;             /* bad address, error */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "ADDRSPEC10 OP %04x addr %08x\n", OP, addr);
                goto newpsd;                        /* go execute the trap now */
            }
            temp2 = CC;                             /* save the old CC's */
            CC = PSD1 & 0x78000000;                 /* get CC's if any */
            switch(reg) {
            case 0:     t = 1; break;
            case 1:     t = (CC & CC1BIT) != 0; break;
            case 2:     t = (CC & CC2BIT) != 0; break;
            case 3:     t = (CC & CC3BIT) != 0; break;
            case 4:     t = (CC & CC4BIT) != 0; break;
            case 5:     t = (CC & (CC2BIT|CC4BIT)) != 0; break;
            case 6:     t = (CC & (CC3BIT|CC4BIT)) != 0; break;
            case 7:     t = (CC & (CC1BIT|CC2BIT|CC3BIT|CC4BIT)) != 0; break;
            }
            if (t) {                                /* see if we are going to branch */
                /* we are taking the branch, set CC's if indirect, else leave'm */
                /* update the PSD with new address */
                PSD1 = (PSD1 & 0xff000000) | (addr & 0xfffffe); /* set new PC */
                i_flags |= BT;                      /* we branched, so no PC update */
                if (((MODES & BASEBIT) == 0) && (IR & IND)) /* see if CCs from last indirect wanted */
                    PSD1 = (PSD1 & 0x87fffffe) | temp2; /* insert last indirect CCs */
/*FIX F77*/     if ((MODES & (BASEBIT|EXTDBIT)) == 0)   /* see if basemode */
/*FIX F77*/         PSD1 &= 0xff07ffff;             /* only 19 bit address allowed */
            }
            /* branch not taken, go do next instruction */
            break;

        case 0xF0>>2:       /* 0xF0 ADR - ADR */    /* Branch False or Branch Function True BFT */
            /* GOOF alert, the assembler sets bit 31 to 1 so this test will fail*/
            /* so just test for F bit and go on */
            /* if ((FC & 5) != 0) { */
            if ((FC & 4) != 0) {
                TRAPME = ADDRSPEC_TRAP;             /* bad address, error */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "ADDRSPEC11 OP %04x addr %08x\n", OP, addr);
                goto newpsd;                        /* go execute the trap now */
            }
            temp2 = CC;                             /* save the old CC's */
            CC = PSD1 & 0x78000000;                 /* get CC's if any */
            switch(reg) {
            case 0:     t = (GPR[4] & (0x8000 >> ((CC >> 27) & 0xf))) != 0; break;
            case 1:     t = (CC & CC1BIT) == 0; break;
            case 2:     t = (CC & CC2BIT) == 0; break;
            case 3:     t = (CC & CC3BIT) == 0; break;
            case 4:     t = (CC & CC4BIT) == 0; break;
            case 5:     t = (CC & (CC2BIT|CC4BIT)) == 0; break;
            case 6:     t = (CC & (CC3BIT|CC4BIT)) == 0; break;
            case 7:     t = (CC & (CC1BIT|CC2BIT|CC3BIT|CC4BIT)) == 0; break;
            }
            if (t) {                                /* see if we are going to branch */
                /* we are taking the branch, set CC's if indirect, else leave'm */
                /* update the PSD with new address */
                PSD1 = (PSD1 & 0xff000000) | (addr & 0xfffffe); /* set new PC */
                i_flags |= BT;                      /* we branched, so no PC update */
                if (((MODES & BASEBIT) == 0) && (IR & IND)) /* see if CCs from last indirect wanted */
                    PSD1 = (PSD1 & 0x87fffffe) | temp2; /* insert last indirect CCs */
/*FIX F77*/     if ((MODES & (BASEBIT|EXTDBIT)) == 0)   /* see if basemode */
/*FIX F77*/         PSD1 &= 0xff07ffff;             /* only 19 bit address allowed */
            }
            break;

        case 0xF4>>2:       /* 0xF4 RR|SD|ADR - RR|SB|WRD */ /* Branch increment */
            dest += ((t_uint64)1) << ((IR >> 21) & 3);  /* use bits 9 & 10 to incr reg */
            if (dest != 0) {                        /* if reg is not 0, take the branch */
                /* we are taking the branch, set CC's if indirect, else leave'm */
                /* update the PSD with new address */

#if 0           /* set #if to 1 to stop branch to self while tracing, for now */
                if (PC == (addr & 0xFFFFFC)) {      /* BIB to current PC, bump branch addr */
                    addr += 4;
//                  fprintf(stderr, "BI? stopping BIB $ addr %x PC %x\r\n", addr, PC);
                    dest = 0;                       /* force reg to zero */
                }
#endif
                PSD1 = (PSD1 & 0xff000000) | (addr & 0xfffffe); /* set new PC */
                if (((MODES & BASEBIT) == 0) && (IR & IND)) /* see if CCs from last indirect wanted */
                    PSD1 = (PSD1 & 0x87fffffe) | CC;    /* insert last CCs */
                i_flags |= BT;                      /* we branched, so no PC update */
/*FIX F77*/     if ((MODES & (BASEBIT|EXTDBIT)) == 0)     /* see if basemode */
/*FIX F77*/         PSD1 &= 0xff07ffff;             /* only 19 bit address allowed */
            }
            break;

        case 0xF8>>2:       /* 0xF8 SM|ADR - SM|ADR */  /* ZMx, BL, BRI, LPSD, LPSDCM, TPR, TRP */
            switch((opr >> 7) & 0x7) {              /* use bits 6-8 to determine instruction */
            case 0x0:       /* ZMx F80x */          /* SM */
                dest = 0;                           /* destination value is zero */
                i_flags |= SM;                      /* SM not set so set it to store value */
                break;
            case 0x1:       /* BL F880 */
                /* copy CC's from instruction and PC incremented by 4 */
                GPR[0] = ((PSD1 & 0xff000000) | ((PSD1 + 4) & 0xfffffe));
                if (((MODES & BASEBIT) == 0) && (IR & IND)) /* see if CCs from last indirect wanted */
                    PSD1 = (PSD1 & 0x87fffffe) | CC;    /* insert last CCs */
                /* update the PSD with new address */
                if (MODES & BASEBIT) 
                    PSD1 = (PSD1 & 0xff000000) | (addr & 0xfffffe); /* bit 8-30 */
                else
                    PSD1 = (PSD1 & 0xff000000) | (addr & 0x07fffe); /* bit 13-30 */
                i_flags |= BT;                      /* we branched, so no PC update */
/*FIX F77*/     if ((MODES & (BASEBIT|EXTDBIT)) == 0)     /* see if basemode */
/*FIX F77*/         PSD1 &= 0xff07ffff;             /* only 19 bit address allowed */
                break;

            case 0x3:       /* LPSD F980 */
                /* fall through */;
            case 0x5:       /* LPSDCM FA80 */
                irq_pend = 1;                       /* start scanning interrupts again */
                if ((MODES & PRIVBIT) == 0) {       /* must be privileged */
                    TRAPME = PRIVVIOL_TRAP;         /* set the trap to take */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                        TRAPSTATUS |= BIT0;         /* set bit 0 of trap status */
                    else
                        TRAPSTATUS |= BIT19;        /* set bit 19 of trap status */
                    goto newpsd;                    /* Privlege violation trap */
                }
                CPUSTATUS |= BIT25;                 /* enable software traps */
                                                    /* this will allow attn and */
                                                    /* power fail traps */
                if ((FC & 04) != 0 || FC == 2) {    /* can not be byte or doubleword */
                    /* Fault */
                    TRAPME = ADDRSPEC_TRAP;         /* bad reg address, error */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "ADDRSPEC12 OP %04x addr %08x\n", OP, addr);
                    goto newpsd;                    /* go execute the trap now */
                }
                if ((TRAPME = Mem_read(addr, &temp))) { /* get PSD1 from memory */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
                        TRAPSTATUS |= BIT10;        /* set bit 10 of trap status */
                        TRAPSTATUS |= BIT7;         /* set bit 7 of trap status */
                    } else
                        TRAPSTATUS |= BIT18;        /* set bit 18 of trap status */
                    goto newpsd;                    /* memory read error or map fault */
                }
                bc = CPUSTATUS;                     /* save the CPU STATUS */
                TPSD[0] = PSD1;                     /* save the PSD for the instruction */
                TPSD[1] = PSD2;
                t = MODES;                          /* save modes too */
                ix = SPAD[0xf5];                    /* save the current PSD2 */
                reg = irq_pend;                     /* save intr status */

                if ((TRAPME = Mem_read(addr+4, &temp2))) {   /* get PSD2 from memory */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
                        TRAPSTATUS |= BIT10;        /* set bit 10 of trap status */
                        TRAPSTATUS |= BIT7;         /* set bit 7 of trap status */
                    } else
                        TRAPSTATUS |= BIT18;        /* set bit 18 of trap status */
                    goto newpsd;                    /* memory read error or map fault */
                }
                if (opr & 0x0200) {                 /* Was it LPSDCM? */
                    /* LPSDCM */
                    PSD2 = temp2 & 0xfffffff8;      /* PSD2 access good, clean & save it */
                } else {
                    /* LPSD */
                    /* lpsd can not change cpix, so keep it */
                    PSD2 = ((PSD2 & 0x3ff8) | (temp2 & 0xffffc000)); /* use current cpix */
                }
                PSD1 = temp;                        /* PSD1 good, so set it */
                sim_debug(DEBUG_IRQ, &cpu_dev,
                    "LPSD(CM) load [%06x] New PSD1 %08x %08x OPSD2 %08x SPAD %08x CPUSTATUS %08x\n",
                    addr, PSD1, PSD2, TPSD[1], ix, CPUSTATUS);
#ifdef MPXTEST
                for (ii=0; ii<8; ii+=4) {
                    sim_debug(DEBUG_IRQ, &cpu_dev,
                        "LPSD(CM) GPR[%d] %.8x GPR[%d] %.8x GPR[%d] %.8x GPR[%d] %.8x\n",
                        ii, GPR[ii], ii+1, GPR[ii+1], ii+2, GPR[ii+2], ii+3, GPR[ii+3]);
                }
                /* DYNAMIC 05282021 */
#endif
                /* set the mode bits and CCs from the new PSD */
                CC = PSD1 & 0x78000000;             /* extract bits 1-4 from PSD1 */
                MODES = PSD1 & 0x87000000;          /* extract bits 0, 5, 6, 7 from PSD 1 */
                CPUSTATUS &= ~0x87000000;           /* reset bits in CPUSTATUS */
                CPUSTATUS |= MODES;                 /* now insert into CPUSTATUS */

                /* set new map mode and interrupt blocking state in CPUSTATUS */
                if (PSD2 & MAPBIT) {
                    CPUSTATUS |= BIT8;              /* set bit 8 of cpu status */
                    MODES |= MAPMODE;               /* set mapped mode */
                } else {
                    CPUSTATUS &= ~BIT8;             /* reset bit 8 of cpu status */
/*TRY_01072022*/    MODES &= ~MAPMODE;              /* reset mapped mode */
                }

                /* set interrupt blocking state */
                if ((PSD2 & RETBBIT) == 0) {        /* is it retain blocking state */
                    if (PSD2 & SETBBIT) {           /* no, is it set blocking state */
                        CPUSTATUS |= BIT24;         /* yes, set blk state in cpu status bit 24 */
                        MODES |= BLKMODE;           /* set blocked mode */
                    } else {
                        CPUSTATUS &= ~BIT24;        /* no, reset blk state in cpu status bit 24 */
                        MODES &= ~BLKMODE;          /* reset blocked mode */
                        irq_pend = 1;               /* start scanning interrupts again */
#ifdef LEAVE_ACTIVE
                        if (irq_auto) {
/*AIR*/                     INTS[irq_auto] &= ~INTS_ACT;  /* deactivate specified int level */
/*AIR*/                     SPAD[irq_auto+0x80] &= ~SINT_ACT; /* deactivate in SPAD too */
                            sim_debug(DEBUG_IRQ, &cpu_dev,
                                "<|>IntX deactivate level %02x at LPSD(CM) %08x %08x R[3] %08x\n",
                                irq_auto, PSD1, PSD2, GPR[3]);
/*AIR*/                     irq_auto = 0;           /* show done processing in blocked mode */
#ifdef DYNAMIC_DEBUG_01172021
                            cpu_dev.dctrl &= ~DEBUG_INST;    /* end instruction trace */
#endif
                        }
#endif
                    }
                } else {
                    /* set new blocking state in PSD2 */
                    PSD2 &= ~(SETBBIT|RETBBIT);     /* clear bit 48 & 49 to be unblocked */
                    MODES &= ~(BLKMODE|RETBLKM);    /* reset blocked & retain mode bits */
                    if (bc & BIT24) {               /* see if old mode is blocked */
                        PSD2 |= SETBBIT;            /* set to blocked state */
                        MODES |= BLKMODE;           /* set blocked mode */
                    }
                }

#ifdef MPXTEST  /* set to 1 for traceme to work */
                /* get current MPX task name */
                {
                    int j;
                    char n[9];
                    uint32 sq59 = M[0x930>>2];      /* get C.SQ59 headcell */
                    uint32 dqe = M[0x8e8>>2];       /* get DQE of current task */
                    if (sq59 != 0x930)
                        goto skipdqe;               /* not running on mpx, skip */
                    for (j=0; j<8; j++) {           /* get the task name */
                        n[j] = (M[((dqe+0x18)>>2)+(j/4)] >> ((3-(j&7))*8)) & 0xff;
                        if (n[j] == 0)
                            n[j] = 0x20;
                    }
                    n[8] = 0;
#if 0
#if DYNAMIC_DEBUG
    if (dqe == 0x56e0) {
        cpu_dev.dctrl |= DEBUG_INST;                /* start instruction trace */
    }
#endif
#endif
                    if (opr & 0x0200) {             /* Was it LPSDCM? */
sim_debug(DEBUG_IRQ, &cpu_dev,
    "LPSDCM OPSD %.8x %.8x NPSD %.8x %.8x SPDF5 %.8x DQE %x LMN %8s\n",
        TPSD[0], TPSD[1], PSD1, PSD2, SPAD[0xf5], dqe, n);
sim_debug(DEBUG_IRQ, &cpu_dev,
    "   R0=%.8x R1=%.8x R2=%.8x R3=%.8x\n", GPR[0], GPR[1], GPR[2], GPR[3]);
sim_debug(DEBUG_IRQ, &cpu_dev,
    "   R4=%.8x R5=%.8x R6=%.8x R7=%.8x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
                    } else {
sim_debug(DEBUG_IRQ, &cpu_dev,
    "LPSD OPSD %.8x %.8x NPSD %.8x %.8x SPDF5 %.8x DQE %x LMN %8s\n",
        TPSD[0], TPSD[1], PSD1, PSD2, SPAD[0xf5], dqe, n);
sim_debug(DEBUG_IRQ, &cpu_dev,
    "   R0=%.8x R1=%.8x R2=%.8x R3=%.8x\n", GPR[0], GPR[1], GPR[2], GPR[3]);
sim_debug(DEBUG_IRQ, &cpu_dev,
    "   R4=%.8x R5=%.8x R6=%.8x R7=%.8x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
                    }
//   cpu_dev.dctrl |= DEBUG_DETAIL;                 /* start instruction trace */
                }
skipdqe:
#else
                    if (opr & 0x0200) {             /* Was it LPSDCM? */
sim_debug(DEBUG_IRQ, &cpu_dev,
    "LPSDCM OPSD %.8x %.8x NPSD %.8x %.8x SPDF5 %.8x CPUSTATUS %08x\n",
        TPSD[0], TPSD[1], PSD1, PSD2, SPAD[0xf5], CPUSTATUS);
sim_debug(DEBUG_IRQ, &cpu_dev,
    "   R0=%.8x R1=%.8x R2=%.8x R3=%.8x\n", GPR[0], GPR[1], GPR[2], GPR[3]);
sim_debug(DEBUG_IRQ, &cpu_dev,
    "   R4=%.8x R5=%.8x R6=%.8x R7=%.8x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
                    } else {
sim_debug(DEBUG_IRQ, &cpu_dev,
    "LPSD OPSD %.8x %.8x NPSD %.8x %.8x SPDF5 %.8x CPUSTATUS %08x\n",
        TPSD[0], TPSD[1], PSD1, PSD2, SPAD[0xf5], CPUSTATUS);
sim_debug(DEBUG_IRQ, &cpu_dev,
    "   R0=%.8x R1=%.8x R2=%.8x R3=%.8x\n", GPR[0], GPR[1], GPR[2], GPR[3]);
sim_debug(DEBUG_IRQ, &cpu_dev,
    "   R4=%.8x R5=%.8x R6=%.8x R7=%.8x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
                    }
#endif
                if (opr & 0x0200) {                 /* Was it LPSDCM? */
                    /* map bit must be on to load maps */
                    if (PSD2 & MAPBIT) {
                        /* set mapped mode in cpu status */
                        CPUSTATUS |= BIT8;          /* set bit 8 of cpu status */
#ifdef LOOK_MAP_05272021
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                            "B4 LPSDCM temp %06x TPSD %08x %08x PSD %08x %08x\n",
                            temp, TPSD[0], TPSD[1], PSD1, PSD2);
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                            "B4 LPSDCM BPIX %04x CPIX %04x CPIXPL %04x\n",
                            BPIX, CPIX, CPIXPL);
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                            "B4 LPSDCM OS MAPC[0-7] %08x %08x %08x %08x %08x %08x %08x %08x\n",
                            MAPC[0], MAPC[1], MAPC[2], MAPC[3], MAPC[4], MAPC[5], MAPC[6], MAPC[7]);
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                            "B4 LPSDCM US MAPC[%x-%x] %08x %08x %08x %08x %08x %08x %08x %08x\n",
                            BPIX, BPIX+5, MAPC[BPIX], MAPC[BPIX+1], MAPC[BPIX+2],
                            MAPC[BPIX+3], MAPC[BPIX+4], MAPC[BPIX+5],
                            MAPC[BPIX+6], MAPC[BPIX+7]);
#endif
                        /* this mod fixes MPX 1.X 1st swapr load */
                        /* any O/S or user maps yet? */
                        if (((CPIX != 0) && (CPIXPL == 0)) && (PSD2 & RETMBIT)) {
                            PSD2 &= ~RETMBIT;       /* no, turn off retain bit in PSD2 */
                            sim_debug(DEBUG_EXP, &cpu_dev, "Turn off retain bit\n");
                        }

                        /* test if user count is equal to CPIXPL, if not load maps */
                        /* this fixes software error in MPX3X where count is changed */
                        /* but the retain bit was left set, so new maps were not loaded */
                        /* until the next context switch and causes loading error */
                        /* CHANGED 041420 maybe not right */
                        if ((PSD2 & RETMBIT)) {     /* don't load maps if retain bit set */
                            uint32 mpl = SPAD[0xf3];    /* get mpl from spad address */
                            uint32 cpix = PSD2 & 0x3ff8;    /* get cpix 11 bit offset from psd wd 2 */
                            uint32 midl = RMW(mpl+cpix);    /* get midl entry for given user cpix */
                            uint32 spc = midl & MASK16; /* get 16 bit user segment description count */
#ifdef TRY_TEST_05182021
    /* output O/S and User MPL entries */
    sim_debug(DEBUG_EXP, &cpu_dev,
        "#LPSDCM MEM %06x MPL %06x MPL[0] %08x %06x MPL[%04x] %08x %06x\n",
        MEMSIZE, mpl, RMW(mpl), RMW(mpl+4), cpix,
        RMW(cpix+mpl), RMW(cpix+mpl+4));
    sim_debug(DEBUG_EXP, &cpu_dev,
        "#LPSDCM2 MEM %06x BPIX %04x cpix %04x CPIX %04x CPIXPL %04x HIWM %04x\n",
        MEMSIZE, BPIX, cpix, CPIX, CPIXPL, HIWM);
#endif
                            /* if this code is not present, MPX3X will not boot correctly */
                            if (spc != CPIXPL) {
                                PSD2 &= ~RETMBIT;   /* no, turn off retain bit in PSD2 */
                            }
                            /* if this code is not present MPX3X will abort */
                            /* when trying to mount a secondary disk */
                            else
                            {
                                if ((CPU_MODEL == MODEL_67) || (CPU_MODEL == MODEL_97) ||
                                    (CPU_MODEL == MODEL_V6) || (CPU_MODEL == MODEL_V9)) {
                                    PSD2 &= ~RETMBIT;   /* no, turn off retain bit in PSD2 */
                                }
                            }
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                "LPSDCM FIX MAP TRAPME %02x PSD1 %08x PSD2 %08x spc %02x BPIX %02x CPIXPL %02x retain %01x\n",
                            TRAPME, PSD1, PSD2, spc, BPIX, CPIXPL, PSD2&RETMBIT?1:0);
                        }

                        if ((PSD2 & RETMBIT) == 0) {    /* don't load maps if retain bit set */
                            /* we need to load the new maps */
                            TRAPME = load_maps(PSD, 0); /* load maps for new PSD */
#ifdef LOOK_MAP_05272021
                            sim_debug(DEBUG_IRQ, &cpu_dev,
                                "AF LPSDCM TPSD %08x %08x PSD %08x %08x TRAPME %02x\n",
                                TPSD[0], TPSD[1], PSD1, PSD2, TRAPME);
                            sim_debug(DEBUG_IRQ, &cpu_dev,
                                "AF LPSDCM BPIX %04x CPIX %04x CPIXPL %04x\n",
                                BPIX, CPIX, CPIXPL);
                            sim_debug(DEBUG_IRQ, &cpu_dev,
                                "AF LPSDCM OS MAPC[0-7] %08x %08x %08x %08x %08x %08x %08x %08x\n",
                                MAPC[0], MAPC[1], MAPC[2], MAPC[3], MAPC[4], MAPC[5], MAPC[6], MAPC[7]);
                            sim_debug(DEBUG_IRQ, &cpu_dev,
                                "AF LPSDCM US MAPC[%x-%x] %08x %08x %08x %08x %08x %08x %08x %08x\n",
                                BPIX, BPIX+5, MAPC[BPIX], MAPC[BPIX+1], MAPC[BPIX+2],
                                MAPC[BPIX+3], MAPC[BPIX+4], MAPC[BPIX+5],
                                MAPC[BPIX+6], MAPC[BPIX+7]);
#endif
                        }
                        PSD2 &= ~RETMBIT;           /* turn off retain bit in PSD2 */
                        SPAD[0xf5] = PSD2;          /* save the current PSD2 */
                        SPAD[0xf9] = CPUSTATUS;     /* save the cpu status in SPAD */
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                "LPSDCM MAPS LOADED TRAPME %02x PSD1 %08x PSD2 %08x BPIX %02x CPIXPL %02x retain %01x\n",
                            TRAPME, PSD1, PSD2, BPIX, CPIXPL, PSD2&RETMBIT?1:0);
                    }
                    PSD2 &= ~RETMBIT;               /* turn off retain bit in PSD2 */
                } else {
                    /* LPSD */
                    /* if cpix is zero, copy cpix from PSD2 in SPAD[0xf5] */
                    if ((PSD2 & 0x3ff8) == 0) {
                        PSD2 |= (SPAD[0xf5] & 0x3ff8);  /* use new cpix */
                    }
                }
                /* TRAPME can be error from LPSDCM or OK here */
                if (TRAPME) {                       /* if we have an error, restore old PSD */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
            "LPSDCM MAPS2 LOADED TRAPME = %02x PSD1 %08x PSD2 %08x CPUSTAT %08x SPAD[f9] %08x\n",
                        TRAPME, PSD1, PSD2, CPUSTATUS, SPAD[0xf9]);
                    PSD1 = TPSD[0];                 /* restore PSD1 */
                    /* HACK HACK HACK */
                    /* Diags wants the new PSD2, not the original??? */
                    /* if old one was used, we fail test 21/0 in cn.mmm for 32/67 */
                    CPUSTATUS = bc;                 /* restore the CPU STATUS */
                    MODES = t;                      /* restore modes too */
                    SPAD[0xf5] = ix;                /* restore the current PSD2 to SPAD */
                    SPAD[0xf9] = CPUSTATUS;         /* save the cpu status in SPAD */
                    irq_pend = reg;                 /* restore intr status */
                    if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9)) {
                        TRAPSTATUS |= BIT10;        /* set bit 10 of trap status */
                        TRAPSTATUS |= BIT7;         /* set bit 7 of trap status */
                    } else
                        TRAPSTATUS |= BIT18;        /* set bit 18 of trap status */
                    goto newpsd;                    /* go process error */
                }
                SPAD[0xf5] = PSD2;                  /* save the current PSD2 */
                SPAD[0xf9] = CPUSTATUS;             /* save the cpu status in SPAD */
                drop_nop = 0;                       /* nothing to drop */
                goto newpsd;                        /* load the new psd, or process error */
                break;

            case 0x4:       /* JWCS */              /* not used in simulator */
                sim_debug(DEBUG_EXP, &cpu_dev, "Got JWCS\n");
                break;
            case 0x2:       /* BRI */               /* TODO - only for 32/55 or 32/7X in PSW mode */
            case 0x6:       /* TRP */
            case 0x7:       /* TPR */
                TRAPME = UNDEFINSTR_TRAP;           /* trap condition */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* undefined instruction trap */
                break;
            }
            break;

/* F Class I/O device instruction format */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03 04 05|06 07 08|09 10 11 12|13 14 15|16|17 18 19 20 21 22 23|24 25 26 27 28 29 30 31| */
/* |     Op Code     |  Reg   |  I/O type |  Aug   |0 |   Channel Address  |  Device Sub-address   | */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */
/* E Class I/O device instruction format */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* |00 01 02 03 04 05|06 07 08 09 10 11 12|13 14 15|16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31| */
/* |     Op Code     |     Device Number  |  Aug   |                  Command Code                 | */
/* |-----------+-----------+-----------+-----------+-----------+-----------+-----------+-----------| */
/* */
        case 0xFC>>2:       /* 0xFC IMM - IMM */    /* XIO, CD, TD, Interrupt Control */
#ifdef MAYBE_NO
            irq_pend = 1;                           /* start scanning interrupts again */
#endif
            if ((MODES & PRIVBIT) == 0) {           /* must be privileged to do I/O */
                TRAPME = PRIVVIOL_TRAP;             /* set the trap to take */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                else
                    TRAPSTATUS |= BIT19;            /* set bit 19 of trap status */
                goto newpsd;                        /* Privlege violation trap */
            }
            if ((opr & 0x7) != 0x07) {              /* aug is 111 for XIO instruction */
                /* Process Non-XIO instructions */
                uint32 status = 0;                  /* status returned from device */
                uint32 device = (opr >> 3) & 0x7f;  /* get device code */
                uint32 prior = device;              /* interrupt priority */
                uint32 maxlev = 0x5f;               /* max lev for all but 32/27 in diags */
//MAYBEBAD      uint32 maxlev = 0x6f;               /* max lev for all but 32/27 in diags */

                t = SPAD[prior+0x80];               /* get spad entry for interrupt */
                addr = SPAD[0xf1] + (prior<<2);     /* vector address in SPAD */
                addr = M[addr>>2];                  /* get the interrupt context block addr */
                prior = (opr >> 3) & 0x7f;          /* get priority level */
                if (CPU_MODEL <= MODEL_27) {
                    maxlev = 0x6f;                  /* 27 uses 112 */
                }

                switch(opr & 0x7) {                 /* use bits 13-15 to determine instruction */
                case 0x0:   /* EI  FC00  Enable Interrupt */
                    if (prior > maxlev)             /* ignore for invalid levels */
                        break;                      /* ignore */
                    /* SPAD entries for interrupts begin at 0x80 */
                    t = SPAD[prior+0x80];           /* get spad entry for interrupt */
                    if ((t == 0) || ((t&MASK24) == MASK24))  /* if unused, ignore instruction */
                        break;                      /* ignore */
      
                    if ((t & 0x0f800000) == 0x0f000000) /* if class F ignore instruction */
                        break;                      /* ignore for F class */

                    /* does not effect REQ status */
                    INTS[prior] |= INTS_ENAB;       /* enable specified int level */
                    SPAD[prior+0x80] |= SINT_ENAB;  /* enable in SPAD too */
                    irq_pend = 1;                   /* start scanning interrupts again */

#ifdef TRY_ME_01072022
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "EI skipinstr %d PSD1 %08x irq_pend %d wait4int %d irq_auto %x\n",
                skipinstr, PSD1, irq_pend, wait4int, irq_auto);
            sim_debug(DEBUG_IRQ, &cpu_dev,
                "EI INTS[%d] = %08x SPAD[%d] %08x CPUSTATUS %08x\n",
                prior, INTS[prior], 0x80+prior, SPAD[prior+0x80], CPUSTATUS);
#endif
                    /* test for clock at address 0x7f06 and interrupt level 0x18 */
                    /* the diags want the type to be 0 */
                    /* UTX wants the type to be 3?? */
                    /* UTX would be 0x03807f06 Diags would be 0x00807f06 */
                    if ((SPAD[prior+0x80] & 0x0000ffff) == 0x00007f06) {
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                            "Clock EI %02x SPAD %08x Turn on\n", prior, t);
                        rtc_setup(1, prior);        /* tell clock to start */
                    }
                    /* the diags want the type to be 3 */
                    if ((SPAD[prior+0x80] & 0x0f00ffff) == 0x03007f04) {
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                            "Intv Timer EI %02x SPAD %08x Turn on\n", prior, t);
                        itm_setup(1, prior);        /* tell timer to start */
#ifdef DO_DYNAMIC_INSTR
        cpu_dev.dctrl |= DEBUG_INST;                /* start instruction trace */
#endif
                    }
                    break;

                case 0x1:   /* DI FC01 */
                    if (prior > maxlev)             /* ignore for invalid levels */
                        break;                      /* ignore */
                    /* SPAD entries for interrupts begin at 0x80 */
                    t = SPAD[prior+0x80];           /* get spad entry for interrupt */
     
                    if ((t == 0) || ((t&MASK24) == MASK24)) /* if unused, ignore instruction */
                        break;                      /* ignore */
         
                    if ((t & 0x0f800000) == 0x0f000000) /* if class F ignore instruction */
                        break;                      /* ignore for F class */

                    /* active state is left alone */
                    INTS[prior] &= ~INTS_ENAB;      /* disable specified int level */
                    SPAD[prior+0x80] &= ~SINT_ENAB; /* disable in SPAD too */
                    INTS[prior] &= ~INTS_REQ;       /* clears any requests also */
                    irq_pend = 1;                   /* start scanning interrupts again */

                    /* test for clock at address 0x7f06 and interrupt level 0x18 */
                    /* the diags want the type to be 0 */
                    /* UTX wants the type to be 3?? */
                    /* UTX would be 0x03807f06 Diags would be 0x00807f06 */
                    if ((SPAD[prior+0x80] & 0x0000ffff) == 0x00007f06) {
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                            "Clock DI %02x SPAD %08x Turn off\n", prior, t);
                        rtc_setup(0, prior);        /* tell clock to stop */
                    }
                    /* the diags want the type to be 3 */
                    if ((SPAD[prior+0x80] & 0x0f00ffff) == 0x03007f04) {
                        sim_debug(DEBUG_IRQ, &cpu_dev,
                            "Intv Timer DI %02x SPAD %08x Turn off\n", prior, t);
#ifdef DO_DYNAMIC_INSTR
        cpu_dev.dctrl &= ~DEBUG_INST;               /* stop instruction trace */
#endif
                        itm_setup(0, prior);        /* tell timer to stop */
                    }
                    break;

                case 0x2:   /* RI FC02 */
                    if (prior > maxlev)             /* ignore for invalid levels */
                        break;                      /* ignore */
                    /* SPAD entries for interrupts begin at 0x80 */
                    t = SPAD[prior+0x80];           /* get spad entry for interrupt */
                    if ((t == 0) || ((t&MASK24) == MASK24))  /* if unused, ignore instruction */
                        break;                      /* ignore */
           
                    if ((t & 0x0f800000) == 0x0f000000) /* if class F ignore instruction */
                        break;                      /* ignore for F class */

                    INTS[prior] |= INTS_REQ;        /* set the request flag for this level */
                    irq_pend = 1;                   /* start scanning interrupts again */
                    break;

                case 0x3:   /* AI FC03 */
                    if (prior > maxlev)             /* ignore for invalid levels */
                        break;                      /* ignore */
                    /* SPAD entries for interrupts begin at 0x80 */
                    t = SPAD[prior+0x80];           /* get spad entry for interrupt */
                    if ((t == 0) || ((t&MASK24) == MASK24))  /* if unused, ignore instruction */
                        break;                      /* ignore */
      
                    if ((t & 0x0f800000) == 0x0f000000) /* if class F ignore instruction */
                        break;                      /* ignore for F class */

                    INTS[prior] |= INTS_ACT;        /* activate specified int level */
                    SPAD[prior+0x80] |= SINT_ACT;   /* activate in SPAD too */
                    irq_pend = 1;                   /* start scanning interrupts again */
                    break;

                case 0x4:   /* DAI FC04 */
                    if (prior > maxlev)             /* ignore for invalid levels */
                        break;                      /* ignore */
                    /* SPAD entries for interrupts begin at 0x80 */
                    t = SPAD[prior+0x80];           /* get spad entry for interrupt */
                    if ((t == 0) || ((t&MASK24) == MASK24))  /* if unused, ignore instruction */
                        break;                      /* ignore */
        
                    if ((t & 0x0f800000) == 0x0f000000) /* if class F ignore instruction */
                        break;                      /* ignore for F class */

                    sim_debug(DEBUG_IRQ, &cpu_dev,
                        "DAI spad %08x INTS[%02x] %08x\n", t, prior, INTS[prior]);
                    INTS[prior] &= ~INTS_ACT;       /* deactivate specified int level */
                    SPAD[prior+0x80] &= ~SINT_ACT;  /* deactivate in SPAD too */
                    irq_pend = 1;                   /* start scanning interrupts again */
                    /* instruction following a DAI can not be interrupted */
                    /* skip tests for interrupts if this is the case */
                    skipinstr = 1;                  /* skip interrupt test */
#ifdef DO_DYNAMIC_DEBUG
                    /* test for DAI 0x03 */
                    if ((prior == 3) && (GPR[5] == 0xfc1c0000) && (GPR[6] == 0x44a4)) {
                        cpu_dev.dctrl |= (DEBUG_INST|DEBUG_TRAP|DEBUG_IRQ); /* start instruction trace */
                        con_dev.dctrl |= DEBUG_XIO|DEBUG_CMD;
                        sim_debug(DEBUG_IRQ, &cpu_dev, "DAI 0x03 received start debug\n");
                    }
                    if ((prior == 0x0a) && (GPR[5] == 0xfc540000)) {    /* test for DAI 0x0A */
                        /* turn off debug trace because we are already hung */
                        sim_debug(DEBUG_IRQ, &cpu_dev, "DAI 0x0A received stopping debug\n");
                        cpu_dev.dctrl &= ~(DEBUG_INST|DEBUG_TRAP|DEBUG_IRQ); /* start instruction trace */
                        con_dev.dctrl &= ~(DEBUG_XIO|DEBUG_CMD);
                    }
#endif
                    break;

                case 0x5:   /* TD FC05 */           /* bits 13-15 is test code type */
                case 0x6:   /* CD FC06 */
                    /* If CD or TD, make sure device is not F class device */
                    /* the channel must be defined as a non class F I/O channel in SPAD */
                    /* if class F, the system will generate a system check trap */
                    t = SPAD[device];               /* get spad entry for channel */
                    if ((t & 0x0f000000) == 0x0f000000) {   /* class in bits 4-7 */
                        TRAPME = SYSTEMCHK_TRAP;    /* trap condition if F class */
                        TRAPSTATUS &= ~BIT0;        /* class E error bit */
                        TRAPSTATUS &= ~BIT1;        /* I/O processing error */
                        goto newpsd;                /* machine check trap */
                    }
                    /* t has spad entry for device */
                    /* get the 1's comp of interrupt address from bits 9-15 SPAD entry */
                    ix = ((~t)>>16)&0x7f;           /* get positive number for interrupt */
                    if (opr & 0x1) {                /* see if CD or TD */
                        /* TODO process a TD */
                        if (device == 0x7f) {
                            /* if this is for the interval timer check cmd type */
                            /* if TD 8000 or 4000, set all cc's zero */
                            temp = (IR & 0xf000);   /* get cmd from instruction */
                            if ((temp == 0x4000) || (temp == 0x8000))
                                status = 0;         /* no CC's */
                            else
                                /* if TD 2000 set CC2 for caller */
                                if (temp == 0x2000)
                                    status = CC2BIT;    /* set CC2 */
                            /* return status has new CC's in bits 1-4 of status word */
                            /* insert status CCs */
                            PSD1 = ((PSD1 & 0x87fffffe) | (status & 0x78000000));
                        } else {
#if 0
                            /* may want to handle class E someday */
                            if ((TRAPME = testEIO(device, testcode, &status)))
                                goto newpsd;        /* error returned, trap cpu */
                             /* return status has new CC's in bits 1-4 of status word */
                             /* insert status CCs */
                             PSD1 = ((PSD1 & 0x87fffffe) | (status & 0x78000000));
#endif
                             goto inv;               /* invalid instruction until I fix it */
                        }
                    } else {
                        /* TODO process a CD */
#if 0
                        if ((TRAPME = startEIO(device, &status)))
                            goto newpsd;            /* error returned, trap cpu */
#endif
                        if (device == 0x7f) {
                            temp = (IR & 0x7f);     /* get cmd from instruction */
                            status = itm_rdwr(temp, GPR[0], ix);    /* read/write the interval timer */
                            /* see if the cmd does not return value */
                            /* if bit 25 set, read reg val into R0 */
                            if (temp & 0x40) 
                                GPR[0] = status;    /* return count in reg 0 */
                            /* No CC's going out */
                        } else {
                            goto inv;               /* invalid instruction until I fix it */
                        }
                    }
                    break;
                case 0x7:   /* XIO FC07*/           /* should never get here */
                    break;
                }
                break;                              /* skip over XIO code */
            }

            /* Process XIO instructions */
            /* see if valid instruction */
            /* DIAGS wants this tested first */
            switch((opr >> 3) & 0xf) {              /* use bits 9-12 to determine I/O instruction */
            case 0x00:      /* Unassigned */
            case 0x01:      /* Unassigned */
            case 0x0A:      /* Unassigned */
                TRAPME = UNDEFINSTR_TRAP;           /* trap condition */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* undefined instruction trap */
            }

            /* if reg is non-zero, add reg to 15 bits from instruction */
            if (reg)
                temp2 = (IR & 0x7fff) + (GPR[reg] & 0x7fff);    /* set new chan/suba into IR */
            else
                temp2 = (IR & 0x7fff);              /* set new chan/suba into IR */
            lchan = (temp2 & 0x7F00) >> 8;          /* get 7 bit logical channel address */
            suba = temp2 & 0xFF;                    /* get 8 bit subaddress */
            lchsa = (lchan << 8) | suba;            /* logical address */
            /* the channel must be defined as a class F I/O channel in SPAD */
            /* if not class F, the system will generate a system check trap */
            t = SPAD[lchan];                        /* get spad entry for channel */
            if ((t == 0) || ((t&MASK24) == MASK24) || /* if not set up, system check */
                ((t & 0x0f800000) != 0x0f000000)) {   /* class in bits 4-7 */
                TRAPME = SYSTEMCHK_TRAP;            /* trap condition if F class */
                TRAPSTATUS |= BIT0;                 /* class F error bit */
                TRAPSTATUS &= ~BIT1;                /* I/O processing error */
                goto newpsd;                        /* undefined instruction trap */
            }
            /* get real channel from spad device entry */
            chan = (t & 0x7f00) >> 8;               /* real channel */
            rchsa = (chan << 8) | suba;             /* real ch & sa */
            /* get the 1's comp of interrupt address from bits 9-15 SPAD entry */
            ix = ((~t)>>16)&0x7f;                   /* get positive number for interrupt */
            bc = SPAD[ix+0x80];                     /* get interrupt spad entry for channel */
            /* SPAD address F1 has interrupt table address */
            temp = SPAD[0xf1] + (ix<<2);            /* vector address in SPAD */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "$$ XIO chsa %04x spad %08x BLK %1x INTS[%02x] %08x\n",
                rchsa, t, CPUSTATUS&0x80?1:0, ix, INTS[ix]);
            sim_debug(DEBUG_XIO, &cpu_dev,
                "$$ XIO chsa %04x PSD1 %08x PSD2 %08x IR %08x ICBA %06x\n",
                rchsa, PSD1, PSD2, IR, temp);
            if ((TRAPME = Mem_read(temp, &addr))) { /* get interrupt context block addr */
mcheck:
                /* machine check if not there */
                TRAPME = MACHINECHK_TRAP;           /* trap condition */
                TRAPSTATUS |= BIT0;                 /* class F error bit */
                TRAPSTATUS &= ~BIT1;                /* I/O processing error */
                goto newpsd;                        /* machine check trap */
            }
            /* the context block contains the old PSD, */
            /* new PSD, IOCL address, and I/O status address */
            if ((addr == 0) || (addr == 0xffffffff)) {  /* must be initialized address */
                goto mcheck;                        /* bad int icb address */
            }
            if ((TRAPME = Mem_read(addr+16, &temp))) { /* get iocl address from icb wd 4 */
                goto mcheck;                        /* machine check if not there */
            }
            /* iocla must be valid addr if it is a SIO instruction */
            if (((temp & MASK24) == 0) && (((opr >> 2) & 0xf) == 2))  {
                goto mcheck;                        /* bad iocl address */
            }

            sim_debug(DEBUG_XIO, &cpu_dev,
                "XIO rdy PSD1 %08x chan %02x irq %02x icb %06x iocla %06x iocd %08x %08x\n",
                PSD1, chan, ix, addr, addr+16, RMW(temp), RMW(temp+4));
            /* at this point, the channel has a valid SPAD channel entry */
            /* t is SPAD entry contents for chan device */
            /* temp2 has logical channel address */
            /* lchan - logical channel address */
            /* lchsa - logical channel & subaddress */
            /* chan - channel address */
            /* suba - channel device subaddress */
            /* rchsa - real chan & sub address from spad for logical channel */
            /* ix - positive interrupt level */
            /* addr - ICBA for specified interrupt level, points to 6 wd block */
            /* temp - First IOCD address */
            sim_debug(DEBUG_XIO, &cpu_dev,
                "XIO switch %02x lchan %02x irq %02x rchsa %04x IOCDa %08x CPUSTATUS %08x BLK %1x\n",
                ((opr>>3)&0x0f), lchan, ix, rchsa, temp, CPUSTATUS, CPUSTATUS&0x80?1:0);

            switch((opr >> 3) & 0xf) {              /* use bits 9-12 to determine I/O instruction */
            case 0x00:      /* Unassigned */
            case 0x01:      /* Unassigned */
            case 0x0A:      /* Unassigned */
                TRAPME = UNDEFINSTR_TRAP;           /* trap condition */
                if ((CPU_MODEL == MODEL_97) || (CPU_MODEL == MODEL_V9))
                    TRAPSTATUS |= BIT0;             /* set bit 0 of trap status */
                goto newpsd;                        /* undefined instruction trap */
                break;

            case 0x09:      /* Enable write channel ECWCS */
            case 0x0B:      /* Write channel WCS WCWCS */
                /* TODO, provide support code */
                /* for now or maybe forever, return unsupported transaction */
                PSD1 = ((PSD1 & 0x87fffffe) | (CC2BIT|CC4BIT)); /* insert status 5 */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "XIO unsupported WCS chan %04x chsa %04x status %08x\n",
                    chan, rchsa, rstatus);
                /* just give unsupported transaction */
#ifdef JUST_RETURN_STATUS
                TRAPME = SYSTEMCHK_TRAP;            /* trap condition if F class */
                TRAPSTATUS |= BIT0;                 /* class F error bit */
                TRAPSTATUS &= ~BIT1;                /* I/O processing error */
                goto newpsd;                        /* undefined instruction trap */
#endif
                break;

            case 0x02:      /* Start I/O SIO */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "SIO b4 call PSD1 %08x rchsa %04x lchsa %04x BLK %1x\n",
                    PSD1, rchsa, lchsa, CPUSTATUS&0x80?1:0);
                if ((TRAPME = startxio(lchsa, &rstatus)))
                    goto newpsd;                    /* error returned, trap cpu */
                PSD1 = ((PSD1 & 0x87fffffe) | (rstatus & 0x78000000));   /* insert status */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "SIO ret PSD1 %08x chsa %04x status %08x BLK %1x\n",
                    PSD1, lchsa, rstatus, CPUSTATUS&0x80?1:0);
                break;
                            
            case 0x03:      /* Test I/O TIO */
                if ((TRAPME = testxio(lchsa, &rstatus))) {
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "TIO ret PSD1 %x rchsa %x lchsa %x status %x BLK %1x\n",
                        PSD1, rchsa, lchsa, rstatus, CPUSTATUS&0x80?1:0);
                        goto newpsd;                /* error returned, trap cpu */
               }
               PSD1 = ((PSD1 & 0x87fffffe) | (rstatus & 0x78000000));   /* insert status */
               sim_debug(DEBUG_XIO, &cpu_dev,
                   "TIO ret PSD1 %08x lchsa %04x stat %08x spad %08x INTS[%02x] %08x BLK %1x\n",
                   PSD1, lchsa, rstatus, t, ix, INTS[ix], CPUSTATUS&0x80?1:0);
                break;
                            
            case 0x04:      /* Stop I/O STPIO */
                if ((TRAPME = stopxio(lchsa, &rstatus)))
                    goto newpsd;                    /* error returned, trap cpu */
                PSD1 = ((PSD1 & 0x87fffffe) | (rstatus & 0x78000000));   /* insert status */
                sim_debug(DEBUG_XIO, &cpu_dev, "STPIO ret rchsa %04x lchsa %04x status %08x\n",
                    rchsa, lchsa, rstatus);
                break;

            case 0x05:      /* Reset channel RSCHNL */
                if ((TRAPME = rschnlxio(lchsa, &rstatus)))
                    goto newpsd;                    /* error returned, trap cpu */
                /* SPAD entries for interrupts begin at 0x80 */
                INTS[ix] &= ~INTS_REQ;              /* clears any requests */
                INTS[ix] &= ~INTS_ACT;              /* deactivate specified int level */
                SPAD[ix+0x80] &= ~SINT_ACT;         /* deactivate in SPAD too */
                PSD1 = ((PSD1 & 0x87fffffe) | (rstatus & 0x78000000));   /* insert status */
                sim_debug(DEBUG_XIO, &cpu_dev, "RSCHNL rschnlxio ret rchsa %04x lchsa %04x status %08x\n",
                    rchsa, lchsa, rstatus);
                break;

            case 0x06:      /* Halt I/O HIO */
                if ((TRAPME = haltxio(lchsa, &rstatus)))
                    goto newpsd;                    /* error returned, trap cpu */
                PSD1 = ((PSD1 & 0x87fffffe) | (rstatus & 0x78000000));   /* insert status */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "HIO haltxio ret rchsa %04x lchsa %04x status %08x\n",
                    rchsa, lchsa, rstatus);
                break;

            case 0x07:      /* Grab controller GRIO n/u */
                if ((TRAPME = grabxio(lchsa, &rstatus)))
                    goto newpsd;                    /* error returned, trap cpu */
                PSD1 = ((PSD1 & 0x87fffffe) | (rstatus & 0x78000000));   /* insert status */
                sim_debug(DEBUG_XIO, &cpu_dev, "GRIO ret rchsa %04x lchsa %04x status %08x\n",
                    rchsa, lchsa, rstatus);
                break;

            case 0x08:      /* Reset controller RSCTL */
                 if ((TRAPME = rsctlxio(lchsa, &rstatus)))
                     goto newpsd;                   /* error returned, trap cpu */
                 PSD1 = ((PSD1 & 0x87fffffe) | (rstatus & 0x78000000));   /* insert status */
                 sim_debug(DEBUG_XIO, &cpu_dev, "RSCTL ret rchsa %04x lchsa %04x status %08x\n",
                     rchsa, lchsa, rstatus);
                 break;

            case 0x0C:      /* Enable channel interrupt ECI */
                /* disable int only */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "ECI chsa %04x lchsa %04x spad %08x INTS[%02x] %08x\n",
                    rchsa, lchsa, t, ix, INTS[ix]);
                if ((TRAPME = checkxio(lchsa, &rstatus)))
                    goto newpsd;                    /* error returned, trap cpu */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "ECI after checkxio rchsa %04x suba %04x status %08x\n",
                    rchsa, suba, rstatus);
    
                if ((INTS[ix] & INTS_ACT) == 0)
                    sim_debug(DEBUG_XIO, &cpu_dev,
                        "ECI INT %02x is NOT set rchsa %04x lchsa %04x status %08x\n",
                        ix, rchsa, lchsa, rstatus);
                /* SPAD entries for interrupts begin at 0x80 */
                INTS[ix] |= INTS_ENAB;              /* enable specified int level */
                SPAD[ix+0x80] |= SINT_ENAB;         /* enable in SPAD too */
                irq_pend = 1;                       /* start scanning interrupts again */
                /* return status of zero for present and functioning */
                PSD1 = ((PSD1 & 0x87fffffe) | (rstatus & 0x78000000));   /* insert status */
                break;

            case 0x0D:  /* Disable channel interrupt DCI */
                /* disable int, leave req */
                sim_debug(DEBUG_XIO, &cpu_dev,
                "DCI rchsa %04x lchsa %04x spad %08x INTS[%02x] %08x\n",
                    rchsa, lchsa, t, ix, INTS[ix]);

                if ((TRAPME = checkxio(lchsa, &rstatus)))
                    goto newpsd;                    /* error returned, trap cpu */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "DCI After checkxio call rstatus %08x\n", rstatus);
                /* doc says we need to drop 1 queued status entry too */
                if ((INTS[ix] & INTS_ACT) == 0)
                    sim_debug(DEBUG_XIO, &cpu_dev,
                        "DCI INT %02x is NOT set rchsa %04x lchsa %04x status %08x\n",
                        ix, rchsa, lchsa, rstatus);
                /* SPAD entries for interrupts begin at 0x80 */
                INTS[ix] &= ~INTS_ENAB;             /* disable specified int level */
                SPAD[ix+0x80] &= ~SINT_ENAB;        /* disable in SPAD too */
                PSD1 = ((PSD1 & 0x87fffffe) | (rstatus & 0x78000000));   /* insert status */
                break;

            case 0x0E:      /* Activate channel interrupt ACI */
                /* Set int active, clear request */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "ACI rchsa %04x lchsa %04x spad %08x INTS[%02x] %08x\n",
                    rchsa, lchsa, t, ix, INTS[ix]);

                if ((TRAPME = checkxio(lchsa, &rstatus)))
                    goto newpsd;                    /* error returned, trap cpu */
                if ((INTS[ix] & INTS_ACT) == 0)
                    sim_debug(DEBUG_XIO, &cpu_dev,
                        "ACI INT %02x is NOT set rchsa %04x lchsa %04x status %08x\n",
                        ix, rchsa, lchsa, rstatus);
                /* SPAD entries for interrupts begin at 0x80 */
                INTS[ix] |= INTS_ACT;               /* activate specified int level */
                SPAD[ix+0x80] |= SINT_ACT;          /* enable in SPAD too */
                PSD1 = ((PSD1 & 0x87fffffe) | (rstatus & 0x78000000));   /* insert status */
                break;

            case 0x0F:      /* Deactivate channel interrupt DACI */
                /* Clear active and leave any request */
                /* Note, instruction following DACI is not interruptable */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "DACI rchsa %04x lchsa %04x spad %08x INTS[%02x] %08x\n",
                    rchsa, lchsa, t, ix, INTS[ix]);

                if ((TRAPME = checkxio(rchsa, &rstatus)))
                    goto newpsd;                    /* error returned, trap cpu */
                if ((INTS[ix] & INTS_ACT) == 0)
                    sim_debug(DEBUG_XIO, &cpu_dev,
                        "DACI INT %02x is NOT set chan %04x suba %04x status %08x\n",
                        ix, chan, suba, rstatus);
                /* SPAD entries for interrupts begin at 0x80 */
                INTS[ix] &= ~INTS_ACT;              /* deactivate specified int level */
                SPAD[ix+0x80] &= ~SINT_ACT;         /* deactivate in SPAD too */
                irq_pend = 1;                       /* start scanning interrupts again */
                skipinstr = 1;                      /* skip interrupt test */
                PSD1 = ((PSD1 & 0x87fffffe) | (rstatus & 0x78000000));   /* insert status */
                sim_debug(DEBUG_XIO, &cpu_dev,
                    "DACI ret lchsa %04x status %08x spad %08x INTS[%02x] %08x BLK %1x\n",
                    lchsa, rstatus, t, ix, INTS[ix], CPUSTATUS&0x80?1:0);
                break;
            }                                       /* end of XIO switch */
            break;
        }                                           /* End of Instruction Switch */

        /* [*][*][*][*][*][*][*][*][*][*][*][*][*][*][*][*][*][*][*][*][*][*] */

        /* any instruction with an arithmetic exception will still end up here */
        /* after the instruction is done and before incrementing the PC, */
        /* we will trap the cpu if ovl is set nonzero by an instruction */

        /* Store result to register */
        if (i_flags & SD) {
            if (dbl) {                              /* if double reg, store 2nd reg */
                if (reg & 1) {                      /* is it double regs into odd reg */
                    TRAPME = ADDRSPEC_TRAP;         /* bad address, error */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "ADDRSPEC13 OP %04x addr %08x\n", OP, addr);
                    goto newpsd;                    /* go execute the trap now */
                }
                GPR[reg+1] = (uint32)(dest & FMASK);    /* save the low order reg */
                GPR[reg] = (uint32)((dest>>32) & FMASK);/* save the hi order reg */
            } else {
                GPR[reg] = (uint32)(dest & FMASK);  /* save the reg */
            }
        }

        /* Store result to base register */
        if (i_flags & SB) {
            if (dbl)  {                             /* no dbl wd store to base regs */
                TRAPME = ADDRSPEC_TRAP;             /* bad address, error */
                goto newpsd;                        /* go execute the trap now */
            }
            BR[reg] = (uint32)(dest & FMASK);       /* save the base reg */
        }

        /* Store result to memory */
        if (i_flags & SM) {
            /* Check if byte of half word */
            if (((FC & 04) || (FC & 5) == 1)) {     /* hw or byte requires read first */
                if ((TRAPME = Mem_read(addr, &temp))) { /* get the word from memory */
                    goto newpsd;                    /* memory read error or map fault */
                }
            }
            switch(FC) {
            case 2:         /* double word store */
                if ((addr & 7) != 2) {
                    TRAPME = ADDRSPEC_TRAP;         /* address not on dbl wd boundry, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                temp = (uint32)(dest & MASK32);/* get lo 32 bit */
                if ((TRAPME = Mem_write(addr + 4, &temp)))
                    goto newpsd;                    /* memory write error or map fault */
                temp = (uint32)(dest >> 32);        /* move upper 32 bits to lo 32 bits */
                break;

            case 0:         /* word store */
                temp = (uint32)(dest & FMASK);      /* mask 32 bit of reg */
                if ((addr & 3) != 0) {
                    /* Address fault */
                    TRAPME = ADDRSPEC_TRAP;         /* address not on wd boundry, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                break;

            case 1:         /* left halfword write */
                temp &= RMASK;                      /* mask out 16 left most bits */
                temp |= (uint32)(dest & RMASK) << 16;   /* put into left most 16 bits */
                if ((addr & 1) != 1) {
                    /* Address fault */
                    TRAPME = ADDRSPEC_TRAP;         /* address not on hw boundry, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                break;

            case 3:         /* right halfword write */
                temp &= LMASK;                      /* mask out 16 right most bits */
                temp |= (uint32)(dest & RMASK);     /* put into right most 16 bits */
                if ((addr & 3) != 3) {
                    TRAPME = ADDRSPEC_TRAP;         /* address not on hw boundry, error */
                    goto newpsd;                    /* go execute the trap now */
                }
                break;

            case 4:
            case 5:
            case 6:
            case 7:         /* byte store operation */
                temp &= ~(0xFF << (8 * (7 - FC)));  /* clear the byte to store */
                temp |= (uint32)(dest & 0xFF) << (8 * (7 - FC));    /* insert new byte */
                break;
            }
            /* store back the modified memory location */
            if ((TRAPME = Mem_write(addr, &temp)))  /* store back to memory */
                goto newpsd;                        /* memory write error or map fault */
        }

        /* Update condition code registers */
        if (i_flags & SCC) {
            PSD1 &= 0x87FFFFFE;                     /* clear the old CC's */
            if (ovr)                                /* if overflow, set CC1 */
                CC = CC1BIT;                        /* show we had AEXP */
            else
                CC = 0;                             /* no CC's yet */   
            if (dest & DMSIGN)                      /* if neg, set CC3 */
                CC |= CC3BIT;                       /* if neg, set CC3 */
            else if (dest == 0)
                CC |= CC4BIT;                       /* if zero, set CC4 */
            else
                CC |= CC2BIT;                       /* if gtr than zero, set CC2 */
            PSD1 |= CC & 0x78000000;                /* update the CC's in the PSD */
        }

        /* check if we had an arithmetic exception on the last instruction*/
        if (ovr && (MODES & AEXPBIT)) {
            TRAPME = AEXPCEPT_TRAP;                 /* trap the system now */
            goto newpsd;                            /* process the trap */
        }

        /* Update instruction pointer to next instruction */
        if ((i_flags & BT) == 0) {                  /* see if PSD was replaced on a branch instruction */
            /* branch not taken, so update the PC */
            if (EXM_EXR != 0) {                     /* special handling for EXM, EXR, EXRR */
                PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);
                EXM_EXR = 0;                        /* reset PC increment for EXR */
            } else
            if (i_flags & HLF) {                    /* if nop in rt hw, bump pc a word */
                if ((drop_nop) && ((CPU_MODEL == MODEL_67) || (CPU_MODEL == MODEL_V6)))
                {    
                    PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);
                } else {
                    PSD1 = (PSD1 + 2) | (((PSD1 & 2) >> 1) & 1);
                }
            } else {
                PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);
            }
            drop_nop = 0;                           /* no NOP to drop */
        } else {
            EXM_EXR = 0;                            /* reset PC increment for EXR */
            drop_nop = 0;                           /* no NOP to drop */
        }

        OPSD1 &= 0x87FFFFFE;                        /* clear the old CC's */
        OPSD1 |= PSD1 & 0x78000000;                 /* update the CC's in the PSD */
        /* TODO Update other history information for this instruction */
        if (hst_lnt) {
            hst[hst_p].opsd1 = OPSD1;               /* update the CC in opsd1 */
            hst[hst_p].npsd1 = PSD1;                /* save new psd1 */
            hst[hst_p].npsd2 = PSD2;                /* save new psd2 */
            hst[hst_p].modes = MODES;               /* save current mode bits */
            hst[hst_p].modes |= (CPUSTATUS & BIT24);    /* save blocking mode bit */
            for (ix=0; ix<8; ix++) {
                hst[hst_p].reg[ix] = GPR[ix];       /* save reg */
                hst[hst_p].reg[ix+8] = BR[ix];      /* save breg */
            }
        }

        /* DEBUG_INST support code */
        /* output mapped/unmapped */
        if (MODES & BASEBIT)
            BM = 'B';
        else
            BM = 'N';
        if (MODES & MAPMODE)
            MM = 'M';
        else
            MM = 'U';
        if (CPUSTATUS & BIT24)
            BK = 'B';
        else
            BK = 'U';
        sim_debug(DEBUG_INST, &cpu_dev, "%c%c%c %.8x %.8x %.8x ",
            BM, MM, BK, OPSD1, PSD2, OIR);
        if (cpu_dev.dctrl & DEBUG_INST) {
            fprint_inst(sim_deb, OIR, 0);           /* display instruction */
        sim_debug(DEBUG_INST, &cpu_dev,
            "\n\tR0=%.8x R1=%.8x R2=%.8x R3=%.8x", GPR[0], GPR[1], GPR[2], GPR[3]);
        sim_debug(DEBUG_INST, &cpu_dev,
            " R4=%.8x R5=%.8x R6=%.8x R7=%.8x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
        if (MODES & BASEBIT) {
            sim_debug(DEBUG_INST, &cpu_dev,
                "\tB0=%.8x B1=%.8x B2=%.8x B3=%.8x", BR[0], BR[1], BR[2], BR[3]);
            sim_debug(DEBUG_INST, &cpu_dev,
                " B4=%.8x B5=%.8x B6=%.8x B7=%.8x\n", BR[4], BR[5], BR[6], BR[7]);
        }
        }
#ifdef BAD_02102022
        SPAD[0xf5] = PSD2;                          /* save the current PSD2 */
        SPAD[0xf9] = CPUSTATUS;                     /* save the cpu status in SPAD */
#endif
        continue;                                   /* keep running */

newpsd:
#ifndef BAD_02102022
        SPAD[0xf5] = PSD2;                          /* save the current PSD2 */
        SPAD[0xf9] = CPUSTATUS;                     /* save the cpu status in SPAD */
#endif
        /* Trap Context Block - 6 words */
        /* WD1  Old PSD Wd 1 */
        /* WD2  Old PSD Wd 2 */
        /* WD3  New PSD WD 1 */
        /* WD4  New PSD Wd 2 */
        /* WD5  Multi Use */            /* N/U for Interrupts */
        /* WD6  Multi Use */            /* N/U for Interrupts */

        /* WD5  Multi Use */            /* IOCL address for I/O */
        /* WD6  Multi Use */            /* Status address for I/O */

        /* WD5  Multi Use */            /* Secondary vector table for SVC */
        /* WD6  Multi Use */            /* N/U for SVC */

        /* WD5  Multi Use */            /* Trap status word for traps */
        /* WD6  Multi Use */            /* N/U for traps */

        /* WD5  Multi Use */            /* Trap status word for page faults */
        /* WD6  Multi Use */            /* Page fault status word */
            /* Bit 0 = 0  The map fault was caused by an instruction fetch */
            /*       = 1  The mp fault was caused by an operand access */
            /* Bits 1-20  Always zero */
            /* Map register number (logical map block number) */

        /* we get here from a LPSD, LPSDCM, INTR, or TRAP */
        if (TRAPME) {
            /* SPAD location 0xf0 has trap vector base address */
            uint32 tta = SPAD[0xf0];                /* get trap table address in memory */
            uint32 tvl;                             /* trap vector location */
            if ((tta == 0) || ((tta&MASK24) == MASK24))
                tta = 0x80;                         /* if not set, assume 0x80 FIXME */
            /* Trap Table Address in memory is pointed to by SPAD 0xF0 */
            /* TODO update cpu status and trap status words with reason too */
            switch(TRAPME) {
            case POWERFAIL_TRAP:                    /* 0x80 PL00/PL01 power fail trap */
            case POWERON_TRAP:                      /* 0x84 PL00/PL01 Power-On trap */
            case MEMPARITY_TRAP:                    /* 0x88 PL02 Memory Parity Error trap */
            case NONPRESMEM_TRAP:                   /* 0x8C PL03 Non Present Memory trap */
            case UNDEFINSTR_TRAP:                   /* 0x90 PL04 Undefined Instruction Trap */
            case PRIVVIOL_TRAP:                     /* 0x94 PL05 Privlege Violation Trap */
//MOVED     case SVCCALL_TRAP:                      /* 0x98 PL06 Supervisor Call Trap */
            case MACHINECHK_TRAP:                   /* 0x9C PL07 Machine Check Trap */
            case SYSTEMCHK_TRAP:                    /* 0xA0 PL08 System Check Trap */
            case MAPFAULT_TRAP:                     /* 0xA4 PL09 Map Fault Trap */
            case IPUUNDEFI_TRAP:                    /* 0xA8 PL0A IPU Undefined Instruction Trap */
//MOVED     case CALM_TRAP:                         /* 0xA8 PL0A Call Monitor Instruction Trap */
            case SIGNALIPU_TRAP:                    /* 0xAC PL0B Signal IPU/CPU Trap */
            case ADDRSPEC_TRAP:                     /* 0xB0 PL0C Address Specification Trap */
//BAD HERE  case CONSOLEATN_TRAP:                   /* 0xB4 PL0D Console Attention Trap */
            case PRIVHALT_TRAP:                     /* 0xB8 PL0E Privlege Mode Halt Trap */
            case AEXPCEPT_TRAP:                     /* 0xBC PL0F Arithmetic Exception Trap */
            case CACHEERR_TRAP:                     /* 0xC0 PL10 Cache Error Trap (V9 Only) */
                /* drop through */
            default:
                sim_debug(DEBUG_TRAP, &cpu_dev,
                "##TRAPME %02x LOAD MAPS PSD1 %08x PSD2 %08x CPUSTATUS %08x drop_nop %1x i_flags %04x\n",
                TRAPME, PSD1, PSD2, CPUSTATUS, drop_nop, i_flags);
                /* adjust PSD1 to next instruction */
                /* Update instruction pointer to next instruction */
                if ((i_flags & BT) == 0) {          /* see if PSD was replaced on a branch instruction */
                    /* branch not taken, so update the PC */
                    if (EXM_EXR != 0) {             /* special handling for EXM, EXR, EXRR */
                        PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);
                        EXM_EXR = 0;                /* reset PC increment for EXR */
                    } else
                    if (i_flags & HLF) {            /* if nop in rt hw, bump pc a word */
                        if ((drop_nop) && ((CPU_MODEL == MODEL_67) || (CPU_MODEL == MODEL_V6))) {    
                            PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);
                        } else {
                            PSD1 = (PSD1 + 2) | (((PSD1 & 2) >> 1) & 1);
                        }
                        drop_nop = 0;
                    } else {
                        PSD1 = (PSD1 + 4) | (((PSD1 & 2) >> 1) & 1);
                        //DIAG fix for test 34/10 in MMM diag, reset bit 31
                        if ((CPU_MODEL == MODEL_87) || (CPU_MODEL == MODEL_97) ||
                            (CPU_MODEL == MODEL_V9))
                            PSD1 &= ~BIT31;         /* force off last right */
                    }
                } else {
                    EXM_EXR = 0;                    /* reset PC increment for EXR */
                    if ((drop_nop) && ((CPU_MODEL == MODEL_67) || (CPU_MODEL >= MODEL_V6)))
                        PSD1 &= ~BIT31;             /* force off last right */
                    drop_nop = 0;
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "##GOT BT TRAPME %04x LOAD MAPS PSD1 %08x PSD2 %08x\n",
                        TRAPME, PSD1, PSD2);
                }
                /* fall through */
                /* do not update pc for page fault */
            case DEMANDPG_TRAP:                     /* 0xC4 Demand Page Fault Trap (V6&V9 Only) */
                if (TRAPME == DEMANDPG_TRAP) {      /* 0xC4 Demand Page Fault Trap (V6&V9 Only) */
                    /* Set map number */
                    if (CPU_MODEL >= MODEL_V9)
                        PSD1 &= ~BIT31;             /* force off last right */
                    /* pfault will have 11 bit page number and bit 0 set if op fetch */
                    sim_debug(DEBUG_TRAP, &cpu_dev,
                    "##PAGEFAULT TRAPS %02x page# %04x LOAD MAPS PSD1 %08x PSD2 %08x CPUSTATUS %08x\n",
                    TRAPME, pfault, PSD1, PSD2, CPUSTATUS);
                }
                /* Moved here 05/28/2021 so PC gets incremented incorrectly */
                /* This caused the 2nd instruction of an int service routine to be skipped */
                /* The attn trap had to be on 2nd instruction */
            case CONSOLEATN_TRAP:                   /* 0xB4 PL0D Console Attention Trap */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "At TRAPME %02x PSD1 %08x PSD2 %08x CPUSTATUS %08x drop_nop %02x\n",
                    TRAPME, PSD1, PSD2, CPUSTATUS, drop_nop);
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "At TRAP %02x IR %08x PSD1 %08x PSD2 %08x CPUSTATUS %08x ovr %01x drop_nop %01x\n",
                    TRAPME, IR, PSD1, PSD2, CPUSTATUS, ovr, drop_nop);
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "R0=%.8x R1=%.8x R2=%.8x R3=%.8x\n", GPR[0], GPR[1], GPR[2], GPR[3]);
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "R4=%.8x R5=%.8x R6=%.8x R7=%.8x\n", GPR[4], GPR[5], GPR[6], GPR[7]);

                tta = tta + (TRAPME - 0x80);        /* tta has mem addr of trap vector */
                if (MODES & (BASEBIT | EXTDBIT))
                    tvl = M[tta>>2] & 0xFFFFFC;     /* get 24 bit trap address from trap vector loc */
                else
                    tvl = M[tta>>2] & 0x7FFFC;      /* get 19 bit trap address from trap vector loc */
                sim_debug(DEBUG_TRAP, &cpu_dev,
                    "tvl %08x, tta %08x status %08x page# %04x\n", tvl, tta, CPUSTATUS, pfault);
#ifndef TEMP_CHANGE_FOR_MPX3X_DEBUG
                if (tvl == 0 || (CPUSTATUS & 0x40) == 0) {
#else
                /* next line changed to force halt on halt trap */
                /* TRIED 041320 for MPX3.X install and testing */
                if (((tvl == 0) || (CPUSTATUS & 0x40) == 0) || 
                    (TRAPME == PRIVHALT_TRAP)) {    /* 0xB8 PL0E Privlege Mode Halt Trap */
#endif
                    /* vector is zero or software has not enabled traps yet */
                    /* execute a trap halt */
                    /* set the PSD to trap vector location */
                    fprintf(stderr, "[][][][][][][][][][] HALT TRAP [][][][][][][][][][]\r\n");
                    fprintf(stderr, "PSD1 %08x PSD2 %08x TRAPME %04x\r\n", PSD1, PSD2, TRAPME);
                    PSD1 = 0x80000000 + TRAPME;     /* just priv and PC to trap vector */
                    PSD2 = 0x00004000;              /* unmapped, blocked interrupts mode */
                    M[0x680>>2] = PSD1;             /* store PSD 1 */
                    M[0x684>>2] = PSD2;             /* store PSD 2 */
                    M[0x688>>2] = TRAPSTATUS;       /* store trap status */
                    M[0x68C>>2] = 0;                /* This will be device table entry later TODO */
                    for (ix=0; ix<8; ix+=2) {
                        fprintf(stderr, "GPR[%d] %08x GPR[%d] %08x\r\n", ix, GPR[ix], ix+1, GPR[ix+1]);
                    }
                    if (MODES & BASEBIT) {
                        for (ix=0; ix<8; ix+=2) {
                            fprintf(stderr, "BR[%d] %08x BR[%d] %08x\r\n", ix, BR[ix], ix+1, BR[ix+1]);
                        }
                    }
                    fprintf(stderr, "[][][][][][][][][][] HALT TRAP [][][][][][][][][][]\r\n");
                    return STOP_HALT;               /* exit to simh for halt */
                } else {
                    uint32 oldstatus = CPUSTATUS;   /* keep for retain blocking state */
                    /* valid vector, so store the PSD, fetch new PSD */
                    bc = PSD2 & 0x3ff8;             /* get copy of cpix */
                    if ((TRAPME) && ((CPU_MODEL <= MODEL_27))) {
                            /* Traps on 27 have bit 31 reset */
                            M[tvl>>2] = PSD1 & 0xfffffffe;  /* store PSD 1 */
                    } else
                        M[tvl>>2] = PSD1 & 0xffffffff;  /* store PSD 1 */
                    M[(tvl>>2)+1] = PSD2;           /* store PSD 2 */
                    PSD1 = M[(tvl>>2)+2];           /* get new PSD 1 */
                    PSD2 = (M[(tvl>>2)+3] & ~0x3ff8) | bc;  /* get new PSD 2 w/old cpix */
                    M[(tvl>>2)+4] = TRAPSTATUS;     /* store trap status */
                    if (TRAPME == DEMANDPG_TRAP) {  /* 0xC4 Demand Page Fault Trap (V6&V9 Only) */
                        M[(tvl>>2)+5] = pfault;     /* store page fault number */
                        sim_debug(DEBUG_TRAP, &cpu_dev,
                            "DPAGE tvl %06x PSD1 %08x PSD2 %08x TRAPME %04x TRAPSTATUS %08x\n",
                            tvl, PSD1, PSD2, TRAPME, pfault);
                    }

                    /* set the mode bits and CCs from the new PSD */
                    CC = PSD1 & 0x78000000;         /* extract bits 1-4 from PSD1 */
                    MODES = PSD1 & 0x87000000;      /* extract bits 0, 5, 6, 7 from PSD 1 */
                    CPUSTATUS &= ~0x87000000;       /* reset bits in CPUSTATUS */
                    CPUSTATUS |= MODES;             /* now insert into CPUSTATUS */

                    /* set new map mode and interrupt blocking state in CPUSTATUS */
                    if (PSD2 & MAPBIT) {
                        CPUSTATUS |= BIT8;          /* set bit 8 of cpu status */
                        MODES |= MAPMODE;           /* set mapped mode */
                    } else {
                        CPUSTATUS &= ~BIT8;         /* reset bit 8 of cpu status */
/*TRY_01072022*/        MODES &= ~MAPMODE;          /* reset mapped mode */
                    }

                    /* set interrupt blocking state */
                    if ((PSD2 & RETBBIT) == 0) {    /* is it retain blocking state */
                        if (PSD2 & SETBBIT) {       /* no, is it set blocking state */
                            CPUSTATUS |= BIT24;     /* yes, set blk state in cpu status bit 24 */
                            MODES |= BLKMODE;       /* set blocked mode */
                        } else {
                            CPUSTATUS &= ~BIT24;    /* no, reset blk state in cpu status bit 24 */
                            MODES &= ~BLKMODE;      /* reset blocked mode */
                        }
                    } else {
                        /* handle retain blocking state */
                        PSD2 &= ~RETMBIT;           /* turn off retain bit in PSD2 */
                        /* set new blocking state in PSD2 */
                        PSD2 &= ~(SETBBIT|RETBBIT); /* clear bit 48 & 49 to be unblocked */
                        MODES &= ~(BLKMODE|RETBLKM);/* reset blocked & retain mode bits */
                        if (oldstatus & BIT24) {    /* see if old mode is blocked */
                            PSD2 |= SETBBIT;        /* set to blocked state */
                            MODES |= BLKMODE;       /* set blocked mode */
                        }
                    }

                    SPAD[0xf5] = PSD2;              /* save the current PSD2 */
                    SPAD[0xf9] = CPUSTATUS;         /* save the cpu status in SPAD */

                    sim_debug(DEBUG_TRAP, &cpu_dev,
                        "Process TRAPME %04x PSD1 %08x PSD2 %08x CPUSTATUS %08x\n",
                        TRAPME, PSD1, PSD2, CPUSTATUS);
                    /* TODO provide page fault data to word 6 */
                    if (TRAPME == DEMANDPG_TRAP) {  /* 0xC4 Demand Page Fault Trap (V6&V9 Only) */
                        /* Set map number */
                        /* pfault will have 11 bit page number and bit 0 set if op fetch */
                        sim_debug(DEBUG_TRAP, &cpu_dev,
            "PAGE TRAP %04x TSTAT %08x LOAD MAPS PSD1 %08x PSD2 %08x CPUSTAT %08x pfault %08x\n",
                        TRAPME, TRAPSTATUS, PSD1, PSD2, CPUSTATUS, pfault);
                    }
                    TRAPSTATUS = CPUSTATUS & 0x57;  /* clear all trap status except cpu type */
                    break;                          /* Go execute the trap */
                }
                break;
            }
        }
        /* we have a new PSD loaded via a LPSD or LPSDCM */
        /* finish instruction history, then continue */
        /* update cpu status word too */
        OPSD1 &= 0x87FFFFFF;                        /* clear the old CC's */
        OPSD1 |= PSD1 & 0x78000000;                 /* update the CC's in the PSD */
        /* Update other history information for this instruction */
        if (hst_lnt) {
            hst[hst_p].opsd1 = OPSD1;               /* update the CC in opsd1 */
            hst[hst_p].npsd1 = PSD1;                /* save new psd1 */
            hst[hst_p].npsd2 = PSD2;                /* save new psd2 */
            hst[hst_p].modes = MODES;               /* save current mode bits */
            hst[hst_p].modes |= (CPUSTATUS & BIT24);    /* save blocking mode bit */
            for (ix=0; ix<8; ix++) {
                hst[hst_p].reg[ix] = GPR[ix];       /* save reg */
                hst[hst_p].reg[ix+8] = BR[ix];      /* save breg */
            }
        }

        /* DEBUG_INST support code */
        /* output mapped/unmapped */
        if (MODES & BASEBIT)
            BM = 'B';
        else
            BM = 'N';
        if (MODES & MAPMODE)
            MM = 'M';
        else
            MM = 'U';
        if (CPUSTATUS & BIT24)
            BK = 'B';
        else
            BK = 'U';
        sim_debug(DEBUG_INST, &cpu_dev, "%c%c%c %.8x %.8x %.8x ",
            BM, MM, BK, OPSD1, PSD2, OIR);
        if (cpu_dev.dctrl & DEBUG_INST) {
            fprint_inst(sim_deb, OIR, 0);    /* display instruction */
        sim_debug(DEBUG_INST, &cpu_dev,
            "\n\tR0=%.8x R1=%.8x R2=%.8x R3=%.8x", GPR[0], GPR[1], GPR[2], GPR[3]);
        sim_debug(DEBUG_INST, &cpu_dev,
            " R4=%.8x R5=%.8x R6=%.8x R7=%.8x\n", GPR[4], GPR[5], GPR[6], GPR[7]);
        if (MODES & BASEBIT) {
            sim_debug(DEBUG_INST, &cpu_dev,
                "\tB0=%.8x B1=%.8x B2=%.8x B3=%.8x", BR[0], BR[1], BR[2], BR[3]);
            sim_debug(DEBUG_INST, &cpu_dev,
            " B4=%.8x B5=%.8x B6=%.8x B7=%.8x\n", BR[4], BR[5], BR[6], BR[7]);
        }
        }
        continue;                                   /* single step cpu just for now */
    }   /* end while */

    /* Simulation halted */
    return reason;
}

/* these are the default ipl devices defined by the CPU jumpers */
/* they can be overridden by specifying IPL device at ipl time */
uint32 def_disk = 0x0800;                           /* disk channel 8, device 0 */
uint32 def_tape = 0x1000;                           /* tape device 10, device 0 */
uint32 def_floppy = 0x7ef0;                         /* IOP floppy disk channel 7e, device f0 */

/* Reset routine */
/* do any one time initialization here for cpu */
t_stat cpu_reset(DEVICE *dptr)
{
    int     i;
    t_stat  devs = SCPE_OK;

    /* leave regs alone so values can be passed to boot code */
    PSD1 = 0x80000000;                              /* privileged, non mapped, non extended, address 0 */
    PSD2 = 0x00004000;                              /* blocked interrupts mode */
    MODES = (PRIVBIT | BLKMODE);                    /* set modes to privileged and blocked interrupts */
    CC = 0;                                         /* no CCs too */
    CPUSTATUS = CPU_MODEL;                          /* clear all cpu status except cpu type */
    CPUSTATUS |= PRIVBIT;                           /* set privleged state bit 0 */
    CPUSTATUS |= BIT24;                             /* set blocked mode state bit 24 */
    CPUSTATUS |= BIT22;                             /* set HS floating point unit not present bit 22 */
    TRAPSTATUS = CPU_MODEL;                         /* clear all trap status except cpu type */
    CMCR = 0;                                       /* No Cache Enabled */
    SMCR = 0;                                       /* No Shared Memory Enabled */
    CMSMC = 0x00ff0a10;                             /* No V9 Cache/Shadow Memory Configuration */
    CSMCW = 0;                                      /* No V9 CPU Shadow Memory Configuration */
    ISMCW = 0;                                      /* No V9 IPU Shadow Memory Configuration */
    RDYQIN = RDYQOUT = 0;                           /* initialize channel ready queue */

    devs = chan_set_devs();                         /* set up the defined devices on the simulator */

    /* set default breaks to execution tracing */
    sim_brk_types = sim_brk_dflt = SWMASK('E');
    /* zero regs */
    for (i = 0; i < 8; i++) {
        GPR[i] = BOOTR[i];                          /* set boot register values */
        BR[i] = 0;                                  /* clear the registers */
    }

    /* set console switch settings */
    M[0x780>>2] = CSW;                              /* set console switch settings */

    /* zero interrupt status words */
    for (i = 0; i < 112; i++)
        INTS[i] = 0;                                /* clear interrupt status flags */

    /* add code here to initialize the SEL32 cpu scratchpad on initial start */
    /* see if spad setup by software, if yes, leave spad alone */
    /* otherwise set the default values into the spad */
    /* CPU key is 0xECDAB897, IPU key is 0x13254768 */
    /* Keys are loaded by the O/S software during the boot loading sequence */
    if (SPAD[0xf7] != 0xecdab897)
    {
        int ival = 0;                               /* init value for concept 32 */

        if (CPU_MODEL < MODEL_27)
            ival = 0xfffffff;                       /* init value for 32/7x int and dev entries */
        for (i = 0; i < 1024; i++)
            MAPC[i] = 0;                            /* clear 2048 halfword map cache */
        for (i = 0; i < 224; i++)
            SPAD[i] = ival;                         /* init 128 devices and 96 ints in the spad */
        for (i = 224; i < 256; i++)                 /* clear the last 32 extries */
            SPAD[i] = 0;                            /* clear the spad */
        SPAD[0xf0] = 0x80;                          /* default Trap Table Address (TTA) */
        SPAD[0xf1] = 0x100;                         /* Interrupt Table Address (ITA) */
        SPAD[0xf2] = 0x700;                         /* IOCD Base Address */
        SPAD[0xf3] = 0x788;                         /* Master Process List (MPL) table address */
        SPAD[0xf4] = def_tape;                      /* Default IPL address from console IPL command or jumper */
        SPAD[0xf5] = PSD2;                          /* current PSD2 defaults to blocked */
        SPAD[0xf6] = 0;                             /* reserved (PSD1 ??) */
        SPAD[0xf7] = 0xecdab897;                    /* load the CPU key */
        SPAD[0xf8] = 0x0000f000;                    /* set DRT to class f (anything else is E) */
        SPAD[0xf9] = CPUSTATUS;                     /* set default cpu type in cpu status word */
        SPAD[0xff] = 0x00ffffff;                    /* interrupt level 7f 1's complament */
    }
#if 0
    /* set low memory bootstrap code */
    /* moved to boot code in sel32_chan.c so we can reset system and not destroy memory */
    M[0] = 0x02000000;                              /* 0x00 IOCD 1 read into address 0 */
    M[1] = 0x60000078;                              /* 0x04 IOCD 1 CMD Chain, Suppress incor len, 120 bytes */
    M[2] = 0x53000000;                              /* 0x08 IOCD 2 BKSR or RZR to re-read boot code */
    M[3] = 0x60000001;                              /* 0x0C IOCD 2 CMD chain,Supress incor length, 1 byte */
    M[4] = 0x02000000;                              /* 0x10 IOCD 3 Read into address 0 */
    M[5] = 0x000006EC;                              /* 0x14 IOCD 3 Read 0x6EC bytes */
#endif
    loading = 0;                                    /* not loading yet */
    /* we are good to go or error from device setup */
    if (devs != SCPE_OK)
        return devs;
    return SCPE_OK;
}

/* Memory examine */
/* examine a 32bit memory location and return a byte */
t_stat cpu_ex(t_value *vptr, t_addr baddr, UNIT *uptr, int32 sw)
{
    uint32 status, realaddr, prot;
    uint32 addr = (baddr & 0xfffffc) >> 2;          /* make 24 bit byte address into word address */

    if (sw & SWMASK('V')) {
        /* convert address to real physical address */
        status = RealAddr(addr, &realaddr, &prot, MEM_RD);
        sim_debug(DEBUG_CMD, &cpu_dev, "cpu_ex Mem_read status = %02x\n", status);
        if (status == ALLOK) {
            *vptr = (M[realaddr] >> (8 * (3 - (baddr & 0x3))));  /* return memory contents */
            return SCPE_OK;                         /* we are all ok */
        }
        return SCPE_NXM;                            /* no, none existant memory error */
    }
    /* MSIZE is in 32 bit words */
    if (!MEM_ADDR_OK(addr))                         /* see if address is within our memory */
        return SCPE_NXM;                            /* no, none existant memory error */
    if (vptr == NULL)                               /* any address specified by user */
        return SCPE_OK;                             /* no, just ignore the request */
    *vptr = (M[addr] >> (8 * (3 - (baddr & 0x3)))); /* return memory contents */
    return SCPE_OK;                                 /* we are all ok */
}

/* Memory deposit */
/* modify a byte specified by a 32bit memory location */
/* address is byte address with bits 30,31 = 0 */
t_stat cpu_dep(t_value val, t_addr baddr, UNIT *uptr, int32 sw)
{
    uint32 addr = (baddr & 0xfffffc) >> 2;          /* make 24 bit byte address into word address */
    static const uint32 bmasks[4] = {0x00FFFFFF, 0xFF00FFFF, 0xFFFF00FF, 0xFFFFFF00};

    /* MSIZE is in 32 bit words */
    if (!MEM_ADDR_OK(addr))                         /* see if address is within our memory */
        return SCPE_NXM;                            /* no, none existant memory error */
    val = (M[addr] & bmasks[baddr & 0x3]) | (val << (8 * (3 - (baddr & 0x3))));
    M[addr] = val;                                  /* set new value */
    return SCPE_OK;                                 /* all OK */
}

/* set the CPU memory size */
/* table values are in words, not bytes */
uint32 memwds [] = {
    0x008000,   /* size index 0 - 128KB =  32KW */
    0x010000,   /*            1 - 256KB =  64KW */
    0x020000,   /*            2 - 512KB = 128KW */
    0x040000,   /*            3 -   1MB = 256KW */
    0x080000,   /*            4 -   2MB = 512KW */
    0x0c0000,   /*            5 -   3MB = 768KW */
    0x100000,   /*            6 -   4MB =   1MW */
    0x180000,   /*            7 -   6MB = 1.5MW */
    0x200000,   /*            8 -   8MB =   2MW */
    0x300000,   /*            9 -  12MB =   3MW */
    0x400000,   /*           10 -  16MB =   4MW */
};

t_stat cpu_set_size(UNIT *uptr, int32 sval, CONST char *cptr, void *desc)
{
    uint32      i;
    uint32      sz;
    int32       val = (int32)sval;
    t_addr      msize;

    val >>= UNIT_V_MSIZE;                           /* shift index right 19 bits */
    if (val >= (int32)(sizeof(memwds)/sizeof(uint32)))  /* is size valid */
        return SCPE_ARG;                            /* nope, argument error */
    sz = memwds[val];                               /* (128KB/4) << index == memory size in KW */
    if ((sz <= 0) || (sz > MAXMEMSIZE))             /* is size valid */
        return SCPE_ARG;                            /* nope, argument error */
    msize = sz << 2;                                /* Convert to words */
    if (msize < MEMSIZE) {                          /* is size smaller */
        uint32 mc = 0;                              /* yes, see if larger memory was used */
        for (i = sz-1; i < (MEMSIZE>>2); i++)
            mc = mc | M[i];                         /* or in any bits we might find */
        if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
            return SCPE_OK;                         /* forget update */
    }
    for (i = (MEMSIZE>>2) - 1; i < sz; i++)
        M[i] = 0;                                   /* zero all of the new memory */
    cpu_unit.flags &= ~UNIT_MSIZE;                  /* clear old size value 0-31 */
    cpu_unit.flags |= val << UNIT_V_MSIZE;          /* set new memory size index value (0-31) */
    cpu_unit.capac = (t_addr)msize;                 /* set new size */
    return SCPE_OK;                                 /* we done */
}

/* Handle execute history */

/* Set history */
t_stat
cpu_set_hist(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    int32               i, lnt;
    t_stat              r;

    if (cptr == NULL) {                             /* check for any user options */
        for (i = 0; i < hst_lnt; i++)               /* none, so just zero the history */
            hst[i].opsd1 = 0;                       /* just psd1 for now */
        hst_p = 0;                                  /* start at the beginning */
        return SCPE_OK;                             /* all OK */
    }
    /* the user has specified options, process them */
    lnt = (int32)get_uint(cptr, 10, HIST_MAX, &r);
    if (r != SCPE_OK)
        return sim_messagef (SCPE_ARG, "Invalid Numeric Value: %s.  Maximum is %d\n", cptr, HIST_MAX);
    if (lnt && (lnt < HIST_MIN))
        return sim_messagef (SCPE_ARG, "%d is less than the minumum history value of %d\n", lnt, HIST_MIN);
    hst_p = 0;                                      /* start at beginning */
    if (hst_lnt) {                                  /* if a new length was input, resize history buffer */
        free(hst);                                  /* out with the old */
        hst_lnt = 0;                                /* no length anymore */
        hst = NULL;                                 /* and no pointer either */
    }
    if (lnt) {                                      /* see if new size specified, if so get new resized bfer */
        hst = (struct InstHistory *)calloc(sizeof(struct InstHistory), lnt);
        if (hst == NULL)
            return SCPE_MEM;                        /* allocation error, so tell user */
        hst_lnt = lnt;                              /* set new length */
    }
    return SCPE_OK;                                 /* we are good to go */
}

/* Show history */
t_stat cpu_show_hist(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    int32               k, di, lnt;
    char               *cptr = (char *) desc;
    t_stat              r;
    uint8               BM, MM, BK;                 /* basemode, mapped mode, blocked mode */
    struct InstHistory *h;

    if (hst_lnt == 0)                               /* see if show history is enabled */
        return SCPE_NOFNC;                          /* no, so we are out of here */
    if (cptr) {                                     /* see if user provided a display count */
        lnt = (int32)get_uint(cptr, 10, hst_lnt, &r);   /* get the count */
        if ((r != SCPE_OK) || (lnt == 0))           /* if error or 0 count */
            return SCPE_ARG;                        /* report argument error */
    } else
        lnt = hst_lnt;                              /* dump all the entries */
    di = hst_p - lnt;                               /* work forward */
    if (di < 0)
        di = di + hst_lnt;                          /* wrap */
    for (k = 0; k < lnt; k++) {                     /* print specified entries */
        if (stop_cpu) {                             /* Control-C (SIGINT) */
            stop_cpu = FALSE;
            break;                                  /* abandon remaining output */
        }
        h = &hst[(++di) % hst_lnt];                 /* entry pointer */
        /* display the instruction and results */
        if (MODES & BASEBIT)
            BM = 'B';
        else
            BM = 'N';
        if (MODES & MAPMODE)
            MM = 'M';
        else
            MM = 'U';
        if (MODES & 0x80)                           /* get blocked bit */
            BK = 'B';
        else
            BK = 'U';
        fprintf(st, "%c%c%c %.8x %.8x %.8x ", BM, MM, BK, h->opsd1, h->npsd2, h->oir);
        if (h->modes & BASEBIT)
            fprint_inst(st, h->oir, SWMASK('M'));   /* display basemode instruction */
        else
            fprint_inst(st, h->oir, SWMASK('N'));   /* display non basemode instruction */
        fprintf(st, "\n");
        fprintf(st, "\tR0=%.8x R1=%.8x R2=%.8x R3=%.8x", h->reg[0], h->reg[1], h->reg[2], h->reg[3]);
        fprintf(st, " R4=%.8x R5=%.8x R6=%.8x R7=%.8x", h->reg[4], h->reg[5], h->reg[6], h->reg[7]);
        if (h->modes & BASEBIT) {
            fprintf(st, "\n");
            fprintf(st, "\tB0=%.8x B1=%.8x B2=%.8x B3=%.8x", h->reg[8], h->reg[9], h->reg[10], h->reg[11]);
            fprintf(st, " B4=%.8x B5=%.8x B6=%.8x B7=%.8x", h->reg[12], h->reg[13], h->reg[14], h->reg[15]);
        }
        fprintf(st, "\n");
    }                                               /* end for */
    return SCPE_OK;                                 /* all is good */
}

/* return description for the specified device */
const char *cpu_description (DEVICE *dptr) 
{
    return "SEL 32 CPU";                            /* return description */
}

t_stat cpu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    fprintf(st, "The CPU can maintain a history of the most recently executed instructions.\n");
    fprintf(st, "This is controlled by the SET CPU HISTORY and SHOW CPU HISTORY commands:\n\n");
    fprintf(st, "   sim> SET CPU HISTORY            clear history buffer\n");
    fprintf(st, "   sim> SET CPU HISTORY=0          disable history\n");
    fprintf(st, "   sim> SET CPU HISTORY=n{:file}   enable history, length = n\n");
    fprintf(st, "   sim> SHOW CPU HISTORY           print CPU history\n");
    return SCPE_OK;
}
