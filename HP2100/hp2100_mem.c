/* hp2100_mem.c: HP 21xx/1000 Main Memory/Memory Expansion Module/Memory Protect simulator

   Copyright (c) 1993-2016, Robert M. Supnik
   Copyright (c) 2017-2018, J. David Bryan

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.

   Main memory  12615A/12839A/12885A Core Memory Subsystems
                2102B MOS Memory Subsystem
   MEM          12731A Memory Expansion Module
   MP           12581A/12892B Memory Protect

   02-Aug-18    JDB     Added MEM device
   30-Jul-18    JDB     Renamed "iop_sp" to "SPR" (stack pointer register)
   20-Jul-18    JDB     Split out from hp2100_cpu.c

   References:
     - HP 1000 M/E/F-Series Computers Technical Reference Handbook
         (5955-0282, March 1980)
     - HP 1000 M/E/F-Series Computers Engineering and Reference Documentation
         (92851-90001, March 1981)
     - HP 1000 M/E/F-Series Computers I/O Interfacing Guide
         (02109-90006, September 1980)
     - 12892B Memory Protect Installation Manual
         (12892-90007, June 1978)
     - 2100A Computer Reference Manual
         (02100-90001, December 1971)
     - Central Processor Options Computer Maintenance Course Student's Manual
       Volumes V, VI, VII, VIII, & IX
         (5950-8707, April 1969)

   This module simulates the HP 12615A and 12839A core memory subsystems for the
   2116 and 2115/2114, respectively, the 12885A core memory subsystem for the
   2100, and the 2102B MOS memory subsystem for the 1000 M/E/F-Series CPUs.
   Main memory is implemented as a dynamically allocated array, "M", of
   MEMORY_WORD words.  The MEMORY_WORD type is a 16-bit unsigned type,
   corresponding with the 16-bit main memory word of the HP 21xx/1000.  The
   largest supported memory size (one megaword for the HP 1000) is allocated
   when the simulator is started, while the configured memory size for the
   current CPU is kept in the "mem_size" variable.  Installed memory sizes may
   range from 4K words to 1M words.

   HP 21xx and 1000 CPUs address a maximum of 32K words with 15-bit addresses.
   This is the logical address space.  1000-series machines may employ an
   optional Memory Expansion Module to map the logical address space anywhere
   with a 1M-word physical memory on a 1K-per-page basis.  For all machines,
   reads to addresses outside of installed memory return all-zeros worda, and
   writes outside of memory are ignored.  Neither operation causes an error.

   The core memory machines (2114, 2115, 2116, and 2100) have a protected area
   of memory where a binary loader program may be stored.  The protected loader
   area resides in the last 64 words of installed memory and is normally
   protected against reading and writing; as with non-existent memory, reads
   returns all zeros words, and writes are ignored.  The loader is unprotected
   by a switch on the CPU front panel so that it may be executed, typically to
   bootstrap a system from paper tape, magnetic tape, or disc.  The loader is
   automatically protected when the machine executes a HLT instruction.  It may
   also be protected manually through the front panel.  In simulation, loader
   protection is controlled by the "mem_end" variable.  When it is equal to
   "mem_size", the loader is unprotected and available for execution.  When it
   is less than "mem_size", the loader is protected, and memory logically ends
   at the "mem_end" address.

   This module provides routines to read and write memory words and bytes.  All
   memory accesses are classified as to the type of the access, which determines
   the mapping mode and protection applied.  Utility routines to initialize,
   zero, and copy loaders to and from memory are also supplied.



   This module also simulates the 12731A Memory Expansion Module for the 1000
   M/E/F-Series machines.  The MEM provides mapping of the 32 1K-word logical
   memory pages into a one-megaword physical memory.  Four separate 32-page maps
   are provided: system, user, DCPC port A (used by channel 1), and DCPC port B
   (used by channel 2).

   The MEM is controlled by the associated Dynamic Mapping System instructions.
   While enabled, all programmed memory accesses are translated via the system
   or user map, depending on which is currently enabled, and all DCPC accesses
   are translated through one of the two port maps, depending on which channel
   is making the access.

   In addition, page 0 (the base page) accesses have an additional translation
   step.  A base page fence separates a mapped portion from an unmapped potion
   in the system and user maps.  The mapped portion is mapped to the physical
   page that resides in the first map register.  The unmapped portion is not
   mapped and accesses physical page 0.  A MEM setting controls whether the
   mapped portion is above or below the fence.

   Each map page may be protected against reading or writing.  Write protection
   also extends to executing jump instructions that target the page.  Attempting
   a protected access results in a MEM violation, which is handled by the Memory
   Protect card.  If MP is enabled, a MEM violation causes an interrupt on
   select code 05; if MP is disabled, no violation occurs, and the read or write
   proceeds normally.  MP and MEM violations are distinguished by executing an
   SFS 05 instruction, which skips for MEM violations but not for MP violations.
   Read and write protections are ignored for DCPC accesses.

   In addition, MEM violations also occur for attempts to write into the
   unmapped portion of the base page (i.e., to physical page 0), as well as
   attempts to execute privileged DMS instructions (i.e., those that load any of
   the map registers).  The MEM status and violation registers reflect the
   current status of the MEM and the last violation, if any.  They are formatted
   as follows.

   MEM Status Register:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | I | M | E | U | P | B |        base page fence address        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     I = MEM was disabled/enabled (0/1) at last interrupt
     M = System/user map (0/1) was selected at last interrupt
     E = MEM is disabled/enabled (0/1) currently
     U = System/user map (0/1) is selected currently
     P = Protected mode is disabled/enabled (0/1) currently
     B = Base-page portion mapped is above/below (0/1) the fence


   MEM Violation Register:

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | R | W | B | P | -   -   -   - | S | E | M |   page address    |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     R = Read violation
     W = Write violation
     B = Base-page violation
     P = Privileged instruction violation
     S = ME bus disabled/enabled (0/1) at violation
     E = MEM disabled/enabled (0/1) at violation
     M = System/user map (0/1) selected at violation


   The MEM card has four hardware configuration jumpers:

     W1 - configure for 1000 M-Series (A)
          or 1000 E/F-Series (B)

     W2 - normal operation (IN)
          or factory test (OUT)

     W3 - factory test (IN)
          or normal operation (OUT)

     W4 (RME) - MEM remains in the system map after IAK for IOG trap instruction (A)
                or returns to the prior map (B)

   These jumpers are not simulated.  Instead, the simulation behaves as though
   W1 is set correctly for the current CPU type, W2 is IN, W3 is OUT, and W4 is
   set to the A position.



   This module also simulates the 12581A/12892B Memory Protect accessories for
   the 2116 and 1000 M/E/F-Series, respectively, and the memory protect feature
   that is standard equipment for the 2100.  MP is addressed via select code 05
   and provides a fence register that holds the address of the start of
   unprotected memory and a violation register that holds the address of the
   instruction that has caused a memory protect violation.

   In hardware, if the Memory Protect accessory is installed and enabled, I/O
   operations to select codes other than 01 are prohibited.  Also, in
   combination with the MPCK micro-order, MP validates the M-register contents
   (memory address) against the memory protect fence.  If a violation occurs, an
   I/O instruction or memory write is inhibited, and a memory read returns
   invalid data.

   In simulation, MP violations are usually detected automatically when the
   "mem_write" routine is called to write to memory or the "cpu_iog" routine is
   called to execute an I/O instruction.  A few instruction executors detect MP
   violations explicitly and call the "mp_violation" routine.  If MP is enabled,
   the routine sets the MP flag and then calls "cpu_microcode_abort" to abort
   the instruction.  That routine executes a "longjmp" to the abort handler,
   which is outside of and precedes the instruction execution loop.

   An MP interrupt (SC 05) is qualified by "interrupt_system" but not by
   "cpu_interrupt_enable".  If the interrupt system is off when an MP violation
   is detected, the violating instruction will be aborted, even though no
   interrupt occurs.  In this case, neither the flag nor the flag buffer are
   set.

   MP is controlled by I/O instructions directed to select code 05, as follows.

   Output Data Word format (OTA and OTB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 |          starting address of unprotected memory           | fence
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Input Data Word formats (LIA, LIB, MIA, and MIB):

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 |               violating instruction address               | MP violation
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 |               violating instruction address               | PE violation
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   After setting the fence regiater with an OTA 05 or OTB 05 instruction, MP is
   enabled by an STC 05.  MP cannot be disabled programmatically; it is disabled
   only by a violation.  The SFS 05 and SFC 05 instructions test the Memory
   Expansion Violation flip-flop, not the MP flag flip-flop.  The MEV flip-flop
   is set for a MEM violation and is clear for an MP violation.


   The 12892B card has six hardware configuration jumpers:

     W3 (HLTPE) - parity violation register clocked when an error occurs (OUT)
                  or not clocked when error an occurs and the CPU switch is in
                  the HLT PE position (IN)

     W4 (MX)    - timing is for an E/F-Series (OUT)
                  or for an M-Series (IN)

     W5 (JSB)   - JSB to locations 0 and 1 are prohibited (OUT)
                  or permitted (IN)

     W6 (INT)   - interrupts are enabled immediately if MP is enabled (OUT)
                  or only after three levels of indirection (IN)

     W7 (SEL1)  - permit I/O only to select code 01 (OUT)
                  or to all select codes (IN)

     W8 (RME)   - MEM remains in the system map after IAK for IOG trap instruction (OUT)
                  or returns to the prior map (IN)

   In simulation, jumpers W5, W6, and W7 may be set via the SCP command line;
   the default (normal) positions are W5 IN, W6 IN, and W7 OUT.  Jumpers W3, W4,
   and W8 are not simulated.  Instead, the simulation behaves as though W3 is
   OUT, W4 is set correctly for the current CPU type, and W8 is OUT.  The
   jumpers designated as W1 and W2 do not exist.


   Implementation notes:

    1. The terms MEM (Memory Expansion Module), MEU (Memory Expansion Unit), DMI
       (Dynamic Mapping Instructions), and DMS (Dynamic Mapping System) are used
       somewhat interchangeably to refer to the logical-to-physical memory
       address translation option provided on the 1000-Series.  DMS consists of
       the MEM card (12731A) and the DMI firmware (13307A).  However, MEM and
       MEU have been used interchangeably to refer to the mapping card, as have
       DMI and DMS to refer to the firmware instructions.

       In this module, MEM routines and state variables are prefixed "meu_"
       rather than "mem_" to avoid confusion with the main memory symbols.
*/



#include "hp2100_defs.h"
#include "hp2100_cpu.h"
#include "hp2100_cpu_dmm.h"



/* Main memory instruction masks */

#define IR_MRG              (MRG | AB_MASK)     /* MRG instructions mask */

#define IR_ISZ              0034000u            /* ISZ instruction */
#define IR_STF              0102100u            /* STF instruction */


/* Main memory access classification table */

typedef struct {
    uint32      debug_flag;                     /* the debug flag for tracing */
    const char  *name;                          /* the classification name */
    } ACCESS_PROPERTIES;

static const ACCESS_PROPERTIES mem_access [] = {    /* indexed by ACCESS_CLASS */
/*    debug_flag    name                */
/*    ------------  ------------------- */
    { TRACE_FETCH,  "instruction fetch" },          /*   instruction fetch */
    { TRACE_DATA,   "data"              },          /*   data access */
    { TRACE_DATA,   "data"              },          /*   data access, alternate map */
    { TRACE_DATA,   "unprotected"       },          /*   data access, system map */
    { TRACE_DATA,   "unprotected"       },          /*   data access, user map */
    { TRACE_DATA,   "dma"               },          /*   DMA channel 1, port A map */
    { TRACE_DATA,   "dma"               }           /*   DMA channel 2, port B map */
    };


/* Main memory OS base page addresses */

static const uint32 m64  = 0000040u;            /* (DOS) constant -64 address */
static const uint32 p64  = 0000067u;            /* (DOS) constant +64 address */

static const uint32 xeqt = 0001717u;            /* (RTE) XEQT address */
static const uint32 tbg  = 0001674u;            /* (RTE) TBG address */


/* Main memory tracing constants */

static const char * const register_values [] = {    /* register values, indexed by EOI concatenation */
    "e o i",                                        /*   E = 0, O = 0, interrupt_system = off */
    "e o I",                                        /*   E = 0, O = 0, interrupt_system = on */
    "e O i",                                        /*   E = 0, O = 1, interrupt_system = off */
    "e O I",                                        /*   E = 0, O = 1, interrupt_system = on */
    "E o i",                                        /*   E = 1, O = 0, interrupt_system = off */
    "E o I",                                        /*   E = 1, O = 0, interrupt_system = on */
    "E O i",                                        /*   E = 1, O = 1, interrupt_system = off */
    "E O I"                                         /*   E = 1, O = 1, interrupt_system = on */
    };

static const char mp_value [] = {               /* memory protection value, indexed by mp_control */
    '-',                                        /*   MP is off */
    'P'                                         /*   MP is on */
    };

static const char * const register_formats [] = {       /* CPU register formats, indexed by is_1000 */
    REGA_FORMAT "  A %06o, B %06o, ",                   /*   is_1000 = FALSE format */
    REGA_FORMAT "  A %06o, B %06o, X %06o, Y %06o, "    /*   is_1000 = TRUE  format */
    };

static const char * const mp_mem_formats [] = {                 /* MP/MEM register formats, indexed by is_1000 */
    REGB_FORMAT "  MPF %06o, MPV %06o\n",                       /*   is_1000 = FALSE format */
    REGB_FORMAT "  MPF %06o, MPV %06o, MES %06o, MEV %06o\n"    /*   is_1000 = TRUE  format */
    };


/* Main memory global state declarations */

uint32 mem_size = 0;                            /* size of main memory in words */
uint32 mem_end  = 0;                            /* address of the first word beyond installed memory */


/* Main memory local state declarations */

static MEMORY_WORD *M          = NULL;          /* the pointer to allocated memory */
static DIB         *tbg_dibptr = NULL;          /* a pointer to the time-base generator DIB (for RTE idle check) */
static t_bool      is_1000     = FALSE;         /* TRUE if the CPU is a 1000 M/E/F-Series */


/* Memory Expansion Unit command line switches */

#define ALL_MAPMODES        (SWMASK ('S') | SWMASK ('U') |  \
                             SWMASK ('P') | SWMASK ('Q'))


/* Memory Expansion Unit program limits */

#define MAP_COUNT           4                   /* number of maps */
#define REG_COUNT           32                  /* number of map registers per map */


/* Memory Expansion Unit program constants */

#define LWA_BASE_PAGE       0001777u            /* address of the last word on the base page */

#define MAP_MASK            (MAP_COUNT - 1)     /* mask to the map selection bits */

#define ALTERNATE_MAP(m)    ((MEU_MAP_SELECTOR) ((m) ^ 1))  /* switch to alternate map (user or system) */


/* Memory Expansion Unit state constant declarations */

static const char map_indicator [] = {          /* MEU map indicator, indexed by MEU_MAP_SELECTOR */
    'S',                                        /*   System_Map */
    'U',                                        /*   User_Map   */
    'A',                                        /*   Port_A_Map */
    'B'                                         /*   Port_B_Map */
    };


/* MEU Page Map Registers.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | R | W | -   -   -   - |         physical page address         |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define READ_PROTECTED      0100000u            /* (R) read protection bit */
#define WRITE_PROTECTED     0040000u            /* (W) write protection bit */
#define MAP_RESERVED        0036000u            /* reserved bits */
#define PAGE_MASK           PP_MASK             /* physical page address mask */

#define NO_PROTECTION       0000000u            /* no read/write protection */

#define MAP_PAGE(r)         ((r) & PAGE_MASK)   /* extract the page number from a map register */



/* MEU status register.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | I | M | E | U | P | B |        base page fence address        |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define MEST_ENBL_INT       0100000u            /* (I) MEM was enabled at last interrupt */
#define MEST_UMAP_INT       0040000u            /* (M) User map was selected at last interrupt */
#define MEST_ENABLED        0020000u            /* (E) MEM is enabled currently */
#define MEST_USER_MAP       0010000u            /* (U) User map is selected currently (set dynamically) */
#define MEST_PROTECTED      0004000u            /* (P) Protected mode is enabled currently (set dynamically) */
#define MEST_BELOW          0002000u            /* (B) Base page below fence is mapped */
#define MEST_FENCE_MASK     0001777u            /* Base page fence mask */

#define MEST_DYNAMIC        (MEST_USER_MAP | MEST_PROTECTED)
#define MEST_DYNAMIC_IAK    (MEST_ENBL_INT | MEST_UMAP_INT)


/* MEU Violation Register.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | R | W | B | P | -   -   -   - | S | E | M |    page index     |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define MEVI_READ           0100000u            /* (R) = Read violation */
#define MEVI_WRITE          0040000u            /* (W) = Write violation */
#define MEVI_BASE_PAGE      0020000u            /* (B) = Base page violation */
#define MEVI_PRIVILEGE      0010000u            /* (P) = Privileged instruction violation */
#define MEVI_BUS_ENABLED    0000200u            /* (S) = ME bus was enabled at violation */
#define MEVI_MEM_ENABLED    0000100u            /* (E) = MEM was enabled at violation */
#define MEVI_USER_MAP       0000040u            /* (M) = User map was selected at violation */
#define MEVI_INDEX_MASK     0000037u            /* Page register index for which the violation occurred */


/* Memory Expansion Unit global state declarations */

char   meu_indicator;                           /* last map access indicator (S | U | A | B | -) */
uint32 meu_page;                                /* last physical page number accessed */


/* Memory Expansion Unit local state declarations */

static MEU_MAP_SELECTOR meu_current_map = System_Map;       /* the current map */
static t_bool           meu_bus_enabled = FALSE;            /* TRUE if the memory expansion bus is enabled */
static HP_WORD          meu_status      = 0;                /* the MEM status register */
static HP_WORD          meu_violation   = 0;                /* the MEM violation register */
static HP_WORD          meu_maps [MAP_COUNT] [REG_COUNT];   /* the MEM map registers */


/* Memory Expansion Unit local SCP support routine declarations */

static t_stat meu_reset (DEVICE *dptr);


/* Memory Expansion Unit local utility routine declarations */

static void   dm_violation (HP_WORD violation);
static t_bool is_mapped    (HP_WORD address);
static uint32 map_address  (HP_WORD address, MEU_MAP_SELECTOR map, HP_WORD protection);


/* Memory Expansion Unit SCP data declarations */


/* Unit list */

static UNIT meu_unit [] = {
/*           Event Routine  Unit Flags  Capacity  Delay */
/*           -------------  ----------  --------  ----- */
    { UDATA (NULL,              0,         0)           }   /* dummy unit */
    };


/* Register list.


   Implementation notes:

    1. The REG definitions for the maps must be 17 bits (not 16) to ensure that
       the map entries are accessed as 32-bit HP_WORDs and not uint16s.
*/

static REG meu_reg [] = {
/*    Macro   Name       Location                Radix  Width   Offset     Depth           Flags       */
/*    ------  ---------  ----------------------  -----  -----  --------  ----------  ----------------- */
    { FLDATA (ENABLED,   meu_status,                             13)                                   },
    { FLDATA (CURMAP,    meu_current_map,                         0)                                   },
    { ORDATA (STATUS,    meu_status,                     16)                                           },
    { ORDATA (VIOL,      meu_violation,                  16)                                           },
    { BRDATA (SMAP,      meu_maps [System_Map],     8,   17,             REG_COUNT)                    },
    { BRDATA (UMAP,      meu_maps [User_Map],       8,   17,             REG_COUNT)                    },
    { BRDATA (PAMAP,     meu_maps [Port_A_Map],     8,   17,             REG_COUNT)                    },
    { BRDATA (PBMAP,     meu_maps [Port_B_Map],     8,   17,             REG_COUNT)                    },
    { FLDATA (MEBEN,     meu_bus_enabled,                         0),                REG_HRO           },

    { NULL }
    };


/* Device descriptor */

DEVICE meu_dev = {
    "MEM",                                      /* device name */
    meu_unit,                                   /* unit array */
    meu_reg,                                    /* register array */
    NULL,                                       /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    1,                                          /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &meu_reset,                                 /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    NULL,                                       /* device information block pointer */
    DEV_DIS,                                    /* device flags */
    0,                                          /* debug control flags */
    NULL,                                       /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Memory Protect unit flags */

#define UNIT_V_MP_JSB       (UNIT_V_UF + 0)             /* MP jumper W5 */
#define UNIT_V_MP_INT       (UNIT_V_UF + 1)             /* MP jumper W6 */
#define UNIT_V_MP_SEL1      (UNIT_V_UF + 2)             /* MP jumper W7 */

#define UNIT_MP_JSB         (1 << UNIT_V_MP_JSB)        /* 1 = W5 is out */
#define UNIT_MP_INT         (1 << UNIT_V_MP_INT)        /* 1 = W6 is out */
#define UNIT_MP_SEL1        (1 << UNIT_V_MP_SEL1)       /* 1 = W7 is out */


/* Memory Protect violation register.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | P |               violating instruction address               |
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

#define MPVR_PARITY_ERROR   0100000u            /* (P) parity error violation */


/* Memory Protect global state declarations */

HP_WORD mp_fence = 0;                           /* MP fence register */


/* Memory Protect local state declarations */

static HP_WORD   mp_VR          = 0;            /* MP violation register */
static FLIP_FLOP mp_control     = CLEAR;        /* MP control flip-flop */
static FLIP_FLOP mp_flag_buffer = CLEAR;        /* MP flag buffer flip-flop */
static FLIP_FLOP mp_flag        = CLEAR;        /* MP flag flip-flop */
static FLIP_FLOP mp_mevff       = CLEAR;        /* memory expansion violation flip-flop */
static FLIP_FLOP mp_evrff       = SET;          /* enable violation register flip-flop */
static FLIP_FLOP mp_enabled     = CLEAR;        /* MP was enabled at interrupt */
static FLIP_FLOP mp_reenable    = CLEAR;        /* MP will be reenabled after IAK */
static t_bool    mp_mem_changed = TRUE;         /* TRUE if the MP or MEM registers have been altered */
static uint32    jsb_bound      = 2;            /* protected lower bound for JSB */


/* Memory Protect I/O interface routine declarations */

static INTERFACE mp_interface;


/* Memory Protect local SCP support routine declarations */

static t_stat mp_set_jsb (UNIT *uptr, int32 value, CONST char *cptr, void *desc);
static t_stat mp_reset   (DEVICE *dptr);


/* Memory Protect local utility routine declarations */

static t_stat mp_service (UNIT *uptr);


/* Memory Protect SCP data declarations */

/* Unit list.


   Implementation notes:

    1. The default flags correspond to the following jumper settings: JSB in,
       INT in, SEL1 out.
*/

static UNIT mp_unit [] = {
/*           Event Routine  Unit Flags    Capacity  Delay */
/*           -------------  ------------  --------  ----- */
    { UDATA (&mp_service,   UNIT_MP_SEL1,    0)           }
    };


/* Device information block */

static DIB mp_dib = {
    &mp_interface,                              /* the device's I/O interface function pointer */
    MPPE,                                       /* the device's select code (02-77) */
    0,                                          /* the card index */
    NULL,                                       /* the card description */
    NULL                                        /* the ROM description */
    };


/* Register list */

static REG mp_reg [] = {
/*    Macro   Name      Location        Width  Flags   */
/*    ------  --------  --------------  -----  ------- */
    { FLDATA (CTL,      mp_control,       0)           },
    { FLDATA (FLG,      mp_flag,          0)           },
    { FLDATA (FBF,      mp_flag_buffer,   0)           },
    { ORDATA (FR,       mp_fence,        15)           },
    { ORDATA (VR,       mp_VR,           16)           },
    { FLDATA (EVR,      mp_evrff,         0)           },
    { FLDATA (MEV,      mp_mevff,         0)           },

    { FLDATA (ENABLED,  mp_enabled,       0),  REG_HRO },
    { FLDATA (REENABLE, mp_reenable,      0),  REG_HRO },
    { ORDATA (PLBOUND,  jsb_bound,       16),  REG_HRO },
    { NULL }
    };


/* Modifier list */

static MTAB mp_mod [] = {
/*    Mask Value     Match Value   Print String     Match String  Validation   Display  Descriptor */
/*    -------------  ------------  ---------------  ------------  -----------  -------  ---------- */
    { UNIT_MP_JSB,   UNIT_MP_JSB,  "JSB (W5) out",  "JSBOUT",     &mp_set_jsb, NULL,    NULL       },
    { UNIT_MP_JSB,   0,            "JSB (W5) in",   "JSBIN",      &mp_set_jsb, NULL,    NULL       },

    { UNIT_MP_INT,   UNIT_MP_INT,  "INT (W6) out",  "INTOUT",     NULL,        NULL,    NULL       },
    { UNIT_MP_INT,   0,            "INT (W6) in",   "INTIN",      NULL,        NULL,    NULL       },

    { UNIT_MP_SEL1,  UNIT_MP_SEL1, "SEL1 (W7) out", "SEL1OUT",    NULL,        NULL,    NULL       },
    { UNIT_MP_SEL1,  0,            "SEL1 (W7) in",  "SEL1IN",     NULL,        NULL,    NULL       },

    { 0 }
    };


/* Trace list */

static DEBTAB mp_deb [] = {
    { "IOBUS", TRACE_IOBUS },                   /* trace I/O bus signals and data words received and returned */
    { NULL,    0           }
    };


/* Device descriptor */

DEVICE mp_dev = {
    "MP",                                       /* device name */
    mp_unit,                                    /* unit array */
    mp_reg,                                     /* register array */
    mp_mod,                                     /* modifier array */
    1,                                          /* number of units */
    8,                                          /* address radix */
    1,                                          /* address width */
    1,                                          /* address increment */
    8,                                          /* data radix */
    16,                                         /* data width */
    NULL,                                       /* examine routine */
    NULL,                                       /* deposit routine */
    &mp_reset,                                  /* reset routine */
    NULL,                                       /* boot routine */
    NULL,                                       /* attach routine */
    NULL,                                       /* detach routine */
    &mp_dib,                                    /* device information block pointer */
    DEV_DISABLE | DEV_DIS | DEV_DEBUG,          /* device flags */
    0,                                          /* debug control flags */
    mp_deb,                                     /* debug flag name table */
    NULL,                                       /* memory size change routine */
    NULL                                        /* logical device name */
    };



/* Main memory global utility routines */


/* Initialize main memory.

   This routine allocates and zeros the array of MEMORY_WORDs that represent the
   main memory of the CPU.  It also obtains and saves a pointer to the DIB of
   the Time Base Generator device for RTE idle detection.

   On entry, the "memory_size" parameter will be the number of words to allocate
   and will represent the largest possible memory supported by the most
   expansive CPU model.  If memory allocation failed, or the TBG device was not
   found, the routine returns an appropriate error.


   Implementation notes:

    1. This routine is called only once during simulator startup.  If it returns
       an error status, then the simulator exits.

    2. The TBG device is initially named "CLK" (for backward compatibility).
       The logical name "TBG" is assigned by the TBG device reset routine, but
       we are called before that routine is executed, so the logical name does
       not exist when we are called.
*/

t_stat mem_initialize (uint32 memory_size)
{
DEVICE *tbg_dptr;

M = (MEMORY_WORD *) calloc (memory_size,                /* allocate the maximum amount of memory needed */
                            sizeof (MEMORY_WORD));

tbg_dptr = find_dev ("CLK");                            /* get a pointer to the time-base generator device */

if (tbg_dptr == NULL)                                   /* if the TBG device is not present */
    return SCPE_IERR;                                   /*   then something is seriously wrong */
else                                                    /* otherwise */
    tbg_dibptr = (DIB *) tbg_dptr->ctxt;                /*   set a pointer to the device's DIB */

if (M == NULL)                                          /* if memory allocation failed */
    return SCPE_MEM;                                    /*   then report the error */
else                                                    /* otherwise */
    return SCPE_OK;                                     /*   report successful allocation */
}


/* Read a word from memory.

   This routine reads and returns a word from memory at the indicated logical
   address.  On entry, "dptr" points to the DEVICE structure of the device
   requesting access, "classification" is the type of access requested, and
   "address" is the offset into the 32K logical address space implied by the
   classification.

   If memory expansion is enabled, the logical address is mapped into a physical
   memory location; the map used is determined by the access classification.
   The current map (user or system), alternate map (the map not currently
   selected), or an explicit map (system, user, DCPC port A, or DCPC port B) may
   be requested.  Read protection is enabled for current or alternate map access
   and disabled for the others.  If memory expansion is disabled or not present,
   the logical address directly accesses the first 32K of memory.

   The Memory Protect (MP) and Memory Expansion Module (MEM) accessories provide
   a protected mode that guards against improper accesses by user programs.
   They may be enabled or disabled independently, although protection requires
   that both be enabled.  MEM checks that read protection rules on the target
   page are compatible with the access desired.  If the check fails, and MP is
   enabled, then the request is aborted.

   The 1000 family maps memory location 0 to the A-register and location 1 to
   the B-register.  CPU reads of these locations return the A- or B-register
   values, while DCPC reads access physical memory locations 0 and 1 instead.


   Implementation notes:

    1. A read beyond the limit of physical memory returns 0.  This is handled by
       allocating the maximum memory array and initializing memory beyond the
       defined limit to zero, so no special handling is needed here.

    2. A MEM read protection violation with MP enabled causes an MP abort
       instead of a normal return from the "map_address" routine..

    3. In hardware, a FTCH micro-order clocks the address on the MBUS into the
       MP Violation Register if the EVR (Enable Violation Register) flip-flop is
       set.  An MP or MEM violation clears EVR, preserving the address of the
       violating instruction until the Violation Register is read during abort
       processing.
*/

HP_WORD mem_read (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD address)
{
uint32  index;
MEU_MAP_SELECTOR map;
HP_WORD protection;

MR = address;                                           /* save the logical memory address */

switch (classification) {                               /* dispatch on the access classification */

    case Fetch:
        if (mp_evrff)                                   /* if the violation register is enabled */
            mp_VR = address;                            /*   then update it with the instruction address */

        map = meu_current_map;                          /* use the currently selected map (user or system) */
        protection = READ_PROTECTED;                    /*   and enable read protection */
        break;


    case Data:
    default:                                            /* needed to quiet the compiler's anxiety */
        map = meu_current_map;                          /* use the currently selected map (user or system) */
        protection = READ_PROTECTED;                    /*   and enable read protection */
        break;


    case Data_Alternate:
        map = ALTERNATE_MAP (meu_current_map);          /* use the alternate map (user or system) */
        protection = READ_PROTECTED;                    /*   and enable read protection */
        break;


    case Data_System:
        map = System_Map;                               /* use the system map explicitly */
        protection = NO_PROTECTION;                     /*   without protection */
        break;


    case Data_User:
        map = User_Map;                                 /* use the user map explicitly */
        protection = NO_PROTECTION;                     /*   without protection */
        break;


    case DMA_Channel_1:
        map = Port_A_Map;                               /* use the DCPC port A map */
        protection = NO_PROTECTION;                     /*   without protection */
        break;


    case DMA_Channel_2:
        map = Port_B_Map;                               /* use the DCPC port B map */
        protection = NO_PROTECTION;                     /*   without protection */
        break;
    }                                                   /* all cases are handled */

index = map_address (address, map, protection);         /* translate the logical address to a physical address */

if (index > 1 || map >= Port_A_Map)                     /* if memory is referenced or this is a DCPC transfer */
    TR = (HP_WORD) M [index];                           /*   then return the physical memory value */
else                                                    /* otherwise */
    TR = ABREG [index];                                 /*   return the selected register value */

tpprintf (dptr, mem_access [classification].debug_flag,
          DMS_FORMAT "  %s%s\n",
          meu_indicator, meu_page, MR, TR,
          mem_access [classification].name,
          mem_access [classification].debug_flag == TRACE_FETCH ? "" : " read");

return TR;                                              /* return the word that was read */
}


/* Write a word to memory.

   This routine writes a word to memory at the indicated logical address.  On
   entry, "dptr" points to the DEVICE structure of the device requesting access,
   "classification" is the type of access requested, "address" is the offset
   into the 32K logical address space implied by the classification, and "value"
   is the value to write.

   If memory expansion is enabled, the logical address is mapped into a physical
   memory location; the map used is determined by the access classification.
   The current map (user or system), alternate map (the map not currently
   selected), or an explicit map (system, user, DCPC port A, or port B) may be
   requested.  Write protection is enabled for current or alternate map access
   and disabled for the others.  If memory expansion is disabled or not present,
   the logical address directly accesses the first 32K of memory.

   The Memory Protect (MP) and Memory Expansion Module (MEM) accessories provide
   a protected mode that guards against improper accesses by user programs.
   They may be enabled or disabled independently, although protection requires
   that both be enabled.  MP checks that memory writes do not fall below the
   Memory Protect Fence Register (MPFR) value, and MEM checks that write
   protection rules on the target page are compatible with the access desired.
   If either check fails, and MP is enabled, then the request is aborted (so, to
   pass, a page must be writable AND the target must be above the MP fence).  In
   addition, a MEM write violation will occur if MP is enabled and the alternate
   map is selected, regardless of the page protection.

   The 1000 family maps memory location 0 to the A-register and location 1 to
   the B-register.  CPU writes to these locations store the values into the A or
   B register, while DCPC writes access physical memory locations 0 and 1
   instead.  MP uses a lower bound of 2 for memory writes, allowing unrestricted
   access to the A and B registers.


   Implementation notes:

    1. if memoy expansion is disabled, a write beyond the limit of physical
       memory is a no-operation.  If expansion is enabled, it is a NOP if the
       page is not write-protected.

    2. When the alternate map is enabled, writes are permitted only in the
       unprotected mode, regardless of page protections or the MP fence setting.
       This behavior is not mentioned in the MEM documentation, but it is tested by
       the MEM diagnostic and is evident from the MEM schematic.  Referring to
       Sheet 2 in the ERD, gates U125 and U127 provide this logic:

         WTV = MPCNDB * MAPON * (WPRO + ALTMAP)

       The ALTMAP signal is generated by the not-Q output of flip-flop U117,
       which toggles on control signal -CL3 assertion (generated by the MESP
       microorder) to select the alternate map.  Therefore, a write violation is
       indicated whenever a memory protect check occurs while the MEM is enabled
       and either the page is write-protected or the alternate map is selected.

       The hardware reference manuals that contain descriptions of those DMS
       instructions that write to the alternate map (e.g., MBI) say, "This
       instruction will always cause a MEM violation when executed in the
       protected mode and no bytes [or words] will be transferred."  However,
       they do not state that a write violation will be indicated, nor does the
       description of the write violation state that this is a potential cause.
*/

void mem_write (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD address, HP_WORD value)
{
uint32  index;
MEU_MAP_SELECTOR map;
HP_WORD protection;

MR = address;                                           /* save the logical memory address */

switch (classification) {                               /* dispatch on the access classification */

    case Data:
    default:                                            /* needed to quiet the compiler's anxiety */
        map = meu_current_map;                          /* use the currently selected map (user or system) */
        protection = WRITE_PROTECTED;                   /*   and enable write protection */
        break;


    case Data_Alternate:
        map = ALTERNATE_MAP (meu_current_map);          /* use the alternate map (user or system) */
        protection = WRITE_PROTECTED;                   /*   and enable write protection */

        if (meu_status & MEST_ENABLED)                  /* if the MEM is enabled */
            dm_violation (MEVI_WRITE);                  /*   then a violation always occurs if in protected mode */
        break;


    case Data_System:
        map = System_Map;                               /* use the system map explicitly */
        protection = NO_PROTECTION;                     /*   without protection */
        break;


    case Data_User:
        map = User_Map;                                 /* use the user map explicitly */
        protection = NO_PROTECTION;                     /*   without protection */
        break;


    case DMA_Channel_1:
        map = Port_A_Map;                               /* use the DCPC port A map */
        protection = NO_PROTECTION;                     /*   without protection */
        break;


    case DMA_Channel_2:
        map = Port_B_Map;                               /* use the DCPC port B map */
        protection = NO_PROTECTION;                     /*   without protection */
        break;


    case Fetch:                                         /* instruction fetches */
        return;                                         /*   do not cause writes */

    }                                                   /* all cases are handled */

index = map_address (address, map, protection);         /* translate the logical address to a physical address */

if (protection == WRITE_PROTECTED                       /* if protection is wanted */
  && address >= 2 && address < mp_fence)                /*   and the MP check fails */
    mp_violation ();                                    /*     then a memory protect violation occurs */

if (index <= 1 && map <= User_Map)                      /* if the A/B register is referenced in the system or user map */
    ABREG [index] = value;                              /*   then write the value to the selected register */

else if (index < mem_end)                               /* otherwise if the location is within defined memory */
    M [index] = (MEMORY_WORD) value;                    /*   then write the value to memory */

TR = value;                                             /* save the value just written */

tpprintf (dptr, mem_access [classification].debug_flag,
          DMS_FORMAT "  %s write\n",
          meu_indicator, meu_page, MR, TR,
          mem_access [classification].name);

return;
}


/* Read a byte from memory.

   This routine reads and returns a byte from memory at the indicated logical
   address.  On entry, "dptr" points to the DEVICE structure of the device
   requesting access, "classification" is the type of access requested, and
   "byte_address" is the byte offset into the 32K logical address space implied
   by the classification.

   The HP 1000 is a word-oriented machine.  To permit byte accesses, a logical
   byte address is defined as two times the associated word address.  The LSB of
   the byte address designates the byte to access: 0 for the upper byte, and 1
   for the lower byte.  As all 16 bits are used, byte addresses cannot be
   indirect.


   Implementation notes:

    1. Word buffering is not used to minimize memory reads, as the HP 1000
       microcode does a full word read for each byte accessed.
*/

uint8 mem_read_byte (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD byte_address)
{
const HP_WORD word_address = byte_address >> 1;         /* the address of the word containing the byte */
HP_WORD word;

word = mem_read (dptr, classification, word_address);   /* read the addressed word */

if (byte_address & LSB)                                 /* if the byte address is odd */
    return LOWER_BYTE (word);                           /*   then return the right-hand byte */
else                                                    /* otherwise */
    return UPPER_BYTE (word);                           /*   return the left-hand byte */
}


/* Write a byte to memory.

   This routine writes a byte to memory at the indicated logical address.  On
   entry, "dptr" points to the DEVICE structure of the device requesting access,
   "classification" is the type of access requested, "byte_address" is the byte
   offset into the 32K logical address space implied by the classification, and
   "value" is the value to write.

   The HP 1000 is a word-oriented machine.  To permit byte accesses, a logical
   byte address is defined as two times the associated word address.  The LSB of
   the byte address designates the byte to access: 0 for the upper byte, and 1
   for the lower byte.  As all 16 bits are used, byte addresses cannot be
   indirect.


   Implementation notes:

    1. Word buffering is not used to minimize memory writes, as the HP 1000
       base-set microcode does a full word write for each byte accessed.  (The
       DMS byte instructions, e.g., MBI, do full-word accesses for each pair of
       bytes, but that is to minimize the number of map switches.)
*/

void mem_write_byte (DEVICE *dptr, ACCESS_CLASS classification, HP_WORD byte_address, uint8 value)
{
const HP_WORD word_address = byte_address >> 1;         /* the address of the word containing the byte */
HP_WORD word;

word = mem_read (dptr, classification, word_address);   /* read the addressed word */

if (byte_address & LSB)                                 /* if the byte address is odd */
    word = REPLACE_LOWER (word, value);                 /*   then replace the right-hand byte */
else                                                    /* otherwise */
    word = REPLACE_UPPER (word, value);                 /*   replace the left-hand byte */

mem_write (dptr, classification, word_address, word);   /* write the updated word back */

return;
}


/* Fast read from memory.

   This routine reads and returns a word from memory at the indicated logical
   address using the specified map.  Memory protection is not used, and tracing
   is not available.

   This routine is used when fast, unchecked access to mapped memory is
   required.
*/

HP_WORD mem_fast_read (HP_WORD address, MEU_MAP_SELECTOR map)
{
if (map == Current_Map)                                 /* if the current map is requested */
    map = meu_current_map;                              /*   then use it */

return mem_examine (map_address (address, map, NO_PROTECTION)); /* return the value from the selected map */
}


/* Zero a range of memory locations.

   Main memory locations from a supplied starting address through the end of
   defined memory are filled with the specified value.  This routine is
   typically called to zero non-existent memory when the main memory size is
   reduced (so that non-existent locations will read as zero).
*/

void mem_zero (uint32 starting_address, uint32 fill_count)
{
memset (M + starting_address, 0,                        /* zero the words */
        fill_count * sizeof (MEMORY_WORD));             /*   in the specified memory range */

return;
}


/* Check for a non-zero value within a memory address range.

   This routine checks a range of memory locations for the presence of a
   non-zero value.  The starting address of the range is supplied, and the check
   continues through the end of defined memory.  The routine returns TRUE if the
   memory range was empty (i.e., contained only zero values) and FALSE
   otherwise.
*/

t_bool mem_is_empty (uint32 starting_address)
{
uint32 address;

for (address = starting_address; address < mem_size; address++) /* loop through the specified address range */
    if (M [address] != 0)                                       /* if this location is non-zero */
        return FALSE;                                           /*   then indicate that memory is not empty */

return TRUE;                                            /* return TRUE if all locations contain zero values */
}


/* Copy a binary loader to or from protected memory.

   This routine is called to copy a 64-word binary loader from a buffer to
   memory or vice versa.  On entry, "buffer" points at an array of MEMORY_WORDs
   sufficiently large to hold a 64-word binary loader, "starting_address" is the
   address in memory corresponding to the loader target, and "mode" is
   "To_Memory" to copy from the buffer to memory or "From_Memory" to copy from
   memory to the buffer.  If copying from memory, the copied memory area is
   zeroed before returning (memory is zeroed in preparation to protecting the
   reserved loader area).
*/

void mem_copy_loader (MEMORY_WORD *buffer, uint32 starting_address, COPY_DIRECTION mode)
{
if (mode == To_Memory)                                  /* if copying into memory */
    memcpy (M + starting_address, buffer,               /*   then transfer the loader */
            IBL_SIZE * sizeof (MEMORY_WORD));           /*     from the buffer to memory */

else {                                                  /* otherwise */
    memcpy (buffer, M + starting_address,               /*   transfer the loader */
            IBL_SIZE * sizeof (MEMORY_WORD));           /*     from memory to the buffer */

    mem_zero (starting_address, IBL_SIZE);              /* zero the vacated memory area */
    }

return;
}


/* Determine if the CPU is idle.

   This routine determines whether the CPU is executing an operating system idle
   loop.  It is called when a JMP or JMP,I instruction is executed with CPU
   idling enabled and no interrupt pending.

   The 21xx/1000 CPUs have no "wait for interrupt" instruction.  Idling in HP
   operating systems consists of sitting in "idle loops" that end with JMP
   instructions.  We test for certain known patterns when a JMP instruction is
   executed to decide if the simulator should idle.

   If execution is within a recognized idle loop, the routine returns TRUE; in
   response, the simulator will call the "sim_idle" routine to suspend execution
   until the next event service is due.  If the CPU is not executing an idle
   loop, the routine returns FALSE to continue normal execution.

   On entry, MR contains the address of the jump target, and "err_PR" contains
   the address of the jump instruction.  The difference gives the jump
   displacement.  The recognized idle patterns are operating-system-specific, as
   follows:

     for RTE-6/VM:
       - ISZ <n> / JMP *-1
       - mp_fence = 0
       - XEQT (address 1717B) = 0
       - MEU on with system map enabled
       - RTE verification: TBG (address 1674B) = TBG select code

     for RTE though RTE-IVB:
       - JMP *
       - mp_fence = 0
       - XEQT (address 1717B) = 0
       - MEU on with user map enabled (RTE-III through RTE-IVB only)
       - RTE verification: TBG (address 1674B) = TBG select code

     for DOS through DOS-III:
       - STF 0 / CCA / CCB / JMP *-3
       - DOS verification: A = B = -1, address 40B = -64, address 67B = +64

   Note that in DOS, the TBG is set to 100 milliseconds vs. 10 milliseconds for
   RTE.
*/

t_bool mem_is_idle_loop (void)
{
const int32 displacement = (int32) MR - (int32) err_PR; /* the jump displacement */

if ((displacement == 0                                  /* if the jump target is * (RTE through RTE-IVB) */
  || displacement == -1 && (M [MR] & IR_MRG) == IR_ISZ) /*   or the target is *-1 (RTE-6/VM) and *-1 is ISZ <n> */
  && mp_fence == 0                                      /*   and the MP fence is zero */
  && M [xeqt] == 0                                      /*   and no program is executing */
  && M [tbg] == tbg_dibptr->select_code                 /*   and the TBG select code is correct */

  || displacement == -3                                 /*   or the jump target is *-3 (DOS through DOS-III) */
  && M [MR] == IR_STF                                   /*   and *-3 is STF 0 */
  && AR == 0177777u                                     /*   and the A and B registers */
  && BR == 0177777u                                     /*     are both set to -1 */
  && M [m64] == 0177700u                                /*   and the -64 and +64 base-page constants */
  && M [p64] == 0000100u)                               /*     are set as expected */
    return TRUE;                                        /*   then the system is executing an idle lop */

else                                                    /* otherwise */
    return FALSE;                                       /*   the system is not executing an idle loop */
}


/* Trace the working and MP/MEM registers.

   This routine is called when CPU register tracing is enabled.  It reports the
   content of the working registers (S. A, B, X, Y, E, and O), memory protection
   status (on or off), interrupt system status (on or off), and the current MEU
   base page fence value.  If the MP or MEM working registers changed since the
   last trace report, an additional line is printed to report the memory protect
   fence and violation registers and the memory expansion status and violation
   registers.


   Implementation notes:

    1. The "is_1000" flag is used to include or omit, based on the CPU model,
       the X and Y registers from the working register trace and the MEVR and
       MESR from the memory protection trace.
*/

void mem_trace_registers (FLIP_FLOP interrupt_system)
{
hp_trace (&cpu_dev, TRACE_REG,              /* output the working registers */
          register_formats [is_1000],       /*   using a format appropriate for the CPU model */
          mp_value [mp_control],
          meu_status & MEST_FENCE_MASK,
          SR, AR, BR, XR, YR);

fputs (register_values [E << 2 | O << 1 | interrupt_system], sim_deb);  /* output E, O, and interrupt system */
fputc ('\n', sim_deb);

if (mp_mem_changed) {                       /* if the MP/MEM registers have been altered */
    hp_trace (&cpu_dev, TRACE_REG,          /*   then output the register values */
              mp_mem_formats [is_1000],     /*     using a format appropriate for the CPU model */
              mp_value [mp_control],
              mp_fence, mp_VR, meu_status, meu_violation);

    mp_mem_changed = FALSE;                 /* clear the MP/MEM registers changed flag */
    }
}


/* Examine a physical memory address.

   This routine reads and returns a word from memory at the indicated physical
   address.  If the address lies outside of allocated memory, a zero value is
   returned.  There are no protections or error indications.
*/

HP_WORD mem_examine (uint32 address)
{
if (address <= 1 && !(sim_switches & SIM_SW_REST))      /* if the address is 0 or 1 and not restoring memory */
    return ABREG [address];                             /*   then return the A or B register value */

else if (address <= PA_MAX)                             /* otherwise if the address is within allocated memory */
    return (HP_WORD) M [address];                       /*   then return the memory value */

else                                                    /* otherwise the access is outside of memory */
    return 0;                                           /*   which reads as zero */
}


/* Deposit into a physical memory address.

   This routine writes a word into memory at the indicated physical address.  If
   the address lies outside of defined memory, the write is ignored.  There are
   no protections or error indications.
*/

void mem_deposit (uint32 address, HP_WORD value)
{
if (address <= 1 && !(sim_switches & SIM_SW_REST))      /* if the address is 0 or 1 and not restoring memory */
    ABREG [address] = value & DV_MASK;                  /*   then store into the A or B register */

else if (address < mem_end)                             /* otherwise if the address is within defined memory */
    M [address] = (MEMORY_WORD) value & DV_MASK;        /*   then store the value */

return;
}



/* Memory Expansion Unit global utility routines */


/* Configure the Memory Expansion Module.

   This routine enables or disables the MEM, depending on the "configuration"
   parameter.  If the MEM is being enabled, the "device disabled" flag is
   cleared.  Otherwise, the flag is set, and mapping is disabled so that address
   translation will not occur.

   The routine is called when the DMS instruction set is enabled or disabled.
   The MEM device state tracks the instruction state and cannot be set
   independently, i.e., with a SET MEM DISABLED command.
*/

void meu_configure (MEU_STATE configuration)
{
if (configuration == ME_Enabled)                        /* if DMS instructions are enabled */
    meu_dev.flags &= ~DEV_DIS;                          /*   then enable the MEM device */

else {                                                  /* otherwise */
    meu_dev.flags |= DEV_DIS;                           /*   disable the MEM */
    meu_set_state (ME_Disabled, System_Map);            /*     and disable mapping */
    }

return;
}


/* Read a map register.

   This routine is called to read one map register from the specified map.  The
   map index may be from 0-31 to read from a specific map (System_Map, User_Map,
   etc.) or may be from 0-127 to read a linear sequence of maps (Linear_Map).
   The map content (the protection bits and a physical page number corresponding
   to the logical page number specified by the index) is returned.
*/

HP_WORD meu_read_map (MEU_MAP_SELECTOR map, uint32 index)
{
if (map == Linear_Map)                                  /* if linear access is specified */
    return meu_maps [index / REG_COUNT & MAP_MASK]      /*   then use the upper index bits for the map */
                    [index % REG_COUNT];                /*     and the lower index bits for the register */

else                                                    /* otherwise */
    return meu_maps [map] [index];                      /*   read from the specified map and register */
}


/* Write a map register.

   This routine is called to write a value into one map register of the
   specified map.  The map index may be from 0-31 to write to a specific map
   (System_Map, User_Map, etc.) or may be from 0-127 to write a linear sequence
   of maps (Linear_Map).  The map content (the protection bits and a physical
   page number corresponding to the logical page number specified by the index)
   is stored in the indicated register.
*/

void meu_write_map (MEU_MAP_SELECTOR map, uint32 index, uint32 value)
{
if (map == Linear_Map)                                      /* if linear access is specified */
    meu_maps [index / REG_COUNT & MAP_MASK]                 /*   then use the upper index bits for the map */
             [index % REG_COUNT] = value & ~MAP_RESERVED;   /*     and the lower index bits for the register */

else                                                        /* otherwise */
    meu_maps [map] [index] = value & ~MAP_RESERVED;         /*   write to the specified map and register */

return;
}


/* Set the MEM fence register.

   This routine sets a new value into the MEM base-page fence register.  The
   value must have the "portion mapped" flag in bit 10 and the fence address is
   bits 9-0.  No error checking is performed.
*/

void meu_set_fence (HP_WORD new_fence)
{
meu_status = meu_status & ~(MEST_BELOW | MEST_FENCE_MASK)   /* mask off the old mapping and address */
           | new_fence & (MEST_BELOW | MEST_FENCE_MASK);    /*   and merge in the new values */

mp_mem_changed = TRUE;                                  /* set the MP/MEM registers changed flag */

return;
}


/* Set the Memory Expansion Unit state.

   This routine is called to enable or disable the MEM and to set the current
   map.
*/

void meu_set_state (MEU_STATE operation, MEU_MAP_SELECTOR map)
{
if (operation == ME_Enabled)                            /* if the MEM is being enabled */
    meu_status |= MEST_ENABLED;                         /*   then set the MEM enabled status bit */

else {                                                  /* otherwise disable the MEM */
    meu_status &= ~MEST_ENABLED;                        /*   by clearing the MEM enabled status bit */
    meu_bus_enabled = FALSE;                            /*     and disabling the MEM expansion bus */
    }

meu_current_map = map;                                  /* set the current map in either case */
mp_mem_changed = TRUE;                                  /*   and set the MP/MEM registers changed flag */

return;
}


/* Update the MEM violation register.

   This routine is called to update the MEM violation register.  This is done
   whenever the value in the register might be examined.

   In hardware, the MEM violation register (MEVR) is clocked on every memory
   read, every JMP or memory write (actually, every use of the MPCK micro-order)
   above the lower bound of protected memory, and every execution of a
   privileged DMS instruction.  The register is not clocked when MP is disabled
   by an MP or MEM error (i.e., when MEVFF sets or CTL5FF clears), in order to
   capture the state of the MEM.  In other words, the MEVR continually tracks
   the memory map register accessed plus the MEM state (MEBEN, MAPON, and USR)
   until a violation occurs, and then it's "frozen."

   Under simulation, we do not have to update the MEVR on every memory access,
   because the visible state is only available via a programmed RVA/B
   instruction or via the SCP interface.  Therefore, it is sufficient if the
   register is updated:

     - at a MEM violation (when freezing)
     - at an MP violation (when freezing)
     - during RVA/B execution (if not frozen)
     - before returning to SCP after a simulator stop (if not frozen)

   The routine returns the updated content of the violation register.
*/

HP_WORD meu_update_violation (void)
{
if (mp_control && mp_mevff == CLEAR) {                  /* if the violation register is not frozen */
    meu_violation = PAGE (MR);                          /*   then set the last map addressed */

    if (meu_status & MEST_ENABLED)                      /* if the MEM is currently enabled */
        meu_violation |= MEVI_MEM_ENABLED;              /*   then add the status bit */

    if (meu_current_map == User_Map)                    /* if the user map is currently enabled */
        meu_violation |= MEVI_USER_MAP;                 /*   then add the status bit */

    if (meu_bus_enabled)                                /* if the last memory address was mapped */
        meu_violation |= MEVI_BUS_ENABLED;              /*  then add the "ME bus is enabled" bit */

    mp_mem_changed = TRUE;                              /* set the MP/MEM registers changed flag */
    }

return meu_violation;                                   /* return the violation register content */
}


/* Update the MEM status register.

   This routine is called to update the MEM status register.  This is done
   whenever the value in the register might be examined.

   In hardware, the MEM status register (MESR) is not a physical register but
   rather a set of tristate drivers that enable the base-page fence register,
   the current state of the MEM (disabled or enabled, system or user map), and
   the MEM state at last interrupt onto the CPU's S-bus.

   Under simulation, we do not have to update the MESR each time the current map
   changes, because the visible state is only available via programmed RSA/B and
   SSM instructions, via an RTE OS trap cell instruction (where it is used to
   save the MEM state), or via the SCP interface.  Therefore, it is sufficient
   if the register is updated:

     - during RSA/B or SSM or RTE OS trap cell instruction execution
     - before returning to SCP after a simulator stop

   The routine returns the updated content of the status register.
*/

HP_WORD meu_update_status (void)
{
meu_status &= ~MEST_DYNAMIC;                            /* clear the current MEM state */

if (meu_current_map == User_Map)                        /* if the user map is enabled */
    meu_status |= MEST_USER_MAP;                        /*   then set the currently enabled bit */

if (mp_control)                                         /* if MP is enabled */
    meu_status |= MEST_PROTECTED;                       /*   then set the protected mode bit */

mp_mem_changed = TRUE;                                  /* set the MP/MEM registers changed flag */

return meu_status;                                      /* return the status register value */
}


/* Assert an Interrupt Acknowledge signal to the MEM.

   This routine asserts the IAK signal to the Memory Expansion Module.  It is
   called when the CPU acknowledges an interrupt.  In response, the MEM saves
   its current state and switches to the system map for interrupt processing.

   In addition, if the CPU is tracing instructions, the routine calls
   "map_address" to set the current map indicator and the page number of the
   next instruction to execute.  This will be used by the CPU to print the
   interrupt location.
*/

void meu_assert_IAK (void)
{
meu_status &= ~MEST_DYNAMIC_IAK;                        /* clear the MEM interrupt state */

if (meu_status & MEST_ENABLED)                          /* if the MEM is enabled */
    meu_status |= MEST_ENBL_INT;                        /*   then add the enabled-at-interrupt bit */

if (meu_current_map == User_Map)                        /* if the user map is enabled */
    meu_status |= MEST_UMAP_INT;                        /*   then add the user-map-at-interrupt bit */

if (TRACING (cpu_dev, TRACE_INSTR))                     /* if instruction tracing is active */
    map_address (PR, meu_current_map, NO_PROTECTION);   /*   then set the MEM page and indicator */

meu_current_map = System_Map;                           /* switch to the system map for the interrupt */

mp_mem_changed = TRUE;                                  /* set the MP/MEM registers changed flag */

return;
}


/* Generate a MEM privilege violation.

   This routine conditionally generates a dynamic mapping violation.  If the
   condition is "Always", then a privilege violation is generated.  If the
   condition is "If_User_Map", then a violation occurs if the user map is the
   current map; otherwise, no violation occurs.


   Implmentation notes:

    1. If the MEM is in the protected mode, i.e., memory protect is on, a DM
       violation will cause a microcode abort, and this routine will not return.
*/

void meu_privileged (MEU_CONDITION condition)
{
if (condition == Always || meu_current_map == User_Map)
    dm_violation (MEVI_PRIVILEGE);

return;
}


/* Get the current MEM breakpoint type.

   This routine returns a command line switch value representing the breakpoint
   type that corresponds to the current MEM configuration.  It is used to get
   the current default breakpoint type, as follows:

     MEM State  Current Map  Breakpoint Type
     ---------  -----------  ---------------
     disabled       --              N
      enabled     System            S
      enabled      User             U

   The "is_iak" parameter is used to qualify the "U" type.  If the user map is
   currently enabled but an interupt acknowledgement is pending, then the
   returned type is "S", as the IAK will be handled in the system map.
*/

uint32 meu_breakpoint_type (t_bool is_iak)
{
if (meu_status & MEST_ENABLED)                          /* if MEM is currently enabled */
    if (meu_current_map == User_Map && !is_iak)         /*   then if the user map is currently enabled */
        return SWMASK ('U');                            /*     then return the user breakpoint switch */
    else                                                /*   otherwise */
        return SWMASK ('S');                            /*     return the system breakpoint switch */
else                                                    /* otherwise MEM is disabled */
    return SWMASK ('N');                                /*   so return the non-MEM breakpoint switch */
}


/* Translate a logical address for console access.

   This routine translates a logical address interpreted in the context of the
   translation map implied by the specified switch to a physical address.  It is
   called to map addresses when the user is examining or depositing memory.  It
   is also called to restore a saved configuration, although mapping is not used
   for restoration.  All memory protection checks are off for console access.

   Command line switches modify the interpretation of logical addresses as
   follows:

     Switch  Meaning
     ------  --------------------------------------------------
       -N    Use the address directly with no mapping
       -S    If memory expansion is enabled, use the system map
       -U    If memory expansion is enabled, use the user map
       -P    If memory expansion is enabled, use the port A map
       -Q    If memory expansion is enabled, use the port B map

   If no switch is specified, then the address is interpreted using the current
   map if memory expansion is enabled; otherwise, the address is not mapped.  If
   the current or specified map is used, then the address must lie within the
   32K logical address space; if not, then an address larger than the current
   memory size is returned to indicate that a translation error occurred.
*/

uint32 meu_map_address (HP_WORD logical, int32 switches)
{
MEU_MAP_SELECTOR map;

if (switches & (SWMASK ('N') | SIM_SW_REST))            /* if no mapping is requested */
    return logical;                                     /*   then the address is already a physical address */

else if ((meu_status & MEST_ENABLED) == 0               /* otherwise if the MEM is disabled */
  && switches & ALL_MAPMODES)                           /*   but a mapping mode was given */
    return D32_UMAX;                                    /*     then the command is not allowed */

else if ((meu_status & MEST_ENABLED || switches & ALL_MAPMODES) /* otherwise if mapping is enabled or requested */
  && logical > LA_MAX)                                          /*   and the address is not a logical address */
    return mem_size;                                            /*     then report a memory overflow */

else if (switches & SWMASK ('S'))                       /* otherwise if the -S switch is specified */
    map = System_Map;                                   /*   then use the system map */

else if (switches & SWMASK ('U'))                       /* otherwise if the -U switch is specified */
    map = User_Map;                                     /*   then use the user map */

else if (switches & SWMASK ('P'))                       /* otherwise if the -P switch is specified */
    map = Port_A_Map;                                   /*   then use the DCPC port A map */

else if (switches & SWMASK ('Q'))                       /* otherwise if the -Q switch is specified */
    map = Port_B_Map;                                   /*   then use the DCPC port B map */

else                                                    /* otherwise */
    map = meu_current_map;                              /*   use the current map (system or user) */

return map_address (logical, map, NO_PROTECTION);       /* translate the address without protection */
}



/* Memory Expansion Unit local SCP support routines */


/* Memory Expansion Unit reset.

   The MEM processes POPIO but is not addressed by a select code and so does not
   have an I/O interface.  Therefore, we handle POPIO here.
*/

static t_stat meu_reset (DEVICE *dptr)
{
meu_current_map = System_Map;                           /* enable the system map */

meu_status = 0;                                         /* disable MEM and clear the status register */
meu_violation = 0;                                      /* clear the violation register */

mp_mem_changed = TRUE;                                  /* set the MP/MEM registers changed flag */

return SCPE_OK;
}



/* Memory Expansion Unit local utility routines */


/* Process a MEM violation.

   A MEM violation will report the cause in the violation register.  This occurs
   even if the MEM is not in the protected mode (i.e., MP is not enabled).  If
   MP is enabled, an MP abort is taken with the MEV flip-flop set.  Otherwise,
   we return to the caller.
*/

static void dm_violation (HP_WORD violation)
{
meu_violation = violation | meu_update_violation ();    /* set the cause in the violation register */

if (mp_control) {                                       /* if memory protect is on */
    mp_mem_changed = TRUE;                              /*   then set the MP/MEM registers changed flag */

    mp_mevff = SET;                                     /* record a memory expansion violation */
    mp_violation ();                                    /*   and a memory protect violation */
    }

return;
}


/* Determine whether an address is mapped.

   This routine determines whether a logical address is mapped to a physical
   address or represents a physical address itself.  It corresponds to the
   hardware MEBEN (Memory Expansion Bus Enable) signal and indicates that a
   memory access is not in the unmapped portion of the base page.  The routine
   is called only if the MEM is enabled and returns TRUE if the address is
   mapped or FALSE if it is unmapped.  Before returning, "meu_bus_enabled" is
   set to reflect the mapping state.
*/

static t_bool is_mapped (HP_WORD address)
{
HP_WORD dms_fence;

if (address <= 1)                                       /* if the reference is to the A or B register */
    meu_bus_enabled = FALSE;                            /*   then the location is not mapped */

else if (address <= LWA_BASE_PAGE) {                    /* otherwise if the address is on the base page */
    dms_fence = meu_status & MEST_FENCE_MASK;           /*   then get the base-page fence value */

    if (meu_status & MEST_BELOW)                        /* if the lower portion is mapped */
        meu_bus_enabled = (address < dms_fence);        /*   then mapping occurs if the address is below the fence */
    else                                                /* otherwise the upper portion is mapped */
        meu_bus_enabled = (address >= dms_fence);       /*   so mapping occurs if the address is at or above the fence */
    }

else                                                    /* otherwise the address is not on page 0 */
    meu_bus_enabled = TRUE;                             /*   so it is always mapped */

return meu_bus_enabled;                                 /* return the mapping state */
}


/* Map a logical address to a physical address.

   This routine translates logical to physical addresses.  The logical address,
   desired map, and desired access protection are supplied.  If the access is
   legal, the mapped physical address is returned; if it is not, then a MEM
   violation occurs.

   The current map may be specified by passing "meu_current_map" as the "map"
   parameter, or a specific map may be used.  Normally, read and write accesses
   pass READ_PROTECTED or WRITE_PROTECTED, respectively, as the "protection"
   parameter to request access checking.  For DCPC accesses, NO_PROTECTION must
   be passed to inhibit access checks.

   This routine checks for read, write, and base-page violations and will call
   "dm_violation" as appropriate.  The latter routine will abort if MP is
   enabled, or will return if protection is off.
*/

static uint32 map_address (HP_WORD address, MEU_MAP_SELECTOR map, HP_WORD protection)
{
uint32 map_register;

if (meu_status & MEST_ENABLED) {                        /* if the Memory Expansion Unit is enabled */
    meu_indicator = map_indicator [map];                /*   then set the map indicator to the applied map */

    if (address > LWA_BASE_PAGE                         /* if the address is not on the base page */
      || map >= Port_A_Map                              /*   or this is a DCPC transfer */
      || is_mapped (address)) {                         /*     or it is to the mapped portion of the base page */
        map_register = meu_maps [map] [PAGE (address)]; /*       then get the map register for the logical page */

        meu_page = MAP_PAGE (map_register);             /* save the physical page number */

        if (map_register & protection)                  /* if the desired access is not allowed */
            dm_violation (protection);                  /*   then a read or write protection violation occurs */

        return TO_PA (meu_page, address);               /* form the physical address from the mapped page and offset */
        }

    else {                                              /* otherwise the address is unmapped */
        meu_page = 0;                                   /*   so the physical page is page 0 */

        if (address > 1 && protection == WRITE_PROTECTED)   /* a write to the unmapped part of the base page */
            dm_violation (MEVI_BASE_PAGE);                  /*   causes a base-page violation if protection is enabled */

        return address;                                 /* the address is already physical */
        }
    }

else {                                                  /* otherwise the MEU is disabled */
    meu_page = PAGE (address);                          /*   so the physical page is the logical page */
    meu_indicator = '-';                                /*     and no mapping occurs */

    return address;                                     /* the physical address is the logical address */
    }
}



/* Memory Protect I/O interface routine */


/* Memory Protect/Parity Error interface (select code 05).

   I/O operations directed to select code 5 manipulate the Memory Protect
   accessory.  They also affect main memory parity error and memory expansion
   violation reporting.

   STC turns on memory protect, which is turned off only by an MP violation or a
   POPIO.  CLC does nothing.  STF and CLF turn parity error interrupts on and
   off.  SFS skips if a MEM violation occurred, while SFC skips if an MP
   violation occurred.  IOI reads the MP violation register; bit 15 of the
   register is 1 for a parity error and 0 for an MP error.  IOO outputs the
   address of the start of unprotected memory to the MP fence.  PRL and IRQ are
   a function of the MP flag flip-flop only, not the flag and control flip-flops
   as is usual.

   IAK is asserted when any interrupt is acknowledged by the CPU.  Normally, an
   interface qualifies IAK with its own IRQ to ensure that it responds only to
   an acknowledgement of its own request.  The MP card does this to reset its
   flag buffer and flag flip-flops, and to reset the parity error indication.
   However, it also responds to an unqualified IAK (i.e., for any interface) by
   clearing the MPV flip-flop, clearing the indirect counter, clearing the
   control flip-flop, and setting the INTPT flip-flop.

   The hardware INTPT flip-flop indicates an occurrence of an interrupt.  If the
   trap cell of the interrupting device contains an I/O instruction that is not
   a HLT, action equivalent to STC 05 is taken, i.e., the interface sets the
   control and EVR (Enable Violation Register) flip-flops and clears the MEV
   (Memory Expansion Violation) and PARERR (Parity Error) flip-flops.

   In simulation, this is handled during IAK processing by setting "mp_enabled"
   to the state of the MP control flip-flop and scheduling the MP event service
   routine to enter after the next instruction.  If the next instruction, which
   is the trap cell instruction, is an I/O instruction, "cpu_iog" will call
   "mp_check_io" as part of its processing.  If that routine is called for a
   non-HLT instruction, it sets "mp_reenable" to the value saved in
   "mp_enabled", i.e., "mp_reenable" will be SET if MP was enabled when the
   interrupt occurred (it's initialized to CLEAR).  When the service routine is
   entered after the trap instruction executes, it sets "mp_control" to the
   value of "mp_reenable", which reenables MP if MP was on.

   The effect of all of this is to turn MP off when an interrupt occurs but then
   to reenable it if the interrupt trap cell contained a non-HLT I/O
   instruction.  For example, consider a program executing with MP on and an
   interrupt from an interface whose trap cell contains a CLF instruction.  When
   the interrupt occurs, MP is turned off, the CLF is executed, MP is turned on,
   and the program continues.  If the trap cell contained a HLT, MP would be
   turned off, and then the CPU would halt.  If the trap cell contained a JSB,
   MP would be turned off and would remain off while the interrupt subroutine
   executes.


   Implementation notes:

    1. Because the MP card uses IAK unqualified, this routine is called whenever
       any interrupt occurs.  It is also called when the MP card itself is
       interrupting.  The latter condition is detected by the MP flag flip-flop
       being set.  As MP has higher priority than all devices except power fail,
       if the flag is set, the IAK must be for the MP card.

    2. The MEV flip-flop records memory expansion violations.  It is set when a
       MEM violation is encountered and can be tested via SFC/SFS.

    3. The Parity Error logic is not currently implemented.
*/

static SIGNALS_VALUE mp_interface (const DIB *dibptr, INBOUND_SET inbound_signals, HP_WORD inbound_value)
{
INBOUND_SIGNAL signal;
INBOUND_SET    working_set  = inbound_signals;
SIGNALS_VALUE  outbound     = { ioNONE, 0 };
t_bool         irq_enabled  = FALSE;

while (working_set) {                                   /* while signals remain */
    signal = IONEXTSIG (working_set);                   /*   isolate the next signal */

    switch (signal) {                                   /* dispatch the I/O signal */

        case ioCLF:                                     /* Clear Flag flip-flop */
            break;                                      /* turn parity error interrupts off */


        case ioSTF:                                     /* Set Flag flip-flop */
            break;                                      /* turn parity error interrupts on */


        case ioENF:                                     /* Enable Flag */
            if (mp_flag_buffer == SET)                  /* if the flag buffer flip-flop is set */
                if (inbound_signals & ioIEN) {          /*   then if the interrupt system is on */
                    mp_flag = SET;                      /*     then set the flag flip-flop */
                    mp_evrff = CLEAR;                   /*       and inhibit violation register updates */
                    }

                else                                    /*   otherwise interrupts are off */
                    mp_flag_buffer = CLEAR;             /*     and the flag buffer does not set if IEN5 is denied */
            break;


        case ioSFC:                                     /* Skip if Flag is Clear */
            if (mp_mevff == CLEAR)                      /* if this is a memory protect violation */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioSFS:                                     /* Skip if Flag is Set */
            if (mp_mevff == SET)                        /* if this is a memory expansion violation */
                outbound.signals |= ioSKF;              /*   then assert the Skip on Flag signal */
            break;


        case ioIOI:                                     /* I/O Data Input */
            outbound.value = mp_VR;                     /* return the MP violation register */
            break;


        case ioIOO:                                     /* I/O Data Output */
            mp_fence = inbound_value & LA_MASK;         /* store the address in the MP fence register */

            if (cpu_configuration & CPU_2100)           /* the 2100 IOP instructions */
                SPR = mp_fence;                         /*   use the MP fence as a stack pointer */

            mp_mem_changed = TRUE;                      /* set the MP/MEM registers changed flag */
            break;


        case ioPOPIO:                                   /* Power-On Preset to I/O */
            mp_control     = CLEAR;                     /* clear the control flip-flop */
            mp_flag_buffer = CLEAR;                     /*   and the flag buffer flip-flop */
            mp_flag        = CLEAR;                     /*     and the flag flip-flop */

            mp_mevff = CLEAR;                           /* clear the Memory Expansion Violation flip-flop */
            mp_evrff = SET;                             /*   and set the Enable Violation Fegister flip-flop */

            mp_reenable = CLEAR;                        /* clear the MP reenable */
            mp_enabled  = CLEAR;                        /*   and MP currently enabled flip-flops */

            mp_mem_changed = TRUE;                      /* set the MP/MEM registers changed flag */
            break;


        case ioSTC:                                     /* Set Control flip-flop */
            mp_control = SET;                           /* set the control flip-flop to turn on MP */

            mp_mevff = CLEAR;                           /* clear the Memory Expansion Violation flip-flop */
            mp_evrff = SET;                             /*   and set the Enable Violation Register flip-flop */
            break;


        case ioSIR:                                     /* Set Interrupt Request */
            if (mp_flag)                                /* if the flag flip-flop is set */
                outbound.signals |= cnIRQ | cnVALID;    /*   then deny PRL and conditionally assert IRQ */
            else                                        /* otherwise */
                outbound.signals |= cnPRL | cnVALID;    /*   conditionally assert PRL */
            break;


        case ioIAK:                                     /* Interrupt Acknowledge */
            if (mp_flag) {                              /* if the MP itself is interrupting */
                mp_flag_buffer = CLEAR;                 /*   then clear the flag buffer */
                mp_flag        = CLEAR;                 /*     and flag flip-flops */
                }

            mp_enabled = mp_control;                    /* set the MP interrupt flip-flop if MP was on */
            mp_control = CLEAR;                         /*   and then turn memory protection off */

            sim_activate (mp_unit, mp_unit [0].wait);   /* schedule a status check for the next instruction */
            break;


        case ioIEN:                                     /* Interrupt Enable */
            irq_enabled = TRUE;                         /* permit IRQ to be asserted */
            break;


        case ioPRH:                                         /* Priority High */
            if (irq_enabled && outbound.signals & cnIRQ)    /* if IRQ is enabled and conditionally asserted */
                outbound.signals |= ioIRQ | ioFLG;          /*   then assert IRQ and FLG */

            if (!irq_enabled || outbound.signals & cnPRL)   /* if IRQ is disabled or PRL is conditionally asserted */
                outbound.signals |= ioPRL;                  /*   then assert it unconditionally */
            break;


        case ioCRS:                                     /* not used by this interface */
        case ioCLC:                                     /* not used by this interface */
        case ioEDT:                                     /* not used by this interface */
        case ioPON:                                     /* not used by this interface */
            break;
        }

    IOCLEARSIG (working_set, signal);                   /* remove the current signal from the set */
    }                                                   /*   and continue until all signals are processed */

return outbound;                                        /* return the outbound signals and value */
}



/* Memory Protect global utility routines */


/* Initialize memory protect.

   This routine is called from the instruction execution prelude to set up the
   internal state of the memory protect accessory.  It returns the state of the
   MP device (enabled or disabled) to avoid having to make the DEVICE structure
   global.
*/

t_bool mp_initialize (void)
{
is_1000 = (cpu_configuration & CPU_1000) != 0;          /* set the CPU model index */

mp_mem_changed = TRUE;                                  /* request an initial MP/MEM trace */

return (mp_dev.flags & DEV_DIS) == 0;                   /* return TRUE if MP is enabled and FALSE if not */
}


/* Configure the Memory Protect accessory.

   This routine enables or disables MP, depending on the "is_enabled" parameter,
   and makes the MP configurable or non-configurable, depending on the
   "is_optional" parameter.  It adds or removes the DEV_DIS device flag to
   disable or enable the device, and adds or removes the DEV_DISABLE device flag
   to allow or deny the use of SET MP ENABLED/DISABLED SCP commands to change
   the device state.
*/

void mp_configure (t_bool is_enabled, t_bool is_optional)
{
if (is_enabled)                                         /* if MP is to be enabled */
    mp_dev.flags &= ~DEV_DIS;                           /*   then remove the "disabled" flag */
else                                                    /* otherwise */
    mp_dev.flags |= DEV_DIS;                            /*   add the flag to disable the device */

if (is_optional)                                        /* if MP is to be made configurable */
    mp_dev.flags |= DEV_DISABLE;                        /*   then add the "can be disabled" flag */
else                                                    /* otherwise */
    mp_dev.flags &= ~DEV_DISABLE;                       /*   make the current setting unalterable */

return;
}


/* Check a jump for memory protect or memory expansion violations.

   This routine checks a jump target address for protection violations.  On
   entry, "address" is the logical address of the jump target, and "lower_bound"
   is the lowest protected memory address.  If a violation occurs, the routine
   does not return; instead, a microcode abort is taken.

   Program execution jumps are a special case of write validation.  The target
   address is treated as a write, even when no physical write takes place (e.g.,
   when executing a JMP instead of a JSB), so jumping to a write-protected page
   causes a MEM violation.  In addition, a MEM violation occurs if the jump is
   to the unmapped portion of the base page.  Finally, jumping to a location
   under the memory-protect fence causes an MP violation.

   Because the MP and MEM hardware works in parallel, all three violations may
   exist concurrently.  For example, a JMP to the unmapped portion of the base
   page that is write protected and under the MP fence will indicate a
   base-page, a write, and an MP violation, whereas a JMP to the mapped portion
   will indicate a write and an MP violation (BPV is inhibited by the MEBEN
   signal).  If MEM and MP violations occur concurrently, the MEM violation
   takes precedence, as the SFS and SFC instructions test the MEV flip-flop.

   The lower bound of protected memory must be either 0 or 2.  All violations
   are qualified by the MPCND signal, which responds to the lower bound.
   Therefore, if the lower bound is 2, and if the part below the base-page fence
   is unmapped, or if the base page is write-protected, then a MEM violation
   will occur only if the access is not to locations 0 or 1.  The instruction
   set firmware uses a lower bound of 0 for JMP, JLY, and JPY (and for JSB with
   W5 out), and of 2 for DJP, SJP, UJP, JRS, and .GOTO (and JSB with W5 in).

   Finally, all violations are inhibited if MP is off (i.e., the MP control
   flip-flop is clear), and MEM violations are inhibited if the MEM is disabled.
*/

void mp_check_jmp (HP_WORD address, uint32 lower_bound)
{
const uint32 lp = PAGE (address);                       /* the logical page number */
HP_WORD violation = 0;                                  /* the MEM violation conditions */

if (mp_control) {                                               /* if memory protect is enabled */
    if (meu_status & MEST_ENABLED) {                            /*   then if the MEM is enabled */
        if (meu_maps [meu_current_map] [lp] & WRITE_PROTECTED)  /*     then if the page is write protected */
            violation = MEVI_WRITE;                             /*       then a write violation occurs */

        if (address >= lower_bound && ! is_mapped (address))    /* if the unmapped base page is the target */
            violation |= MEVI_BASE_PAGE;                        /*   then a base page violation occurs */

        if (violation)                                  /* if a violation occurred */
            dm_violation (violation);                   /*   then assert a MEM violation */
        }

    if (address >= lower_bound && address < mp_fence)   /* if the jump is under the memory protect fence */
        mp_violation ();                                /*   then a memory protect violation occurs */
    }

return;
}


/* Check a jump-to-subroutine for memory protect or memory expansion violations.

   This routine checks a jump-to-subroutine target address for protection
   violations.  On entry, "address" is the logical address of the jump target.
   If a violation occurs, the routine does not return; instead, a microcode
   abort is taken.

   The protected lower bound address for the JSB instruction depends on the W5
   jumper setting.  If W5 is in, then the lower bound is 2, allowing JSBs to the
   A and B registers.  If W5 is out, then the lower bound is 0, just as with
   JMP.
*/

void mp_check_jsb (HP_WORD address)
{
mp_check_jmp (address, jsb_bound);                      /* check the jump target with the selectex bound */

return;
}


/* Check an I/O operation for memory protect violations.

   This routine is called by the IOG instruction executor to verify that an I/O
   instruction is allowed under the current protection settings.  On entry,
   "select_code" is set to the select code addressed by the instruction, and
   "micro_op" is the IOG operation to be executed.  The routine returns if the
   operation is allowed.  Otherwise, an MP abort is performed.

   If MP is off, then all I/O instructions are allowed.  MP will be off during
   execution of an IOG instruction in an interrupt trap cell; in this case, MP
   will be reenabled if the instruction is not a HLT and MP was enabled prior to
   the interrupt.

   If MP is on, then HLT instructions are illegal and will cause a memory
   protect violation.  If jumper W7 (SEL1) is in, then all other I/O
   instructions are legal; if W7 is out, then only I/O instructions that address
   select code 1 are legal, and I/O to other select codes will cause a
   violation.
*/

void mp_check_io (uint32 select_code, IO_GROUP_OP micro_op)
{
if (mp_control == CLEAR) {                              /* if memory protect is off */
    if (micro_op != iog_HLT && micro_op != iog_HLT_C)   /*   then if the instruction is not a HLT */
        mp_reenable = mp_enabled;                       /*     then set up to reenable if servicing an interrupt */
    }

else if (micro_op == iog_HLT || micro_op == iog_HLT_C           /* otherwise if checking a HLT instruction */
  || select_code != OVF && (mp_unit [0].flags & UNIT_MP_SEL1))  /*   or SC is not 1 and the SEL1 jumper is out */
    mp_violation ();                                            /*     then a memory protect violation occurs */

return;
}


/* Process a memory protect violation.

   If memory protect is on, this routine updates the MEM violation register (if
   this is an MP and not a MEM violation), sets the MP flag buffer and flag
   flip-flops (if interupts are enabled), and performs a microcode abort.  The
   latter does a "longjmp" back to the microcode abort handler just prior to the
   CPU instruction execution loop.

   If memory protect is off, MP violations are ignored.


   Implementation notes:

    1. The "cpu_microcode_abort" routine is called both for MP and MEM
       violations.  The MEV flip-flop will be clear for the former and set for
       the latter.  The MEV violation register will be updated by
       "meu_update_violation" only if the call is NOT for an MEM violation; if
       it is, then the register has already been set and should not be
       disturbed.
*/

void mp_violation (void)
{
if (mp_control) {                                       /* if memory protect is on */
    meu_update_violation ();                            /*   then update the MEVR (if not a MEV) */

    mp_flag_buffer = SET;                               /* set the MP flag buffer flip-flop (if IEN) */
    io_assert (&mp_dev, ioa_ENF);                       /*   and the flag flip-flop (if IEN) */

    cpu_microcode_abort (Memory_Protect);               /* abort the instruction */
    }

return;
}


/* Turn memory protect off.

   This routine is called to disable memory protect.  In hardware, MP cannot be
   turned off, except by causing a violation.  Microcode typically does this by
   executing an IOG micro-order with a select code not equal to 1, followed by
   an IAK to clear the interrupt, and a FTCH to clear the INTPT flip-flop.
   Under simulation, clearing the MP control flip-flop produces the same effect.

   This routine also cancels any scheduled MP event service, in case it's called
   during execution of a microcoded trap cell instruction.
*/

void mp_disable (void)
{
mp_control = CLEAR;                                     /* clear the control flip-flop to turn MP off */

mp_reenable = CLEAR;                                    /* clear the MP reenable */
mp_enabled  = CLEAR;                                    /*   and MP currently enabled flip-flops */

sim_cancel (mp_unit);                                   /* cancel any pending MP reenable */

return;
}


/* Report the memory protect state.

   This routine returns TRUE if MP is on and FALSE otherwise.  It is used by the
   RTE OS microcode executors to check the protection state.  In hardware, this
   is done by reading the MEM status register and checking the protected mode
   bit (bit 11).  In simulation, the MP control flip-flop is checked, as the
   MEM status register is not global.
*/

t_bool mp_is_on (void)
{
return (mp_control == SET);                             /* return TRUE if MP is on and FALSE if it is off */
}


/* Report the INT (W6) jumper position.

   This routine returns TRUE if jumper W6 is not installed and MP is on, and
   FALSE otherwise.  It is called when an interrupt is pending but deferred
   because the Interrupt Enable flip-flop is clear.  If jumper W6 is installed,
   instructions that reference memory will hold off pending but deferred
   interrupts until three levels of indirection have been followed.  If W6 is
   removed, then deferred interrupts are recognized immediately if MP is on.
*/

t_bool mp_reenable_interrupts (void)
{
return mp_unit [0].flags & UNIT_MP_INT && mp_control;   /* return TRUE if interrupts are always recognized */
}


/* Trace a memory protect violation.

   This routine is called when CPU operand tracing is enabled and the microcoded
   memory protect trap cell instruction is executed.  It reports the reason for
   the interrupt (MP, MEM, or PE violation).

   The routine returns TRUE for a MP/MEM violation and FALSE for a PE violation.
   This information is used by the instruction microcode.
*/

t_bool mp_trace_violation (void)
{
tprintf (cpu_dev, TRACE_OPND, OPND_FORMAT "  entry is for a %s\n",
         PR, IR,
         (mp_VR & MPVR_PARITY_ERROR
           ? "parity error"
           : (mp_mevff == SET
               ? "dynamic mapping violation"
               : "memory protect violation")));

return (mp_VR & MPVR_PARITY_ERROR) == 0;                /* return TRUE for MP, FALSE for PE */
}



/* Memory Protect local SCP support routines */


/* Service the memory protect accessory.

   This routine is scheduled whenever IAK is asserted to the MP interface, and
   the MP card itself is not interrupting.  The purpose is to reenable memory
   protection if the interrupt trap cell contains a non-HLT I/O instruction.

   In hardware, the MP card responds to a "foreign" IAK (i.e., one acknowledging
   another interface's interrupt request) by disabling memory protection while
   the trap cell instruction is executed.  If that instruction is a non-HLT IOG
   instruction, MP is automatically reenabled before instruction resumes at the
   point of interruption.  Otherwise, MP remains off while the interrupt handler
   executes.

   In simulation, this is handled during IAK processing by setting "mp_enabled"
   to the state of the MP control flip-flop and scheduling the MP event service
   routine to enter after the next instruction.  If the trap cell instruction is
   an I/O instruction, "cpu_iog" will call "mp_check_io" as part of its
   processing.  If that routine is called for a non-HLT instruction, it sets
   "mp_reenable" to the value saved in "mp_enabled", i.e., "mp_reenable" will be
   SET if MP was enabled when the interrupt occurred (it's initialized to
   CLEAR).  When this routine is entered after the trap instruction executes, it
   sets "mp_control" to the value of "mp_reenable", which reenables MP if MP was
   on.


   Implementation notes:

    1. The two-level setting (mp_enabled -> mp_reenable -> mp_control) is
       necessary to avoid having to clear the reenable flag on every instruction
       execution.  Consider if "mp_reenable" is set directly from "mp_control"
       in the IAK processor.  The "mp_check_io" routine would clear it if the
       instruction is a HLT.  But it would also have to be cleared for all other
       non-IOG instructions, which means inserting a "mp_reenable = CLEAR"
       statement in all other instruction execution paths.  With the two-level
       setting, "mp_reenable" is set from "mp_enabled" only in the "mp_check_io"
       routine, and then only if the instruction is not a HLT instruction.  This
       saves the delay inherent in clearing "mp_reenable" in the 99.99% of the
       cases where an IAK is not being serviced.
*/

static t_stat mp_service (UNIT *uptr)
{
mp_control = mp_reenable;                               /* reenable MP if a non-HLT I/O instruction was executed */

mp_reenable = CLEAR;                                    /* clear the reenable */
mp_enabled  = CLEAR;                                    /*   and enabled-at-interrupt flip-flops */

if (mp_control) {                                       /* if MP was reenabled */
    mp_mevff = CLEAR;                                   /*   then clear the Memory Expansion Violation flip-flop */
    mp_evrff = SET;                                     /*     and set the Enable Violation Register flip-flop */
    }

return SCPE_OK;
}


/* Set the JSB (W5) jumper mode.

   This validation routine is entered with the "value" parameter set to zero or
   UNIT_MP_JSB, depending on whether jumper W5 is being installed or removed.
   The unit, character, and descriptor pointers are not used.

   The protected lower bound address for JSB instruction protection depends on
   the W5 jumper setting.  If W5 is in, then the lower bound is 2, allowing JSBs
   to the A and B registers.  If W5 is out, then the lower bound is 0, just as
   with JMP.
*/

static t_stat mp_set_jsb (UNIT *uptr, int32 value, CONST char *cptr, void *desc)
{
if (value == UNIT_MP_JSB)                               /* if jumper W5 is out */
    jsb_bound = 0;                                      /*   then the protected lower bound is address 0 */
else                                                    /* otherwise W5 is installed */
    jsb_bound = 2;                                      /*   so the protected bound is address 2 */

return SCPE_OK;
}


/* Reset memory protect.

   This routine is called for a RESET, RESET MP, RUN, or BOOT command.  It is
   the simulation equivalent of an initial power-on condition (corresponding to
   PON, POPIO, and CRS signal assertion) or a front-panel PRESET button press
   (corresponding to POPIO and CRS assertion).  SCP delivers a power-on reset to
   all devices when the simulator is started.
*/

static t_stat mp_reset (DEVICE *dptr)
{
io_assert (dptr, ioa_POPIO);                            /* PRESET the device (does not use PON) */

mp_mem_changed = TRUE;                                  /* set the MP/MEM registers changed flag */

return SCPE_OK;
}
