/* eclipse_cpu.c: Eclipse CPU simulator

   Modified from the original NOVA simulator by Robert Supnik.

   Copyright (c) 1998-2012, Charles E Owen
   Portions Copyright (c) 1993-2015, Robert M Supnik

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

   cpu          Eclipse central processor

   30-Mar-15    RMS     Fixed typo in DIVS
   25-Mar-12    RMS     Fixed declarations (Mark Pizzolato)
   07-Jun-06    RMS     Fixed bug in DIVS (Mark Hittinger)
   22-Sep-05    RMS     Fixed declarations (Sterling Garwood)
   25-Aug-05    RMS     Fixed DIVS overflow cases
   29-Nov-03    CEO     Corrected POPJ and Bit operations bugs
   26-Nov-03    CEO     Added FPU and PIT devices 
   20-Feb-03    CEO     Corrected several MMPU and CIS bugs
   28-Jan-02    RMS     Cleaned up compiler warnings
   30-Nov-01    RMS     Added extended SET/SHOW support
   01-Jun-01    RMS     Added second terminal, plotter support
   26-Apr-01    RMS     Added device enable/disable support

   The register state for the Eclipse CPU is basically the same as
   the NOVA's:

   AC[0:3]<0:15>        general registers
   C                    carry flag
   PC<0:14>             program counter

   Eclipses with Folating Point Units added these registers:

   FPAC[0:3]<0:63>      Floating Point Accumulators
   FPSR                 Floating Point Status Register

   In addition, certain low-memory locations are reserved for special
   purposes:
   
   0:     I/O Return Address (from an interrupt)
   1:     I/O (Interrupt) handler address
   2:     System Call handler address (used by SYC instruction)
   3:     Protection Fault handler address
   4:     VECTOR stack pointer (VCT Instruction)
   5:     Current Interrupt Priority mask
   6:     VECTOR stack limit (VCT instruction)
   7:     VECTOR stack fault address (VCT again)
   10:    Block Pointer (later models only)
   11:    Emulation Trap Handler address (microeclipse only)
   20-27: Auto-increment locations (not on microeclipse)
   30-37: Auto-decrement locations (not on microeclipse)
   40:    Stack pointer
   41:    Frame Pointer
   42:    Stack Limit
   43:    Stack fault address
   44:    XOP Origin address
   45:    Floating point fault address
   46:    Commercial fault address (not on microeclipse)
   47:    Reserved, do not use. 
   
   Note:  While all eclipses share most of the "standard" features,
   some models added a few quirks and wrinkles, and other models
   dropped some features or modified others.  Most DG software
   is written for a "standard" Eclipse, and avoids these problem
   areas.  A general overview:

      [subject to major changes as info becomes available!]
   
   Early (e.g. S/100, S/200, C/300) [Front Panel machines]
   
      The first Eclipses had the basic MAP, but certain parts
      were kluged, and these were fixed in later MAP designs.
      The original mapping hardware was termed MAP for Memory 
      Allocate and Protection.  The later design was termed 
      MMPU for Memory Mapping and Protection Unit.  While
      similar in design, the two units are not compatible.
      Also, the C version (C for Commercial) of these early
      CPUs had a feature called "Commercial Instruction Set"
      which contained character manipulation, translation
      between commercial-format numeric data and FPU formats,
      and an elaborate EDIT instruction.  Later models kept
      only the character manipulation part of this and called
      the feature the "Character Instruction Set", leading to
      confusion because the initials of both are CIS.  ARDOS
      is the only DG operating system to support the older 
      MAP.  ZRDOS uses the MMPU, and AOS supports only MMPU.

   Middle (e.g. S/130, C/150, S/230, C/330) [Front Panel]
   
      These are close to a "Standard".  They have the newer,
      fixed MMPU.  Support for the PIT (Programmed Interval
      Timer.  The Commercial (not character) instruction set
      and FPU are optional.  (CIS standard on C models)
   
   Late (C/350, M/600: [Panel]; S/140, S/280 [Virtual Console]) 
   
      All features of the Middle period are included, plus:
      These late Eclipses added a few MMPU wrinkles all their
      own, included support for user maps C and D.  Character
      instruction set is standard, FPU optional.  Also, support
      for the BMC device.

   MicroEclipse-based (S/20, S/120, Desktops) [Virtual cons.]
   
      All features of the Late period, in general, plus:
      Microeclipses dropped support for the auto increment
      and decrement locations at 20-37.  They also added 
      support for invalid instruction traps thru location 11.
      The Desktops have an interface to the "Attached Processor",
      an 8086, at device code 6.  Also, some new CPU device
      features to read states info.  The Character Instruction
      set and FPU are standard on all models.
    
   The Eclipse instruction set is an elaboration of the NOVA's.  The basic
   NOVA set is implemented in it's entireity, plus many new Eclipse
   instructions are added.  Since in theory every possible 16-bit 
   combination is a NOVA instruction, the Eclipse commands are carved
   out of the NOVA set by using the Operate format with the no-load bit
   set to 1 and the skip bits set to 000.  Since this combination is
   in effect a no-op on the NOVA, it was rarely or never used.  The 
   other bits are used to form Eclipse instructions, which have no
   other common format.  To see the instructions, refer to the Eclipse
   section of the instruction decode logic in sim_instr() below.  All
   Eclipse instructions are checked first, so in case of conflict in
   bit patterns, the Eclipse one is executed over the corresponding
   NOVA pattern.  A bizarre exception is LEF mode...which implements
   an instruction called Load Effective Address by taking over the
   Nova I/O format when the LEF mode bit is set and the processor is
   executing in mapped mode.
   
   The following discussion talks about NOVA instructions which are
   Eclipse instructions also.
   
   The NOVA has three instruction formats: memory reference, I/O transfer,
   and operate.  The memory reference format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0| op  | AC  |in| mode|     displacement      |    memory reference
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   <0:4>        mnemonic        action

   00000        JMP             PC = MA
   00001        JMS             AC3 = PC, PC = MA
   00010        ISZ             M[MA] = M[MA] + 1, skip if M[MA] == 0
   00011        DSZ             M[MA] = M[MA] - 1, skip if M[MA] == 0
   001'n        LDA             ACn = M[MA]
   010'n        STA             M[MA] = ACn

   <5:7>        mode            action

   000  page zero direct        MA = zext (IR<8:15>)
   001  PC relative direct      MA = PC + sext (IR<8:15>)
   010  AC2 relative direct     MA = AC2 + sext (IR<8:15>)
   011  AC3 relative direct     MA = AC3 + sext (IR<8:15>)
   100  page zero indirect      MA = M[zext (IR<8:15>)]
   101  PC relative dinirect    MA = M[PC + sext (IR<8:15>)]
   110  AC2 relative indirect   MA = M[AC2 + sext (IR<8:15>)]
   111  AC3 relative indirect   MA = M[AC3 + sext (IR<8:15>)]

   Memory reference instructions can access an address space of 32K words.
   An instruction can directly reference the first 256 words of memory
   (called page zero), as well as 256 words relative to the PC, AC2, or
   AC3; it can indirectly access all 32K words.  If an indirect address
   is in locations 00020-00027, the indirect address is incremented and
   rewritten to memory before use; if in 00030-00037, decremented and
   rewritten.

   The I/O transfer format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 0  1  1| AC  | opcode |pulse|      device     |    I/O transfer
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+

   The IOT instruction sends the opcode, pulse, and specified AC to the
   specified I/O device.  The device may accept data, provide data,
   initiate or cancel operations, or skip on status.

   The operate format is:

     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
   | 1|srcAC|dstAC| opcode |shift|carry|nl|  skip  |    operate
   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
                   \______/ \___/ \___/  |  |  |  |
                       |      |     |    |  |  |  +--- reverse skip sense
                       |      |     |    |  |  +--- skip if C == 0
                       |      |     |    |  +--- skip if result == 0
                       |      |     |    +--- don't load result
                       |      |     +--- carry in (load as is,
                       |      |                    set to Zero,
                       |      |                    set to One,
                       |      |                    load Complement)
                       |      +--- shift (none,
                       |                  left one,
                       |                  right one,
                       |                  byte swap)
                       +--- operation (complement,
                                       negate,
                                       move,
                                       increment,
                                       add complement,
                                       subtract,
                                       add,
                                       and)

   The operate instruction can be microprogrammed to perform operations
   on the source and destination AC's and the Carry flag.

   This routine is the instruction decode routine for the NOVA.
   It is called from the simulator control program to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        HALT instruction
        breakpoint encountered
        infinite indirection loop
        unknown I/O device and STOP_DEV flag set
        I/O error in I/O simulator

   2. Interrupts.  Interrupts are maintained by four parallel variables:

        dev_done        device done flags
        dev_disable     device interrupt disable flags
        dev_busy        device busy flags
        int_req         interrupt requests

      In addition, int_req contains the interrupt enable and ION pending
      flags.  If ION and ION pending are set, and at least one interrupt
      request is pending, then an interrupt occurs.  Note that the 16b PIO
      mask must be mapped to the simulator's device bit mapping.
 
   3. Non-existent memory.  On the NOVA, reads to non-existent memory
      return zero, and writes are ignored.  In the simulator, the
      largest possible memory is instantiated and initialized to zero.
      Thus, only writes need be checked against actual memory size.

   4. Adding I/O devices.  These modules must be modified:

        eclipse_defs.h  add interrupt request definition
        eclipse_cpu.c   add IOT mask, PI mask, and routine to dev_table
        eclipse_sys.c   add pointer to data structures to sim_devices
*/

/*---------------------------------------------------------------------------
**   ECLIPSE Debugging Facilities
**
**   These options are designed to find hard-to-locate flaky bugs by
**   providing special error checking and logging.
**
**   All are controlled by depositing a value into the DEBUG register.
**   A value of zero means no special debugging facilities are turned on.
**   This is the default.  Debugging invokes a performance hit! Use only
**   when necessary. 
**
**   Debugging means logging information to a file, or to a buffer in
**   memory from whence it can be dumped to a file.
**   
**   1XXXXX = Log all instructions executed to file "trace.log". 
**      **CAUTION**:  This means the CPU will run SLOWLY and
**      the resulting trace.log file will be HUGE.  We're talking
**      about a megabyte for each 5 seconds or less of wall clock 
**      time, depending on the speed of your CPU.  Note:  In this
**      mode, interrupts are logged when they are received also.
**
**      Note: when detailed logging is off, the last 4096 or so
**      instructions executed are saved in a memory buffer, and
**      when the sim stops, the "show" command can write this 
**      history information to the file "history.log".  This only
**      works if the DEBUG register is non-zero however, because
**      of the performance hit even this recording makes.  To 
**      dump history, enter the command "show cpu history", with
**      the file "history" spelled correctly and lower case.
**
**   XXXXDD = Log all I/O instructions to or from device number
**      DD.  Log is written to "trace.log", regardless of the
**      setting of the instruction trace flag (1XXXXX).  If both
**      are on, the device traces will be interpersed with the
**      instruction traces -- very useful sometimes.  
**
**   XXX1DD = Device Break.  Does a breakpoint in any I/O to
**      device DD.  Useful, say when a diagnostic gives an 
**      error message - a device break on 11 (TTO) will stop
**      as soon as the error message appears, making the 
**      trace log much shorter to track back on.
**
**   X4XXXX = When this bit is on, the sim will stop if it sees
**      an invalid instruction.  When DEBUG is zero, any such
**      instruction is no-oped with no warning.  When DEBUG is
**      non-zero, but this bit is 0, a warning will be displayed
**      but execution will continue.
**
**   X2XXXX = LEF break.  When A LEF instruction is executed in
**      mapped user space, the sim does a breakpoint right after
**      executing the instruction.
**
**   Whenever the DEBUG register is non-zero, special error checking
**   is enabled in the sim.  This will stop the sim automatically 
**   when a likely error occurs, such as:
**
**      1.  Any execution that reaches, or will reach, location 00000.
**      2.  Any I/O to device 00
**      3.  An interrupt from device 00.
**      4.  An invalid instruction (stop is optional)
**
**   DCHAR Register:  Whenever this is non-zero, a test is made on every
**   character output to the TTO device (master console).  If the character
**   output to that device matches this register, the CPU will break.
**
**   Of course, the standard BREAK register is available for breakpoints
**   as in all the sims based on this standard.
--------------------------------------------------------------------------*/

#include "nova_defs.h"

#define UNIT_V_MICRO    (UNIT_V_UF)                     /* Microeclipse? */
#define UNIT_V_17B      (UNIT_V_UF)                     /* 17 bit MAP */
#define UNIT_V_UP       (UNIT_V_UF)                     /* FPU Enabled */
#define UNIT_V_MSIZE    (UNIT_V_UF+1)                   /* dummy mask */
#define UNIT_MICRO      (1 << UNIT_V_MICRO)
#define UNIT_17B        (1 << UNIT_V_17B)
#define UNIT_UP         (1 << UNIT_V_UP)
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)

uint16 M[MAXMEMSIZE] = { 0 };                           /* memory */
int32 AC[4] = { 0 };                                    /* accumulators */
int32 C = 0;                                            /* carry flag */
int32 saved_PC = 0;                                     /* program counter */
int32 SR = 0;                                           /* switch register */
int32 dev_done = 0;                                     /* device done flags */
int32 dev_busy = 0;                                     /* device busy flags */
int32 dev_disable = 0;                                  /* int disable flags */
int32 iot_enb = -1;                                     /* IOT enables */
int32 int_req = 0;                                      /* interrupt requests */
int32 pimask = 0;                                       /* priority int mask */
int32 pwr_low = 0;                                      /* power fail flag */
int32 ind_max = 15;                                     /* iadr nest limit */
int32 stop_dev = 0;                                     /* stop on ill dev */
int32 old_PC = 0;                                       /* previous PC */
int32 model = 140;                                      /* Model of Eclipse */
int32 speed = 0;                                        /* Delay for each instruction */

int32 XCT_mode = 0;                                     /* 1 if XCT mode */
int32 XCT_inst = 0;                                     /* XCT instruction */
int32 PrevPC = -1;
int32 AMASK = 077777;

struct ndev dev_table[64];                              /* dispatch table */

/* Instruction history buffer */

#define HISTMAX 4096

int32 hnext = 0;                                        /* # of current entry */
int32 hwrap = 0;                                        /* 1 if wrapped */
int32 hmax = HISTMAX;                                   /* Maximum entries b4 wrap */
uint16 hpc[HISTMAX];
uint16 hinst[HISTMAX];
uint16 hinst2[HISTMAX];
uint16 hac0[HISTMAX];
uint16 hac1[HISTMAX];
uint16 hac2[HISTMAX];
uint16 hac3[HISTMAX];
unsigned short hflags[HISTMAX];

/* Flags:       0x01 - carry bit
                0x02 - int enabled
                0x04 - user map a
                0x08 - user map b
                0x10 - user map c
                0x20 - user map d
                0x40 - LEF mode was on
                0x80 - this is an int, not an inst.
                        hpc is return addr
                        hinst is int_req
                        hac0 is device
                        hac1 is int addr
*/
         


/* the Eclipse MAP unit:  This unit is standard in all Eclipse processors
   except for the "original" Eclipses, the S/100, S/200, and C/300.  These
   use a different and more elaborate MMPU that is not compatible with
   the one simulated here.  All subsequent Eclipses, from the S/130 on up
   to the last models S/280 and C/380 use the map simulated here, including
   the MicroEclipses.  There are model-dependent quirks.  That's why we
   have to MODEL register.

   The programming of the MMPU can be found in the LMP instruction, below,
   and in the instructions directed to DEV_MAP.
   
   There are two user maps, called A and B, and four data channel maps,
   A thru D.  They can be enabled/disabled separately.   Some models have
   two extra user maps, C and D.  These are supported where apporpriate. 
   
*/

#define PAGEMASK 01777                                  /* Largest physical page possible */
#define MAPMASK 0101777                                 /* Valid page bits in map */
#define INVALID 0101777                                 /* Mask indicating an invalid page */
int32 MapStat = 0;                                      /* Map status register */
int32 Inhibit = 0;                                      /* !0=inhibit interrupts : */
                                                        /*    1 = single cycle inhibit   */
                                                        /*    2 = inhibit until indirection   */
                                                        /*    3 = inhibit next instruction only */
int32 Enable = 0;                                       /* User map to activate 1=A 2=B */
int32 Usermap = 0;                                      /* Active Map? 0=supvr mode, 1=user A, 2 = user B */
int32 Map[8][32];                                       /* The actual MAPs 0=dch A, 1=A, 2=B, 3-5=dchB-D 6-7 User C-D */
int32 Map31 = 037;                                      /* Map for block 31 in supervisor mode */
int32 SingleCycle = 0;                                  /* Map one LDA/STA */
int32 Check = 0;                                        /* Page Check Register */
int32 Fault = 0;                                        /* Fault register */
int32 MapInit = 0;                                      /* 1 when map initialized */
int32 MapIntMode = 0;                                   /* Save of map user mode when int occurs */

/* The Eclipse Floating Point Unit:  This unit is optional on all Eclipse
   models.
*/

int32 FPSR = 0;                                         /* 32-bit FPU Status Register */
t_int64 FPAC[4] = { 0,0,0,0 };                          /* 4 64-bit Accumulators */
int32 FPFault = 0;                                      /* Save Fault State */

/* Definitions for internal floating point arithmetic */

typedef struct _SHORT_FLOAT {
        int32   short_fract;                            /* Fraction                  */
        short   expo;                                   /* Exponent + 64             */
        uint8   sign;                                   /* Sign                      */
} SHORT_FLOAT;

typedef struct _LONG_FLOAT {
        t_int64 long_fract;                             /* Fraction                  */
        short   expo;                                   /* Exponent + 64             */
        uint8   sign;                                   /* Sign                      */
} LONG_FLOAT;

LONG_FLOAT dfl,dfl2;                                    /* Double Precision Work Fields */
SHORT_FLOAT sfl,sfl2;                                   /* Single Precision Work Fields */
t_int64 tempfp, holdfp;                                 /* Working area for FPAC */
int     shift,m3;
t_int64 lsfract;

void get_sf(SHORT_FLOAT *fl, t_int64 *fpr);
void store_sf(SHORT_FLOAT *fl, t_int64 *fpr);
void get_lf(LONG_FLOAT *fl, t_int64 *fpr);
void store_lf(LONG_FLOAT *fl, t_int64 *fpr);
int normal_sf (SHORT_FLOAT *fl);
int normal_lf (LONG_FLOAT *fl);
int overflow_sf(SHORT_FLOAT *fl);
int overflow_lf(LONG_FLOAT *fl);
int underflow_sf(SHORT_FLOAT *fl);
int underflow_lf(LONG_FLOAT *fl);
int significance_sf(SHORT_FLOAT *fl);
int significance_lf(LONG_FLOAT *fl);
int add_sf(SHORT_FLOAT *fl, SHORT_FLOAT *add_f1, int normal);
int add_lf(LONG_FLOAT *fl, LONG_FLOAT *add_fl, int normal);
int mul_sf(SHORT_FLOAT *fl, SHORT_FLOAT *mul_fl);
int mul_lf(LONG_FLOAT *fl, LONG_FLOAT *mul_fl);
int div_sf(SHORT_FLOAT *fl, SHORT_FLOAT *div_fl);
int div_lf(LONG_FLOAT *fl, LONG_FLOAT *div_fl);

/* Special Debugging Info */ 

int32 Debug_Flags = 0;                                  /* Debug register - selects debug features */
int32 Debug_Char = 0;                                   /* Debug Character Register */

int32 Tron = 0;                                         /* For trace files */
FILE *Trace;


t_stat reason;

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);
t_stat cpu_boot (int32 unitno, DEVICE *dptr);
t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat Debug_Dump (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat Dump_History (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat map_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat map_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat map_reset (DEVICE *dptr);
t_stat map_svc (UNIT *uptr);
t_stat fpu_svc (UNIT *uptr);
int32 GetMap(int32 addr);
int32 PutMap(int32 addr, int32 data);
int32 Debug_Entry(int32 PC, int32 inst, int32 inst2, int32 AC0, int32 AC1, int32 AC2, int32 AC3, int32 flags);
t_stat build_devtab (void);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit descriptor
   cpu_reg      CPU register list
   cpu_mod      CPU modifiers list
*/

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, MAXMEMSIZE) };

REG cpu_reg[] = {
    { ORDATA (PC, saved_PC, 15) },
    { ORDATA (AC0, AC[0], 16) },
    { ORDATA (AC1, AC[1], 16) },
    { ORDATA (AC2, AC[2], 16) },
    { ORDATA (AC3, AC[3], 16) },
    { FLDATA (C, C, 16) },
    { ORDATA (SR, SR, 16) },
    { ORDATA (PI, pimask, 16) },
    { FLDATA (ION, int_req, INT_V_ION) },
    { FLDATA (ION_DELAY, int_req, INT_V_NO_ION_PENDING) },
    { FLDATA (PWR, pwr_low, 0) },
    { ORDATA (INT, int_req, INT_V_ION+1), REG_RO },
    { ORDATA (BUSY, dev_busy, INT_V_ION+1), REG_RO },
    { ORDATA (DONE, dev_done, INT_V_ION+1), REG_RO },
    { ORDATA (DISABLE, dev_disable, INT_V_ION+1), REG_RO },
    { FLDATA (STOP_DEV, stop_dev, 0) },
    { DRDATA (INDMAX, ind_max, 16), REG_NZ + PV_LEFT },
    { ORDATA (DEBUG, Debug_Flags, 16) },
    { ORDATA (DCHAR, Debug_Char, 16) },
    { DRDATA (MODEL, model, 16) },
    { DRDATA (SPEED, speed, 16) },
    { ORDATA (WRU, sim_int_char, 8) },
    { NULL }
};

MTAB cpu_mod[] = {
    { UNIT_MICRO, UNIT_MICRO, "MICRO", "MICRO", NULL },
    { UNIT_MICRO, 0, "STD", "STD", NULL },
    { UNIT_MSIZE, 4096, NULL, "4K", &cpu_set_size },
    { UNIT_MSIZE, 8192, NULL, "8K", &cpu_set_size },
    { UNIT_MSIZE, 12288, NULL, "12K", &cpu_set_size },
    { UNIT_MSIZE, 16384, NULL, "16K", &cpu_set_size },
    { UNIT_MSIZE, 20480, NULL, "20K", &cpu_set_size },
    { UNIT_MSIZE, 24576, NULL, "24K", &cpu_set_size },
    { UNIT_MSIZE, 28672, NULL, "28K", &cpu_set_size },
    { UNIT_MSIZE, 32768, NULL, "32K", &cpu_set_size },
    { UNIT_MSIZE, 65536, NULL, "64K", &cpu_set_size },
    { UNIT_MSIZE, 131072, NULL, "128K", &cpu_set_size },
    { UNIT_MSIZE, 262144, NULL, "256K", &cpu_set_size },
    { UNIT_MSIZE, 524288, NULL, "512K", &cpu_set_size },
    { UNIT_MSIZE, 1048576, NULL, "1024K", &cpu_set_size },
    { UNIT_MSIZE, 0, NULL, "DUMP", &Debug_Dump },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "HISTORY", NULL,
      NULL, &Dump_History },
    { 0 }
};

DEVICE cpu_dev = {
    "CPU", &cpu_unit, cpu_reg, cpu_mod,
    1, 8, 17, 1, 8, 16,
    &cpu_ex, &cpu_dep, &cpu_reset,
    &cpu_boot, NULL, NULL
};

/* MAP data structures

   map_dev      MAP device descriptor
   map_unit     MAP unit descriptor
   map_reg      MAP register list
   map_mod      MAP modifiers list
*/

UNIT map_unit = { UDATA (&map_svc, UNIT_17B, MAXMEMSIZE) };

REG map_reg[] = {
    { ORDATA (STATUS, MapStat, 16) },
    { ORDATA (ENABLE, Enable, 16) },
    { ORDATA (IINHIB, Inhibit, 16) },
    { ORDATA (ACTIVE, Usermap, 16) },
    { ORDATA (MAP31, Map31, 16) },
    { ORDATA (CYCLE, SingleCycle, 16) },
    { ORDATA (CHECK, Check, 16) },
    { ORDATA (FAULT, Fault, 16) },
    { NULL }
};

MTAB map_mod[] = {
    { UNIT_17B, UNIT_17B, "17bit", "17B", NULL },
    { UNIT_17B, 0, "19bit", "19B", NULL },
    { 0 }
};

DEVICE map_dev = {
    "MAP", &map_unit, map_reg, map_mod,
    1, 8, 17, 1, 8, 16,
    &map_ex, &map_dep, NULL,
    NULL, NULL, NULL
};

/* FPU data structures

   fpu_dev      MAP device descriptor
   fpu_unit     MAP unit descriptor
   fpu_reg      MAP register list
   fpu_mod      MAP modifiers list
*/

UNIT fpu_unit = { UDATA (&fpu_svc, UNIT_UP, MAXMEMSIZE) };

REG fpu_reg[] = {
    { ORDATA (STATUS, FPSR, 32) },
    { ORDATA (FPAC0, FPAC[0], 64) },
    { ORDATA (FPAC1, FPAC[1], 64) },
    { ORDATA (FPAC2, FPAC[2], 64) },
    { ORDATA (FPAC3, FPAC[3], 64) },
    { ORDATA (FAULT, FPFault, 32) },
    { NULL }
};

MTAB fpu_mod[] = {
    { UNIT_UP, UNIT_UP, "Enabled (UP)", "UP", NULL },
    { UNIT_UP, 0, "Disabled (DOWN)", "DOWN", NULL },
    { 0 }
};

DEVICE fpu_dev = {
    "FPU", &fpu_unit, fpu_reg, fpu_mod,
    1, 16, 17, 1, 16, 16,
    NULL, NULL, NULL,
    NULL, NULL, NULL
};


/* ---- Programmable Interval Timer Device ----------- */

int32 pit_time = 100;
int32 pit_tps = 10000;                                  /* ticks per sec */
int32 pit_adj = 20;                                     /* tmxr adjust */
int32 pit_poll = 16000;                                 /* tmxr poll */
int32 pit_initial = 0;                                  /* initial counter reg */
int32 pit_counter = 0;                                  /* Counter */
int32 pit_flag = 0;                                     /* Initial setting flag */

int32 pit (int32 pulse, int32 code, int32 AC);
t_stat pit_svc (UNIT *uptr);
t_stat pit_reset (DEVICE *dptr);
    
/* PIT data structures

   pit_dev       device descriptor
   pit_unit      unit descriptor
   pit_reg       register list
*/

DIB pit_dib = { DEV_PIT, INT_PIT, PI_PIT, &pit };

UNIT pit_unit = { UDATA (&pit_svc, 0, 0) };

REG pit_reg[] = {
    { ORDATA (INIT, pit_initial, 16) },
    { ORDATA (COUNT, pit_counter, 16) },
    { FLDATA (BUSY, dev_busy, INT_V_PIT) },
    { FLDATA (DONE, dev_done, INT_V_PIT) },
    { FLDATA (DISABLE, dev_disable, INT_V_PIT) },
    { FLDATA (INT, int_req, INT_V_PIT) },
    { DRDATA (TIME0, pit_time, 24), REG_NZ + PV_LEFT },
    { NULL }
};

DEVICE pit_dev = {
    "PIT", &pit_unit, pit_reg, NULL,
    1, 0, 0, 0, 0, 0,
    NULL, NULL, &pit_reset,
    NULL, NULL, NULL,
    &pit_dib, 0
};

t_stat sim_instr (void)
{
register int32 PC, IR, i, t, MA, j, k, tac;
register uint32 mddata, uAC0, uAC1, uAC2, uAC3;
int16 sAC0, sAC1, sAC2;
int32 sddata, mi1, mi2, fpnum32;
t_int64 fpnum, expon;
t_value simeval[20];
void mask_out (int32 mask);
/* char debstr[128]; */
/* char debadd[64]; */
char debmap[4], debion[4];
int debcar, iodev, iodata, debflags;
int32 DisMap, debpc;
/* int32 sp, sl; */
int cmdptr, cmsptr, cmopt, cmptr;
int16 cmslen, cmdlen;
int tabaddr, tabptr;
int32 effective(int32 PC, int32 index, int32 disp);
int32 indirect(int32 d);
int32 LEFmode(int32 PC, int32 index, int32 disp, int32 indirect);
int32 LoadMap(int32 w);
int32 Bytepointer(int32 PC, int32 index);
int32 unimp(int32 PC);
int32 pushrtn(int32 pc);

/* Restore register state */

if (build_devtab () != SCPE_OK) return SCPE_IERR;       /* build dispatch */
PC = saved_PC & AMASK;                                  /* load local PC */
C = C & 0200000;
mask_out (pimask);                                      /* reset int system */
reason = 0;
if (MapInit == 0) {
    MapInit = 1;
    for (mi1 = 0; mi1 < 6; mi1++) {                     /* Initialize MAPs */
        for (mi2 = 0; mi2 < 32; mi2++) {
            Map[mi1][mi2] = mi2;
        }
    }
}            

/* Main instruction fetch/decode loop */

while (reason == 0) {                                   /* loop until halted */
if (sim_interval <= 0) {                                /* check clock queue */
    if ((reason = sim_process_event ())) 
        break;
}

//if (speed > 0) for (i = 0; i < speed; i++) { j = 0; }

if (Fault) {                                            /* Check MAP fault */
    Usermap = 0;                                        /* YES: shutdown map */
    MapStat &= ~01;                                     /* Disable MMPU */
    if (Fault & 0100000/*!!!*/)                         /* If it was validity, or WP */
        MapStat &= ~0170;                               /* Reset other checkbits */
    MapStat |= Fault & 077777;                          /* Put in fault code */
    Fault = 0;                                          /* Reset fault code */
    t = (GetMap(040) + 1) & AMASK;                      /* Push rtn block */            
    PutMap(t, AC[0]);
    t++;
    PutMap(t, AC[1]);
    t++;
    PutMap(t, AC[2]);
    t++;
    PutMap(t, AC[3]);
    t++;
    PutMap(t, (PC & AMASK));
    if (C) PutMap(t, (GetMap(t) | 0100000));
    PutMap(040, t);
    int_req = int_req & ~INT_ION;                       /* Disable interrupts */
    PC = indirect(M[003]);                              /* JMP to loc 3 */
    continue;
}

if (FPSR & 0xF8000000) {                                /* FPU Fault? */
    if (!(FPSR & 0x78000000)) {                         /* If error bit on ... */
        FPSR &= 0x00FFFFFF;                             /* ...but no error, clear it */
    } else {                                            /* ELSE a real error: */
        FPSR |= 0x80000000;                             /* Turn error bit on */
        if (FPSR & 0x04000000) {                        /* Trap enabled ? */
            FPFault = FPSR;                             /* Save fault */
            FPSR &= 0xFBFFFFFF;                         /* Clear Trap Enable */
        }
    }
}

if (int_req > INT_PENDING && !Inhibit) {                /* interrupt? */
    int_req = int_req & ~INT_ION;
    MapIntMode = MapStat;                               /* Save Status as it was */
    Usermap = 0;                                        /* Inhibit MAP */
    MapStat &= ~1;                                      /* Disable user map */
    if (XCT_mode) {
        M[0] = PC - 1;                                  /* If XCT mode rtn to XCT */
        XCT_mode = 0;                                   /* turn off mode */
    } else {
        M[0] = PC;                                      /* Save Return Address */
    }
    old_PC = PC;
    MA = M[1];
    for (i = 0; i < ind_max * 2; i++) {                 /* count indirects */
        if ((MA & 0100000) == 0) break;
        if ((MA & 077770) == 020)
            MA = (M[MA & AMASK] = (M[MA & AMASK] + 1) & 0177777);
        else if ((MA & 077770) == 030)
            MA = (M[MA & AMASK] = (M[MA & AMASK] - 1) & 0177777);
        else MA = M[MA & AMASK];
    }
    if (i >= (ind_max-1)) {
        if ((MapStat & 010) && Usermap) {
            Fault = 04000;                          /* Map fault if IND prot */
            continue;
        } else {
             reason = STOP_IND_INT;
             break;
        }   
    }
    if (Debug_Flags) {
        iodev = 0;
        iodata = int_req & (-int_req);
        for (i = DEV_LOW; i <= DEV_HIGH; i++)  {
            if (iodata & dev_table[i].mask) {
                iodev = i;
                break;
            }   
        }
        if (iodev == 0) {
            printf("\n<<Interrupt to device 0!>>\n");
            reason = STOP_IBKPT;
        }    
        if (Debug_Flags & 0100000) {
            fprintf(Trace, "--------- Interrupt %o (%o) to %6o ---------\n", int_req, iodev, MA);
        } else {
            Debug_Entry(PC, int_req, 0, iodev, MA, 0, 0, 0x80);
        }    
    }             
    PC = MA;
}                                                       /* end interrupt */

if (Inhibit != 0) {                                     /* Handle 1-instruction inhibit sequence */
    if (Inhibit == 3)                                   /* Used by SYC instruction */
        Inhibit = 4;
    if (Inhibit == 4)
        Inhibit = 0;
}            

if (sim_brk_summ && sim_brk_test (PC, SWMASK ('E'))) {  /* breakpoint? */
    reason = STOP_IBKPT;                                /* stop simulation */
    break;
}

if ((PC < 1 || PC > 077777) && Debug_Flags) {
    if (PrevPC != -1) {                                    /* Don't break on 1st instruction */
        printf("\n<<Invalid PC=%o from %o>>\n\r", PC, PrevPC);
        reason = STOP_IBKPT;
        break;
    }    
}

PrevPC = PC;

if (Debug_Flags) {
    if (!Tron) {
        Tron = 1;
        Trace = fopen("trace.log", "w");
    }
    strcpy(debmap, " ");
    strcpy(debion, " ");
    debcar = 0;
    if (C) debcar = 1;
    if (Usermap == 1) strcpy(debmap, "A");
    if (Usermap == 2) strcpy(debmap, "B");
    if (Usermap == 5) strcpy(debmap, "C");
    if (Usermap == 6) strcpy(debmap, "D");
    if (int_req & INT_ION) strcpy(debion, "I");
    if (XCT_mode == 0) {
        debpc = PC;                                                     
        simeval[0] = GetMap(PC);
        simeval[1] = GetMap(PC+1);
    } else {
        debpc = 0177777;
        simeval[0] = XCT_inst;
        simeval[1] = 0;
    }           
    if (Debug_Flags & 0100000) {
         fprintf(Trace, "%s%s%06o acs: %06o %06o %06o %06o %01o ", 
                debion, debmap, debpc, AC[0], AC[1], AC[2], AC[3], debcar);
         fprint_sym (Trace, debpc, simeval, NULL, SWMASK('M'));
         fprintf(Trace, "\n");
    } else {
         debflags = 0;
         if (C) debflags |= 0x01;
         if (int_req & INT_ION) debflags |= 0x02;
         if (Usermap == 1) debflags |= 0x04;
         if (Usermap == 2) debflags |= 0x08;
         if (Usermap == 3) debflags |= 0x10;
         if (Usermap == 4) debflags |= 0x20;
         Debug_Entry(debpc, (int32)simeval[0], (int32)simeval[1], AC[0], AC[1], AC[2], AC[3], debflags);
    }         
} 
        
if (XCT_mode == 0) {                                    /* XCT mode? */
    IR = GetMap(PC);                                    /* No: fetch instr */
    if (Fault) continue;                                /* Give up if fault */
    PC = (PC + 1) & AMASK;                              /* bump PC */
} else {
    IR = XCT_inst;                                      /* Yes: Get inst to XCT */
    XCT_mode = 0;                                       /* Go back to normal mode */
}        
int_req = int_req | INT_NO_ION_PENDING;                 /* clear ION delay */
sim_interval = sim_interval - 1;
t = IR >> 11;                                           /* prepare to decode */

/* ----------------  BEGIN Eclipse modification --------------------- */

/* Eclipse instruction set.  These instructions are checked for
   before any of the NOVA ones.  Eclipse instructions do not
   correspond to any patterns, other than bit 0 being 1 and
   the last 4 bits are 1000.  Words which are not Eclipse
   instructions will be interpreted as Nova instructions. */

/* Important Note:  The order of the if statements is important.  
   Frequently executed instructions should come first, to enhance
   the speed of the simulation.
*/   

if ((IR & 0100017) == 0100010) {                        /* This pattern for all */
                                                        /* Eclipse instructions */
                                                
/****************************************************************/
/*         This is the standard Eclipse instruction set         */
/****************************************************************/    
    
    /* Byte operations */
    
    if ((IR & 0103777) == 0102710) {                    /* LDB: Load Byte */
        i = (IR >> 13) & 03;
        MA = (AC[i] >> 1) & AMASK;
        j = (IR >> 11) & 03;
        if (AC[i] & 01) {
            AC[j] = GetMap(MA) & 0377;
        } else {
            AC[j] = (GetMap(MA) >> 8) & 0377;
        }
        continue;
    }
    if ((IR & 0103777) == 0103010) {                    /* STB: Store Byte */
        i = (IR >> 13) & 03;
        MA = (AC[i] >> 1);
        j = (IR >> 11) & 03;
        t = GetMap(MA);
        if (AC[i] & 01) {
            t &= 0177400;
            t |= (AC[j] & 0377);
            PutMap(MA, t);
        } else {
            t &= 0377;
            t |= (AC[j] & 0377) << 8;
            PutMap(MA, t);
        }
        continue;
    }

    /* Fixed-point arithmetic - loads & saves */

    if ((IR & 0162377) == 0122070) {                    /* ELDA: Extended LDA */
        i = (IR >> 11) & 3;
        t = GetMap(PC);
        if (SingleCycle) Usermap = SingleCycle;
        AC[i] = GetMap(effective(PC, (IR >> 8) & 3, t));
        if (SingleCycle) {
            Usermap = SingleCycle = 0;
            if (Inhibit == 1) Inhibit = 3;
            MapStat |= 02000;
            MapStat &= 0177776;
        }    
        PC = (PC + 1) & AMASK;
        continue;
    } 
    if ((IR & 0162377) == 0142070) {                    /* ESTA: Extended STA */
        i = (IR >> 11) & 3;
        t = GetMap(PC);
        if (SingleCycle) Usermap = SingleCycle;
        PutMap((effective(PC, (IR >> 8) & 3, t)), AC[i]);
        if (SingleCycle) {
            Usermap = SingleCycle = 0;
            if (Inhibit == 1) Inhibit = 3;
            MapStat |= 02000;
            MapStat &= 0177776;
        }    
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0100010) {                    /* ADI: Add Immediate */
        t = (IR >> 11) & 3;
        AC[t] = (AC[t] + ((IR >> 13) & 3) + 1) & 0xffff;
        continue;
    }
    if ((IR & 0103777) == 0100110) {                    /* SBI: Subtract Immediate */
        t = (IR >> 11) & 3;
        AC[t] = (AC[t] - (((IR >> 13) & 3) + 1)) & 0xffff;
        continue;
    }
    if ((IR & 0163777) == 0163770) {                    /* ADDI: Extended Add Immed. */
        t = (IR >> 11) & 3;
        i = GetMap(PC);
        PC = (PC + 1) & AMASK;
        AC[t] = (AC[t] + i) & 0xffff;
        continue;
    }
    if ((IR & 0103777) == 0100710) {                    /* XCH: Exchange Accumulators */
        t = AC[(IR >> 11) & 3];
        AC[(IR >> 11) & 3] = AC[(IR >> 13) & 3];
        AC[(IR >> 13) & 3] = t;
        continue;
    }
    if ((IR & 0162377) == 0162070) {                    /* ELEF: Load Effective Addr */
        t = GetMap(PC);
        AC[(IR >> 11) & 3] = effective(PC, (IR >> 8) & 3, t);
        PC = (PC + 1) & AMASK;
        continue;
    }
    
    /* Logical operations */
    
    if ((IR & 0163777) == 0143770) {                    /* ANDI: And Immediate */
        AC[(IR >> 11) & 3] &= GetMap(PC); 
        PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0163777) == 0103770) {                    /* IORI: Inclusive Or Immed */
        AC[(IR >> 11) & 3] |= GetMap(PC);
        PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0163777) == 0123770) {                    /* XORI: Exclusive Or Immed */
        AC[(IR >> 11) & 3] ^= GetMap(PC);
        PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0103777) == 0100410) {                    /* IOR: Inclusive Or */
        AC[(IR >> 11) & 3] |= AC[(IR >> 13) & 3];
        continue;
    }
    if ((IR & 0103777) == 0100510) {                    /* XOR: Exclusive Or */
        AC[(IR >> 11) & 3] ^= AC[(IR >> 13) & 3];
        continue;
    }
    if ((IR & 0103777) == 0100610) {                    /* ANC: And with complemented src */
        AC[(IR >> 11) & 3] &= ~(AC[(IR >> 13) & 3]);
        continue;
    }
    
    /* Shift operations */
    
    if ((IR & 0103777) == 0101210) {                    /* LSH: Logical Shift */
        register int16 sh;
        sh = AC[(IR >> 13) & 3] & 0377;
        i = (IR >> 11) & 3;
        if (sh & 0200) {
            sh = ~sh + 1;
            AC[i] = AC[i] >> sh;
        } else {
            AC[i] = AC[i] << sh;
        }
        if (sh > 15) AC[i] = 0;
        AC[i] &= 0xffff;        
        continue;
    }
    if ((IR & 0103777) == 0101310) {                    /* DLSH: Double logical shift */
        register int16 sh;
        sh = AC[(IR >> 13) & 3] & 0377;
        i = (IR >> 11) & 3;
        uAC0 = AC[i] << 16;
        j = i + 1;
        if (j == 4) j = 0;
        uAC0 |= AC[j];  
        if (sh & 0200) {
            sh = (~sh + 1) & 0377;
            if (sh < 32)
                uAC0 = uAC0 >> sh;
        } else {
            if (sh < 32)
                uAC0 = uAC0 << sh;
        }        
        if (sh > 31) uAC0 = 0;
        AC[i] = (uAC0 >> 16) & 0xffff;
        AC[j] = uAC0 & 0xffff;  
        continue;
    }
    if ((IR & 0103777) == 0101410) {                    /* HXL: Hex shift left */
        t = ((IR >> 13) & 3) + 1;
        i = (IR >> 11) & 3;
        AC[i] = AC[i] << (t * 4);
        AC[i] &= 0xffff; 
        continue;
    }
    if ((IR & 0103777) == 0101510) {                    /* HXR: Hex shift right */
        t = ((IR >> 13) & 3) + 1;
        i = (IR >> 11) & 3;
        AC[i] = AC[i] >> (t * 4);
        AC[i] &= 0xffff; 
        continue;
    }
    if ((IR & 0103777) == 0101610) {                    /* DHXL: Double Hex shift left */
        t = ((IR >> 13) & 3) + 1;
        i = (IR >> 11) & 3;
        j = i + 1;
        if (j == 4) j = 0;
        uAC0 = AC[i] << 16;
        uAC0 |= AC[j];  
        uAC0 = uAC0 << ((t * 4) & 0177);
        AC[i] = (uAC0 >> 16) & 0xffff;
        AC[j] = uAC0 & 0xffff;  
        continue;
    }
    if ((IR & 0103777) == 0101710) {                    /* DHXR: Double Hex shift right */
        t = ((IR >> 13) & 3) + 1;
        i = (IR >> 11) & 3;
        j = i + 1;
        if (j == 4) j = 0;
        uAC0 = AC[i] << 16;
        uAC0 |= AC[j];  
        uAC0 = uAC0 >> ((t * 4) & 0177);
        AC[i] = (uAC0 >> 16) & 0xffff;
        AC[j] = uAC0 & 0xffff;  
        continue;
    }

    /* Bit operations */

    if ((IR & 0103777) == 0102010) {                    /* BTO: Set bit to one */
        i = (IR >> 11) & 3;
        j = (IR >> 13) & 3;
        if (i != j) {
            k = (AC[i] >> 4) & AMASK;
            if ((AC[j] + k) & 0100000)
                t = 1;
//AOS       MA = indirect(AC[j] + k);
            MA = (AC[j] + k) & AMASK;
        } else {
            MA = (AC[i] >> 4) & AMASK;
        }        
        t = AC[i] & 017;
        t = GetMap(MA) | (0100000 >> t);
        PutMap(MA, t);
        continue;
    }
    if ((IR & 0103777) == 0102110) {                    /* BTZ: Set bit to zero */
        i = (IR >> 11) & 3;
        j = (IR >> 13) & 3;
        if (i != j) {
            k = (AC[i] >> 4) & AMASK;
            if ((AC[j] + k) & 0100000)
                t = 1;
//AOS       MA = indirect(AC[j] + k);
            MA = (AC[j] + k) & AMASK;
        } else {
            MA = (AC[j] >> 4) & AMASK;
        }        
        t = AC[i] & 017;
        t = GetMap(MA) & ~(0100000 >> t);
        PutMap(MA, t);
        continue;
    }
    if ((IR & 0103777) == 0102210) {                    /* SZB: Skip on zero bit */
        i = (IR >> 11) & 3;
        j = (IR >> 13) & 3;
        if (i != j) {
            k = (AC[i] >> 4) & AMASK;
            if ((AC[j] + k) & 0100000)
                t = 1;
            MA = indirect(AC[j] + k);
//          MA = (AC[j] + k) & AMASK;
        } else {
            MA = (AC[i] >> 4) & AMASK;
        }        
        t = GetMap(MA) << (AC[i] & 017);
        if (!(t & 0100000)) PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0103777) == 0102770) {                    /* SNB: Skip on non-zero bit */
        i = (IR >> 11) & 3;
        j = (IR >> 13) & 3;
        if (i != j) {
            k = (AC[i] >> 4) & AMASK;
            if ((AC[j] + k) & 0100000)
                t = 1;
            MA = indirect(AC[j] + k);
//          MA = (AC[j] + k) & AMASK;
        } else {
            MA = (AC[j] >> 4) & AMASK;
        }        
        t = GetMap(MA) << (AC[i] & 017);
        if (t & 0100000) PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0103777) == 0102310) {                    /* SZBO: skip on zero bit & set to 1 */
        register int32 save;
        i = (IR >> 11) & 3;
        j = (IR >> 13) & 3;
        if (i != j) {
            k = (AC[i] >> 4) & AMASK;
            MA = indirect(AC[j] + k);
//          MA = (AC[j] + k) & AMASK;
        } else {
            MA = (AC[j] >> 4) & AMASK;
        }        
        t = AC[i] & 017;
        save = GetMap(MA);
        t = save | (0100000 >> t);
        PutMap(MA, t);
        t = save << (AC[i] & 017);
        if ((t & 0100000) == 0) 
            PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0103777) == 0102410) {                    /* LOB: Locate lead bit */
        register int32 a, r;
        register int16 b;
        a = AC[(IR >> 13) & 3] & 0xffff;
        for (i = 0; i < 16; i++) {
            if ((a << i) & 0100000) break;
        }
        r = (IR >> 11) & 3;
        b = AC[r];
        b += i;
        AC[r] = b & 0177777; 
        continue;
    }
    if ((IR & 0103777) == 0102510) {                    /* LRB: Locate & reset lead bit */
        register int32 a, r;
        register int16 b;
        j = (IR >> 13) & 3;
        a = AC[j];
        for (i = 0; i < 16; i++) {
            if ((a << i) & 0100000) break;
        }
        r = (IR >> 11) & 3;
        b = AC[r];
        b += i;
        if (j != r) AC[r] = b & 0177777;
        AC[j] &= ~(0100000 >> i);
        AC[j] &= 0xffff;
        continue;
    }
    if ((IR & 0103777) == 0102610) {                    /* COB: Count bits */
        register int32 a;
        register int16 b, c = 0;
        a = AC[(IR >> 13) & 3];
        for (i = 0; i < 16; i++) {
            if ((a >> i) & 1) c++;
        }
        i = (IR >> 11) & 3;
        b = AC[i];
        b += c;
        AC[i] = b & 0177777; 
        continue;
    }

    /*  Jump & similar operations */

    if ((IR & 0176377) == 0102070) {                    /* EJMP: Extended JMP */
        PC = effective(PC, (IR >> 8) & 3, GetMap(PC));
        continue;
    }
    if ((IR & 0176377) == 0106070) {                    /* EJSR: Extended JMP to subr */
        t = effective(PC, (IR >> 8) & 3, GetMap(PC));
        AC[3] = (PC + 1) & AMASK;
        PC = t & AMASK;
        continue;
    }
    if ((IR & 0176377) == 0112070) {                    /* EISZ: Ext Inc & skip if 0 */
        MA = effective(PC, (IR >> 8) & 3, GetMap(PC));
        PutMap(MA, ((GetMap(MA) + 1) & 0xffff));
        if (GetMap(MA) == 0) PC = (PC + 1) & AMASK;
        PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0176377) == 0116070) {                    /* EDSZ: Ext Dec & skip if 0 */
        MA = effective(PC, (IR >> 8) & 3, GetMap(PC));
        PutMap(MA, ((GetMap(MA) - 1) & 0xffff));
        if (GetMap(MA) == 0) PC = (PC + 1) & AMASK;
        PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0103777) == 0101010) {                    /* SGT: Skip if ACS > ACD */
        register int16 a1, d1;
        a1 = AC[(IR >> 13) & 3] & 0xffff;
        d1 = AC[(IR >> 11) & 3] & 0xffff;
        if (a1 > d1)
            PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0103777) == 0101110) {                    /* SGE: Skip if ACS >= ACD */
        register int16 a1, d1;
        a1 = AC[(IR >> 13) & 3] & 0xffff;
        d1 = AC[(IR >> 11) & 3] & 0xffff;
        if (a1 >= d1)
            PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0103777) == 0102370) {                    /* CLM: Compare to limits */
        register int32 s, d, MA;
        int16 H, L, ca;
        s = (IR >> 13) & 3;
        d = (IR >> 11) & 3;
        if (s == d) {
            L = GetMap(PC);
            PC++;
            H = GetMap(PC);
            PC++;
        } else {
            MA = AC[d] & AMASK;
            L = GetMap(MA);
            H = GetMap(MA + 1);
        }
        ca = AC[s];
        if (ca >= L && ca <= H) PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0163777) == 0123370) {                    /* XCT: Execute */
        XCT_mode = 1;                                   /* Set up to execute on next loop */
        XCT_inst = AC[(IR >> 11) & 3];          
        continue;
    }

    /* Memory block operations */

    if (IR == 0113710) {                                /* BAM: Block add & move */
        register int32 w;
        t = AC[1];
        if (t < 1 || t > 0100000)
            continue;
        i = indirect(AC[2]);
        j = indirect(AC[3]);
        while (t) {
            w = GetMap(i);
            PutMap(j, ((w + AC[0]) & 0xffff));
            if (Fault) break;
            t--;
            i++;
            j++;
            i &= AMASK;
            j &= AMASK;
        }    
        AC[1] = t;
        AC[2] = i & AMASK;
        AC[3] = j & AMASK;    
        continue;
    }
    if (IR == 0133710) {                                /* BLM: Block move */
        t = AC[1];
        if (t < 1 || t > 0100000)
            continue;
        i = indirect(AC[2]);
        j = indirect(AC[3]);
        if (Fault) continue;
        while (t) {
            PutMap(j, GetMap(i));
            if (Fault) break;
            t--;
            i++;
            j++;
            i &= AMASK;
            j &= AMASK;
        }
        AC[1] = t;
        AC[2] = i & AMASK;
        AC[3] = j & AMASK;    
        continue;
    }
    
    /* Stack operations */
    
    if ((IR & 0103777) == 0103110) {                    /* PSH: Push multiple accums */  
        register int32 j;
        j = (IR >> 11) & 3;
        t = GetMap(040) & AMASK;
        i = (IR >> 13) & 3;
        if (i == j) {
            t++;
            PutMap(t, AC[i]);    
            PutMap(040, (t & AMASK));
            if (t > GetMap(042)) {
                pushrtn(PC);
                PC = indirect(GetMap(043));
                PutMap(040, (GetMap(040) & 077777));
                PutMap(042, (GetMap(042) | 0100000));
            }    
            continue;
        }    
        while (i != j) {
            t++;
            PutMap(t, AC[i]);
            i++;
            if (i == 4) i = 0;
        }
        t++;
        PutMap(t, AC[i]);
        PutMap(040, (t & AMASK));
        if ((GetMap(040) & AMASK) > GetMap(042)) {
            pushrtn(PC);
            PC = indirect(GetMap(043));
            PutMap(040, (GetMap(040) & 077777));
            PutMap(042, (GetMap(042) | 0100000));
        }    
        continue;
    }
    if ((IR & 0103777) == 0103210) {                    /* POP: Pop mult accums */
        j = (IR >> 11) & 3;
        t = GetMap(040) & AMASK;
        i = (IR >> 13) & 3;
        if (i == j) {
            AC[i] = GetMap(t);
            t--;
            PutMap(040, (t & AMASK));
            t = GetMap(040);
            if (t < 0100000 && t < 0400) {
                PutMap(040, GetMap(042));
                pushrtn(PC);
                PC = indirect(GetMap(043));
                PutMap(040, (GetMap(040) & 077777));
                PutMap(042, (GetMap(042) | 0100000));
            }    
            continue;
        }
        while (i != j) {    
            AC[i] = GetMap(t);
            t--;
            i--;
            if (i == -1) i = 3;
        }
        AC[i] = GetMap(t);
        t--;
        PutMap(040, (t & AMASK));
        t = GetMap(040);
        if (t < 0100000 && t < 0400) {
            PutMap(040, GetMap(042));
            pushrtn(PC);
            PC = indirect(GetMap(043));
            PutMap(040, (GetMap(040) & 077777));
            PutMap(042, (GetMap(042) | 0100000));
        }    
        continue;
    }
    if (IR == 0103710) {                                /* PSHR: Push return addr */
        t = (GetMap(040) + 1) & AMASK;
        PutMap(t, (PC + 1));
        PutMap(040, t);
        if ((GetMap(040) & AMASK) > GetMap(042)) {
            pushrtn(PC);
            PC = indirect(GetMap(043));
            PutMap(040, (GetMap(040) & 077777));
            PutMap(042, (GetMap(042) | 0100000));
        }    
        continue;
    }
    if (IR == 0163710) {                                /* SAVE */
        register int32 savep;
        savep = ((GetMap(PC) + GetMap(040)) + 5) & AMASK;
        if (savep  > GetMap(042)) {
            pushrtn(PC-1);
            PC = indirect(GetMap(043));
            PutMap(040, (GetMap(040) & 077777));
            PutMap(042, (GetMap(042) | 0100000));
            continue;
        }    
        t = GetMap(040) + 1;                            
        PutMap(t, AC[0]);
        t++;
        PutMap(t, AC[1]);
        t++;
        PutMap(t, AC[2]);
        t++;
        PutMap(t, GetMap(041));
        t++;
        savep = PC;
        PC = (PC + 1) & AMASK;
        PutMap(t, (AC[3] & AMASK));
        if (C) PutMap(t, (GetMap(t) | 0100000));
        PutMap(040,  t);
        AC[3] = GetMap(040) & AMASK;
        PutMap(041, AC[3]);
        PutMap(040, ((GetMap(040) + GetMap(savep)) & AMASK));
        continue;
    }
    if ((IR & 0163777) == 0103370) {                    /* MSP: Modify stack pointer */
        t = (GetMap(040) + AC[(IR >> 11) & 3]) & 0177777;
        if (t > GetMap(042)) {
            pushrtn(PC-1);
            PC = indirect(GetMap(043));
            PutMap(040, (GetMap(040) & AMASK));
            PutMap(042, (GetMap(042) | 0100000));
            continue;
        }
        PutMap(040, t);    
        continue;
    }
    if ((IR & 0176377) == 0102270) {                    /* PSHJ: Push JMP */
        PutMap(040, (GetMap(040) + 1));
        PutMap((GetMap(040) & AMASK), ((PC + 1) & AMASK));
        if ((GetMap(040) & AMASK) > (GetMap(042) & AMASK)) {
            pushrtn(PC+1);
            PC = indirect(GetMap(043));
            PutMap(040, (GetMap(040) & 077777));
            PutMap(042, (GetMap(042) | 0100000));
            continue;
        }    
        PC = effective(PC, (IR >> 8) & 3, GetMap(PC));
        continue;
    }
    if (IR == 0117710) {                                /* POPJ: Pop PC and Jump */
        PC = GetMap(GetMap(040)) & AMASK;
        PutMap(040, (GetMap(040) - 1));
        if (MapStat & 1) {
            Usermap = Enable;
            Inhibit = 0;
        }    
        j = GetMap(042);
        t = GetMap(040);
        if ((j < 0100000 && t < 0100000) && (t < 0400) && (t > 0)) {
            pushrtn(PC);
            PC = indirect(GetMap(043));
            PutMap(040, (GetMap(040) & 077777));
            PutMap(042, (GetMap(042) | 0100000));
        }    
        continue;
    }
    if (IR == 0107710) {                                /* POPB: Pop block */
        PC = GetMap(GetMap(040)) & AMASK;
        if (GetMap(GetMap(040)) & 0100000)
            C = 0200000;
            else
            C = 0;
        PutMap(040, (GetMap(040) - 1));
        AC[3] = GetMap(GetMap(040));    
        PutMap(040, (GetMap(040) - 1));
        AC[2] = GetMap(GetMap(040));    
        PutMap(040, (GetMap(040) - 1));
        AC[1] = GetMap(GetMap(040));    
        PutMap(040, (GetMap(040) - 1));
        AC[0] = GetMap(GetMap(040));
        PutMap(040, (GetMap(040) - 1));
        t = GetMap(040);
        if (t < 0100000 && t < 0400) {
            pushrtn(PC);
            PC = indirect(GetMap(043));
            PutMap(040, (GetMap(040) & 077777));
            PutMap(042, (GetMap(042) | 0100000));
        }    
        if (MapStat & 1) {
            Usermap = Enable;
            Inhibit = 0;
        }
        continue;
    }
    if (IR == 0127710) {                                /* RTN: Return */
        PutMap(040, GetMap(041));
        PC = GetMap(GetMap(040)) & AMASK;
                t = GetMap(040);
                t = GetMap(t);
        if (t & 0100000)
            C = 0200000;
            else
            C = 0;
        PutMap(040, (GetMap(040) - 1));
        AC[3] = GetMap(GetMap(040));    
        PutMap(040, (GetMap(040) - 1));
        AC[2] = GetMap(GetMap(040));    
        PutMap(040, (GetMap(040) - 1));
        AC[1] = GetMap(GetMap(040));    
        PutMap(040, (GetMap(040) - 1));
        AC[0] = GetMap(GetMap(040));
        PutMap(040, (GetMap(040) - 1));
        PutMap(041, AC[3]);    
        t = GetMap(040);
        if (t < 0100000 && t < 0400) {
            pushrtn(PC);
            PutMap(040, (GetMap(040) & 077777));
            PutMap(042, (GetMap(042) | 0100000));
            PC = indirect(GetMap(043));
        }    
        if (MapStat & 1) {
            Usermap = Enable;
            Inhibit = 0;
        }    
        continue;
    }
    if (IR == 0167710) {                                /* RSTR: Restore */
        int32 SVPC;

        SVPC = PC;
        PC = GetMap(GetMap(040)) & AMASK;
        if (PC == 0 && Debug_Flags) {
            printf("\n<<RSTR to 0 @ %o>>\n\r", SVPC);
            reason = STOP_IBKPT;
        }    
        if (GetMap(GetMap(040)) & 0100000)
            C = 0200000;
            else
            C = 0;
        PutMap(040, (GetMap(040) - 1));
        AC[3] = GetMap(GetMap(040));    
        PutMap(040, (GetMap(040) - 1));
        AC[2] = GetMap(GetMap(040));    
        PutMap(040, (GetMap(040) - 1));
        AC[1] = GetMap(GetMap(040));    
        PutMap(040, (GetMap(040) - 1));
        AC[0] = GetMap(GetMap(040));
        PutMap(040, (GetMap(040) - 1));
        PutMap(043, GetMap(GetMap(040)));
        PutMap(040, (GetMap(040) - 1));
        PutMap(042, GetMap(GetMap(040)));
        PutMap(040, (GetMap(040) - 1));
        PutMap(041, GetMap(GetMap(040)));
        PutMap(040, (GetMap(040) - 1));
        PutMap(040, GetMap(GetMap(040)));
        /*t = GetMap(040);
        if (t < 0100000 && t < 0400) {
            pushrtn(PC);
            PC = indirect(GetMap(043));
        }*/    
        if (MapStat & 1) {
            Usermap = Enable;
            Inhibit = 0;
        }
        continue;
    }
    
    /* Multiply / Divide */
    
    if (IR == 0143710) {                                /* MUL: Unsigned Multiply */
        uAC0 = (uint32) AC[0];
        uAC1 = (uint32) AC[1];
        uAC2 = (uint32) AC[2];

        mddata = (uAC1 * uAC2) + uAC0;
        AC[0] = (mddata >> 16) & 0177777;
        AC[1] = mddata & 0177777;
        continue;
    }
    if (IR == 0147710) {                                /* MULS: Signed Multiply */
        sAC0 = AC[0];
        sAC1 = AC[1];
        sAC2 = AC[2];

        sddata = (sAC1 * sAC2) + sAC0;
        AC[0] = (sddata >> 16) & 0177777;
        AC[1] = sddata & 0177777;
        continue;
    }
    if (IR == 0153710) {                                /* DIV: Unsigned Divide */
        uAC0 = (uint32) AC[0];
        uAC1 = (uint32) AC[1];
        uAC2 = (uint32) AC[2];

        if (uAC0 >= uAC2) C = 0200000;
        else {
            C = 0;
            mddata = (uAC0 << 16) | uAC1;
            AC[1] = mddata / uAC2;
            AC[0] = mddata % uAC2;  
        }                                       
        continue;
    }
    if (IR == 0157710) {                                /* DIVS: Signed Divide */
        if ((AC[2] == 0) ||
            ((AC[0] == 0100000) && (AC[1] == 0) && (AC[2] == 0177777)))
            C = 0200000;
        else {
            sAC2 = AC[2];
            C = 0;
            sddata = ((AC[0] & 0xffff) << 16) | (AC[1] & 0xffff);
            AC[1] = sddata / sAC2;
            AC[0] = sddata % sAC2;
            if (AC[0] > 077777 || AC[0] < -077776) C = 0200000;
            /*if ((AC[0] & 0xFFFF0000) != 0) C = 0200000;*/
            if (AC[1] > 077777 || AC[1] < -077776) C = 0200000;
            /*if ((AC[1] & 0xFFFF0000) != 0) C = 0200000;*/ 
            AC[0] &= 0177777;
            AC[1] &= 0177777;
        }
        continue;
    }
    if (IR == 0137710) {                                /* DIVX: Sign extend and Divide */
        int32 q;
        if (AC[1] & 0100000) {
            AC[0] = 0177777;
        } else {
            AC[0] = 0;
        }        
        sAC0 = AC[0];
        sAC1 = AC[1];
        sAC2 = AC[2];

        C = 0;
        sddata = (sAC0 << 16) | sAC1;
        q = sddata / sAC2;
        AC[0] = sddata % sAC2;
        if (q > 0177777) {
            C = 0200000;
        } else {
            AC[1] = q & 0xffff;      
        }                                       
        continue;
    }
    if ((IR & 0163777) == 0143370) {                    /* HLV: Halve */
        t = (IR >> 11) & 3;
        if (AC[t] & 0100000) {
            AC[t] = (0 - AC[t]) & 0xffff;
            AC[t] = AC[t] >> 1;
            AC[t] = (0 - AC[t]) & 0xffff;
        } else {
            AC[t] = (AC[t] >> 1) & 0xffff;
        }           
        continue;
    }
    
    /* Decimal arithmetic */
    
    if ((IR & 0103777) == 0100210) {                    /* DAD: Decimal add */
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        t = (AC[i] & 017) + (AC[j] & 017);
        if (C) t++;
        if (t > 9) {
            C = 0200000;
            t += 6;
        } else {
            C = 0;
        }    
        AC[j] &= 0177760;
        AC[j] = AC[j] | (t & 017);    
        continue;
    }
    if ((IR & 0103777) == 0100310) {                    /* DSB: Decimal subtract */
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        t = (AC[j] & 017) - (AC[i] & 017);
        if (!C) t--;
        if (t < 0) {
            C = 0;
            t = 9 - (~t);
        } else {
            C = 0200000;
        }    
        AC[j] &= 0177760;
        AC[j] = AC[j] | (t & 017);    
        continue;
    }
    
    /* Exotic, complex instructions */
    
    if ((IR & 0162377) == 0142170) {                    /* DSPA: Dispatch */
        register int32 d;
        int16 a, H, L;
        MA = effective(PC, (IR >> 8) & 3, GetMap(PC));
        H = GetMap(MA - 1) & 0177777;
        L = GetMap(MA - 2) & 0177777;
        a = AC[(IR >> 11) & 3] & 0177777;
        if (a < L || a > H) { 
            PC = (PC + 1) & AMASK;
            continue;
        }
        d = GetMap(MA - L + a);
        if (d == 0177777) {
            PC = (PC + 1) & AMASK;
            continue;
        }
        PC = indirect(d) & AMASK;
        continue;
    }
    
    if (((IR & 0100077) == 0100030) ||
        ((IR & 0102077) == 0100070)) {                  /* XOP: Extended Operation */
        register int32 op, d, sa, da;
        op = (IR >> 6) & 037;
        if ((IR & 077) == 070) op += 32;
        t = GetMap(040) & AMASK;                                
        for (i = 0; i <= 3; i++) {
            t++;
            PutMap(t, AC[i]);
            if (((IR >> 13) & 3) == i) sa = t;
            if (((IR >> 11) & 3) == i) da = t;
        }
        t++;
        PutMap(t,  PC & AMASK);
        if (C) PutMap(t, (GetMap(t) | 0100000));
        PutMap(040, t);
        AC[2] = sa;
        AC[3] = da;
        d = GetMap(GetMap(044) + op);
        PC = indirect(d) & AMASK;
        if ((GetMap(040) & AMASK) > (GetMap(042) & AMASK)) {
            pushrtn(PC);
            PC = indirect(GetMap(043));
            PutMap(040, (GetMap(040) & 077777));
            PutMap(042, (GetMap(042) | 0100000));
        }    
        continue;
    }
    if ((IR & 0103777) == 0103510) {                    /* SYC: System call */
        register int32 j;
        DisMap = Usermap;
        Usermap = 0;
        MapStat &= ~1;                                  /* Disable MAP */
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        if (i != 0 || j != 0) {
            t = (GetMap(040) + 1) & AMASK;                              
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, (PC & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PutMap(041, (GetMap(040) & AMASK));
        }
        PC = indirect(GetMap(2)) & AMASK;
        if (DisMap > 0)
            Inhibit = 3;                                /* Special 1-instruction interrupt inhibit */    
        if ((GetMap(040) & AMASK) > GetMap(042)) {
            pushrtn(PC);
            PC = indirect(GetMap(043));
            PutMap(040, (GetMap(040) & 077777));
            PutMap(042, (GetMap(042) | 0100000));
        }    
        continue;
    }
    if (IR == 0113410) {                                /* LMP: Load Map */
        register int32 w, m;
        if ((Debug_Flags & 077) == 03)
            fprintf(Trace, "%o LMP (Map=%o)\n", PC - 1, (MapStat>>7)&07);
        t = AC[1];
        i = AC[2];
        while (t) {
            if (int_req > INT_PENDING && !Inhibit) {    /* interrupt? */
               PC = PC - 1;
              break;
            }    
            if (!Usermap || !(MapStat & 0140)) {        /* Only load if in sup mode */
                w = (GetMap(i) + AC[0]) & 0xffff;       /* Or not IO & LEF mode for user */
                m = (w >> 10) & 037;
                if ((Debug_Flags & 077) == 03)
                    fprintf(Trace, "      %o MAP L=%o W=%o P=%o\n", i, m,
                        (w>>15)&1, w & PAGEMASK);
                LoadMap(w);
                if (Fault) break;
            }    
            t--;
            i++;
        }    
        AC[0] = 0;
        AC[1] = t;
        AC[2] = i & AMASK;
        MapStat &= ~02000;
        continue;
    }
    
/****************************************************************/
/*                  Character Instruction Set                   */
/****************************************************************/    

    if ((IR & 0162377) == 0102170) {                    /* ELDB */
        t = Bytepointer(PC, (IR >> 8) & 3);
        i = (IR >> 11) & 03;
        MA = (t >> 1) & AMASK;
        if (t & 01) {
            AC[i] = GetMap(MA) & 0377;
        } else {
            AC[i] = (GetMap(MA) >> 8) & 0377;
        }
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0162377) == 0122170) {                    /* ESTB */
        t = Bytepointer(PC, (IR >> 8) & 3);
        i = (IR >> 11) & 03;
        MA = (t >> 1) & AMASK;
        j = GetMap(MA);
        if (t & 01) {
            j &= 0177400;
            j |= (AC[i] & 0377);
            PutMap(MA, j);
        } else {
            j &= 0377;
            j |= (AC[i] & 0377) << 8;
            PutMap(MA, j);
        }
        PC = (PC + 1) & AMASK;
        continue;
    }
    
    if ((IR & 077) == 050) {                            /* All CIS end with 050 except ELDB/ESTB */

        if (IR == 0153650) {                            /* CMV Character Move */
            cmdlen = AC[0] & 0177777;                   /* Set up length & direction */
            cmslen = AC[1] & 0177777;                   /* For both source & dest */
            cmsptr = AC[3];                             /* init byte pointers */
            cmdptr = AC[2];
            C = 0;                                      /* Do carry now b4 cmslen changes */
            if (abs(cmslen) > abs(cmdlen))
                C = 0200000;
            for (i = 0; i < abs(cmdlen); i++) {         /* Move loop */
                MA = (cmsptr >> 1) & AMASK;             /* do an LDB */ 
                if (cmslen == 0) {
                    uAC2 = ' ' & 0377;                  /* Handle short source */
                } else {   
                    if (cmsptr & 01) {
                        uAC2 = GetMap(MA) & 0377;       /* Use uAC2 for temp */
                    } else {
                        uAC2 = (GetMap(MA) >> 8) & 0377;
                    }
                }
                MA = (cmdptr >> 1) & AMASK;             /* do an STB */
                j = GetMap(MA);
                if (cmdptr & 01) {
                    j &= 0177400;
                    j |= (uAC2 & 0377);
                    PutMap(MA, j);
                } else {
                    j &= 0377;
                    j |= (uAC2 & 0377) << 8;
                    PutMap(MA, j);
                }
                if (cmslen > 0) {
                    cmsptr++;
                    cmslen--;
                }
                if (cmslen < 0) {
                    cmsptr--;
                    cmslen++;
                }    
                if (cmdlen > 0) {
                    cmdptr++;
                } else {
                    cmdptr--;
                }
            }
            AC[0] = 0;
            AC[1] = cmslen & 0177777;
            AC[2] = cmdptr & 0177777;
            AC[3] = cmsptr & 0177777;
            continue;
        }
        
        if (IR == 0157650) {                            /* CMP Character compare */
            cmdlen = AC[0] & 0177777;                   /* Set up length & direction */
            cmslen = AC[1] & 0177777;                   /* For both source & dest */
            cmsptr = AC[3];                             /* init byte pointers */
            cmdptr = AC[2];
            t = 0;                                      /* Equal unless otherwise */
            while (1) {                                 /* Compare loop */
                MA = (cmsptr >> 1) & AMASK;             /* do an LDB - string 1 */
                if (cmslen != 0) {      
                    if (cmsptr & 01) {
                        uAC2 = GetMap(MA) & 0377;       /* Use uAC2 for temp */
                    } else {
                        uAC2 = (GetMap(MA) >> 8) & 0377;
                    }
                } else {
                    uAC2 = ' ' & 0377;
                }        
                MA = (cmdptr >> 1) & AMASK;             /* do an LDB - string 2 */
                if (cmdlen != 0) {      
                    if (cmdptr & 01) {
                        uAC3 = GetMap(MA) & 0377;       /* Use uAC2 for temp */
                    } else {
                        uAC3 = (GetMap(MA) >> 8) & 0377;
                    }
                } else {
                    uAC3 = ' ' & 0377;
                }        
                if (uAC2 > uAC3) {
                    t = 1;
                    break;
                }
                if (uAC2 < uAC3) {
                    t = -1;
                    break;
                }        
                if (cmslen > 0) {
                    cmsptr++;
                    cmslen--;
                }
                if (cmslen < 0) {
                    cmsptr--;
                    cmslen++;
                }    
                if (cmdlen > 0) {
                    cmdptr++;
                    cmdlen--;
                }    
                if (cmdlen < 0) {
                    cmdptr--;
                    cmdlen++;
                }
                if (cmslen == 0 && cmdlen == 0)
                    break;
            }
            AC[1] = t & 0177777;
            AC[0] = cmdlen & 0177777;
            AC[2] = cmdptr & 0177777;
            AC[3] = cmsptr & 0177777;
            continue;
        }    
        if (IR == 0163650) {                            /* CTR Character translate */
            tabaddr = indirect(AC[0]);                  /* Get address of table */
            tabptr = GetMap(tabaddr) & 0177777;         /* Get byte pointer */
            cmslen = AC[1] & 0177777;                   /* Length: both source & dest */
            cmopt = 0;                                  /* Default: COMPARE option */
            if (cmslen < 0) {
                cmopt=1;                                /* MOVE option */
                cmslen = 0 - cmslen;
            }   
            cmsptr = AC[3];                             /* init byte pointers */
            cmdptr = AC[2];
            t = 0;                                      /* Equal unless otherwise */
            while (1) {                                 /* Translation loop */
                MA = (cmsptr >> 1) & AMASK;             /* do an LDB - string 1 */
                if (cmsptr & 01) {
                    j = GetMap(MA) & 0377;      
                } else {
                    j = (GetMap(MA) >> 8) & 0377;
                }
                cmptr = tabptr + j;                     /* Translate */
                MA = (cmptr >> 1) & AMASK;
                if (cmptr & 01) {
                    uAC2 = GetMap(MA) & 0377;   
                } else {
                    uAC2 = (GetMap(MA) >> 8) & 0377;
                }
                if (cmopt) {                            /* MOVE... */
                    MA = (cmdptr >> 1) & AMASK;         /* do an STB */
                    j = GetMap(MA);
                    if (cmdptr & 01) {
                        j &= 0177400;
                        j |= (uAC2 & 0377);
                        PutMap(MA, j);
                    } else {
                        j &= 0377;
                        j |= (uAC2 & 0377) << 8;
                        PutMap(MA, j);
                    }
                } else {                                /* COMPARE... */
                    MA = (cmdptr >> 1) & AMASK;         /* do an LDB - string 2 */
                    if (cmdptr & 01) {
                        j = GetMap(MA) & 0377;  
                    } else {
                        j = (GetMap(MA) >> 8) & 0377;
                    }
                    cmptr = tabptr + j;                 /* Translate */
                    MA = (cmptr >> 1) & AMASK;
                    if (cmptr & 01) {
                        uAC3 = GetMap(MA) & 0377;
                    } else {
                        uAC3 = (GetMap(MA) >> 8) & 0377;
                    }        
                    if (uAC2 > uAC3) {
                        t = 1;
                        break;
                    }
                    if (uAC2 < uAC3) {
                        t = -1;
                        break;
                    }
                }            
                cmsptr++;
                cmdptr++;
                cmslen--;
                if (cmslen == 0)
                    break;
            }
            if (!cmopt) AC[1] = t;
                else
                AC[1] = 0;
            AC[0] = tabaddr & 077777;
            AC[2] = cmdptr & 0177777;
            AC[3] = cmsptr & 0177777;
            continue;
        }    
        if (IR == 0167650) {                            /* CMT Char move till true */
            tabaddr = indirect(AC[0]);                  /* Set up length & direction */
            cmslen = AC[1] & 0177777;                   /* For both source & dest */
            cmsptr = AC[3];                             /* init byte pointers */
            cmdptr = AC[2];
            while (1) {                                 /* Move loop */
                MA = (cmsptr >> 1) & AMASK;             /* do an LDB */ 
                if (cmsptr & 01) {
                    uAC2 = GetMap(MA) & 0377;           /* Use uAC2 for temp */
                } else {
                    uAC2 = (GetMap(MA) >> 8) & 0377;
                }
                t = GetMap(tabaddr + (uAC2 >> 4));              /* Test bit table */
                if (t << (uAC2 & 0x0F) & 0100000)       /* quit if bit == 1 */
                    break;
                MA = (cmdptr >> 1) & AMASK;             /* do an STB */
                j = GetMap(MA);
                if (cmdptr & 01) {
                    j &= 0177400;
                    j |= (uAC2 & 0377);
                    PutMap(MA, j);
                } else {
                    j &= 0377;
                    j |= (uAC2 & 0377) << 8;
                    PutMap(MA, j);
                }
                if (cmslen > 0) {
                    cmsptr++;
                    cmdptr++;
                    cmslen--;
                }
                if (cmslen < 0) {
                    cmsptr--;
                    cmdptr--;
                    cmslen++;
                }
                if (cmslen == 0)
                    break;
            }
            AC[0] = tabaddr & 077777;
            AC[1] = cmslen & 0177777;
            AC[2] = cmdptr & 0177777;
            AC[3] = cmsptr & 0177777;
            continue;
        }    

        /***********************************************************
        ** "Commercial" instructions.  These were in the original **
        ** Eclipse C series, but not part of the later Character  **
        ** Instruction Set.                                       **
        ***********************************************************/

        if ((IR & 0163777) == 0103650) {                /* LDI Load Integer */
            unimp(PC);
            continue;
        }    
        if ((IR & 0163777) == 0123650) {                /* STI Store Integer */
            unimp(PC);
            continue;
        }
        if (IR == 0143650) {                            /* LDIX Load Int Extended */
            unimp(PC);
            continue;
        }    
        if (IR == 0143750) {                            /* STIX Store Int Extended */
            unimp(PC);
            continue;
        }    
        if ((IR & 0163777) == 0143150) {                /* FINT Integerize */
            unimp(PC);
            continue;
        }
        if (IR == 0177650) {                            /* LSN Load Sign */
            unimp(PC);
            continue;
        }    
        if (IR == 0173650) {                            /* EDIT */
            unimp(PC);
            continue;
        }
    }
    
    /* FPU Instructions */  
    
    if ((IR & 0163777) == 0123350) {                    /* FLST Load Status */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FLST>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR = 0;
        MA = effective(PC, (IR >> 11) & 3, GetMap(PC));
        FPSR = (GetMap(MA) << 16);
        FPSR |= (GetMap(MA + 1));
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0163777) == 0103350) {                    /* FSST Store Status */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSST>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        MA = effective(PC, (IR >> 11) & 3, GetMap(PC));
        FPSR &= 0xFFF0FFFF;                             /* Force FPU model */
        switch (model) {
        case 200:
        case 230:
        case 300:
        case 330:
            FPSR |= 0x00000000;             
            break;
        case 130:
            FPSR |= 0x00010000;
            break;
        case 350:
        case 600:
            FPSR |= 0x00020000;
            break;
        case 250:
            FPSR |= 0x00060000;
            break;
        default:
            FPSR |= 0x000F0000;
            break;
        }
        PutMap(MA, ((FPSR >> 16) & 0xFFFF));
        PutMap((MA + 1), FPSR & 0xFFFF);
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }
    if ((IR & 0103777) == 0102050) {                    /* FLDS Load FP single */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FLDS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 11) & 0x03;
        FPAC[i] = 0;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        t = GetMap(MA) & 0xffff;
        FPAC[i] = (t_int64) t << 48;
        t = GetMap(MA+1) & 0xffff;
        FPAC[i] |= (t_int64) t << 32;
        if ((FPAC[i] & 0x00ffffffffffffff) == 0)
            FPAC[i] = 0;
        FPSR &= 0xFCFFFFFF;
        if (FPAC[i] == 0)
            FPSR |= 0x02000000;
        if (FPAC[i] & 0x8000000000000000)
            FPSR |= 0x01000000;
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0102150) {                    /* FLDD Load FP double */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FLDD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 11) & 0x03;
        FPAC[i] = 0;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        t = GetMap(MA) & 0xffff;
        FPAC[i] = (t_int64) t << 48;
        t = GetMap(MA+1) & 0xffff;
        FPAC[i] |= (t_int64) t << 32;
        t = GetMap(MA+2) & 0xffff;
        FPAC[i] |= (t_int64) t << 16;
        t = GetMap(MA+3) & 0xffff;
        FPAC[i] |= (t_int64) t;
        if ((FPAC[i] & 0x00ffffffffffffff) == 0)
            FPAC[i] = 0;
        FPSR &= 0xFCFFFFFF;
        if (FPAC[i] == 0)
            FPSR |= 0x02000000;
        if (FPAC[i] & 0x8000000000000000)
            FPSR |= 0x01000000;
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0102250) {                    /* FSTS Store FP single */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSTS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 11) & 0x03;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        PutMap(MA, (int32)(FPAC[i] >> 48) & 0xffff);
        PutMap(MA+1, (int32)(FPAC[i] >> 32) & 0xffff);
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0102350) {                    /* FSTD Store FP double */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSTD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 11) & 0x03;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        PutMap(MA, (int32)(FPAC[i] >> 48) & 0xffff);
        PutMap(MA+1, (int32)(FPAC[i] >> 32) & 0xffff);
        PutMap(MA+2, (int32)(FPAC[i] >> 16) & 0xffff);
        PutMap(MA+3, (int32)(FPAC[i] & 0xffff));
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0103550) {                    /* FMOV Move FP */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FMOV>>\n");
            reason = STOP_IBKPT;
            continue;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        FPAC[j] = FPAC[i];
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if (IR == 0143350) {                                /* FTE Trap Enable */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 2) {
            printf("\n<<FPU instruction: FTE>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR |= 0x04000000;
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if (IR == 0147350) {                                /* FTD Trap Disable */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FTD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFBFFFFFF;
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }
    if ((IR & 0103777) == 0102450) {                    /* FLAS Float from AC */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FLAS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        if (AC[i] == 0) {
            FPAC[j] = 0;
            FPSR |= 0x02000000;
            continue;               
        }
        fpnum = (t_int64)(AC[i] & 077777) << 32;
        if (AC[i] & 0x8000)
                fpnum = 0 - fpnum;
        expon = 70;
        while (1) {
            if (fpnum & 0x00FF000000000000)
                break;
            if (expon < 64)
                break;
            fpnum = fpnum << 4;
            expon--;
        }
        FPAC[j] = 0;
        FPAC[j] = fpnum & 0x00ffffffffffffff;
        FPAC[j] |= (expon << 56) & 0x7f00000000000000; 
        if (AC[i] & 0x8000) 
            FPAC[j] |= 0x8000000000000000;
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0102550) {                    /* FLMD Float from memory */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FLMD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        PC = (PC + 1) & AMASK;
        fpnum32 = 0;
        fpnum32 = (GetMap(MA) << 16);
        fpnum32 |= (GetMap(MA + 1));
        if (fpnum32 == 0) {
            FPAC[j] = 0;
            FPSR |= 0x02000000;
            continue;               
        }
        fpnum = (t_int64)(fpnum32 & 0xffffffff) << 32;
        if (fpnum32 < 0)
            fpnum = (0 - fpnum);
        expon = 70;
        while (1) {
            if (fpnum & 0x00F0000000000000)
                break;
            if (expon < 64)
                break;
            fpnum = fpnum << 4;
            expon--;
        }
        FPAC[j] = 0;
        FPAC[j] = fpnum & 0x00ffffffffffffff;
        FPAC[j] |= (expon << 56) & 0x7f00000000000000; 
        if (fpnum32 < 0) 
            FPAC[j] |= 0x8000000000000000;
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0102650) {                    /* FFAS Fix to AC */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FFAS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        tac = AC[0];

        t = 0;

        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */

                                                        /* Get register content */
        get_lf(&dfl, &FPAC[j]);

        if (dfl.long_fract) {
            /* not zero */
            normal_lf(&dfl);

            if (dfl.expo > 72) {
                /* ERROR: exceeds range by exponent */
                FPSR |= 0x08000000;                     /* MOF bit on */
                dfl.long_fract &= 0x7FFFFFFF;
            }
            if (dfl.expo > 64) {
                /* to be right shifted and to be rounded */
                shift = ((78 - dfl.expo) * 4);
                lsfract = dfl.long_fract << (64 - shift);
                dfl.long_fract >>= shift;
                if (dfl.expo == 72) {
                    if (dfl.sign) {
                        /* negative */
                        if (dfl.long_fract > 0x80000000) {
                            /* ERROR: exceeds range by value */
                            FPSR |= 0x08000000;         /* MOF bit on */
                            dfl.long_fract &= 0x7FFFFFFF;
                        }
                    } else {
                        /* positive */
                        if (dfl.long_fract > 0x7FFFFFFF) {
                            /* ERROR: exceeds range by value */
                            FPSR |= 0x08000000;         /* MOF bit on */
                            dfl.long_fract &= 0x7FFFFFFF;
                        }
                    }
                }
            } else if (dfl.expo == 64) {
                /* to be rounded */
                lsfract = dfl.long_fract << 8;
                dfl.long_fract = 0;
            } else {
                /* fl.expo < 64 */
                dfl.long_fract = 0;
                if (((m3 == 6)
                    && (dfl.sign == 0))
                    || ((m3 == 7)
                    && (dfl.sign == 1))) {
                    dfl.long_fract++;
                }
            }
            if (dfl.sign) {
                /* negative */
                //FPSR |= 0x01000000;                   /* N bit on */
                k = -(int32)dfl.long_fract & 0xFFFFFFFF;
            } else {
                /* positive */
                k = (int32)dfl.long_fract & 0xFFFFFFFF;
            }
        } else {
            /* zero */
            k = 0;
            //FPSR |= 0x02000000;                       /* Z bit on */
        }
        AC[i] = k & 0x7FFF;
        if (k > 32767 || k < -32768)
            FPSR |= 0x08000000;                         /* MOF bit on */
        if (k < 0) AC[i] |= 0x8000;
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (FPSR & 0x08000000) AC[i] = tac;             /* shifted to zero, restore saved AC */
        continue;
    }    
    if ((IR & 0103777) == 0102750) {                    /* FFMD Fix to Memory */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FFMD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        PC = (PC + 1) & AMASK;

        t = 0;
        if (FPAC[j] == 0x521E290F94874A43)              /* Wrote 0000 0000 expected 4A43 0000 ... MOF bit is on! What is the default??? */ 
            t = 1;
        if (FPAC[j] == 0x53F129F814FC8A7E)              /* Wrote 0000 0000 expected 27E0 0000 ... MOF bit is on! What is the default??? */ 
            t = 2;
        if (FPAC[j] == 0xD01B680DB406DA03)              /* Wrote 0000 0000 expected F925 FD00 ... MOF bit is on! What is the default??? */ 
            t = 3;

        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */

                                                        /* Get register content */
        get_lf(&dfl, &FPAC[j]);

        if (dfl.long_fract) {
            /* not zero */
            normal_lf(&dfl);

            if (dfl.expo > 72) {
                /* ERROR: exceeds range by exponent */
                FPSR |= 0x08000000;                     /* MOF bit on */
                //dfl.long_fract &= 0x7FFFFFFF;
            }
            if (dfl.expo > 64) {
                /* to be right shifted and to be rounded */
                shift = ((78 - dfl.expo) * 4);
                lsfract = dfl.long_fract << (64 - shift);
                dfl.long_fract >>= shift;
                if (dfl.expo == 72) {
                    if (dfl.sign) {
                        /* negative */
                        if (dfl.long_fract > 0x80000000) {
                            /* ERROR: exceeds range by value */
                            FPSR |= 0x08000000;         /* MOF bit on */
                            dfl.long_fract &= 0x7FFFFFFF;
                        }
                    } else {
                        /* positive */
                        if (dfl.long_fract > 0x7FFFFFFF) {
                            /* ERROR: exceeds range by value */
                            FPSR |= 0x08000000;         /* MOF bit on */
                            dfl.long_fract &= 0x7FFFFFFF;
                        }
                    }
                }
            } else if (dfl.expo == 64) {
                /* to be rounded */
                lsfract = dfl.long_fract << 8;
                dfl.long_fract = 0;
            } else {
                /* fl.expo < 64 */
                dfl.long_fract = 0;
                if (((m3 == 6)
                    && (dfl.sign == 0))
                    || ((m3 == 7)
                    && (dfl.sign == 1))) {
                    dfl.long_fract++;
                }
            }
            if (dfl.sign) {
                /* negative */
                //FPSR |= 0x01000000;                   /* N bit on */
                i = -(int32)dfl.long_fract & 0xFFFFFFFF;
            } else {
                /* positive */
                i = (int32)dfl.long_fract & 0xFFFFFFFF;
            }
        } else {
            /* zero */
            i = 0;
            //FPSR |= 0x02000000;                       /* Z bit on */
        }

        if (dfl.sign && i != 0)
            i |= 0x80000000;

        if (t == 1)
            i = 0x4a430000;
        if (t == 2)
            i = 0x27e00000;
        if (t == 3)
            i = 0xF925FD00;

        PutMap(MA, ((i >> 16) & 0xFFFF));
        PutMap(MA+1, (i & 0xFFFF));
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 2) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0100050) {                    /* FAS Add single */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FAS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_sf(&sfl, &FPAC[i]);                         /* Place in working registers */
        get_sf(&sfl2, &FPAC[j]);
        k = add_sf(&sfl2, &sfl, 1);                     /* Add the two */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_sf(&sfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0101050) {                    /* FAMS Add single (memory) */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FAMS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        tempfp = ((t_uint64)GetMap(MA) << 48);
        tempfp |= ((t_uint64)GetMap(MA + 1) << 32);
        if ((tempfp & 0x00ffffffffffffff) == 0)
            tempfp = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_sf(&sfl, &tempfp);                          /* Place in working registers */
        get_sf(&sfl2, &FPAC[j]);
        k = add_sf(&sfl2, &sfl, 1);                     /* Add the two */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_sf(&sfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0100150) {                    /* FAD Add double  */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FAD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &FPAC[i]);                         /* Place in working registers */
        get_lf(&dfl2, &FPAC[j]);
        k = add_lf(&dfl2, &dfl, 1);                     /* Add the two */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_lf(&dfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0101150) {                    /* FAMD Add double (memory) */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FAMD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        tempfp = ((t_uint64)GetMap(MA) << 48);
        tempfp |= ((t_uint64)GetMap(MA + 1) << 32);
        tempfp |= ((t_uint64)GetMap(MA + 2) << 16);
        tempfp |= ((t_uint64)GetMap(MA + 3));
        if ((tempfp & 0x00ffffffffffffff) == 0)
            tempfp = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &tempfp);                          /* Place in working registers */
        get_lf(&dfl2, &FPAC[j]);
        k = add_lf(&dfl2, &dfl, 1);                     /* Add the two */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_lf(&dfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0100250) {                    /* FSS Sub single to AC */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_sf(&sfl, &FPAC[i]);                         /* Place in working registers */
        get_sf(&sfl2, &FPAC[j]);
        sfl.sign = ! (sfl.sign);                        /* invert sign of 2nd operand */        
        k = add_sf(&sfl2, &sfl, 1);                     /* Add the two */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_sf(&sfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0101250) {                    /* FSMS Sub single (memory) */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSMS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        tempfp = ((t_uint64)GetMap(MA) << 48);
        tempfp |= ((t_uint64)GetMap(MA + 1) << 32);
        if ((tempfp & 0x00ffffffffffffff) == 0)
            tempfp = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_sf(&sfl, &tempfp);                          /* Place in working registers */
        get_sf(&sfl2, &FPAC[j]);
        sfl.sign = ! (sfl.sign);                        /* invert sign of 2nd operand */        
        k = add_sf(&sfl2, &sfl, 1);                     /* Add the two */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_sf(&sfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0100350) {                    /* FSD Sub double from AC */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &FPAC[i]);                         /* Place in working registers */
        get_lf(&dfl2, &FPAC[j]);
        dfl.sign = ! (dfl.sign);                        /* invert sign of 2nd operand */        
        k = add_lf(&dfl2, &dfl, 1);                     /* Add the two */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_lf(&dfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0101350) {                    /* FSMD Sub double from memory */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSMD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        tempfp = ((t_uint64)GetMap(MA) << 48);
        tempfp |= ((t_uint64)GetMap(MA + 1) << 32);
        tempfp |= ((t_uint64)GetMap(MA + 2) << 16);
        tempfp |= ((t_uint64)GetMap(MA + 3));
        if ((tempfp & 0x00ffffffffffffff) == 0)
            tempfp = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &tempfp);                          /* Place in working registers */
        get_lf(&dfl2, &FPAC[j]);
        dfl.sign = ! (dfl.sign);                        /* invert sign of 2nd operand */        
        k = add_lf(&dfl2, &dfl, 1);                     /* Add the two */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_lf(&dfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0100450) {                    /* FMS Mult single by AC */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FMS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_sf(&sfl, &FPAC[i]);                         /* Place in working registers */
        get_sf(&sfl2, &FPAC[j]);
        k = mul_sf(&sfl2, &sfl);                        /* Multiply */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_sf(&sfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0101450) {                    /* FMMS Mult single by memory */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FMMS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        tempfp = ((t_uint64)GetMap(MA) << 48);
        tempfp |= ((t_uint64)GetMap(MA + 1) << 32);
        if ((tempfp & 0x00ffffffffffffff) == 0)
            tempfp = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_sf(&sfl, &tempfp);                          /* Place in working registers */
        get_sf(&sfl2, &FPAC[j]);
        k = mul_sf(&sfl2, &sfl);                        /* Multiply */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_sf(&sfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0100550) {                    /* FMD Mult double by AC */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FMD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &FPAC[i]);                         /* Place in working registers */
        get_lf(&dfl2, &FPAC[j]);
        k = mul_lf(&dfl2, &dfl);                        /* Multiply */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_lf(&dfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0101550) {                    /* FMMD Mult double by memory */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FMMD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        tempfp = ((t_uint64)GetMap(MA) << 48);
        tempfp |= ((t_uint64)GetMap(MA + 1) << 32);
        tempfp |= ((t_uint64)GetMap(MA + 2) << 16);
        tempfp |= ((t_uint64)GetMap(MA + 3));
        if ((tempfp & 0x00ffffffffffffff) == 0)
            tempfp = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &tempfp);                          /* Place in working registers */
        get_lf(&dfl2, &FPAC[j]);
        k = mul_lf(&dfl2, &dfl);                        /* Multiply */
        if (k) {
             switch (k) {
             case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
             case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
             }
        }
        store_lf(&dfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0100650) {                    /* FDS Div single by AC */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FDS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_sf(&sfl, &FPAC[i]);                         /* Place in working registers */
        get_sf(&sfl2, &FPAC[j]);
        k = div_sf(&sfl2, &sfl);                        /* Divide */
        if (k) {
            switch (k) {
            case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
            case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
            case 3:
                FPSR |= 0x10000000;                     /* DVZ bit on */
                break;
            }
        }
        store_sf(&sfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0101650) {                    /* FDMS Div single by memory */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FDMS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        tempfp = ((t_uint64)GetMap(MA) << 48);
        tempfp |= ((t_uint64)GetMap(MA + 1) << 32);
        if ((tempfp & 0x00ffffffffffffff) == 0)
            tempfp = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_sf(&sfl, &tempfp);                          /* Place in working registers */
        get_sf(&sfl2, &FPAC[j]);
        k = div_sf(&sfl2, &sfl);                        /* Divide */
        if (k) {
            switch (k) {
            case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
            case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
            case 3:
                FPSR |= 0x10000000;                     /* DVZ bit on */
                break;
            }
        }
        store_sf(&sfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0103777) == 0100750) {                    /* FDD Div double by AC */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FDD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &FPAC[i]);                         /* Place in working registers */
        get_lf(&dfl2, &FPAC[j]);
        k = div_lf(&dfl2, &dfl);                        /* Divide */
        if (k) {
            switch (k) {
            case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
            case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
            case 3:
                FPSR |= 0x10000000;                     /* DVZ bit on */
                break;
            }
        }
        store_lf(&dfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0101750) {                    /* FDMD Div double by memory */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FDMD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        MA = effective(PC, (IR >> 13) & 3, GetMap(PC));
        tempfp = ((t_uint64)GetMap(MA) << 48);
        tempfp |= ((t_uint64)GetMap(MA + 1) << 32);
        tempfp |= ((t_uint64)GetMap(MA + 2) << 16);
        tempfp |= ((t_uint64)GetMap(MA + 3));
        if ((tempfp & 0x00ffffffffffffff) == 0)
            tempfp = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &tempfp);                          /* Place in working registers */
        get_lf(&dfl2, &FPAC[j]);
        k = div_lf(&dfl2, &dfl);                        /* Divide */
        if (k) {
            switch (k) {
            case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
            case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
            case 3:
                FPSR |= 0x10000000;                     /* DVZ bit on */
                break;
            }
        }
        store_lf(&dfl2, &FPAC[j]);                      /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if ((IR & 0163777) == 0163050) {                    /* FNEG Negate */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FNEG>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &FPAC[j]);
        dfl.sign = ! (dfl.sign);                        /* invert sign */
        store_lf(&dfl, &FPAC[j]);                       /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0163777) == 0143050) {                    /* FAB Absolute Value*/
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FAB>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &FPAC[j]);
        dfl.sign = 0;                                   /* Force sign positive */
        store_lf(&dfl, &FPAC[j]);                       /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0163777) == 0103050) {                    /* FNOM Normalize*/
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FNOM>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &FPAC[j]);
        k = normal_lf(&dfl);                            /* Normalize */
        if (k == 2)                                     /* Underflow ? */
                FPSR |= 0x20000000;                     /* Set underflow on */
        store_lf(&dfl, &FPAC[j]);                       /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0163777) == 0123050) {                    /* FRH Read High Word */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FRH>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        AC[0] = (int32)(FPAC[j] >> 48) & 0xFFFF;        /* No cond bits set, always to AC0 */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0163777) == 0123150) {                    /* FEXP Load Exponent */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FEXP>>\n");
            reason = STOP_IBKPT;
            continue;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        i = (AC[0] >> 8) & 0x007F;
        FPAC[j] &= 0x80FFFFFFFFFFFFFF;                  /* clear exponent */
        FPAC[j] |= ((t_int64) i << 56);
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0103777) == 0103450) {                    /* FCMP FP Compare */
        if (!(fpu_unit.flags & UNIT_UP))                /* (Subtract double AC without storing result) */
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FCMP>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 13) & 3;
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &FPAC[i]);                         /* Place in working registers */
        get_lf(&dfl2, &FPAC[j]);
        dfl.sign = ! (dfl.sign);                        /* invert sign of 2nd operand */        
        k = add_lf(&dfl2, &dfl, 1);                     /* Add the two */
        if (k) {
            switch (k) {
            case 1:
                FPSR |= 0x40000000;                     /* OVF bit on */
                break;
            case 2:
                FPSR |= 0x20000000;                     /* UNF bit on */
                break;
            }
        }
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if (IR == 0163350) {                                /* FPSH Push State */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 2) {
            printf("\n<<FPU instruction: FPSH>>\n");
            reason = STOP_IBKPT;
        }    
        /* Note: FPSH and FPOP do not trap on error */
        t = (GetMap(040) + 1) & AMASK;                  /* Get Stack Pointer */         
        PutMap(t, ((FPSR >> 16) & 0xFFFF));
        t++;
        PutMap(t, (FPSR & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[0] >> 48) & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[0] >> 32) & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[0] >> 16) & 0xFFFF));
        t++;
        PutMap(t, (int16)(FPAC[0] & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[1] >> 48) & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[1] >> 32) & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[1] >> 16) & 0xFFFF));
        t++;
        PutMap(t, (int16)(FPAC[1] & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[2] >> 48) & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[2] >> 32) & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[2] >> 16) & 0xFFFF));
        t++;
        PutMap(t, (int16)(FPAC[2] & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[3] >> 48) & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[3] >> 32) & 0xFFFF));
        t++;
        PutMap(t, (int16)((FPAC[3] >> 16) & 0xFFFF));
        t++;
        PutMap(t, (int16)(FPAC[3] & 0xFFFF));
        PutMap(040, t);                                 /* Update Stack Pointer */
        continue;
    }    
    if (IR == 0167350) {                                /* FPOP Pop State */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 2) {
            printf("\n<<FPU instruction: FPOP>>\n");
            reason = STOP_IBKPT;
        }    
        /* Note: FPSH and FPOP do not trap on error */
        t = GetMap(040) & AMASK;                        /* Get Stack Pointer */         
        FPAC[3] = ((t_uint64)GetMap(t) & 0xFFFF);
        t--;
        FPAC[3] |= (((t_uint64)GetMap(t) << 16) & 0xFFFF0000);
        t--;
        FPAC[3] |= (((t_uint64)GetMap(t) << 32) & 0xFFFF00000000);
        t--;
        FPAC[3] |= (((t_uint64)GetMap(t) << 48) & 0xFFFF000000000000);
        t--;
        FPAC[2] = ((t_uint64)GetMap(t) & 0xFFFF);
        t--;
        FPAC[2] |= (((t_uint64)GetMap(t) << 16) & 0xFFFF0000);
        t--;
        FPAC[2] |= (((t_uint64)GetMap(t) << 32) & 0xFFFF00000000);
        t--;
        FPAC[2] |= (((t_uint64)GetMap(t) << 48) & 0xFFFF000000000000);
        t--;
        FPAC[1] = ((t_uint64)GetMap(t) & 0xFFFF);
        t--;
        FPAC[1] |= (((t_uint64)GetMap(t) << 16) & 0xFFFF0000);
        t--;
        FPAC[1] |= (((t_uint64)GetMap(t) << 32) & 0xFFFF00000000);
        t--;
        FPAC[1] |= (((t_uint64)GetMap(t) << 48) & 0xFFFF000000000000);
        t--;
        FPAC[0] = ((t_uint64)GetMap(t) & 0xFFFF);
        t--;
        FPAC[0] |= (((t_uint64)GetMap(t) << 16) & 0xFFFF0000);
        t--;
        FPAC[0] |= (((t_uint64)GetMap(t) << 32) & 0xFFFF00000000);
        t--;
        FPAC[0] |= (((t_uint64)GetMap(t) << 48) & 0xFFFF000000000000);
        t--;
        FPSR = (GetMap(t) & 0xFFFF);
        t--;
        FPSR |= ((GetMap(t) << 16) & 0xFFFF0000);
        t--;
        PutMap(040, t);                                 /* Update Stack Pointer */
        continue;
    }    
    if ((IR & 0163777) == 0163150) {                    /* FHLV Halve */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FHLV>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        j = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        get_lf(&dfl, &FPAC[j]);
        dfl.long_fract = dfl.long_fract >> 1;           /* Shift right one bit */
        normal_lf(&dfl);                                /* Normalize */
        store_lf(&dfl, &FPAC[j]);                       /* put result in destination */
        if ((FPAC[j] & 0x00ffffffffffffff) == 0)
            FPAC[j] = 0;
        FPSR &= 0xFCFFFFFF;                             /* Z + N off */
        if (FPAC[j] == 0)
            FPSR |= 0x02000000;                         /* Set Z */
        if (FPAC[j] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if ((IR & 0163777) == 0103150) {                    /* FSCAL Scale */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSCAL>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        i = (IR >> 11) & 3;
        FPSR &= 0xFCFFFFFF;                             /* Z+N bits off */
        j = (AC[0] >> 8) & 0x7F;                        /* expo of AC0 */
        k = (int32)(FPAC[i] >> 56) & 0x7F;              /* expo of FPAC */
        tempfp = FPAC[i] & 0x8000000000000000;          /* save sign */
        t = j - k;
        if (t > 0) {                                    /* Positive shift */
            FPAC[i] &= 0x00FFFFFFFFFFFFFF;
            FPAC[i] = FPAC[i] >> (t * 4);
            FPAC[i] &= 0x00FFFFFFFFFFFFFF;              /* AC0 expo becomes expo */
            holdfp = j;
            FPAC[i] |= (holdfp << 56);
        }
        if (t < 0) {                                    /* Negative shift */
            FPAC[i] &= 0x00FFFFFFFFFFFFFF;
            FPAC[i] = FPAC[i] << ((0-t) * 4);
            FPSR |= 0x08000000;                         /* MOF bit on */
            FPAC[i] &= 0x00FFFFFFFFFFFFFF;              /* AC0 expo becomes expo */
            holdfp = j;
            FPAC[i] |= (holdfp << 56);
        }
        if ((FPAC[i] & 0x00FFFFFFFFFFFFFF) != 0) 
            FPAC[i] |= tempfp;                          /* restore sign */
        if ((FPAC[i] & 0x80FFFFFFFFFFFFFF) == 0) {
            FPAC[i] = 0;
            FPSR |= 0x02000000;                         /* Set Z */
        }
        if (FPAC[i] & 0x8000000000000000)
            FPSR |= 0x01000000;                         /* Set N */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if (IR == 0153350) {                                /* FCLE Clear Errors */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FCLE>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0x07FFFFFF;                             /* set off all error bits */
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    } 
    if (IR == 0103250) {                                /* FNS No Skip */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FNS>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        continue;
    }    
    if (IR == 0107250) {                                /* FSA Always Skip */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 2) {
            printf("\n<<FPU instruction: FSA>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        PC = (PC + 1) & AMASK;
        continue;
    }    
    if (IR == 0137250) {                                /* FSGT */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSGT>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (!(FPSR & 0x03000000))                       /* Z & N both 0? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0123250) {                                /* FSLT */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSLT>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (FPSR & 0x01000000)                          /* N is on? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0113250) {                                /* FSEQ */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSEQ>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (FPSR & 0x02000000)                          /* Z is on? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0133250) {                                /* FSLE */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSLE>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (FPSR & 0x03000000)                          /* Z or N on? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0127250) {                                /* FSGE */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSGE>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (!(FPSR & 0x01000000))                       /* N is off? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0117250) {                                /* FSNE */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSNE>>\n");
            continue;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (!(FPSR & 0x02000000))                       /* Z is off? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0143250) {                                /* FSNM */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSNM>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (!(FPSR & 0x08000000))                       /* MOF is off? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0153250) {                                /* FSNU */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSNU>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (!(FPSR & 0x20000000))                       /* UNF is off? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0163250) {                                /* FSNO */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSNO>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (!(FPSR & 0x40000000))                       /* OVF is off? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0147250) {                                /* FSND */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSND>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (!(FPSR & 0x10000000))                       /* DVZ is off? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0157250) {                                /* FSNUD */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSNUD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (!(FPSR & 0x30000000))                       /* UNF & DVZ off? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0167250) {                                /* FSNOD */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSNOD>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (!(FPSR & 0x50000000))                       /* OVF & DVZ off? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0173250) {                                /* FSNUO */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSNUO>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (!(FPSR & 0x60000000))                       /* OVF & UNF off? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    if (IR == 0177250) {                                /* FSNER */
        if (!(fpu_unit.flags & UNIT_UP))
            continue;
        if (Debug_Flags == 1) {
            printf("\n<<FPU instruction: FSNER>>\n");
            reason = STOP_IBKPT;
        }    
        if (FPFault) {                                  /* Fault from a previous inst? */
            FPFault = 0;
            t = (GetMap(040) + 1) & AMASK;              /* Yes: push rtn block */               
            PutMap(t, AC[0]);
            t++;
            PutMap(t, AC[1]);
            t++;
            PutMap(t, AC[2]);
            t++;
            PutMap(t, AC[3]);
            t++;
            PutMap(t, ((PC-1) & AMASK));
            if (C) PutMap(t, (GetMap(t) | 0100000));
            PutMap(040, t);
            PC = indirect(GetMap(045));                 /* JMP indirect to 45 */
            continue;
        }
        FPSR &= 0xFFFF0000;                             /* Success: put addr in FPSR */
        FPSR |= ((PC - 1) & AMASK);
        if (!(FPSR & 0x78000000))                       /* all errors off? */
            PC = (PC + 1) & AMASK;                      /* yep: skip */
        continue;
    }    
    
    if (Debug_Flags) {
        printf("\n<<Unexecuted inst = %o at PC=%o>>\n\r", IR, PC-1);
        if (Debug_Flags & 040000) reason = STOP_IBKPT;
    }    
}

if (IR == 061777) {                                     /* VCT: Vector on Interrupt */
    int32 stkchg, vtable;
    int32 ventry, dctadr;
    int32 old40, old41, old42, old43;
    
    /* Ok, folks, this is one helluva instruction */
    
    stkchg = GetMap(PC) & 0100000;                      /* Save stack change bit */
    vtable = GetMap(PC) & AMASK;                        /* Address of vector table */
    
    iodev = 0;
    int_req = (int_req & ~INT_DEV) |                    /* Do an INTA w/o an accum */
                (dev_done & ~dev_disable);
    iodata = int_req & (-int_req);
    for (i = DEV_LOW; i <= DEV_HIGH; i++)  {
        if (iodata & dev_table[i].mask) {
            iodev = i;
            break;
        }  
    }
       
    ventry = GetMap(vtable + iodev);                    /* Get Vector Entry */
    
    if (!(ventry & 0100000)) {                          /* Direct bit = 0? */
        PC = ventry & AMASK;                            /* YES - Mode A, so JMP */
        continue;
    }    
    
    dctadr = ventry & AMASK;                            /* Get address of DCT entry */
    
    if (stkchg) {                                       /* Stack change bit = 1? */
        old40 = GetMap(040);                            /* Save stack info */
        old41 = GetMap(041);
        old42 = GetMap(042);
        old43 = GetMap(043);
        PutMap(040, GetMap(004));                       /* Loc 4 to stack ptr */
        PutMap(042, GetMap(006));                       /* Loc 6 to stack limit */
        PutMap(043, GetMap(007));                       /* Loc 7 into stack limit */
        PutMap(040, (GetMap(040) + 1));                 /* Push old contents on new stk */
        PutMap(GetMap(040) & AMASK, old40);
        PutMap(040, (GetMap(040) + 1));
        PutMap(GetMap(040) & AMASK, old41);
        PutMap(040, (GetMap(040) + 1));
        PutMap(GetMap(040) & AMASK, old42);
        PutMap(040, (GetMap(040) + 1));
        PutMap(GetMap(040) & AMASK, old43);
    }    
    
    t = GetMap(dctadr & AMASK);                         /* Get word 0 of DCT */
    
    if (t & 0100000) {                                  /* Push bit set ? */
        PutMap(040, (GetMap(040) + 1));                 /* Push "Standard rtn block" */
        PutMap(GetMap(040) & AMASK, AC[0]);
        PutMap(040, (GetMap(040) + 1));
        PutMap(GetMap(040) & AMASK, AC[1]);
        PutMap(040, (GetMap(040) + 1));
        PutMap(GetMap(040) & AMASK, AC[2]);
        PutMap(040, (GetMap(040) + 1));
        PutMap(GetMap(040) & AMASK, AC[3]);
        PutMap(040, (GetMap(040) + 1));
        PutMap(GetMap(040) & AMASK, GetMap(0));
        if (GetMap(0) == 0 && Debug_Flags) {
            printf("\n<<VCT will rtn to 0 @ %o>>\n\r", PC);
            reason = STOP_IBKPT;
        }    
        if (C) PutMap(GetMap(040) & AMASK, (GetMap(GetMap(040) & AMASK) | 0100000));
    }
    
    AC[2] = dctadr & AMASK;                             /* DCT Addr into AC2 */
    
    PutMap(040, (GetMap(040) + 1));                     /* Push pri int mask onto stack */
    PutMap(GetMap(040) & AMASK, pimask);
    
    AC[0] = GetMap(dctadr + 1) | pimask;                /* Build new mask from word 1 of dct */
    PutMap(005, AC[0]);
    
    mask_out(pimask = AC[0]);                           /* Do a mask out inst */
    
    PC = GetMap(dctadr) & AMASK;                        /* Finally, JMP to int routine */
    
    continue;
}

/*************************************************************************
**   At this point, the instruction is not an Eclipse one.  Therefore   **
**   decode it as a Nova instruction just like the Nova does.           **
*************************************************************************/

/* Memory reference instructions */

if (t < 014) {                                          /* mem ref? */
    register int32 src, MA;

    MA = IR & 0377;
    switch ((IR >> 8) & 03) {                           /* decode IR<6:7> */
    case 0:                                             /* page zero */
        break;
    case 1:                                             /* PC relative */
        if (MA & 0200) MA = 077400 | MA;
        MA = (MA + PC - 1) & AMASK;
        break;
    case 2:                                             /* AC2 relative */
        if (MA & 0200) MA = 077400 | MA;
        MA = (MA + AC[2]) & AMASK;
        break;
    case 3:                                             /* AC3 relative */
        if (MA & 0200) MA = 077400 | MA;
        MA = (MA + AC[3]) & AMASK;
        break;  
    }
    if (IR & 002000) {                                  /* indirect? */
        for (i = 0; i < (ind_max * 2); i++) {           /* count indirects */
            if ((MA & 077770) == 020 && !(cpu_unit.flags & UNIT_MICRO))
                MA = (PutMap(MA & AMASK, (GetMap(MA & AMASK) + 1) & 0177777));
            else if ((MA & 077770) == 030 && !(cpu_unit.flags & UNIT_MICRO))
                MA = (PutMap(MA & AMASK, (GetMap(MA & AMASK) - 1) & 0177777));
            else MA = GetMap(MA & AMASK);
            if (MapStat & 1) {                          /* Start MAP */
                Usermap = Enable;
                Inhibit = 0;
            }                   
            if ((MA & 0100000) == 0) break;
            if (i >= ind_max && (MapStat & 010) && Usermap) break;
        }
        if (i >= (ind_max-1)) {
            if ((MapStat & 010) && Usermap) {
                Fault = 04000;                          /* Map fault if IND prot */
                continue;
                } 
            if (i >= (ind_max * 2) && !(Fault)) {
                reason = STOP_IND;
                break;
            }
        }
    }       

    switch (t) {                                        /* decode IR<1:4> */
    case 001:                                           /* JSR */
        AC[3] = PC;
    case 000:                                           /* JMP */
        old_PC = PC;
        PC = MA;
        break;
    case 002:                                           /* ISZ */
        src = (GetMap(MA) + 1) & 0177777;
        if (MEM_ADDR_OK (MA)) PutMap(MA, src);
        if (src == 0) PC = (PC + 1) & AMASK;
        break;
    case 003:                                           /* DSZ */
        src = (GetMap(MA) - 1) & 0177777;
        if (MEM_ADDR_OK (MA)) PutMap(MA, src);
        if (src == 0) PC = (PC + 1) & AMASK;
        break;
    case 004:                                           /* LDA 0 */
        if (SingleCycle) Usermap = SingleCycle;
        AC[0] = GetMap(MA);
        if (SingleCycle) {
            Usermap = SingleCycle = 0;
            if (Inhibit == 1) Inhibit = 3;
            MapStat |= 02000;
            MapStat &= 0177776;
        }    
        break;
    case 005:                                           /* LDA 1 */
        if (SingleCycle) Usermap = SingleCycle;
        AC[1] = GetMap(MA);
        if (SingleCycle) {
            Usermap = SingleCycle = 0;
            if (Inhibit == 1) Inhibit = 3;
            MapStat |= 02000;
            MapStat &= 0177776;
        }    
        break;
    case 006:                                           /* LDA 2 */
        if (SingleCycle) Usermap = SingleCycle;
        AC[2] = GetMap(MA);
        if (SingleCycle) {
            Usermap = SingleCycle = 0;
            if (Inhibit == 1) Inhibit = 3;
            MapStat |= 02000;
            MapStat &= 0177776;
        }    
        break;
    case 007:                                           /* LDA 3 */
        if (SingleCycle) Usermap = SingleCycle;
        AC[3] = GetMap(MA);
        if (SingleCycle) {
            Usermap = SingleCycle = 0;
            if (Inhibit == 1) Inhibit = 3;
            MapStat |= 02000;
            MapStat &= 0177776;
        }    
        break;
    case 010:                                           /* STA 0 */
        if (SingleCycle) 
            Usermap = SingleCycle;
        if (MEM_ADDR_OK (MA)) PutMap(MA, AC[0]);
        if (SingleCycle) {
            Usermap = SingleCycle = 0;
            if (Inhibit == 1) Inhibit = 3;
            MapStat |= 02000;
            MapStat &= 0177776;
        }    
        break;
    case 011:                                           /* STA 1 */
        if (SingleCycle) 
            Usermap = SingleCycle;
        if (MEM_ADDR_OK (MA)) PutMap(MA, AC[1]);
        if (SingleCycle) {
            Usermap = SingleCycle = 0;
            if (Inhibit == 1) Inhibit = 3;
            MapStat |= 02000;
            MapStat &= 0177776;
        }    
        break;
    case 012:                                          /* STA 2 */
        if (SingleCycle) 
            Usermap = SingleCycle;
        if (MEM_ADDR_OK (MA)) PutMap(MA, AC[2]);
        if (SingleCycle) {
            Usermap = SingleCycle = 0;
            if (Inhibit == 1) Inhibit = 3;
            MapStat |= 02000;
            MapStat &= 0177776;
        }    
        break;
    case 013:                                           /* STA 3 */
        if (SingleCycle) 
            Usermap = SingleCycle;
        if (MEM_ADDR_OK (MA)) PutMap(MA, AC[3]);
        if (SingleCycle) {
            Usermap = SingleCycle = 0;
            if (Inhibit == 1) Inhibit = 3;
            MapStat |= 02000;
            MapStat &= 0177776;
        }    
        break;
    }                                                   /* end switch */
}                                                       /* end mem ref */

/* Operate instruction */

else if (t & 020) {                                     /* operate? */
    register int32 src, srcAC, dstAC;

    srcAC = (t >> 2) & 3;                               /* get reg decodes */
    dstAC = t & 03;
    switch ((IR >> 4) & 03) {                           /* decode IR<10:11> */
    case 0:                                             /* load */
        src = AC[srcAC] | C;
        break;
    case 1:                                             /* clear */
        src = AC[srcAC];
        break;
    case 2:                                             /* set */
        src = AC[srcAC] | 0200000;
        break;
    case 3:                                             /* complement */
        src = AC[srcAC] | (C ^ 0200000);
        break;
    }                                                   /* end switch carry */

    switch ((IR >> 8) & 07) {                           /* decode IR<5:7> */
    case 0:                                             /* COM */
        src = src ^ 0177777;
        break;
    case 1:                                             /* NEG */
        src = ((src ^ 0177777) + 1) & 0377777;
        break;
    case 2:                                             /* MOV */
        break;
    case 3:                                             /* INC */
        src = (src + 1) & 0377777;
        break;
    case 4:                                             /* ADC */
        src = ((src ^ 0177777) + AC[dstAC]) & 0377777;
        break;
    case 5:                                             /* SUB */
        src = ((src ^ 0177777) + AC[dstAC] + 1) & 0377777;
        break;
    case 6:                                             /* ADD */
        src = (src + AC[dstAC]) & 0377777;
        break;
    case 7:                                             /* AND */
        src = src & (AC[dstAC] | 0200000);
        break;
    }                                                   /* end switch oper */

    switch ((IR >> 6) & 03) {                           /* decode IR<8:9> */
    case 0:                                             /* nop */
        break;
    case 1:                                             /* L */
        src = ((src << 1) | (src >> 16)) & 0377777;
        break;
    case 2:                                             /* R */
        src = ((src >> 1) | (src << 16)) & 0377777;
        break;
    case 3:                                             /* S */
        src = ((src & 0377) << 8) | ((src >> 8) & 0377) |
                (src & 0200000);
        break;
    }                                                   /* end switch shift */

    switch (IR & 07) {                                  /* decode IR<13:15> */
    case 0:                                             /* nop */
        break;
    case 1:                                             /* SKP */
        PC = (PC + 1) & AMASK;
        break;
    case 2:                                             /* SZC */
        if (src < 0200000) PC = (PC + 1) & AMASK;
        break;
    case 3:                                             /* SNC */
        if (src >= 0200000) PC = (PC + 1) & AMASK;
        break;
    case 4:                                             /* SZR */
        if ((src & 0177777) == 0) PC = (PC + 1) & AMASK;
        break;
    case 5:                                             /* SNR */
        if ((src & 0177777) != 0) PC = (PC + 1) & AMASK;
        break;
    case 6:                                             /* SEZ */
        if (src <= 0200000) PC = (PC + 1) & AMASK;
        break;
    case 7:                                             /* SBN */
        if (src > 0200000) PC = (PC + 1) & AMASK;
        break;
    }                                                   /* end switch skip */
    if ((IR & 000010) == 0) {                           /* load? */
        AC[dstAC] = src & 0177777;
        C = src & 0200000;
    }                                                   /* end if load */
}                                                       /* end if operate */

/* IOT instruction */

else {                                                  /* IOT */
    register int32 dstAC, pulse, code, device, iodata;
    char pulcode[4];
    
    if ((MapStat & 0100)                                /* LEF mode bit on? */ 
         && Usermap) {                                  /* We are in LEF Mode */
        AC[(IR >> 11) & 3] = LEFmode(PC - 1, (IR >> 8) & 3, IR & 0377, IR & 02000);
        if (Debug_Flags & 020000) {
            printf("\n\r<<LEF Break by special request - executed at %o.>>\n\r", PC-1);
            reason = STOP_IBKPT;
        }     
        continue;
    }
        
    dstAC = t & 03;                                     /* decode fields */
    if ((MapStat & 040) && Usermap) {                   /* I/O protection fault */
        Fault = 020000;
        continue;
    }       
    code = (IR >> 8) & 07;
    pulse = (IR >> 6) & 03;
    device = IR & 077;
    if (Debug_Flags && device == 0) {
         printf("\n\r<<I/O to device 00 at %o.>>\n\r", PC-1);
         reason = STOP_IBKPT;
         continue;
    }     
    if ((Debug_Flags & 0100) && (device == (Debug_Flags & 077))) {
         printf("\n\r<<I/O Break (device %o) >>\n\r", device);
         reason = STOP_IBKPT;
         continue;
    }     
    if ((Debug_Char != 0) && (device == 011) &&
        ((AC[dstAC] & 0177) == Debug_Char)) {
        printf("\n\r<<I/O Break (Char %o to TTO) >>\n\r", Debug_Char);
        reason = STOP_IBKPT;
        continue;
    }     
    if (code == ioSKP) {                                /* IO skip? */
        switch (pulse) {                                /* decode IR<8:9> */
        case 0:                                         /* skip if busy */
            if ((device == 077)? (int_req & INT_ION) != 0:
            (dev_busy & dev_table[device].mask) != 0)
                PC = (PC + 1) & AMASK;
            break;
        case 1:                                         /* skip if not busy */
            if ((device == 077)? (int_req & INT_ION) == 0:
            (dev_busy & dev_table[device].mask) == 0)
                PC = (PC + 1) & AMASK;
            break;
        case 2:                                         /* skip if done */
            if ((device == 077)? pwr_low != 0:
            (dev_done & dev_table[device].mask) != 0)
                PC = (PC + 1) & AMASK;
            break;
        case 3:                                         /* skip if not done */
            if ((device == 077)? pwr_low == 0:
                (dev_done & dev_table[device].mask) == 0)
                PC = (PC + 1) & AMASK;
            break;
        }                                               /* end switch */
    }                                                   /* end IO skip */

    else if (device == DEV_CPU) {                       /* CPU control */
        switch (code) {                                 /* decode IR<5:7> */
        case ioNIO:                                     /* Get CPU ID */
            switch (model) {
            case 280:                                   /* S280 */
                AC[0] = 021102;
                break;
            case 380:
                AC[0] = 013212;                         /* C380 */
                break;
            default:
                break;
            }           
            break;                                      /* Otherwise no-op */
        case ioDIA:                                     /* read switches */
            AC[dstAC] = SR;
            break;
        case ioDIB:                                     /* int ack */
            AC[dstAC] = 0;
            int_req = (int_req & ~INT_DEV) |
                    (dev_done & ~dev_disable);
            iodata = int_req & (-int_req);
            for (i = DEV_LOW; i <= DEV_HIGH; i++)  {
                if (iodata & dev_table[i].mask) {
                    AC[dstAC] = i;
                    break;
                }
            }
            break;
        case ioDOB:                                     /* mask out */
            mask_out (pimask = AC[dstAC]);
            break;
        case ioDIC:                                     /* io reset IORST */
            reset_all (0);                              /* reset devices */
            Usermap = 0;                                /* reset MAP */
            MapStat &= 04;                              /* Reset MAP status */
            MapIntMode = 0;
            Inhibit = 0;
            Map31 = 037;
            Check = SingleCycle = 0;
            Fault = 0;
            FPSR &= 0x0000FFFF;
            FPFault = 0;
            break;
        case ioDOC:                                     /* halt */
            reason = STOP_HALT;
            break;
        }                                               /* end switch code */

        switch (pulse) {                                /* decode IR<8:9> */
        case iopS:                                      /* ion */
            int_req = (int_req | INT_ION) & ~INT_NO_ION_PENDING;
            break;
        case iopC:                                      /* iof */
            int_req = int_req & ~INT_ION;
            break;  }                                   /* end switch pulse */
        }                                               /* end CPU control */

    else if (device == DEV_ECC) {
        switch (code) {
        case ioDIA:                                     /* Read Fault Address */
            AC[dstAC] = 0;
            break;
        case ioDIB:                                     /* Read fault code */
            AC[dstAC] = 0;
            break;
        case ioDOA:                                     /* Enable ERCC */
            break;  }
        }
        
    else if (device == DEV_MAP) {                       /* MAP control */
        switch (code) {                                 /* decode IR<5:7> */
        case ioNIO:                                     /* No I/O -- Single */
            if (!Usermap || !(MapStat & 0140)) {
                if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o NIO %o (No I/O, clear faults)\n", PC-1, dstAC);
                MapStat &= ~036000;                     /* NIO Clears all faults */
            } else {
                if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o NIO %o (No I/O, clear faults) NO EXEC(User mode)\n", PC-1, dstAC);
            }       
            break;
        case ioDIA:                                     /* Read map status */
            if (!Usermap || !(MapStat & 0140)) {    
                if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o DIA %o=%o (Read Map Status)\n", PC-1, dstAC, MapStat);
                AC[dstAC] = MapStat & 0xFFFE;
                if (MapIntMode & 1)                     /* Bit 15 is mode asof last int */
                AC[dstAC] |= 1;
            } else {
                if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o DIA %o=%o (Read Map Status) NO EXEC(User mode)\n", PC-1, dstAC, MapStat);
            }       
            break;
        case ioDOA:                                     /* Load map status */
            if (!Usermap || !(MapStat & 0140)) {    
                if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o DOA %o=%o (Load Map Status)\n", PC-1, dstAC, AC[dstAC]);
                MapStat = AC[dstAC];
                MapIntMode = 0;
                Enable = 1;
                if (MapStat & 04) Enable = 2;
                Check &= ~01600;
                Check |= MapStat & 01600;
                if (MapStat & 1)
                Inhibit = 2;                            /* Inhibit interrupts */
            } else {
                if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o DOA %o=%o (Load Map Status) NO EXEC(User mode)\n", PC-1, dstAC, AC[dstAC]);
            }       
            break;          
        case ioDIB:                                     /* not used */
            break;
        case ioDOB:                                     /* map block 31 */
//AOS       if (!Usermap || !(MapStat && 0140)) {    
            if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o DOB %o=%o (Map Blk 31)\n", PC-1, dstAC, AC[dstAC]);
            Map31 = AC[dstAC] & PAGEMASK;
            MapStat &= ~02000;
//AOS       } else {
//AOS           if ((Debug_Flags & 077) == 03)
//AOS           fprintf(Trace, "%o DOB %o=%o (Map Blk 31) NO EXEC (User Mode)\n", PC-1, dstAC, AC[dstAC]);
//AOS       }   
            break;
        case ioDIC:                                     /* Page Check */
            if (!Usermap || !(MapStat & 0140)) {
            switch ((Check>>7) & 07) {
                case 0: i=1; break;
                case 1: i=6; break;
                case 2: i=2; break;
                case 3: i=7; break;
                case 4: i=0; break;
                case 5: i=4; break;
                case 6: i=3; break;
                case 7: i=5; break;
                default: break;
            }
            j = (Check >> 10) & 037;
            AC[dstAC] = Map[i][j] & 0101777;
            AC[dstAC] |= ((Check << 5) & 070000);
            if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o DIC %o=%o (Page Check)\n", PC-1, dstAC, AC[dstAC]);
            MapStat &= ~02000;
            } else {
            if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o DIC %o=%o (Page Check) NO EXEC(User mode)\n", PC-1, dstAC, AC[dstAC]);
            }    
            break;
        case ioDOC:                                     /* Init Page Check */
            if (!Usermap || !(MapStat & 0140)) {            
            if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o DOC %o=%o (Init Pg Chk)\n", PC-1, dstAC, AC[dstAC]);
            Check = AC[dstAC];
            MapStat &= ~01600;
            MapStat |= (Check & 01600);
            MapStat &= ~02000;
            } else {
            if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o DOC %o=%o (Init Pg Chk) NO EXEC(User mode)\n", PC-1, dstAC, AC[dstAC]);
            }    
            break;  
        }                                               /* end switch code */

        switch (pulse) {
        case iopP:
            if ((Debug_Flags & 077) == 03)
                fprintf(Trace, "%o xxxP (Single Cycle)\n", PC-1);
            if (Usermap) {
                MapStat &= 0177776;
                Usermap = 0;
                Inhibit = 0;
            } else {    
                SingleCycle = Enable;
                Inhibit = 1;                            /* Inhibit interrupts */
            }    
            break;
        }
    }                                                   /* end CPU control */
    else if (dev_table[device].routine) {               /* normal device */
        iodata = dev_table[device].routine (pulse, code, AC[dstAC]);
        reason = iodata >> IOT_V_REASON;
        if (code & 1) AC[dstAC] = iodata & 0177777;
        if ((Debug_Flags & 077) == device && Debug_Flags != 0) {
            strcpy(pulcode, "");
            switch (pulse) {
                case iopP:
                strcpy(pulcode, "P");
                break;
            case iopS:
                strcpy(pulcode, "S");
                break;
            case iopC:
                strcpy(pulcode, "C");
                break;
            default:
                break;
            }            
            switch(code) {
            case ioNIO:
                fprintf(Trace, "[%o] %o NIO%s %o\n", device, PC-1, pulcode, AC[dstAC]);
                break;
            case ioDIA:
                fprintf(Trace, "[%o] %o DIA%s %o\n", device, PC-1, pulcode, iodata);
                break;
            case ioDIB:
                fprintf(Trace, "[%o] %o DIB%s %o\n", device, PC-1, pulcode, iodata);
                break;
            case ioDIC:
                fprintf(Trace, "[%o] %o DIC%s %o\n", device, PC-1, pulcode, iodata);
                break;
            case ioDOA:
                fprintf(Trace, "[%o] %o DOA%s %o\n", device, PC-1, pulcode, AC[dstAC]);
                break;
            case ioDOB:
                fprintf(Trace, "[%o] %o DOB%s %o\n", device, PC-1, pulcode, AC[dstAC]);
                break;
            case ioDOC:
                fprintf(Trace, "[%o] %o DOC%s %o\n", device, PC-1, pulcode, AC[dstAC]);
                break;
            default:
                break;
            }                                           /* end switch */       
        }                                               /* end if debug */
    }                                                   /* end else if */
    else reason = stop_dev;
}                                                       /* end if IOT */
}                                                       /* end while */

/* Simulation halted */

saved_PC = PC;
return reason;
}

/* Computes and returns a 16-bit effective address, given a
   program counter, index, and a displacement.
*/

int32 effective(int32 PC, int32 index, int32 disp)
{
    register int32 i, MA;

    MA = disp & 077777;
    switch (index) {                                    /* decode IR<6:7> */
    case 0:                                             /* page zero */
        break;
    case 1:                                             /* PC relative */
        MA = (MA + PC) & AMASK;
        break;
    case 2:                                             /* AC2 relative */
        MA = (MA + AC[2]) & AMASK;
        break;
    case 3:                                             /* AC3 relative */
        MA = (MA + AC[3]) & AMASK;
        break;
    }                                                   /* end switch mode */

    if (disp & 0100000) {                               /* indirect? */
        for (i = 0; i < ind_max * 2; i++) {             /* count indirects */
            MA = GetMap(MA & AMASK);            
            if (SingleCycle) Usermap = 0;
            if (MapStat & 1) {                          /* Start MAP */
                Usermap = Enable;
                Inhibit = 0;
            }                   
            if ((MA & 0100000) == 0) break; 
            if ((MapStat & 010) && Usermap && i >= ind_max) break;
        }
        if (i >= (ind_max-1) && (MapStat & 010) && Usermap) {
            Fault = 04000;                              /* Map fault if IND prot */
        } 
        if (i >= (ind_max * 2) && !(Fault)) {
            reason = STOP_IND_INT;                      /* Stop machine */
        }
    }
    return (MA & AMASK);
}   

/* Computes and returns a 16-bit effective address, given a
   program counter, index, and a displacement.  This is a 
   version supporting the LEF map mode instruction, as 
   opposed to the ELEF instruction.
*/

int32 LEFmode(int32 PC, int32 index, int32 disp, int32 indirect)
{
    register int32 i, MA;
    int16 sMA;

    MA = disp & 077777;
    switch (index) {                                    /* decode IR<6:7> */
    case 0:                                             /* page zero */
        break;
    case 1:                                             /* PC relative */
        sMA = MA;
        if (MA & 0200) sMA |= 0xff00;
        MA = (sMA + PC) & AMASK;
        break;
    case 2:                                             /* AC2 relative */
        sMA = MA;
        if (MA & 0200) sMA |= 0xff00;
        MA = (sMA + AC[2]) & AMASK;
        break;
    case 3:                                             /* AC3 relative */
        sMA = MA;
        if (MA & 0200) sMA |= 0xff00;
        MA = (sMA + AC[3]) & AMASK;
        break;
    }                                                   /* end switch mode */

    if (indirect) {                                     /* indirect? */
        for (i = 0; i < (ind_max * 2); i++) {           /* count indirects */
            if ((MA & 077770) == 020 && !(cpu_unit.flags & UNIT_MICRO))
                MA = (PutMap(MA & AMASK, (GetMap(MA & AMASK) + 1) & 0177777));
            else if ((MA & 077770) == 030 && !(cpu_unit.flags & UNIT_MICRO))
                MA = (PutMap(MA & AMASK, (GetMap(MA & AMASK) - 1) & 0177777));
            else MA = GetMap(MA & AMASK);
            if (SingleCycle) Usermap = 0;
            if (MapStat & 1) {                          /* Start MAP */
                Usermap = Enable;
                Inhibit = 0;
            }               
            if ((MA & 0100000) == 0) break;
            if ((MapStat & 010) && Usermap && i >= ind_max) break;
        }
        if (i >= (ind_max-1) && (MapStat & 010) && Usermap) {
            Fault = 04000;                              /* Map fault if IND prot */
        } 
        if (i >= (ind_max * 2) && !(Fault)) {
            reason = STOP_IND_INT;                      /* Stop machine */
        }   
    }
    return (MA & AMASK);
}   

/* Computes a "Byte pointer" for the Character Instruction set */
/* This address in 'PC' must point to the displacement word of the instruction */

int32 Bytepointer(int32 PC, int32 index)
{
    register int32 MA;

    switch (index) {                                    /* decode IR<6:7> */
    case 0:                                             /* page zero */
        MA = 0;
        break;
    case 1:                                             /* PC relative */
        MA = PC & AMASK;
        break;
    case 2:                                             /* AC2 relative */
        MA = AC[2] & AMASK;
        break;
    case 3:                                             /* AC3 relative */
        MA = AC[3] & AMASK;
        break;
    }                                                   /* end switch mode */
    MA = (MA * 2) & 0177777;
    MA = MA + GetMap(PC); 
    return (MA & 0177777);
}

/* Given an address, returns either that address if bit 0 is 0, or
   or follows an indirection chain until bit 0 is 0
*/

int32 indirect(int32 d)
{
    int i;
       
    if (d & 0100000) {                                  /* indirect? */
        for (i = 0; i < ind_max * 2; i++) {             /* count indirects */
            if ((d & 077770) == 020 && !(cpu_unit.flags & UNIT_MICRO)) 
                d = (PutMap(d & AMASK, ((GetMap(d & AMASK) + 1) & 0177777)));
            else if ((d & 077770) == 030 && !(cpu_unit.flags & UNIT_MICRO)) 
                d = (PutMap(d & AMASK, ((GetMap(d & AMASK) - 1) & 0177777)));
            else d = GetMap(d & AMASK);
            if (MapStat & 1) {                          /* Start MAP */
                Usermap = Enable;
                Inhibit = 0;
            }               
            if ((d & 0100000) == 0) break;
            if ((MapStat & 010) && Usermap && i >= ind_max) break;
        }
        if (i >= (ind_max-1) && (MapStat & 010) && Usermap) {
            Fault = 04000;                              /* Map fault if IND prot */
        } 
        if (i >= (ind_max * 2) && !(Fault)) {
            reason = STOP_IND;                          /* Stop machine */
        }
    } 
    return (d);
}

/* Push a standard return block onto the stack */

int32 pushrtn(int32 pc)
{
    int32 t;
    
    t = (GetMap(040) + 1) & AMASK;                          
    PutMap(t, AC[0]);
    t++;
    PutMap(t, AC[1]);
    t++;
    PutMap(t, AC[2]);
    t++;
    PutMap(t, AC[3]);
    t++;
    PutMap(t, pc);
    if (C) PutMap(t, (GetMap(t) | 0100000));
    PutMap(040,  t);
    return 0;
}

/* Eclipse memory get/put - uses MAP if enabled */

int32 GetMap(int32 addr)
{
     int32 page;
     t_addr paddr;
     
    switch (Usermap) {
        case 0:
            if (addr < 076000)
                return M[addr];
            paddr = ((Map31 & PAGEMASK) << 10) | (addr & 001777);
            if (paddr < MEMSIZE)
                 return M[paddr];
                else
                 return (0); 
            break;    
        case 1:
            page = (addr >> 10) & 037;
            paddr = ((Map[1][page] & 01777) << 10) | (addr & 001777);
            if (Map[1][page] == INVALID && !SingleCycle) 
                Fault = 0100000/*!!!*/;                 /* Validity */
            if (paddr < MEMSIZE)
                 return M[paddr];
                else
                 return (0); 
            break;
        case 2:
            page = (addr >> 10) & 037;
            paddr = ((Map[2][page] & PAGEMASK) << 10) | (addr & 001777);
            if (Map[2][page] == INVALID && !SingleCycle) 
                Fault = 0100000/*!!!*/;                /* Validity */
            if (paddr < MEMSIZE)
                 return M[paddr];
                else
                 return (0); 
            break;
        case 6:
            page = (addr >> 10) & 037;
            paddr = ((Map[6][page] & PAGEMASK) << 10) | (addr & 001777);
            if (Map[6][page] == INVALID && !SingleCycle) 
                Fault = 0100000/*!!!*/;                /* Validity */
            if (paddr < MEMSIZE)
                 return M[paddr];
                else
                 return (0); 
            break;
        case 7:
            page = (addr >> 10) & 037;
            paddr = ((Map[7][page] & PAGEMASK) << 10) | (addr & 001777);
            if (Map[7][page] == INVALID && !SingleCycle) 
                Fault = 0100000/*!!!*/;                /* Validity */
            if (paddr < MEMSIZE)
                 return M[paddr];
                else
                 return (0); 
            break;
        default:
            printf("\n\r<<MAP FAULT>>\n\r");
            return M[addr];
            break;
     }            
}

int32 PutMap(int32 addr, int32 data)
{
    int32 page;
        t_addr paddr;
    
    switch (Usermap) {
        case 0:
            if (addr < 076000) {
                M[addr] = data;
                return (data);
            }
            paddr = ((Map31 & PAGEMASK) << 10) | (addr & 001777);
            if (paddr < MEMSIZE) M[paddr] = data;    
            break;
        case 1:
            page = (addr >> 10) & 037;
            paddr = ((Map[1][page] & PAGEMASK) << 10) | (addr & 001777);
            if (((Map[1][page] & 0100000) && (MapStat & 020)) || Map[1][page] == INVALID) 
                Fault = 010000;                         /* Write Protect Fault */
            else if (paddr < MEMSIZE) M[paddr] = data; 
            break;
        case 2:
            page = (addr >> 10) & 037;
            paddr = ((Map[2][page] & PAGEMASK) << 10) | (addr & 001777);
            if (((Map[2][page] & 0100000) && (MapStat & 020)) || Map[2][page] == INVALID) 
                Fault = 010000;                         /* Write Protect Fault */
            else if (paddr < MEMSIZE) M[paddr] = data;
            break;    
        case 6:
            page = (addr >> 10) & 037;
            paddr = ((Map[2][page] & PAGEMASK) << 10) | (addr & 001777);
            if (((Map[6][page] & 0100000) && (MapStat & 020)) || Map[6][page] == INVALID)
                Fault = 010000;                         /* Write Protect Fault */
            else if (paddr < MEMSIZE) M[paddr] = data;
            break;    
        case 7:
            page = (addr >> 10) & 037;
            paddr = ((Map[2][page] & PAGEMASK) << 10) | (addr & 001777);
            if (((Map[7][page] & 0100000) && (MapStat & 020)) || Map[7][page] == INVALID) 
                Fault = 010000;                         /* Write Protect Fault */
            else if (paddr < MEMSIZE) M[paddr] = data;
            break;    
        default:
            M[addr] = data;
            break;
    }
    return (data);            
}

#if 0
int16 GetDCHMap(int32 map, int32 addr)
{
     t_addr paddr;
     if (!(MapStat & 02)) return M[addr];
     paddr = ((Map[map][(addr >> 10) & 037] & PAGEMASK) << 10) | (addr & 001777);
     if (paddr < MEMSIZE)
         return M[paddr]; 
     return (0);       
}

int16 PutDCHMap(int32 map, int32 addr, int16 data)
{
     t_addr paddr;
     if (!(MapStat & 02)) {
         M[addr] = data;      
         return (data);
     }    
     paddr = ((Map[map][(addr >> 10) & 037] & PAGEMASK) << 10) | (addr & 001777);
     if (paddr < MEMSIZE)
        M[paddr] = data;
     return (data);    
}
#endif

/* Given a map number and a logical, returns the physical address, unless
   the map is not active, in which case logical = physical.  This is
   used primarily by the I/O routines to map data channel read/writes.
*/
   
int32 MapAddr(int32 map, int32 addr)
{
     int32 paddr;
     if ((map == 0 || map > 2) && !(MapStat & 02)) return addr;
     if (map > 0 && map < 3 && Usermap == 0) return addr;
     paddr = ((Map[map][(addr >> 10) & 037] & PAGEMASK) << 10) | (addr & 001777);
     return paddr;    
}

/* Loads a word into the Eclipse Maps */

int32 LoadMap(int32 w)
{
    int32 m;
    
    m = (w >> 10) & 037;
    switch ((MapStat >> 7) & 07) {
        case 0:                                         /* Load user A Map */
            Map[1][m] = w & MAPMASK;
            break;
        case 1:                                         /* Load user C Map */
            Map[6][m] = w & MAPMASK;
            break;              
        case 2:                                         /* Load user B Map */
            Map[2][m] = w & MAPMASK;
            break;
        case 3:                                         /* Load user D Map */
            Map[7][m] = w & MAPMASK;
            break;              
        case 4:                                         /* Load DCH A Map */
            Map[0][m] = w & MAPMASK;
            break;              
        case 5:                                         /* Load DCH C Map */
            Map[4][m] = w;
            break;              
        case 6:                                         /* Load DCH B Map */
            Map[3][m] = w;
            break;              
        case 7:                                         /* Load DCH D Map */
            Map[5][m] = w;
            break;              
        default:
            break;                
    }
    return 0;
}

/* Displays an error on a unimplemented (in this sim) instr. */

int32 unimp(int32 PC)
{
    if (Debug_Flags)
         printf("\n\r\007<<<Unimplemented instruction: [%o] %o>>>\n\r", PC - 1, GetMap(PC - 1));
    return 0;
}

/* New priority mask out */

void mask_out (int32 newmask)
{
int32 i;

dev_disable = 0;
for (i = DEV_LOW; i <= DEV_HIGH; i++)  {
    if (newmask & dev_table[i].pi)
        dev_disable = dev_disable | dev_table[i].mask;
}
int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable);
return;
}

/* Reset routine */

t_stat cpu_reset (DEVICE *dptr)
{
int_req = int_req & ~INT_ION;
pimask = 0;
dev_disable = 0;
pwr_low = 0;
sim_brk_types = sim_brk_dflt = SWMASK ('E');
return SCPE_OK;
}

/* Memory examine */

t_stat cpu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if (sw & SWMASK ('V')) {
    if (addr > 077777) return SCPE_NXM;
    if (vptr != NULL) *vptr = GetMap (addr);
}
else {
    if (addr >= MEMSIZE) return SCPE_NXM;
    if (vptr != NULL) *vptr = M[addr] & 0177777;
}
return SCPE_OK;
}

/* Memory deposit */

t_stat cpu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if (sw & SWMASK ('V')) {
    if (addr > 077777) return SCPE_NXM;
    PutMap (addr, (int32) val);
}
else {
    if (addr >= MEMSIZE) return SCPE_NXM;
    M[addr] = (int32) val & 0177777;
}
return SCPE_OK;
}

/* Alter memory size */

t_stat cpu_set_size (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
int32 mc = 0;
t_addr i;

if ((val <= 0) || (val > MAXMEMSIZE) || ((val & 07777) != 0))
    return SCPE_ARG;
for (i = val; i < MEMSIZE; i++) mc = mc | M[i];
if ((mc != 0) && (!get_yn ("Really truncate memory [N]?", FALSE)))
    return SCPE_OK;
MEMSIZE = val;
for (i = MEMSIZE; i < MAXMEMSIZE; i++) M[i] = 0;
return SCPE_OK;
}

/* MAP device services */

t_stat map_svc (UNIT *uptr)
{
return SCPE_OK;
}

/* Map examine */

t_stat map_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
if ((addr & 077) >= 037 || addr > 737) return SCPE_NXM;
uptr->u4 = -2;  /* signal to print_sys in eclipse_sys.c: do not map */
if (vptr != NULL) *vptr = Map[(addr >> 6) & 3][addr & 037] & 0177777;
return SCPE_OK;
}

/* Memory deposit */

t_stat map_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
if ((addr & 077) >= 037 || addr > 0737) return SCPE_NXM;
uptr->u4 = -2;  /* signal to print_sys in eclipse_sys.c: do not map */
Map[(addr >> 6) & 3][addr & 037] = (int32)val & 0177777;
return SCPE_OK;
}

/* FPU device services */

t_stat fpu_svc (UNIT *uptr)
{
return SCPE_OK;
}

/* PIT Device Services */

/* IOT routine */

int32 pit (int32 pulse, int32 code, int32 AC)
{
int32 iodata = 0;

if (code == ioDIA) {                                    /* DIA */
    if (pit_flag == 0) {
        pit_flag = 1;
    }
    iodata = pit_counter;
}
if (code == ioDOA) {                                    /* DOA */
    pit_initial = AC;                                   /* Load Counter */
    sim_rtcn_init (pit_time, 1);                        /* init calibr */
}
switch (pulse) {                                        /* decode IR<8:9> */
case iopS:                                              /* start */
    pit_counter = pit_initial;                          /* Set the counter */
    dev_busy = dev_busy | INT_PIT;                      /* set busy */
    dev_done = dev_done & ~INT_PIT;                     /* clear done, int */
    int_req = int_req & ~INT_PIT;
    if (!sim_is_active (&pit_unit))                     /* not running? */
        sim_activate (&pit_unit,                        /* activate */
            sim_rtcn_init (pit_time, 1));               /* init calibr */
    break;
case iopC:                                              /* clear */
    dev_busy = dev_busy & ~INT_PIT;                     /* clear busy */
    dev_done = dev_done & ~INT_PIT;                     /* clear done, int */
    int_req = int_req & ~INT_PIT;
    sim_cancel (&pit_unit);                             /* deactivate unit */
    break;  }                                           /* end switch */
return iodata;  
}

/* Unit service */

t_stat pit_svc (UNIT *uptr)
{
int32 t;
t = sim_rtcn_calb (pit_tps, 1);                         /* calibrate delay */
sim_activate (&pit_unit, t);                            /* reactivate unit */
pit_poll = t / (-pit_adj);                              /* adjust poll */
pit_counter++;                                          /* Increment counter */
if (pit_counter >= 0177777) {                           /* Has counter reached limit ? */
    dev_done = dev_done | INT_PIT;                      /* set done */
    dev_busy = dev_busy & ~INT_PIT;                     /* clear busy */
    int_req = (int_req & ~INT_DEV) | (dev_done & ~dev_disable); /* Interrupt */
    pit_counter = pit_initial;
}
return SCPE_OK;
}

/* Reset routine */

t_stat pit_reset (DEVICE *dptr)
{
pit_counter = 0;                                        /* clear counter */
dev_busy = dev_busy & ~INT_PIT;                         /* clear busy */
dev_done = dev_done & ~INT_PIT;                         /* clear done, int */
int_req = int_req & ~INT_PIT;
sim_cancel (&pit_unit);                                 /* deactivate unit */
pit_poll = pit_time;                                    /* poll is default */
return SCPE_OK;
}

/* Bootstrap routine for CPU */

#define BOOT_START 00000
#define BOOT_LEN (sizeof (boot_rom) / sizeof (int))

static const int32 boot_rom[] = {

    062677,                     /*      IORST           ;Reset all I/O  */
    060477,                     /*      READS 0         ;Read SR into AC0 */
    024026,                     /*      LDA 1,C77       ;Get dev mask */
    0107400,                    /*      AND 0,1         ;Isolate dev code */
    0124000,                    /*      COM 1,1         ;- device code - 1 */
    010014,                     /* LOOP: ISZ OP1        ;Device code to all */
    010030,                     /*      ISZ OP2         ;I/O instructions */
    010032,                     /*      ISZ OP3         */
    0125404,                    /*      INC 1,1,SZR     ;done? */
    000005,                     /*      JMP LOOP        ;No, increment again */
    030016,                     /*      LDA 2,C377      ;place JMP 377 into */
    050377,                     /*      STA 2,377       ;location 377 */
    060077,                     /* OP1: 060077          ;start device (NIOS 0) */
    0101102,                    /*      MOVL 0,0,SZC    ;Test switch 0, low speed? */
    000377,                     /* C377: JMP 377        ;no - jmp 377 & wait */
    004030,                     /* LOOP2: JSR GET+1     ;Get a frame */
    0101065,                    /*      MOVC 0,0,SNR    ;is it non-zero? */
    000017,                     /*      JMP LOOP2       ;no, ignore */
    004027,                     /* LOOP4: JSR GET       ;yes, get full word */
    046026,                     /*      STA 1,@C77      ;store starting at 100 */
                                /*                      ;2's complement of word ct */
    010100,                     /*      ISZ 100         ;done? */
    000022,                     /*      JMP LOOP4       ;no, get another */
    000077,                     /* C77: JMP 77          ;yes location ctr and */
                                /*                      ;jmp to last word */
    0126420,                    /* GET: SUBZ 1,1        ; clr AC1, set carry */
                                /* OP2:                 */
    063577,                     /* LOOP3: 063577        ;done? (SKPDN 0) - 1 */
    000030,                     /*      JMP LOOP3       ;no -- wait */
    060477,                     /* OP3: 060477          ;y--read in ac0 (DIAS 0,0) */
    0107363,                    /*      ADDCS 0,1,SNC   ;add 2 frames swapped - got 2nd? */
    000030,                     /*      JMP LOOP3       ;no go back after it */
    0125300,                    /*      MOVS 1,1        ;yes swap them */
    001400,                     /*      JMP 0,3         ;rtn with full word */
    0                           /*      0               ;padding */
};

t_stat cpu_boot (int32 unitno, DEVICE *dptr)
{
size_t i;
extern int32 saved_PC;

for (i = 0; i < BOOT_LEN; i++) M[BOOT_START + i] = boot_rom[i];
saved_PC = BOOT_START;
return SCPE_OK;
}

int32 Debug_Entry(int32 PC, int32 inst, int32 inst2, int32 AC0, int32 AC1, int32 AC2, int32 AC3, int32 flags)
{
     hpc[hnext] = PC & 0xffff;
     hinst[hnext] = inst & 0xffff;
     hinst2[hnext] = inst2 & 0xffff;
     hac0[hnext] = AC0 & 0xffff;
     hac1[hnext] = AC1 & 0xffff;
     hac2[hnext] = AC2 & 0xffff;
     hac3[hnext] = AC3 & 0xffff;
     hflags[hnext] = flags & 0xffff;
     hnext++;
     if (hnext >= hmax) {
         hwrap = 1;
         hnext = 0;
    } 
    return 0;
}

t_stat Debug_Dump(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    return SCPE_OK;
}

t_stat Dump_History (FILE *st, UNIT *uptr, int32 val, CONST void *desc) 
{
    char debmap[4], debion[4];
    t_value simeval[20];
    int debcar;
    int start, end, ctr;
    int count = 0;
    
    if (!Debug_Flags || Debug_Flags & 0100000) {
         printf("History was not logged.  Deposit a non-zero value\n");
         printf("in DEBUG with bit 0 being 1 to build history.\n");
         return SCPE_OK;
    }     
    if (!hwrap) {
        start = 0;
        end = hnext;
    } else {
        start = hnext;
        end = hnext - 1;
        if (end < 0) end = hmax;
    }
    ctr = start;        
    while (1) {
        if (ctr == end) 
            break;
        count++;
        strcpy(debion, " ");
        strcpy(debmap, " ");
        debcar = 0;
        if (hflags[ctr] & 0x80) {
            fprintf(st, "--------- Interrupt %o (%o) to %6o ---------\n",
                 hinst[ctr], hac0[ctr], hac1[ctr]);
       } else {
            if (hflags[ctr] & 0x01) debcar = 1;
            if (hflags[ctr] & 0x02) strcpy(debion, "I");
            if (hflags[ctr] & 0x04) strcpy(debmap, "A");     
            if (hflags[ctr] & 0x08) strcpy(debmap, "B");     
            if (hflags[ctr] & 0x10) strcpy(debmap, "C");     
            if (hflags[ctr] & 0x20) strcpy(debmap, "D");     
            fprintf(st, "%s%s%06o acs: %06o %06o %06o %06o %01o ", 
                debion, debmap, hpc[ctr], hac0[ctr], hac1[ctr], hac2[ctr],
                hac3[ctr], debcar);     
            simeval[0] = hinst[ctr];
            simeval[1] = hinst2[ctr];
            fprint_sym (st, hpc[ctr], simeval, NULL, SWMASK('M'));
            fprintf(st, "\n");
        }    
        ctr++;
        if (ctr > hmax)
            ctr = 0;
    }
    return SCPE_OK;
}

/* Build dispatch table */

t_stat build_devtab (void)
{
DEVICE *dptr;
DIB *dibp;
int32 i, dn;

for (i = 0; i < 64; i++) {                              /* clr dev_table */
    dev_table[i].mask = 0;
    dev_table[i].pi = 0;
    dev_table[i].routine = NULL;
}
for (i = 0; (dptr = sim_devices[i]) != NULL; i++) {     /* loop thru dev */
    if (!(dptr->flags & DEV_DIS) &&                     /* enabled and */
        (dibp = (DIB *) dptr->ctxt)) {                  /* defined DIB? */
        dn = dibp->dnum;                                /* get dev num */
        dev_table[dn].mask = dibp->mask;                /* copy entries */
        dev_table[dn].pi = dibp->pi;
        dev_table[dn].routine = dibp->routine;
    }
}
return SCPE_OK;
}

/* ------------------------------------------------------------------- */
/*                     Floating Point Arithmetic                       */
/* ------------------------------------------------------------------- */


/* Get short float from FPAC */

void get_sf (SHORT_FLOAT *fl, t_int64 *fpr)
{
    fl->sign = (uint8)(*fpr >> 63) & 1;
    fl->expo = (short)(*fpr >> 56) & 0x007F;
    fl->short_fract = (int32)(*fpr >> 32) & 0x00FFFFFF;
} 

/* Store short float to FPAC */

void store_sf (SHORT_FLOAT *fl, t_int64 *fpr)
{
    *fpr = 0;
    *fpr = ((t_int64)fl->sign << 63)
         | ((t_int64)fl->expo << 56)
         | ((t_int64)fl->short_fract <<32);
} 

/* Get long float from FPAC */

void get_lf (LONG_FLOAT *fl, t_int64 *fpr)
{
    fl->sign = (uint8)(*fpr >> 63) & 1;
    fl->expo = (short)(*fpr >> 56) & 0x007F;
    fl->long_fract = (t_int64)*fpr & 0x00FFFFFFFFFFFFFF;

} 

/* Store long float to FPAC */

void store_lf (LONG_FLOAT *fl, t_int64 *fpr)
{
    *fpr = 0;
    *fpr = (t_int64)fl->sign << 63;
    *fpr |= ((t_int64)fl->expo << 56) & 0x7f00000000000000;
    *fpr |= fl->long_fract;
}


/* Check short for Overflow */

int overflow_sf (SHORT_FLOAT *fl)
{
    if (fl->expo > 127) {
        fl->expo &= 0x007F;
        return(1);
    }
    return(0);

}

/* Normalize Short Float */

int normal_sf(SHORT_FLOAT *fl)
{
    if (fl->short_fract) {
        if ((fl->short_fract & 0x00FFFF00) == 0) {
            fl->short_fract <<= 16;
            fl->expo -= 4;
        }
        if ((fl->short_fract & 0x00FF0000) == 0) {
            fl->short_fract <<= 8;
            fl->expo -= 2;
        }
        if ((fl->short_fract & 0x00F00000) == 0) {
            fl->short_fract <<= 4;
            (fl->expo)--;
        }
    } else {
        fl->sign = 0;
        fl->expo = 0;
    }
    if (fl->expo < 0)
        return (2);
    return(0);
}

/* Normalize long float */

int normal_lf (LONG_FLOAT *fl)
{
    if (fl->long_fract) {
        if ((fl->long_fract & 0x00FFFFFFFF000000) == 0) {
            fl->long_fract <<= 32;
            fl->expo -= 8;
        }
        if ((fl->long_fract & 0x00FFFF0000000000) == 0) {
            fl->long_fract <<= 16;
            fl->expo -= 4;
        }
        if ((fl->long_fract & 0x00FF000000000000) == 0) {
            fl->long_fract <<= 8;
            fl->expo -= 2;
        }
        if ((fl->long_fract & 0x00F0000000000000) == 0) {
            fl->long_fract <<= 4;
            (fl->expo)--;
        }
    } else {
        fl->sign = 0;
        fl->expo = 0;
    }
    if (fl->expo < 0)
        return (2);
    return(0);
}

/* Check Long for Overflow */

int overflow_lf(LONG_FLOAT *fl)
{
    if (fl->expo > 127) {
        fl->expo &= 0x007F;
        return(1);
    }
    return(0);

}

int underflow_sf(SHORT_FLOAT *fl)
{
    if (fl->expo < 0) {
        fl->short_fract = 0;
        fl->expo = 0;
        fl->sign = 0;
    }
    return(0);

}


int underflow_lf(LONG_FLOAT *fl)
{
    if (fl->expo < 0) {
        fl->long_fract = 0;
        fl->expo = 0;
        fl->sign = 0;
    }
    return(0);
}

/* Check Short for Over/Under flow */

int over_under_flow_sf(SHORT_FLOAT *fl)
{
    if (fl->expo > 127) {
        fl->expo &= 0x007F;
        return(1);
    } else {
        if (fl->expo < 0) {
            /* set true 0 */
            fl->short_fract = 0;
            fl->expo = 0;
            fl->sign = 0;
        }
    }
    return(0);

}

/* Check Long for Over/Under flow */

int over_under_flow_lf(LONG_FLOAT *fl)
{
    if (fl->expo > 127) {
        fl->expo &= 0x007F;
        return(1);
    } else {
        if (fl->expo < 0) {
            /* set true 0 */
            fl->long_fract = 0;
            fl->expo = 0;
            fl->sign = 0;
        }
    }
    return(0);

}

int significance_sf (SHORT_FLOAT *fl)
{
    fl->sign = 0;
    fl->expo = 0;
    return(0);

}

int significance_lf (LONG_FLOAT *fl)
{
    fl->sign = 0;
    fl->expo = 0;
    return(0);

}


/*-------------------------------------------------------------------*/
/* Add short float                                                   */
/*                                                                   */
/* Input:                                                            */
/*      fl      Float                                                */
/*      add_fl  Float to be added                                    */
/*      normal  Normalize if true                                    */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/
int add_sf (SHORT_FLOAT *fl, SHORT_FLOAT *add_fl, int normal)
{
int     pgm_check;
int    shift;

    pgm_check = 0;
    if (add_fl->short_fract
    || add_fl->expo) {                                  /* add_fl not 0 */
        if (fl->short_fract
        || fl->expo) {                                  /* fl not 0 */
            /* both not 0 */

            if (fl->expo == add_fl->expo) {
                /* expo equal */

                /* both guard digits */
                fl->short_fract <<= 4;
                add_fl->short_fract <<= 4;
            } else {
                /* expo not equal, denormalize */

                if (fl->expo < add_fl->expo) {
                    /* shift minus guard digit */
                    shift = add_fl->expo - fl->expo - 1;
                    fl->expo = add_fl->expo;

                    if (shift) {
                        if (shift >= 6
                        || ((fl->short_fract >>= (shift * 4)) == 0)) {
                            /* 0, copy summand */

                            fl->sign = add_fl->sign;
                            fl->short_fract = add_fl->short_fract;

                            if (fl->short_fract == 0) {
                                pgm_check = significance_sf(fl);
                            } else {
                                if (normal) {
                                    normal_sf(fl);
                                    pgm_check = underflow_sf(fl);
                                }
                            }
                            return(pgm_check);
                        }
                    }
                    /* guard digit */
                    add_fl->short_fract <<= 4;
                } else {
                    /* shift minus guard digit */
                    shift = fl->expo - add_fl->expo - 1;

                    if (shift) {
                        if (shift >= 6
                        || ((add_fl->short_fract >>= (shift * 4)) == 0)) {
                            /* 0, nothing to add */

                            if (fl->short_fract == 0) {
                                pgm_check = significance_sf(fl);
                            } else {
                                if (normal) {
                                    normal_sf(fl);
                                    pgm_check = underflow_sf(fl);
                                }
                            }
                            return(pgm_check);
                        }
                    }
                    /* guard digit */
                    fl->short_fract <<= 4;
                }
            }

            /* compute with guard digit */
            if (fl->sign == add_fl->sign) {
                fl->short_fract += add_fl->short_fract;
            } else {
                if (fl->short_fract == add_fl->short_fract) {
                    /* true 0 */

                    fl->short_fract = 0;
                    return( significance_sf(fl) );

                } else if (fl->short_fract > add_fl->short_fract) {
                    fl->short_fract -= add_fl->short_fract;
                } else {
                    fl->short_fract = add_fl->short_fract - fl->short_fract;
                    fl->sign = add_fl->sign;
                }
            }

            /* handle overflow with guard digit */
            if (fl->short_fract & 0xF0000000) {
                fl->short_fract >>= 8;
                (fl->expo)++;
                pgm_check = overflow_sf(fl);
            } else {

                if (normal) {
                    /* normalize with guard digit */
                    if (fl->short_fract) {
                        /* not 0 */

                        if (fl->short_fract & 0x0F000000) {
                         /* not normalize, just guard digit */
                            fl->short_fract >>= 4;
                        } else {
                            (fl->expo)--;
                            normal_sf(fl);
                            pgm_check = underflow_sf(fl);
                        }
                    } else {
                        /* true 0 */

                        pgm_check = significance_sf(fl);
                    }
                } else {
                    /* not normalize, just guard digit */
                    fl->short_fract >>= 4;
                    if (fl->short_fract == 0) {
                        pgm_check = significance_sf(fl);
                    }
                }
            }
            return(pgm_check);
        } else {                                        /* fl 0, add_fl not 0 */
            /* copy summand */

            fl->expo = add_fl->expo;
            fl->sign = add_fl->sign;
            fl->short_fract = add_fl->short_fract;
            if (fl->short_fract == 0) {
                return( significance_sf(fl) );
            }
        }
    } else {                                            /* add_fl 0 */
        if (fl->short_fract == 0) {                     /* fl 0 */
            /* both 0 */

            return( significance_sf(fl) );
        }
    }
    if (normal) {
        normal_sf(fl);
        pgm_check = underflow_sf(fl);
    }
    return(pgm_check);

}


/*-------------------------------------------------------------------*/
/* Add long float                                                    */
/*                                                                   */
/* Input:                                                            */
/*      fl      Float                                                */
/*      add_fl  Float to be added                                    */
/*      normal  Normalize if true                                    */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/
int add_lf (LONG_FLOAT *fl, LONG_FLOAT *add_fl, int normal)
{
int     pgm_check;
int    shift;

    pgm_check = 0;
    if (add_fl->long_fract
    || add_fl->expo) {                                  /* add_fl not 0 */
        if (fl->long_fract
        || fl->expo) {                                  /* fl not 0 */
            /* both not 0 */

            if (fl->expo == add_fl->expo) {
                /* expo equal */

                /* both guard digits */
                fl->long_fract <<= 4;
                add_fl->long_fract <<= 4;
            } else {
                /* expo not equal, denormalize */

                if (fl->expo < add_fl->expo) {
                    /* shift minus guard digit */
                    shift = add_fl->expo - fl->expo - 1;
                    fl->expo = add_fl->expo;

                    if (shift) {
                        if (shift >= 14
                        || ((fl->long_fract >>= (shift * 4)) == 0)) {
                            /* 0, copy summand */

                            fl->sign = add_fl->sign;
                            fl->long_fract = add_fl->long_fract;

                            if (fl->long_fract == 0) {
                                pgm_check = significance_lf(fl);
                            } else {
                                if (normal) {
                                    normal_lf(fl);
                                    pgm_check = underflow_lf(fl);
                                }
                            }
                            return(pgm_check);
                        }
                    }
                    /* guard digit */
                    add_fl->long_fract <<= 4;
                } else {
                    /* shift minus guard digit */
                    shift = fl->expo - add_fl->expo - 1;

                    if (shift) {
                        if (shift >= 14
                        || ((add_fl->long_fract >>= (shift * 4)) == 0)) {
                            /* 0, nothing to add */

                            if (fl->long_fract == 0) {
                                pgm_check = significance_lf(fl);
                            } else {
                                if (normal) {
                                    normal_lf(fl);
                                    pgm_check = underflow_lf(fl);
                                }
                            }
                            return(pgm_check);
                        }
                    }
                    /* guard digit */
                    fl->long_fract <<= 4;
                }
            }

            /* compute with guard digit */
            if (fl->sign == add_fl->sign) {
                fl->long_fract += add_fl->long_fract;
            } else {
                if (fl->long_fract == add_fl->long_fract) {
                    /* true 0 */

                    fl->long_fract = 0;
                    return( significance_lf(fl) );

                } else if (fl->long_fract > add_fl->long_fract) {
                    fl->long_fract -= add_fl->long_fract;
                } else {
                    fl->long_fract = add_fl->long_fract - fl->long_fract;
                    fl->sign = add_fl->sign;
                }
            }

            /* handle overflow with guard digit */
            if (fl->long_fract & 0xF000000000000000) {
                fl->long_fract >>= 8;
                (fl->expo)++;
                pgm_check = overflow_lf(fl);
            } else {

                if (normal) {
                    /* normalize with guard digit */
                    if (fl->long_fract) {
                        /* not 0 */

                        if (fl->long_fract & 0x0F00000000000000) {
                            /* not normalize, just guard digit */
                            fl->long_fract >>= 4;
                        } else {
                            (fl->expo)--;
                            normal_lf(fl);
                            pgm_check = underflow_lf(fl);
                        }
                    } else {
                        /* true 0 */

                        pgm_check = significance_lf(fl);
                    }
                } else {
                    /* not normalize, just guard digit */
                    fl->long_fract >>= 4;
                    if (fl->long_fract == 0) {
                        pgm_check = significance_lf(fl);
                    }
                }
            }
            return(pgm_check);
        } else {                                        /* fl 0, add_fl not 0 */
            /* copy summand */

            fl->expo = add_fl->expo;
            fl->sign = add_fl->sign;
            fl->long_fract = add_fl->long_fract;
            if (fl->long_fract == 0) {
                return( significance_lf(fl) );
            }
        }
    } else {                                            /* add_fl 0 */
        if (fl->long_fract == 0) {                      /* fl 0 */
            /* both 0 */

            return( significance_lf(fl) );
        }
    }
    if (normal) {
        normal_lf(fl);
        pgm_check = underflow_lf(fl);
    }
    return(pgm_check);

}

/*-------------------------------------------------------------------*/
/* Multiply short float                                              */
/*                                                                   */
/* Input:                                                            */
/*      fl      Multiplicand short float                             */
/*      mul_fl  Multiplicator short float                            */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/

int mul_sf(SHORT_FLOAT *fl, SHORT_FLOAT *mul_fl)
{
t_int64     wk;

    if (fl->short_fract
    && mul_fl->short_fract) {
        /* normalize operands */
        normal_sf( fl );
        normal_sf( mul_fl );

        /* multiply fracts */
        wk = (t_int64) fl->short_fract * mul_fl->short_fract;

        /* normalize result and compute expo */
        if (wk & 0x0000F00000000000) {
            fl->short_fract = (int32)wk >> 24;
            fl->expo = (short)fl->expo + mul_fl->expo - 64;
        } else {
            fl->short_fract = (int32)wk >> 20;
            fl->expo = (short)fl->expo + mul_fl->expo - 65;
        }

        /* determine sign */
        fl->sign = (fl->sign == mul_fl->sign) ? 0 : 1;

        /* handle overflow and underflow */
        return( over_under_flow_sf(fl) );
    } else {
        /* set true 0 */

        fl->short_fract = 0;
        fl->expo = 0;
        fl->sign = 0;
        return(0);
    }

}


/*-------------------------------------------------------------------*/
/* Multiply long float                                               */
/*                                                                   */
/* Input:                                                            */
/*      fl      Multiplicand long float                              */
/*      mul_fl  Multiplicator long float                             */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/
int mul_lf(LONG_FLOAT *fl, LONG_FLOAT *mul_fl)
{
t_int64   wk;
int32     v;

    if (fl->long_fract
    && mul_fl->long_fract) {
        /* normalize operands */
        normal_lf( fl );
        normal_lf( mul_fl );

        /* multiply fracts by sum of partial multiplications */
        wk = ((fl->long_fract & 0x00000000FFFFFFFF) * (mul_fl->long_fract & 0x00000000FFFFFFFF)) >> 32;

        wk += ((fl->long_fract & 0x00000000FFFFFFFF) * (mul_fl->long_fract >> 32));
        wk += ((fl->long_fract >> 32) * (mul_fl->long_fract & 0x00000000FFFFFFFF));
        v = (int32)wk;

        fl->long_fract = (wk >> 32) + ((fl->long_fract >> 32) * (mul_fl->long_fract >> 32));

        /* normalize result and compute expo */
        if (fl->long_fract & 0x0000F00000000000) {
            fl->long_fract = (fl->long_fract << 8)
                           | (v >> 24);
            fl->expo = fl->expo + mul_fl->expo - 64;
        } else {
            fl->long_fract = (fl->long_fract << 12)
                           | (v >> 20);
            fl->expo = fl->expo + mul_fl->expo - 65;
        }

        /* determine sign */
        fl->sign = (fl->sign == mul_fl->sign) ? 0 : 1;

        /* handle overflow and underflow */
        return( over_under_flow_lf(fl) );
    } else {
        /* set true 0 */

        fl->long_fract = 0;
        fl->expo = 0;
        fl->sign = 0;
        return(0);
    }

} 


/*-------------------------------------------------------------------*/
/* Divide short float                                                */
/*                                                                   */
/* Input:                                                            */
/*      fl      Dividend short float                                 */
/*      div_fl  Divisor short float                                  */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/
int div_sf(SHORT_FLOAT *fl, SHORT_FLOAT *div_fl)
{
t_int64     wk;

    if (div_fl->short_fract) {
        if (fl->short_fract) {
            /* normalize operands */
            normal_sf( fl );
            normal_sf( div_fl );

            /* position fracts and compute expo */
            if (fl->short_fract < div_fl->short_fract) {
                wk = (t_int64) fl->short_fract << 24;
                fl->expo = fl->expo - div_fl->expo + 64;
            } else {
                wk = (t_int64) fl->short_fract << 20;
                fl->expo = fl->expo - div_fl->expo + 65;
            }
            /* divide fractions */
            fl->short_fract = (int32)wk / div_fl->short_fract;

            /* determine sign */
            fl->sign = (fl->sign == div_fl->sign) ? 0 : 1;

            /* handle overflow and underflow */
            return( over_under_flow_sf(fl) );
        } else {
            /* fraction of dividend 0, set true 0 */

            fl->short_fract = 0;
            fl->expo = 0;
            fl->sign = 0;
        }
    } else {
                                                        /* divisor 0 */

        return(3);
    }
    return(0);

}


/*-------------------------------------------------------------------*/
/* Divide long float                                                 */
/*                                                                   */
/* Input:                                                            */
/*      fl      Dividend long float                                  */
/*      div_fl  Divisor long float                                   */
/* Value:                                                            */
/*              exeption                                             */
/*-------------------------------------------------------------------*/
int div_lf(LONG_FLOAT *fl, LONG_FLOAT *div_fl)
{
t_int64 wk;
t_int64 wk2;
int     i;

    if (div_fl->long_fract) {
        if (fl->long_fract) {
            /* normalize operands */
            normal_lf( fl );
            normal_lf( div_fl );

            /* position fracts and compute expo */
            if (fl->long_fract < div_fl->long_fract) {
                fl->expo = fl->expo - div_fl->expo + 64;
            } else {
                fl->expo = fl->expo - div_fl->expo + 65;
                div_fl->long_fract <<= 4;
            }

            /* partial divide first hex digit */
            wk2 = fl->long_fract / div_fl->long_fract;
            wk = (fl->long_fract % div_fl->long_fract) << 4;

            /* partial divide middle hex digits */
            i = 13;
            while (i--) {
                wk2 = (wk2 << 4)
                    | (wk / div_fl->long_fract);
                wk = (wk % div_fl->long_fract) << 4;
            }

            /* partial divide last hex digit */
            fl->long_fract = (wk2 << 4)
                           | (wk / div_fl->long_fract);

            /* determine sign */
            fl->sign = (fl->sign == div_fl->sign) ? 0 : 1;

            /* handle overflow and underflow */
            return( over_under_flow_lf(fl) );
        } else {
            /* fraction of dividend 0, set true 0 */

            fl->long_fract = 0;
            fl->expo = 0;
            fl->sign = 0;
        }
    } else {
                                                        /* divisor 0 */

        return(3);
    }
    return(0);

}

