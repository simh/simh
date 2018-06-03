/* hp2100_sys.c: HP 2100 system common interface

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

   07-Mar-18    JDB     Added the GET_SWITCHES macro from scp.c
   22-Feb-18    JDB     Added the <dev> option to the LOAD command
   07-Sep-17    JDB     Replaced "uint16" cast with "MEMORY_WORD" for loader ROM
   07-Aug-17    JDB     Added "hp_attach" to attach a file for appending
   01-Aug-17    JDB     Added "ispunct" test for implied mnemonic parse
   20-Jul-17    JDB     Removed STOP_OFFLINE, STOP_PWROFF messages
   14-Jul-17    JDB     Interrupt deferral is now calculated in instr postlude
   11-Jul-17    JDB     Moved "hp_enbdis_pair" here from hp2100_cpu.c
                        Renamed "ibl_copy" to "cpu_ibl"
   16-May-17    JDB     Rewrote "fprint_sym" and "parse_sym" for better coverage
   03-Apr-17    JDB     Rewrote "parse_cpu" to improve efficiency and coverage
   25-Mar-17    JDB     Increased "sim_emax" from 3 to 10 for VIS instructions
   20-Nar-17    JDB     Rewrote "fprint_cpu" to improve efficiency and coverage
   03-Mar-17    JDB     Added binary input parsing
   01-Mar-17    JDB     Added physical vs. logical address parsing and printing
   27-Feb-17    JDB     Revised the LOAD command and added the DUMP command
   25-Jan-17    JDB     Replaced ReadIO with mem_fast_read, added hp_trace routine
   22-Jan-17    JDB     Separated instruction mnemonic printing
   15-Jan-17    JDB     Corrected HLT decoding to add the 1060xx and 1070xx ranges
                        Corrected SFB decoding
   14-Jan-17    JDB     Removed use of Fprintf functions for version 4.x and on
   30-Dec-16    JDB     Corrected parsing of memory reference instructions
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

   References:
     - HP 1000 M/E/F-Series Computers Technical Reference Handbook
         (5955-0282, March 1990)
     - RTE-IV Assembler Reference Manual
         (92067-90003, October 1980)


   This module provides the interface between the Simulation Control Program
   (SCP) and the HP 2100 simulator.  It includes the required global VM
   interface data definitions (e.g., the simulator name, device array, etc.),
   symbolic display and parsing routines, utility routines for tracing and
   execution support, and SCP command replacement routines.
*/



#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>

#include "hp2100_defs.h"
#include "hp2100_cpu.h"



/* Command-line switch parsing from scp.c */

#define GET_SWITCHES(cp) \
    if ((cp = get_sim_sw (cp)) == NULL) return SCPE_INVSW


/* External I/O data structures */

extern DEVICE mp_dev;                           /* Memory Protect */
extern DEVICE dma1_dev, dma2_dev;               /* Direct Memory Access/Dual-Channel Port Controller */
extern DEVICE ptr_dev;                          /* Paper Tape Reader */
extern DEVICE ptp_dev;                          /* Paper Tape Punch */
extern DEVICE tty_dev;                          /* Teleprinter */
extern DEVICE clk_dev;                          /* Time Base Generator */
extern DEVICE lps_dev;                          /* 2767 Line Printer */
extern DEVICE lpt_dev;                          /* 2607/13/17/18 Line Printer */
extern DEVICE baci_dev;                         /* Buffered Asynchronous Communication Interface */
extern DEVICE mpx_dev;                          /* Eight-Channel Asynchronous Multiplexer */
extern DEVICE mtd_dev, mtc_dev;                 /* 3030 Magnetic Tape Drive */
extern DEVICE msd_dev, msc_dev;                 /* 7970B/E Magnetic Tape Drive */
extern DEVICE dpd_dev, dpc_dev;                 /* 2870/7900 Disc Drive */
extern DEVICE dqd_dev, dqc_dev;                 /* 2883 Disc Drive */
extern DEVICE drd_dev, drc_dev;                 /* 277x Disc/Drum Drive */
extern DEVICE ds_dev;                           /* 7905/06/20/25 Disc Drive */
extern DEVICE muxl_dev, muxu_dev, muxc_dev;     /* Sixteen-Channel Asynchronous Multiplexer */
extern DEVICE ipli_dev, iplo_dev;               /* Processor Interconnect */
extern DEVICE pif_dev;                          /* Privileged Interrupt Fence */
extern DEVICE da_dev;                           /* 7906H/20H/25H ICD Disc Drive */
extern DEVICE dc_dev;                           /* Dummy HP-IB Interface */


/* Program constants */

#define VAL_EMPTY           (PA_MAX + 1)        /* flag indicating that the value array must be loaded */


/* Symbolic production/consumption values */

#define SCPE_OK_2_WORDS     ((t_stat) -1)       /* two words produced or consumed */


/* Symbolic mode and format override switches */

#define A_SWITCH            SWMASK ('A')
#define B_SWITCH            SWMASK ('B')
#define C_SWITCH            SWMASK ('C')
#define D_SWITCH            SWMASK ('D')
#define H_SWITCH            SWMASK ('H')
#define M_SWITCH            SWMASK ('M')
#define O_SWITCH            SWMASK ('O')

#define MODE_SWITCHES       (C_SWITCH | M_SWITCH)
#define FORMAT_SWITCHES     (A_SWITCH | B_SWITCH | D_SWITCH | H_SWITCH | O_SWITCH)
#define SYMBOLIC_SWITCHES   (C_SWITCH | M_SWITCH | A_SWITCH)

#define ALL_SWITCHES        (MODE_SWITCHES | FORMAT_SWITCHES)


/* Operand types.

   Operand types indicate how to print or parse instruction mnemonics.  There is
   a separate operand type for each unique operand interpretation.  For
   printing, the operand type and associated operand mask indicate which bits
   form the operand value and what interpretation is to be imposed on that
   value.  For parsing, the operand type additionally implies the acceptable
   syntax for symbolic entry.

   Operand masks are used to isolate the operand value from the instruction
   word.  As provided, a logical AND removes the operand value; an AND with the
   complement leaves only the operand value.  The one-for-one correspondence
   between operand types and masks must be preserved when adding new operand
   types.


   Implementation notes:

    1. Operand masks are defined as the complements of the operand bits to make
       it easier to see which bits will be cleared when the value is ANDed.  The
       complements are calculated at compile-time and so impose no run-time
       penalty.
*/

typedef enum {
    opNone,                                     /* no operand */
    opMPOI,                                     /* MRG page bit 10, offset 0000-1777 octal, indirect bit 15 */
    opSC,                                       /* IOG select code range 00-77 octal */
    opSCHC,                                     /* IOG select code range 00-77 octal, hold/clear bit 9 */
    opSCOHC,                                    /* IOG optional select code range 00-77 octal, hold/clear bit 9 */
    opHC,                                       /* IOG hold/clear bit 9 */
    opShift,                                    /* EAU shift/rotate count range 1-16 */

    opIOPON,                                    /* IOP index negative offset range 1-20 octal */
    opIOPOP,                                    /* IOP index positive offset range 0-17 octal */
    opIOPO,                                     /* IOP index offset range 0-37 octal (+20 bias) */

    opZA4,                                      /* UIG zero word, four (indirect) memory addresses */
    opZA5,                                      /* UIG zero word, five (indirect) memory addresses */
    opZA6,                                      /* UIG zero word, six (indirect) memory addresses */
    opZA8,                                      /* UIG zero word, eight (indirect) memory addresses */

    opV1,                                       /* UIG one data value */
    opV2,                                       /* UIG two data values */

    opA1V1,                                     /* UIG one (indirect) memory address, one data value */
    opV2A1,                                     /* UIG two data values, one (indirect) memory address */
    opV1A5,                                     /* UIG one data value, five (indirect) memory addresses */

    opMA1ZI,                                    /* One memory address range 00000-77777 octal, zero word, indirect bit 15 */
    opMA1I,                                     /* One memory address range 00000-77777 octal, indirect bit 15 */
    opMA2I,                                     /* Two memory addresses range 00000-77777 octal, indirect bit 15 */
    opMA3I,                                     /* Three memory addresses range 00000-77777 octal, indirect bit 15 */
    opMA4I,                                     /* Four memory addresses range 00000-77777 octal, indirect bit 15 */
    opMA5I,                                     /* Five memory addresses range 00000-77777 octal, indirect bit 15 */
    opMA6I,                                     /* Six memory addresses range 00000-77777 octal, indirect bit 15 */
    opMA7I                                      /* Seven memory addresses range 00000-77777 octal, indirect bit 15 */
    } OP_TYPE;

typedef struct {
    t_value  mask;                              /* operand mask */
    int32    count;                             /* operand count */
    uint32   address_set;                       /* address operand bitset */
    } OP_PROP;

static const OP_PROP op_props [] = {            /* operand properties, indexed by OP_TYPE */
    { ~0000000u,  0,  000000u },                /*   opNone  */
    { ~0101777u,  0,  000001u },                /*   opMPOI  */
    { ~0000077u,  0,  000000u },                /*   opSC    */
    { ~0000077u,  0,  000000u },                /*   opSCHC  */
    { ~0000077u,  0,  000000u },                /*   opSCOHC */
    { ~0001000u,  0,  000000u },                /*   opHC    */
    { ~0000017u,  0,  000000u },                /*   opShift */

    { ~0000017u,  0,  000000u },                /*   opIOPON */
    { ~0000017u,  0,  000000u },                /*   opIOPOP */
    { ~0000037u,  0,  000000u },                /*   opIOPO  */

    { ~0000000u,  5,  000036u },                /*   opZA4   */
    { ~0000000u,  6,  000076u },                /*   opZA5   */
    { ~0000000u,  7,  000176u },                /*   opZA6   */
    { ~0000000u,  9,  000776u },                /*   opZA8   */

    { ~0000000u,  1,  000000u },                /*   opV1    */
    { ~0000000u,  2,  000000u },                /*   opV2    */

    { ~0000000u,  2,  000001u },                /*   opA1V1  */
    { ~0000000u,  3,  000004u },                /*   opV2A1  */
    { ~0000000u,  6,  000076u },                /*   opV1A5  */

    { ~0000000u,  2,  000001u },                /*   opMA1ZI */
    { ~0000000u,  1,  000001u },                /*   opMA1I  */
    { ~0000000u,  2,  000003u },                /*   opMA2I  */
    { ~0000000u,  3,  000007u },                /*   opMA3I  */
    { ~0000000u,  4,  000017u },                /*   opMA4I  */
    { ~0000000u,  5,  000037u },                /*   opMA5I  */
    { ~0000000u,  6,  000077u },                /*   opMA6I  */
    { ~0000000u,  7,  000177u }                 /*   opMA7I  */
    };


/* Instruction classifications.

   Machine instructions on the HP 21xx/1000 are identified by a varying number
   of bits.  In general, the five most-significant bits identify the general
   group of instruction, and additional bits form a sub-opcode within a group to
   identify an instruction uniquely.  However, many instructions are irregular,
   and those from two groups -- the Shift-Rotate Group and the Alter-Skip Group
   -- are formed from multiple "micro-operations" that may be combined to
   perform up to eight operations per instruction.  Instructions from the
   Extended Arithmetic Group have a number of reserved bits that are defined to
   be zero.  Correct hardware decoding may or may not depend on these bits being
   zero and varies from CPU model to model.

   Each instruction is classified by a mnemonic, a base operation code (without
   the operand), an operand type, and a mask for the significant bits that
   identify the opcode.  Classifications are grouped by class of instruction
   into arrays that are indexed by sub-opcodes, if applicable.  Two-word
   instructions will have base operation codes and signifiant bits masks that
   extend to 32 bits.

   An opcode table consists of two parts, either of which is optional.  If a
   given class has a sub-opcode that fully or almost fully decodes the class,
   the first (primary) part of the table contains the appropriate number of
   classification elements.  This allows rapid access to a specific instruction
   based on its bit pattern.  In this primary part, the significant opcode bits
   masks are not defined or used.

   A number of instructions use bit 11 to indicate whether the instruction
   applies to the A-register or B-register.  If a class uses a primary part, and
   that class also uses the A/B-register indicator, then the primary table is
   twice the expected length (based on the number of significant sub-opcode
   bits).  The first half of the table applies to the sub-opcodes where bit 11
   is zero (meaning, "use the A-register"), and the second half applies where
   bit 11 is one (meaning, "use the B-register").

   If some opcodes in a class are defined by a limited set of significant bits,
   if the sub-opcode decoding is not regular, or if the instruction requires two
   words to decode, then a second (secondary) part of the table contains
   classification elements that specify which bits of the opcode are
   significant.  This part is searched linearly.

   The secondary table may contain dependent or independent entries.  In the
   former case, an instruction will match only one of the entries, and the
   search terminates when the match occurs.  An example is the EAG table.  In
   the latter case, an instruction may independently match multiple entries, and
   the search must continue through the end of the table.  Examples are the SRG
   and ASG tables.

   A primary entry that requires additional bits to decode may indicate that the
   secondary section is to be searched by setting a null string ("") as the
   instruction mnemonic.  The end of the opcode table is indicated by a NULL
   mnemonic pointer.

   As an example, the Memory Reference Group of instructions have bits 14-12
   not equal to zero.  These seven values are combined with bit 11 to encode one
   of 14 instructions.  The opcode table consists of 16 primary entries, indexed
   by bits 14-11 (the first two entries are not valid MRG instructions).

   As a contrasting example, the Shift-Rotate Group instructions contain four
   fields.  The first field is defined by bits 8-6 that select one of eight
   shift or rotate operations, plus bit 11 that selects whether the operation
   applies to the A-register or the B-register.  This field is decoded by an
   opcode table consisting of 16 primary entries in two halves.  The first half
   is indexed by bits 8-6 when bit 11 is zero; the second half is indexed by
   bits 8-6 when bit 11 is one.

   The second and third fields use bits 5 and 3 to enable "clear E-register" and
   "skip if the A/B-register is zero" operations, respectively.  These are
   decoded by a secondary table of four entries that is searched linearly.  The
   significant opcode bits masks are used to mask the instruction word to those
   bits that identify the particular instruction.

   The fourth field encodes a second shift/rotate operation using bits 2-0.  The
   opcode table is structured identically to the one for the first field.

   A third example is the I/O Group opcode table.  IOG instructions are
   primarily encoded by bits 8-6.  A few of these sub-opcodes additionally use
   bit 11 to select the A/B-register or bit 9 for additional instruction
   differentiation.  The opcode table consists of a two-part primary section,
   indexed by bits 8-6 and bit 11, and a secondary section that is searched
   linearly to decode the additional bits.

   The content of each opcode table is described by an opcode descriptor.  This
   structure contains the mask and alignment shift to apply to the instruction
   to obtain the primary index, the mask to obtain the A/B-register select bit
   from the instruction, and the CPU feature that must be enabled to enable the
   associated instructions.  If the opcode table contains only secondary
   entries, the mask field contains the OP_LINEAR value, and the shift field
   contains the OP_SINGLE or OP_MULTIPLE value, depending on whether the opcode
   entries are dependent or independent, respectively.  The register selector
   field contains either AB_SELECT or AB_UNUSED values, depending on whether or
   not the instruction uses bit 11 to select between the A and B registers.  The
   required feature field contains a mask that is applied to the user flags
   field of the CPU device to select a CPU type and firmware option set.  For
   example, the descriptor for the I/O Processor opcodes for the 2100 CPU
   contains "UNIT_IOP | CPU_2100" as the required feature field value.


   Implementation notes:

    1. An opcode table containing both primary and secondary entries uses the
       "mask" and "shift" fields to obtain the primary index.  Therefore, the
       alternate use of the "shift" field to designate dependent vs. independent
       secondaries is not available, and secondaries are assumed to be
       dependent.

    2. Two-word instructions have the first word in the lower half of the 32-bit
       "opcode" field, and the second word in the upper half.  The "op_bits"
       field is similarly arranged.

    3. An opcode descriptor and its associated opcode table form an integral
       object.  Ideally, they would be a single structure.  However, while C
       allows a structure to contain a "flexible array member" as the last
       field, which could represent the opcode table array, the array cannot be
       initialized statically.  Therefore, separate structures and arrays are
       used.

    4. Opcode entries with lower-case mnemonics can be printed but will never
       match when parsing (because the command line is upshifted before calling
       the "parse_sym" routine).  Lower-case mnemonics are used for instructions
       with undocumented names (e.g., self-test instructions) and for
       non-canonical opcodes that execute as other, canonical instructions
       (e.g., 105700, which executes as XMM).
*/

#define OP_LINEAR           0u                  /* (mask) table contains linear search entries only */
#define OP_SINGLE           0u                  /* (shift) match only a single linear entry */
#define OP_MULTIPLE         1u                  /* (shift) match multiple linear entries */

#define AB_SELECT           0004000u            /* mask to select the A or B register */
#define AB_UNUSED           0000000u            /* mask to use when the A/B register bit is not used */

#define OPTION_MASK         (UNIT_OPTS | UNIT_MODEL_MASK)   /* feature flags mask */
#define BASE_SET            UNIT_MODEL_MASK                 /* base set feature flags */

typedef struct {                                /* opcode descriptor */
    uint32      mask;                           /*   mask to get opcode selection */
    uint32      shift;                          /*   shift to get the opcode selection */
    uint32      ab_selector;                    /*   mask to get the A/B-register selector */
    uint32      feature;                        /*   feature set to which opcodes apply */
    } OP_DESC;

typedef struct {                                /* opcode table entry */
    const char  *mnemonic;                      /*   symbolic name of the opcode */
    t_value     opcode;                         /*   base value of the opcode */
    OP_TYPE     type;                           /*   type of operand(s) to the opcode */
    t_value     op_bits;                        /*   significant opcode bits mask */
    } OP_ENTRY;

typedef OP_ENTRY OP_TABLE [];                   /* a table of opcode entries */


/* Memory Reference Group.

   The memory reference instructions are indicated by bits 14-12 not equal to
   000.  They are are fully decoded by bits 14-11.  The table consists of 16
   primary entries.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | I |    mem op     | C |            memory address             |  MRG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | I |  mem op   | B | C |            memory address             |  MRG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     I = direct/indirect (0/1)
     B = A/B register (0/1)
     C = base/current page (0/1)

   Memory operation:

     0010 = AND
     0011 = JDB
     0100 = XOR
     0101 = JMP
     0110 = IOR
     0111 = ISZ
     100x = ADA/ADB
     101x = CPA/CPB
     110x = LDA/LDB
     111x = STA/STB


   Implementation notes:

    1. The first two table entries are placeholders for bits 14-11 = 000x.
       These represent Shift-Rotate Group instructions, which will not be
       decoded with this table.

    2. The A/B-register selector descriptor is set to "unused" because the
       primary index includes bit 11.
*/

static const OP_DESC mrg_desc = {               /* Memory Reference Group descriptor */
    0074000u,                                   /*   opcode mask */
    11u,                                        /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    BASE_SET | CPU_ALL                          /*   applicable feature flags */
    };

static const OP_TABLE mrg_ops = {               /* MRG opcodes, indexed by IR bits 14-11 */
    { "",    0000000u, opNone },                /* SRG */
    { "",    0004000u, opNone },                /* SRG */
    { "AND", 0010000u, opMPOI },
    { "JSB", 0014000u, opMPOI },
    { "XOR", 0020000u, opMPOI },
    { "JMP", 0024000u, opMPOI },
    { "IOR", 0030000u, opMPOI },
    { "ISZ", 0034000u, opMPOI },
    { "ADA", 0040000u, opMPOI },
    { "ADB", 0044000u, opMPOI },
    { "CPA", 0050000u, opMPOI },
    { "CPB", 0054000u, opMPOI },
    { "LDA", 0060000u, opMPOI },
    { "LDB", 0064000u, opMPOI },
    { "STA", 0070000u, opMPOI },
    { "STB", 0074000u, opMPOI },

    { NULL }
    };


/* Shift-Rotate Group.

   The shift-rotate instructions are indicated by bits 15, 14-12, and 10 all
   equal to 0.  They are decoded in three parts.  First, if bit 9 is 1, then
   bits 11 + 8-6 fully decode one of 16 shifts or rotations.  Second, bits 5 and
   11 + 3 independently decode the CLE and SLA/SLB operations.  Third, if bit 4
   is 1, then bits 11 + 2-0 fully decode one of 16 shifts or rotations.

   Three tables are used: two to decode the shifts or rotates, and a third to
   decode the CLE and SLA/SLB operations, plus the NOP that results when all IR
   bits are zero.  The first and third tables consist of 16 primary entries.
   The second consists of four independent secondary entries.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | B | 0 | E |   op 1    | C | E | S |   op 2    |  SRG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     B = A/B register (0/1)
     E = disable/enable op
     C = CLE
     S = SLA/B

   Shift/Rotate operation:

     000 = ALS/BLS
     001 = ARS/BRS
     010 = RAL/RBL
     011 = RAR/RBR
     100 = ALR/BLR
     101 = ERA/ERB
     110 = ELA/ELB
     111 = ALF/BLF


   Implementation notes:

    1. The srg1_ops and srg2_ops tables contain only primary entries.  Normally,
       primary entries don't define the op_bits fields.  In these cases,
       however, they are required to provide A/B conflict detection among the
       two SRG operations and the SLA/SLB micro-op when parsing.

    2. An SRG instruction executes as NOP if both shift-rotate operation fields
       are disabled and the CLE and SLA/B fields are zero.  In these cases, all
       remaining bits are "don't care."  The "NOP" entry in the micro-ops table
       catches these cases.
*/

static const OP_DESC srg1_desc = {              /* Shift-Rotate Group first descriptor */
    0000700u,                                   /*   opcode mask */
    6u,                                         /*   opcode shift */
    AB_SELECT,                                  /*   A/B-register selector */
    BASE_SET | CPU_ALL                          /*   applicable feature flags */
    };

static const OP_TABLE srg1_ops = {              /* SRG opcodes, indexed by IR bits 11 + 8-6 */
    { "ALS", 0001000u, opNone, 0005700u },
    { "ARS", 0001100u, opNone, 0005700u },
    { "RAL", 0001200u, opNone, 0005700u },
    { "RAR", 0001300u, opNone, 0005700u },
    { "ALR", 0001400u, opNone, 0005700u },
    { "ERA", 0001500u, opNone, 0005700u },
    { "ELA", 0001600u, opNone, 0005700u },
    { "ALF", 0001700u, opNone, 0005700u },

    { "BLS", 0005000u, opNone, 0005700u },
    { "BRS", 0005100u, opNone, 0005700u },
    { "RBL", 0005200u, opNone, 0005700u },
    { "RBR", 0005300u, opNone, 0005700u },
    { "BLR", 0005400u, opNone, 0005700u },
    { "ERB", 0005500u, opNone, 0005700u },
    { "ELB", 0005600u, opNone, 0005700u },
    { "BLF", 0005700u, opNone, 0005700u },

    { NULL }
    };

static const OP_DESC srg_udesc = {              /* Shift-Rotate Group micro-ops descriptor */
    OP_LINEAR,                                  /*   linear search only */
    OP_MULTIPLE,                                /*   multiple matches allowed */
    AB_UNUSED,                                  /*   A/B-register selector */
    BASE_SET | CPU_ALL                          /*   applicable feature flags */
    };

static const OP_TABLE srg_uops = {              /* SRG micro-opcodes, searched linearly */
    { "CLE", 0000040u, opNone, 0000040u },
    { "SLA", 0000010u, opNone, 0004010u },
    { "SLB", 0004010u, opNone, 0004010u },
    { "NOP", 0000000u, opNone, 0173070u },

    { NULL }
    };

static const OP_DESC srg2_desc = {              /* Shift-Rotate Group second descriptor */
    0000007u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_SELECT,                                  /*   A/B-register selector */
    BASE_SET | CPU_ALL                          /*   applicable feature flags */
    };

static const OP_TABLE srg2_ops = {              /* SRG opcodes, indexed by IR bits 11 + 2-0 */
    { "ALS", 0000020u, opNone, 0004027u },
    { "ARS", 0000021u, opNone, 0004027u },
    { "RAL", 0000022u, opNone, 0004027u },
    { "RAR", 0000023u, opNone, 0004027u },
    { "ALR", 0000024u, opNone, 0004027u },
    { "ERA", 0000025u, opNone, 0004027u },
    { "ELA", 0000026u, opNone, 0004027u },
    { "ALF", 0000027u, opNone, 0004027u },

    { "BLS", 0004020u, opNone, 0004027u },
    { "BRS", 0004021u, opNone, 0004027u },
    { "RBL", 0004022u, opNone, 0004027u },
    { "RBR", 0004023u, opNone, 0004027u },
    { "BLR", 0004024u, opNone, 0004027u },
    { "ERB", 0004025u, opNone, 0004027u },
    { "ELB", 0004026u, opNone, 0004027u },
    { "BLF", 0004027u, opNone, 0004027u },

    { NULL }
    };


/* Alter-Skip Group.

   The alter-skip instructions are indicated by bits 15 and 14-12 equal to 0
   and bit 10 equal to 1.  All of the operations are independent, and the table
   consists of independent secondary entries for each operation, plus a final
   entry for the instruction that enables none of the operations.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0 | 0   0   0 | B | 1 | a op  | e op  | E | S | L | I | Z | V |  ASG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     B = A/B register (0/1)
     E = SEZ
     S = SSA/B
     L = SLA/B
     I = INA/B
     Z = SZA/B
     V = RSS

   Accumulator operation:

     00 = NOP
     01 = CLA/CLB
     10 = CMA/CMB
     11 = CCA/CCB

   Extend operation:

     00 = NOP
     01 = CLE
     10 = CME
     11 = CCE


   Implementation notes:

    1. The CLE/CME/CCE, SEZ, and RSS fields do not depend on the A/B register
       select bit.  This bit is "don't care" if only these fields appear in the
       instruction.  The entries in the table catch these cases.
*/

static const OP_DESC asg_udesc = {              /* Alter-Skip Group micro-ops descriptor */
    OP_LINEAR,                                  /*   linear search only */
    OP_MULTIPLE,                                /*   multiple matches allowed */
    AB_UNUSED,                                  /*   A/B-register selector */
    BASE_SET | CPU_ALL                          /*   applicable feature flags */
    };

static const OP_TABLE asg_uops = {              /* ASG micro-opcodes, searched linearly */
    { "CLA", 0002400u, opNone, 0007400u },
    { "CMA", 0003000u, opNone, 0007400u },
    { "CCA", 0003400u, opNone, 0007400u },
    { "CLB", 0006400u, opNone, 0007400u },
    { "CMB", 0007000u, opNone, 0007400u },
    { "CCB", 0007400u, opNone, 0007400u },

    { "SEZ", 0002040u, opNone, 0002040u },

    { "CLE", 0002100u, opNone, 0002300u },
    { "CME", 0002200u, opNone, 0002300u },
    { "CCE", 0002300u, opNone, 0002300u },

    { "SSA", 0002020u, opNone, 0006020u },
    { "SLA", 0002010u, opNone, 0006010u },
    { "INA", 0002004u, opNone, 0006004u },
    { "SZA", 0002002u, opNone, 0006002u },
    { "SSB", 0006020u, opNone, 0006020u },
    { "SLB", 0006010u, opNone, 0006010u },
    { "INB", 0006004u, opNone, 0006004u },
    { "SZB", 0006002u, opNone, 0006002u },

    { "RSS", 0002001u, opNone, 0002001u },
    { "NOP", 0002000u, opNone, 0173777u },

    { NULL }
    };


/* I/O Group.

   The I/O instructions are indicated by bits 14-12 equal to 0 and bits 15 and
   10 equal to 1.  They are fully decoded by bits 11 + 9-6.  However, the HP
   assembler assigns special mnemonics to the flag instructions that reference
   select code 1 (the overflow register).  Therefore, secondary entries are used
   to provide the flag mnemonics, based on the select code employed.

   Because only the flag instructions use bit 9 to differentiate between
   instructions, and because the flag instructions are all relegated to
   secondary entries, the primary entries need not be indexed with bit 9.
   Therefore, the table consists of 16 primary entries and 8 secondary entries
   for the flag instructions.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | B | 1 | C |  i/o op   |      select code      |  IOG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     B = A/B register (0/1)
     C = hold/clear flag (0/1)

   I/O operation:

     000 = HLT
     001 = STF/CLF
     010 = SFC
     011 = SFS
     100 = MIA/MIB
     101 = LIA/LIB
     110 = OTA/OTB
     111 = STC/CLC


   Implementation notes:

    1. Bit 11 is "don't care" for the HLT, STF, CLF, SFC, and SFS instructions.

    2. Bit 9, the "hold/clear" bit, is defined as 0 for the SFC and SFS
       instructions; however, setting bit 9 to 1 will clear the flag after
       testing (RTE depends on this behavior in $CIC, where it is used to test
       the state of the interrupt system and turn it off in the same
       instruction).  Therefore, these entries use the "opSCHC" operand type
       instead of the expected "opSC" type.

    3. The select code may be omitted when entering the HLT instruction (it
       defaults to 0), so a special operand code is needed when parsing this
       case.
*/

static const OP_DESC iog_desc = {               /* Input/Output Group descriptor */
    0000700u,                                   /*   opcode mask */
    6u,                                         /*   opcode shift */
    AB_SELECT,                                  /*   A/B-register selector */
    BASE_SET | CPU_ALL                          /*   applicable feature flags */
    };

static const OP_TABLE iog_ops = {               /* IOG opcodes, indexed by IR bits 11 + 8-6 */
    { "HLT", 0102000u, opSCOHC },
    { "",    0102100u, opNone  },               /* STF/CLF and STO/CLO */
    { "",    0102200u, opNone  },               /* SFC and SOC */
    { "",    0102300u, opNone  },               /* SFS and SOS */
    { "MIA", 0102400u, opSCHC  },
    { "LIA", 0102500u, opSCHC  },
    { "OTA", 0102600u, opSCHC  },
    { "STC", 0102700u, opSCHC  },

    { "HLT", 0106000u, opSCOHC },
    { "",    0106100u, opNone  },               /* STF/CLF and STO/CLO */
    { "",    0106200u, opNone  },               /* SFC and SOC */
    { "",    0106300u, opNone  },               /* SFS and SOS */
    { "MIB", 0106400u, opSCHC  },
    { "LIB", 0106500u, opSCHC  },
    { "OTB", 0106600u, opSCHC  },
    { "CLC", 0106700u, opSCHC  },

    { "STO", 0102101u, opNone, 0173777u },      /* STF 01 */
    { "STF", 0102100u, opSC,   0173700u },      /* STF nn */
    { "CLO", 0103101u, opNone, 0173777u },      /* CLF 01 */
    { "CLF", 0103100u, opSC,   0173700u },      /* CLF nn */
    { "SOC", 0102201u, opHC,   0172777u },      /* SFC 01 */
    { "SFC", 0102200u, opSCHC, 0172700u },      /* SFC nn */
    { "SOS", 0102301u, opHC,   0172777u },      /* SFS 01 */
    { "SFS", 0102300u, opSCHC, 0172700u },      /* SFS nn */

    { NULL }
    };


/* Extended Arithmetic Group.

   The EAG instructions are part of the MAC instruction space, which is
   indicated by bit 15 equal to 1 and bits 14-12 and 10 equal to 0.  They are
   enabled by the Extended Arithmetic Unit feature that is optional for the 2116
   and standard for the 2100 and 1000-M/E/F.  There are ten canonical EAG
   instructions.  Four arithmetic instructions are decoded by bits 11 and 9-6
   with bits 5-0 equal to 0.  Six shift-rotate instructions are decoded by bits
   11 and 9-4 with bits 3-0 indicating the shift count.  The remaining 118 bit
   combinations are undefined.

   Three of these undefined combinations are reserved in the 1000 E/F-Series.
   Opcode 100000 is the DIAG instruction, 100060 is the TIMER instruction, and
   100120 is the EXECUTE instruction.  The simulator implements DIAG and TIMER;
   EXECUTE does not appear to have been implemented in the original microcode
   and is not simulated.

   Results of execution of the other undefined instructions are dependent on the
   CPU model.  Rather than implementing each machine's specific behavior for
   each instruction, the simulator follows the 1000 M-Series decoding,
   regardless of the CPU model.

   The table consists of 11 secondary entries; the final entry allows parsing of
   the SWP mnemonic and encoding as RRR 16 as is permitted by the HP Assembler.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 |   | 0 |    eau op     | 0   0   0   0   0   0 |  EAG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 |   | 0 | eau shift/rotate op   |  shift count  |  EAG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The MAC instruction space also includes the User Instruction Group.  EAG
   instructions use the opcode ranges 100000-101377 and 104000-104777.  UIG
   instructions use the ranges 101400-101477 and 105000-105777.


   Implementation notes:

    1. The 2100 has the strictest decoding of the EAU set, so that is the
       encoding represented in the operations table.
*/

static const OP_DESC eag_desc = {               /* Extended Arithmetic Group descriptor */
    OP_LINEAR,                                  /*   linear search only */
    OP_SINGLE,                                  /*   single match allowed */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_EAU | CPU_ALL                          /*   applicable feature flags */
    };

static const OP_TABLE eag_ops = {               /* EAG opcodes, searched linearly */
    { "MPY", 0100200u, opMA1I,  0177760u },
    { "DIV", 0100400u, opMA1I,  0177760u },
    { "DLD", 0104200u, opMA1I,  0177760u },
    { "DST", 0104400u, opMA1I,  0177760u },

    { "ASL", 0100020u, opShift, 0177760u },
    { "LSL", 0100040u, opShift, 0177760u },
    { "RRL", 0100100u, opShift, 0177760u },
    { "ASR", 0101020u, opShift, 0177760u },
    { "LSR", 0101040u, opShift, 0177760u },
    { "RRR", 0101100u, opShift, 0177760u },
    { "SWP", 0101100u, opNone,  0177777u },     /* SWP is equivalent to RRR 16 */

    { NULL }
    };

static const OP_DESC eag_ef_desc = {            /* Extended Arithmetic Group 1000-E/F descriptor */
    OP_LINEAR,                                  /*   linear search only */
    OP_SINGLE,                                  /*   single match allowed */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_EAU | CPU_1000_E | CPU_1000_F          /*   applicable feature flags */
    };

static const OP_TABLE eag_ef_ops = {            /* EAG opcodes, searched linearly */
    { "DIAG",  0100000u, opNone, 0177777u },
    { "TIMER", 0100060u, opNone, 0177777u },

    { NULL }
    };


/* User Instruction Group.

   The UIG instructions are part of the MAC instruction space, which is
   indicated by bit 15 equal to 1 and bits 14-12 and 10 equal to 0.  They are
   divided into two sub-groups.  The first, UIG-0, occupies opcodes
   105000-105377.  The second, UIG-1, occupies opcodes 101400-101777 and
   105400-105777.  The 2100 decodes only UIG-0 instructions, whereas the 1000s
   use both UIG sets.  In particular, the 105740-105777 range is used by the
   1000 Extended Instruction Group (EIG), which is part of the base set.

   All of the optional firmware sets for the 2100 and 1000-M/E/F CPUs implement
   UIG instructions.  Therefore, there are separate tables for each firmware
   feature.

      15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1 | 0   0   0 | B | 0   1 |    module     |     operation     |  UIG
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   Where:

     B = A/B register (0/1)

   For instructions in the range 10x400-10x777, bit 11 optionally may be
   significant.  If it is, then the 101xxx and 105xxx instructions are distinct
   and require separate table entries.  If it is not, then either 105xxx or
   101xxx may be used to invoke the instruction, although 105xxx is the
   canonical form.


   Implementation notes:

    1. If bit 11 is significant for some instructions and not for others, then
       the instructions that ignore bit 11 must be duplicated in the table and
       given the 105xxx forms in both sets of entries.  This ensures that when
       used as a parsing table, the canonical form is obtained.
*/


/* 105000-105362  2000 I/O Processor Instructions */

static const OP_DESC iop_2100_desc = {          /* 2100 I/O Processor descriptor */
    OP_LINEAR,                                  /*   linear search only */
    OP_SINGLE,                                  /*   single match allowed */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_IOP | CPU_2100                         /*   applicable feature flags */
    };

static const OP_TABLE iop_2100_ops = {          /* 2100 IOP opcodes, searched linearly */
    { "SBYTE", 0105300u, opNone,  0177777u },
    { "LBYTE", 0105320u, opNone,  0177777u },
    { "MBYTE", 0105120u, opMA1I,  0177777u },
    { "MWORD", 0105200u, opMA1ZI, 0177777u },

    { "LAI",   0105020u, opIOPON, 0177777u },   /* these must be split into negative and positive operands */
    { "LAI",   0105040u, opIOPOP, 0177777u },   /*   because the offset does not occupy bits */
    { "SAI",   0105060u, opIOPON, 0177777u },   /*     reserved for the value but instead are */
    { "SAI",   0105100u, opIOPOP, 0177777u },   /*       ADDED to the base opcode value */

    { "CRC",   0105150u, opV1,    0177777u },
    { "RESTR", 0105340u, opNone,  0177777u },
    { "READF", 0105220u, opNone,  0177777u },
    { "ENQ",   0105240u, opNone,  0177777u },
    { "PENQ",  0105257u, opNone,  0177777u },
    { "DEQ",   0105260u, opNone,  0177777u },
    { "TRSLT", 0105160u, opV1,    0177777u },
    { "ILIST", 0105000u, opA1V1,  0177777u },
    { "PRFEI", 0105222u, opV2A1,  0177777u },
    { "PRFEX", 0105223u, opMA1I,  0177777u },
    { "PRFIO", 0105221u, opV2,    0177777u },
    { "SAVE",  0105362u, opNone,  0177777u },

    { NULL }
    };


/* 105000-105137  Single-Precision Floating Point Instructions */

static const OP_DESC fp_desc = {                    /* Single-Precision Floating Point descriptor */
    OP_LINEAR,                                      /*   linear search only */
    OP_SINGLE,                                      /*   single match allowed */
    AB_UNUSED,                                      /*   A/B-register selector */
    UNIT_FP | CPU_2100 | CPU_1000_M | CPU_1000_E    /*   applicable feature flags */
    };

static const OP_TABLE fp_ops = {                    /* FP opcodes, searched linearly */
    { "FAD", 0105000u, opMA1I, 0177760u },          /* bits 3-0 do not affect decoding */
    { "FSB", 0105020u, opMA1I, 0177760u },          /* bits 3-0 do not affect decoding */
    { "FMP", 0105040u, opMA1I, 0177760u },          /* bits 3-0 do not affect decoding */
    { "FDV", 0105060u, opMA1I, 0177760u },          /* bits 3-0 do not affect decoding */
    { "FIX", 0105100u, opNone, 0177760u },          /* bits 3-0 do not affect decoding */
    { "FLT", 0105120u, opNone, 0177760u },          /* bits 3-0 do not affect decoding */

    { NULL }
    };


/* 105000-105137  Floating Point Processor Instructions */

static const OP_DESC fpp_desc = {               /* Floating Point Processor descriptor */
    0000137u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_FP | CPU_1000_F                        /*   applicable feature flags */
    };

static const OP_TABLE fpp_ops = {               /* FPP opcodes, indexed by IR bits 6-0 */
    { "FAD",   0105000u, opMA1I },
    { ".XADD", 0105001u, opMA3I },
    { ".TADD", 0105002u, opMA3I },
    { ".EADD", 0105003u, opMA3I },
    { "fptst", 0105004u, opNone },              /* self-test */
    { "xexp",  0105005u, opNone },              /* expand exponent */
    { "reset", 0105006u, opNone },              /* processor reset */
    { "exstk", 0105007u, opNone },              /* execute a stack of operands */
    { "adchk", 0105010u, opNone },              /* addressing check */
    { "",      0105011u, opNone },              /* unimplemented */
    { "",      0105012u, opNone },              /* unimplemented */
    { "",      0105013u, opNone },              /* unimplemented */
    { ".DAD",  0105014u, opMA1I },
    { "",      0105015u, opNone },              /* unimplemented */
    { "",      0105016u, opNone },              /* unimplemented */
    { "",      0105017u, opNone },              /* unimplemented */
    { "FSB",   0105020u, opMA1I },
    { ".XSUB", 0105021u, opMA3I },
    { ".TSUB", 0105022u, opMA3I },
    { ".ESUB", 0105023u, opMA3I },
    { "",      0105024u, opNone },              /* unimplemented */
    { "",      0105025u, opNone },              /* unimplemented */
    { "",      0105026u, opNone },              /* unimplemented */
    { "",      0105027u, opNone },              /* unimplemented */
    { "",      0105030u, opNone },              /* unimplemented */
    { "",      0105031u, opNone },              /* unimplemented */
    { "",      0105032u, opNone },              /* unimplemented */
    { "",      0105033u, opNone },              /* unimplemented */
    { ".DSB",  0105034u, opMA1I },
    { "",      0105035u, opNone },              /* unimplemented */
    { "",      0105036u, opNone },              /* unimplemented */
    { "",      0105037u, opNone },              /* unimplemented */
    { "FMP",   0105040u, opMA1I },
    { ".XMPY", 0105041u, opMA3I },
    { ".TMPY", 0105042u, opMA3I },
    { ".EMPY", 0105043u, opMA3I },
    { "",      0105044u, opNone },              /* unimplemented */
    { "",      0105045u, opNone },              /* unimplemented */
    { "",      0105046u, opNone },              /* unimplemented */
    { "",      0105047u, opNone },              /* unimplemented */
    { "",      0105050u, opNone },              /* unimplemented */
    { "",      0105051u, opNone },              /* unimplemented */
    { "",      0105052u, opNone },              /* unimplemented */
    { "",      0105053u, opNone },              /* unimplemented */
    { ".DMP",  0105054u, opMA1I },
    { "",      0105055u, opNone },              /* unimplemented */
    { "",      0105056u, opNone },              /* unimplemented */
    { "",      0105057u, opNone },              /* unimplemented */
    { "FDV",   0105060u, opMA1I },
    { ".XDIV", 0105061u, opMA3I },
    { ".TDIV", 0105062u, opMA3I },
    { ".EDIV", 0105063u, opMA3I },
    { "",      0105064u, opNone },              /* unimplemented */
    { "",      0105065u, opNone },              /* unimplemented */
    { "",      0105066u, opNone },              /* unimplemented */
    { "",      0105067u, opNone },              /* unimplemented */
    { "",      0105070u, opNone },              /* unimplemented */
    { "",      0105071u, opNone },              /* unimplemented */
    { "",      0105072u, opNone },              /* unimplemented */
    { "",      0105073u, opNone },              /* unimplemented */
    { ".DDI",  0105074u, opMA1I },
    { "",      0105075u, opNone },              /* unimplemented */
    { "",      0105076u, opNone },              /* unimplemented */
    { "",      0105077u, opNone },              /* unimplemented */
    { "FIX",   0105100u, opNone },
    { ".XFXS", 0105101u, opMA1I },
    { ".TXFS", 0105102u, opMA1I },
    { ".EXFS", 0105103u, opMA1I },
    { ".FIXD", 0105104u, opNone },
    { ".XFXD", 0105105u, opMA1I },
    { ".TFXD", 0105106u, opMA1I },
    { ".EFXD", 0105107u, opMA1I },
    { "",      0105110u, opNone },              /* unimplemented */
    { "",      0105111u, opNone },              /* unimplemented */
    { "",      0105112u, opNone },              /* unimplemented */
    { "",      0105113u, opNone },              /* unimplemented */
    { ".DSBR", 0105114u, opMA1I },
    { "",      0105115u, opNone },              /* unimplemented */
    { "",      0105116u, opNone },              /* unimplemented */
    { "",      0105117u, opNone },              /* unimplemented */
    { "FLT",   0105120u, opNone },
    { ".XFTS", 0105121u, opMA1I },
    { ".TFTS", 0105122u, opMA1I },
    { ".EFTS", 0105123u, opMA1I },
    { ".FLTD", 0105124u, opNone },
    { ".XFTD", 0105125u, opMA1I },
    { ".TFTD", 0105126u, opMA1I },
    { ".EFTD", 0105127u, opMA1I },
    { "",      0105130u, opNone },              /* unimplemented */
    { "",      0105131u, opNone },              /* unimplemented */
    { "",      0105132u, opNone },              /* unimplemented */
    { "",      0105133u, opNone },              /* unimplemented */
    { ".DDIR", 0105134u, opMA1I },
    { "",      0105135u, opNone },              /* unimplemented */
    { "",      0105136u, opNone },              /* unimplemented */
    { "",      0105137u, opNone },              /* unimplemented */

    { NULL }
    };


/* 105200-105237  Fast FORTRAN Processor Instructions */

static const OP_DESC ffp_2100_desc = {          /* 2100 Fast FORTRAN Processor descriptor */
    0000037u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_FFP | CPU_2100                         /*   applicable feature flags */
    };

static const OP_TABLE ffp_2100_ops = {          /* 2100 FFP opcodes, indexed by IR bits 4-0 */
    { "",      0105200u, opNone },              /* unimplemented */
    { "DBLE",  0105201u, opMA3I },
    { "SNGL",  0105202u, opMA2I },
    { ".XMPY", 0105203u, opMA3I },
    { ".XDIV", 0105204u, opMA3I },
    { ".DFER", 0105205u, opMA2I },
    { "",      0105206u, opNone },              /* unimplemented */
    { "",      0105207u, opNone },              /* unimplemented */

    { "",      0105210u, opNone },              /* unimplemented */
    { "",      0105211u, opNone },              /* unimplemented */
    { "",      0105212u, opNone },              /* unimplemented */
    { ".XADD", 0105213u, opMA3I },
    { ".XSUB", 0105214u, opMA3I },
    { "",      0105215u, opNone },              /* unimplemented */
    { "",      0105216u, opNone },              /* unimplemented */
    { "",      0105217u, opNone },              /* unimplemented */

    { ".XFER", 0105220u, opNone },
    { ".GOTO", 0105221u, opMA2I },
    { "..MAP", 0105222u, opMA4I },
    { ".ENTR", 0105223u, opMA1I },
    { ".ENTP", 0105224u, opMA1I },
    { "",      0105225u, opNone },              /* unimplemented */
    { "",      0105226u, opNone },              /* unimplemented */
    { "$SETP", 0105227u, opMA1I },

    { "",      0105230u, opNone },              /* unimplemented */
    { "",      0105231u, opNone },              /* unimplemented */
    { "",      0105232u, opNone },              /* unimplemented */
    { "",      0105233u, opNone },              /* unimplemented */
    { "",      0105234u, opNone },              /* unimplemented */
    { "",      0105235u, opNone },              /* unimplemented */
    { "",      0105236u, opNone },              /* unimplemented */
    { "",      0105237u, opNone },              /* unimplemented */

    { NULL }
    };

static const OP_DESC ffp_m_desc = {             /* M-Series Fast FORTRAN Processor descriptor */
    0000037u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_FFP | CPU_1000_M                       /*   applicable feature flags */
    };

static const OP_TABLE ffp_m_ops = {             /* M-Series FFP opcodes, indexed by IR bits 4-0 */
    { "fftst", 0105200u, opNone },              /* self-test */
    { "DBLE",  0105201u, opMA3I },
    { "SNGL",  0105202u, opMA2I },
    { ".XMPY", 0105203u, opMA3I },
    { ".XDIV", 0105204u, opMA3I },
    { ".DFER", 0105205u, opMA2I },
    { ".XPAK", 0105206u, opMA1I },
    { "XADD",  0105207u, opMA4I },

    { "XSUB",  0105210u, opMA4I },
    { "XMPY",  0105211u, opMA4I },
    { "XDIV",  0105212u, opMA4I },
    { ".XADD", 0105213u, opMA3I },
    { ".XSUB", 0105214u, opMA3I },
    { ".XCOM", 0105215u, opMA1I },
    { "..DCM", 0105216u, opMA1I },
    { "DDINT", 0105217u, opMA3I },

    { ".XFER", 0105220u, opNone },
    { ".GOTO", 0105221u, opMA2I },
    { "..MAP", 0105222u, opMA4I },
    { ".ENTR", 0105223u, opMA1I },
    { ".ENTP", 0105224u, opMA1I },
    { ".PWR2", 0105225u, opMA2I },
    { ".FLUN", 0105226u, opMA1I },
    { "$SETP", 0105227u, opMA1I },

    { ".PACK", 0105230u, opMA2I },
    { "",      0105231u, opNone },              /* unimplemented */
    { "",      0105232u, opNone },              /* unimplemented */
    { "",      0105233u, opNone },              /* unimplemented */
    { "",      0105234u, opNone },              /* unimplemented */
    { "",      0105235u, opNone },              /* unimplemented */
    { "",      0105236u, opNone },              /* unimplemented */
    { "",      0105237u, opNone },              /* unimplemented */

    { NULL }
    };

static const OP_DESC ffp_e_desc = {             /* E-Series Fast FORTRAN Processor descriptor */
    0000037u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_FFP | CPU_1000_E                       /*   applicable feature flags */
    };

static const OP_TABLE ffp_e_ops = {             /* E-Series FFP opcodes, indexed by IR bits 4-0 */
    { "fftst", 0105200u, opNone },              /* self-test */
    { "DBLE",  0105201u, opMA3I },
    { "SNGL",  0105202u, opMA2I },
    { ".XMPY", 0105203u, opMA3I },
    { ".XDIV", 0105204u, opMA3I },
    { ".DFER", 0105205u, opMA2I },
    { ".XPAK", 0105206u, opMA1I },
    { "XADD",  0105207u, opMA4I },

    { "XSUB",  0105210u, opMA4I },
    { "XMPY",  0105211u, opMA4I },
    { "XDIV",  0105212u, opMA4I },
    { ".XADD", 0105213u, opMA3I },
    { ".XSUB", 0105214u, opMA3I },
    { ".XCOM", 0105215u, opMA1I },
    { "..DCM", 0105216u, opMA1I },
    { "DDINT", 0105217u, opMA3I },

    { ".XFER", 0105220u, opNone },
    { ".GOTO", 0105221u, opMA2I },
    { "..MAP", 0105222u, opMA4I },
    { ".ENTR", 0105223u, opMA1I },
    { ".ENTP", 0105224u, opMA1I },
    { ".PWR2", 0105225u, opMA2I },
    { ".FLUN", 0105226u, opMA1I },
    { "$SETP", 0105227u, opMA1I },

    { ".PACK", 0105230u, opMA2I },
    { ".CFER", 0105231u, opMA2I },
    { "",      0105232u, opNone },              /* unimplemented */
    { "",      0105233u, opNone },              /* unimplemented */
    { "",      0105234u, opNone },              /* unimplemented */
    { "",      0105235u, opNone },              /* unimplemented */
    { "",      0105236u, opNone },              /* unimplemented */
    { "",      0105237u, opNone },              /* unimplemented */

    { NULL }
    };

static const OP_DESC ffp_f_desc = {             /* F-Series Fast FORTRAN Processor descriptor */
    0000037u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_FFP | CPU_1000_F                       /*   applicable feature flags */
    };

static const OP_TABLE ffp_f_ops = {             /* F-Series FFP opcodes, indexed by IR bits 4-0 */
    { "fftst", 0105200u, opNone },              /* self-test */
    { "DBLE",  0105201u, opMA3I },
    { "SNGL",  0105202u, opMA2I },
    { ".DNG",  0105203u, opNone },
    { ".DCO",  0105204u, opNone },
    { ".DFER", 0105205u, opMA2I },
    { ".XPAK", 0105206u, opMA1I },
    { ".BLE",  0105207u, opMA3I },

    { ".DIN",  0105210u, opNone },
    { ".DDE",  0105211u, opNone },
    { ".DIS",  0105212u, opNone },
    { ".DDS",  0105213u, opNone },
    { ".NGL",  0105214u, opMA2I },
    { ".XCOM", 0105215u, opMA1I },
    { "..DCM", 0105216u, opMA1I },
    { "DDINT", 0105217u, opMA3I },

    { ".XFER", 0105220u, opNone },
    { ".GOTO", 0105221u, opMA2I },
    { "..MAP", 0105222u, opMA4I },
    { ".ENTR", 0105223u, opMA1I },
    { ".ENTP", 0105224u, opMA1I },
    { ".PWR2", 0105225u, opMA2I },
    { ".FLUN", 0105226u, opMA1I },
    { "$SETP", 0105227u, opMA1I },

    { ".PACK", 0105230u, opMA2I },
    { ".CFER", 0105231u, opMA2I },
    { "..FCM", 0105232u, opMA1I },
    { "..TCM", 0105233u, opMA1I },
    { "",      0105234u, opNone },              /* unimplemented */
    { "",      0105235u, opNone },              /* unimplemented */
    { "",      0105236u, opNone },              /* unimplemented */
    { "",      0105237u, opNone },              /* unimplemented */

    { NULL }
    };


/* 105240-105257  RTE-IVA/B Extended Memory Instructions */

static const OP_DESC ema_desc = {               /* Extended Memory Area descriptor */
    0000017u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_EMA | CPU_1000_E | CPU_1000_F          /*   applicable feature flags */
    };

static const OP_TABLE ema_ops = {               /* EMA opcodes, indexed by IR bits 3-0 */
    { ".EMIO", 0105240u, opMA3I },              /* variable operand count */
    { "MMAP",  0105241u, opMA3I },
    { "emtst", 0105242u, opNone },              /* self-test */
    { "",      0105243u, opNone },              /* unimplemented */
    { "",      0105244u, opNone },              /* unimplemented */
    { "",      0105245u, opNone },              /* unimplemented */
    { "",      0105246u, opNone },              /* unimplemented */
    { "",      0105247u, opNone },              /* unimplemented */

    { "",      0105250u, opNone },              /* unimplemented */
    { "",      0105251u, opNone },              /* unimplemented */
    { "",      0105252u, opNone },              /* unimplemented */
    { "",      0105253u, opNone },              /* unimplemented */
    { "",      0105254u, opNone },              /* unimplemented */
    { "",      0105255u, opNone },              /* unimplemented */
    { "",      0105256u, opNone },              /* unimplemented */
    { ".EMAP", 0105257u, opMA3I },              /* variable operand count */

    { NULL }
    };


/* 105240-105257  RTE-6/VM Virtual Memory Instructions */

static const OP_DESC vma_desc = {               /* Virtual Memory Area descriptor */
    0000017u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_VMAOS | CPU_1000_E | CPU_1000_F        /*   applicable feature flags */
    };

static const OP_TABLE vma_ops = {               /* VMA opcodes, indexed by IR bits 3-0 */
    { ".PMAP", 0105240u, opNone },
    { "$LOC",  0105241u, opMA6I },
    { "vmtst", 0105242u, opNone },              /* self-test */
    { ".SWP",  0105243u, opNone },
    { ".STAS", 0105244u, opNone },
    { ".LDAS", 0105245u, opNone },
    { "",      0105246u, opNone },              /* unimplemented */
    { ".UMPY", 0105247u, opMA1I },

    { ".IMAP", 0105250u, opMA1I },              /* operand count varies from 1-n */
    { ".IMAR", 0105251u, opMA1I },
    { ".JMAP", 0105252u, opMA1I },              /* operand count varies from 1-n */
    { ".JMAR", 0105253u, opMA1I },
    { ".LPXR", 0105254u, opMA2I },
    { ".LPX",  0105255u, opMA1I },
    { ".LBPR", 0105256u, opMA1I },
    { ".LBP",  0105257u, opNone },

    { NULL }
    };


/* 105320-105337  Double Integer Instructions */

static const OP_DESC dbi_desc = {               /* Double Integer Instructions descriptor */
    0000017u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_DBI | CPU_1000_E                       /*   applicable feature flags */
    };

static const OP_TABLE dbi_ops = {               /* DBI opcodes, indexed by IR bits 3-0 */
    { "dbtst", 0105320u, opNone },              /* self-test */
    { ".DAD",  0105321u, opMA1I },
    { ".DMP",  0105322u, opMA1I },
    { ".DNG",  0105323u, opNone },
    { ".DCO",  0105324u, opMA1I },
    { ".DDI",  0105325u, opMA1I },
    { ".DDIR", 0105326u, opMA1I },
    { ".DSB",  0105327u, opMA1I },
    { ".DIN",  0105330u, opNone },
    { ".DDE",  0105331u, opNone },
    { ".DIS",  0105332u, opMA1I },
    { ".DDS",  0105333u, opMA1I },
    { ".DSBR", 0105334u, opMA1I },
    { "",      0105335u, opNone },              /* unimplemented */
    { "",      0105336u, opNone },              /* unimplemented */
    { "",      0105337u, opNone },              /* unimplemented */

    { NULL }
    };


/* 105320-105337  Scientific Instruction Set */

static const OP_DESC sis_desc = {               /* Scientific Instruction Set descriptor */
    0000017u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    BASE_SET | CPU_1000_F                       /*   applicable feature flags */
    };

static const OP_TABLE sis_ops = {               /* SIS opcodes, indexed by IR bits 3-0 */
    { "TAN",   0105320u, opNone },
    { "SQRT",  0105321u, opNone },
    { "ALOG",  0105322u, opNone },
    { "ATAN",  0105323u, opNone },
    { "COS",   0105324u, opNone },
    { "SIN",   0105325u, opNone },
    { "EXP",   0105326u, opNone },
    { "ALOGT", 0105327u, opNone },
    { "TANH",  0105330u, opNone },
    { "DPOLY", 0105331u, opV1A5 },
    { "/CMRT", 0105332u, opMA3I },
    { "/ATLG", 0105333u, opMA1I },
    { ".FPWR", 0105334u, opMA1I },
    { ".TPWR", 0105335u, opMA2I },
    { "",      0105336u, opNone },              /* unimplemented */
    { "sitst", 0105337u, opNone },              /* self-test */

    { NULL }
    };


/* 105340-105357  RTE-6/VM Operating System Instructions */

static const OP_DESC os_desc = {                /* RTE-6/VM Operating System descriptor */
    0000017u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_VMAOS | CPU_1000_E | CPU_1000_F        /*   applicable feature flags */
    };

static const OP_TABLE os_ops = {                /* RTE-6/VM OS opcodes, indexed by IR bits 3-0 */
    { "$LIBR", 0105340u, opMA1I },
    { "$LIBX", 0105341u, opMA1I },
    { ".TICK", 0105342u, opNone },
    { ".TNAM", 0105343u, opNone },
    { ".STIO", 0105344u, opMA1I },              /* operand count varies from 1-n */
    { ".FNW",  0105345u, opMA1I },
    { ".IRT",  0105346u, opMA1I },
    { ".LLS",  0105347u, opMA2I },

    { ".SIP",  0105350u, opNone },
    { ".YLD",  0105351u, opMA1I },
    { ".CPM",  0105352u, opMA2I },
    { ".ETEQ", 0105353u, opNone },
    { ".ENTN", 0105354u, opMA1I },
    { "ostst", 0105355u, opNone },              /* self-test */
    { ".ENTC", 0105356u, opMA1I },
    { ".DSPI", 0105357u, opNone },

    { NULL }
    };

static const OP_DESC trap_desc = {              /* RTE-6/VM Operating System trap instructions descriptor */
    OP_LINEAR,                                  /*   linear search only */
    OP_SINGLE,                                  /*   single match allowed */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_VMAOS | CPU_1000_E | CPU_1000_F        /*   applicable feature flags */
    };

static const OP_TABLE trap_ops = {              /* RTE-6/VM OS trap opcodes, searched linearly */
    { "$DCPC", 0105354u, opNone, 0177777u },
    { "$MPV",  0105355u, opNone, 0177777u },
    { "$DEV",  0105356u, opNone, 0177777u },
    { "$TBG",  0105357u, opNone, 0177777u },

    { NULL }
    };


/* 10x400-10x437  2000 I/O Processor Instructions */

static const OP_DESC iop1_1000_desc = {         /* M/E-Series I/O Processor Instructions descriptor */
    OP_LINEAR,                                  /*   linear search only */
    OP_SINGLE,                                  /*   single match allowed */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_IOP | CPU_1000_M | CPU_1000_E          /*   applicable feature flags */
    };

static const OP_TABLE iop1_1000_ops = {         /* M/E-Series IOP opcodes, searched linearly */
    { "SAI", 0101400u, opIOPO, 0177777u },
    { "LAI", 0105400u, opIOPO, 0177777u },

    { NULL }
    };

/* 10x460-10x477  2000 I/O Processor Instructions */

static const OP_DESC iop2_1000_desc = {         /* M/E-Series I/O Processor Instructions descriptor */
    0000017u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_IOP | CPU_1000_M | CPU_1000_E          /*   applicable feature flags */
    };

static const OP_TABLE iop2_1000_ops = {         /* M/E-Series IOP opcodes, indexed by IR bits 3-0 */
    { "CRC",   0105460u, opV1   },
    { "RESTR", 0105461u, opNone },
    { "READF", 0105462u, opNone },
    { "INS",   0105463u, opNone },
    { "ENQ",   0105464u, opNone },
    { "PENQ",  0105465u, opNone },
    { "DEQ",   0105466u, opNone },
    { "TRSLT", 0105467u, opV1   },
    { "ILIST", 0105470u, opA1V1 },
    { "PRFEI", 0105471u, opV2A1 },
    { "PRFEX", 0105472u, opMA1I },
    { "PRFIO", 0105473u, opV2   },
    { "SAVE",  0105474u, opNone },
    { "",      0105475u, opNone },              /* unimplemented */
    { "",      0105476u, opNone },              /* unimplemented */
    { "",      0105477u, opNone },              /* unimplemented */

    { NULL }
    };


/* 10x460-10x477  Vector Instruction Set */

static const OP_DESC vis_desc = {               /* Vector Instruction Set descriptor */
    0000017u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_SELECT,                                  /*   A/B-register selector */
    UNIT_VIS | CPU_1000_F                       /*   applicable feature flags */
    };

static const OP_TABLE vis_ops = {               /* VIS opcodes, indexed by IR bits 11 + 3-0 */
    { "",      0101460u, opNone },              /* .VECT single-precision */
    { "VPIV",  0101461u, opZA8  },
    { "VABS",  0101462u, opZA5  },
    { "VSUM",  0101463u, opZA4  },
    { "VNRM",  0101464u, opZA4  },
    { "VDOT",  0101465u, opZA6  },
    { "VMAX",  0101466u, opZA4  },
    { "VMAB",  0101467u, opZA4  },
    { "VMIN",  0101470u, opZA4  },
    { "VMIB",  0101471u, opZA4  },
    { "VMOV",  0101472u, opZA5  },
    { "VSWP",  0101473u, opZA5  },
    { ".ERES", 0101474u, opMA3I },              /* variable operand count */
    { ".ESEG", 0101475u, opMA2I },
    { ".VSET", 0101476u, opMA7I },
    { "vitst", 0105477u, opNone },              /* self-test */

    { "",      0105460u, opNone },              /* .VECT double-precision */
    { "DVPIV", 0105461u, opZA8  },
    { "DVABS", 0105462u, opZA5  },
    { "DVSUM", 0105463u, opZA4  },
    { "DVNRM", 0105464u, opZA4  },
    { "DVDOT", 0105465u, opZA6  },
    { "DVMAX", 0105466u, opZA4  },
    { "DVMAB", 0105467u, opZA4  },
    { "DVMIN", 0105470u, opZA4  },
    { "DVMIB", 0105471u, opZA4  },
    { "DVMOV", 0105472u, opZA5  },
    { "DVSWP", 0105473u, opZA5  },
    { ".ERES", 0101474u, opMA3I },              /* variable operand count */
    { ".ESEG", 0101475u, opMA2I },
    { ".VSET", 0101476u, opMA7I },
    { "vitst", 0105477u, opNone },              /* self-test */

    { "VADD",  TO_DWORD (0000000u, 0101460u), opMA7I, D32_MASK },
    { "VSUB",  TO_DWORD (0000020u, 0101460u), opMA7I, D32_MASK },
    { "VMPY",  TO_DWORD (0000040u, 0101460u), opMA7I, D32_MASK },
    { "VDIV",  TO_DWORD (0000060u, 0101460u), opMA7I, D32_MASK },
    { "VSAD",  TO_DWORD (0000400u, 0101460u), opMA6I, D32_MASK },
    { "VSSB",  TO_DWORD (0000420u, 0101460u), opMA6I, D32_MASK },
    { "VSMY",  TO_DWORD (0000440u, 0101460u), opMA6I, D32_MASK },
    { "VSDV",  TO_DWORD (0000460u, 0101460u), opMA6I, D32_MASK },
    { "",      0101460u, opNone,   0177777u },                      /* catch unimplemented two-word instructions */

    { "DVADD", TO_DWORD (0004002u, 0105460u), opMA7I, D32_MASK },
    { "DVSUB", TO_DWORD (0004022u, 0105460u), opMA7I, D32_MASK },
    { "DVMPY", TO_DWORD (0004042u, 0105460u), opMA7I, D32_MASK },
    { "DVDIV", TO_DWORD (0004062u, 0105460u), opMA7I, D32_MASK },
    { "DVSAD", TO_DWORD (0004402u, 0105460u), opMA6I, D32_MASK },
    { "DVSSB", TO_DWORD (0004422u, 0105460u), opMA6I, D32_MASK },
    { "DVSMY", TO_DWORD (0004442u, 0105460u), opMA6I, D32_MASK },
    { "DVSDV", TO_DWORD (0004462u, 0105460u), opMA6I, D32_MASK },
    { "",      0105460u, opNone,   0177777u },                      /* catch unimplemented two-word instructions */

    { NULL }
    };


/* 10x600-10x617  SIGNAL/1000 Instruction Set */

static const OP_DESC sig_desc = {               /* SIGNAL/1000 Instruction Set descriptor */
    0000017u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_UNUSED,                                  /*   A/B-register selector */
    UNIT_SIGNAL | CPU_1000_F                    /*   applicable feature flags */
    };

static const OP_TABLE sig_ops = {               /* SIGNAL opcodes, indexed by IR bits 3-0 */
    { "BITRV", 0105600u, opMA4I },
    { "BTRFY", 0105601u, opMA6I },
    { "UNSCR", 0105602u, opMA6I },
    { "PRSCR", 0105603u, opMA6I },
    { "BITR1", 0105604u, opMA5I },
    { "BTRF1", 0105605u, opMA7I },
    { ".CADD", 0105606u, opMA3I },
    { ".CSUB", 0105607u, opMA3I },
    { ".CMPY", 0105610u, opMA3I },
    { ".CDIV", 0105611u, opMA3I },
    { "CONJG", 0105612u, opMA3I },
    { "..CCM", 0105613u, opMA1I },
    { "AIMAG", 0105614u, opMA2I },
    { "CMPLX", 0105615u, opMA4I },
    { "",      0105616u, opNone },              /* unimplemented */
    { "sitst", 0105617u, opNone },              /* self-test */

    { NULL }
    };


/* 10x700-10x737  Dynamic Mapping System Instructions */

static const OP_DESC dms_desc = {               /* Dynamic Mapping System instructions descriptor */
    0000037u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_SELECT,                                  /*   A/B-register selector */
    UNIT_DMS | CPU_1000                         /*   applicable feature flags */
    };

static const OP_TABLE dms_ops = {               /* DMS opcodes, indexed by IR bits 11 + 4-0 */
    { "xmm",   0105700u, opNone },              /* decodes as XMM */
    { "dmtst", 0105701u, opNone },              /* self-test (E/F-Series) or NOP (M-Series) */
    { "MBI",   0105702u, opNone },              /* bit 11 is not significant */
    { "MBF",   0105703u, opNone },              /* bit 11 is not significant */
    { "MBW",   0105704u, opNone },              /* bit 11 is not significant */
    { "MWI",   0105705u, opNone },              /* bit 11 is not significant */
    { "MWF",   0105706u, opNone },              /* bit 11 is not significant */
    { "MWW",   0105707u, opNone },              /* bit 11 is not significant */
    { "SYA",   0101710u, opNone },
    { "USA",   0101711u, opNone },
    { "PAA",   0101712u, opNone },
    { "PBA",   0101713u, opNone },
    { "SSM",   0105714u, opMA1I },              /* bit 11 is not significant */
    { "JRS",   0105715u, opMA2I },              /* bit 11 is not significant */
    { "",      0101716u, opNone },              /* unimplemented */
    { "",      0101717u, opNone },              /* unimplemented */

    { "XMM",   0105720u, opNone },              /* bit 11 is not significant */
    { "XMS",   0105721u, opNone },              /* bit 11 is not significant */
    { "XMA",   0101722u, opNone },
    { "",      0101723u, opNone },              /* unimplemented */
    { "XLA",   0101724u, opMA1I },
    { "XSA",   0101725u, opMA1I },
    { "XCA",   0101726u, opMA1I },
    { "LFA",   0101727u, opNone },
    { "RSA",   0101730u, opNone },
    { "RVA",   0101731u, opNone },
    { "DJP",   0105732u, opMA1I },              /* bit 11 is not significant */
    { "DJS",   0105733u, opMA1I },              /* bit 11 is not significant */
    { "SJP",   0105734u, opMA1I },              /* bit 11 is not significant */
    { "SJS",   0105735u, opMA1I },              /* bit 11 is not significant */
    { "UJP",   0105736u, opMA1I },              /* bit 11 is not significant */
    { "UJS",   0105737u, opMA1I },              /* bit 11 is not significant */

    { "xmm",   0105700u, opNone },              /* decodes as XMM */
    { "dmtst", 0105701u, opNone },              /* self-test (E/F-Series) or NOP (M-Series) */
    { "MBI",   0105702u, opNone },
    { "MBF",   0105703u, opNone },
    { "MBW",   0105704u, opNone },
    { "MWI",   0105705u, opNone },
    { "MWF",   0105706u, opNone },
    { "MWW",   0105707u, opNone },
    { "SYB",   0105710u, opNone },
    { "USB",   0105711u, opNone },
    { "PAB",   0105712u, opNone },
    { "PBB",   0105713u, opNone },
    { "SSM",   0105714u, opMA1I },
    { "JRS",   0105715u, opMA2I },
    { "",      0105716u, opNone },              /* unimplemented */
    { "",      0105717u, opNone },              /* unimplemented */

    { "XMM",   0105720u, opNone },
    { "XMS",   0105721u, opNone },
    { "XMB",   0105722u, opNone },
    { "",      0105723u, opNone },              /* unimplemented */
    { "XLB",   0105724u, opMA1I },
    { "XSB",   0105725u, opMA1I },
    { "XCB",   0105726u, opMA1I },
    { "LFB",   0105727u, opNone },
    { "RSB",   0105730u, opNone },
    { "RVB",   0105731u, opNone },
    { "DJP",   0105732u, opMA1I },
    { "DJS",   0105733u, opMA1I },
    { "SJP",   0105734u, opMA1I },
    { "SJS",   0105735u, opMA1I },
    { "UJP",   0105736u, opMA1I },
    { "UJS",   0105737u, opMA1I },

    { NULL }
    };


/* 10x740-10x777  Extended Instruction Group */

static const OP_DESC eig_desc = {               /* Extended Instruction Group descriptor */
    0000037u,                                   /*   opcode mask */
    0u,                                         /*   opcode shift */
    AB_SELECT,                                  /*   A/B-register selector */
    BASE_SET | CPU_1000                         /*   applicable feature flags */
    };

static const OP_TABLE eig_ops = {               /* EIG opcodes, indexed by IR bits 11 + 4-0 */
    { "SAX", 0101740u, opMA1I  },
    { "CAX", 0101741u, opNone  },
    { "LAX", 0101742u, opMA1I  },
    { "STX", 0105743u, opMA1I  },               /* bit 11 is not significant */
    { "CXA", 0101744u, opNone  },
    { "LDX", 0105745u, opMA1I  },               /* bit 11 is not significant */
    { "ADX", 0105746u, opMA1I  },               /* bit 11 is not significant */
    { "XAX", 0101747u, opNone  },
    { "SAY", 0101750u, opMA1I  },
    { "CAY", 0101751u, opNone  },
    { "LAY", 0101752u, opMA1I  },
    { "STY", 0105753u, opMA1I  },               /* bit 11 is not significant */
    { "CYA", 0101754u, opNone  },
    { "LDY", 0105755u, opMA1I  },               /* bit 11 is not significant */
    { "ADY", 0105756u, opMA1I  },               /* bit 11 is not significant */
    { "XAY", 0101757u, opNone  },

    { "ISX", 0105760u, opNone  },               /* bit 11 is not significant */
    { "DSX", 0105761u, opNone  },               /* bit 11 is not significant */
    { "JLY", 0105762u, opMA1I  },               /* bit 11 is not significant */
    { "LBT", 0105763u, opNone  },               /* bit 11 is not significant */
    { "SBT", 0105764u, opNone  },               /* bit 11 is not significant */
    { "MBT", 0105765u, opMA1ZI },               /* bit 11 is not significant */
    { "CBT", 0105766u, opMA1ZI },               /* bit 11 is not significant */
    { "SFB", 0105767u, opNone  },               /* bit 11 is not significant */
    { "ISY", 0105770u, opNone  },               /* bit 11 is not significant */
    { "DSY", 0105771u, opNone  },               /* bit 11 is not significant */
    { "JPY", 0105772u, opMA1I  },               /* bit 11 is not significant */
    { "SBS", 0105773u, opMA2I  },               /* bit 11 is not significant */
    { "CBS", 0105774u, opMA2I  },               /* bit 11 is not significant */
    { "TBS", 0105775u, opMA2I  },               /* bit 11 is not significant */
    { "CMW", 0105776u, opMA1ZI },               /* bit 11 is not significant */
    { "MVW", 0105777u, opMA1ZI },               /* bit 11 is not significant */

    { "SBX", 0105740u, opMA1I  },
    { "CBX", 0105741u, opNone  },
    { "LBX", 0105742u, opMA1I  },
    { "STX", 0105743u, opMA1I  },
    { "CXB", 0105744u, opNone  },
    { "LDX", 0105745u, opMA1I  },
    { "ADX", 0105746u, opMA1I  },
    { "XBX", 0105747u, opNone  },
    { "SBY", 0105750u, opMA1I  },
    { "CBY", 0105751u, opNone  },
    { "LBY", 0105752u, opMA1I  },
    { "STY", 0105753u, opMA1I  },
    { "CYB", 0105754u, opNone  },
    { "LDY", 0105755u, opMA1I  },
    { "ADY", 0105756u, opMA1I  },
    { "XBY", 0105757u, opNone  },

    { "ISX", 0105760u, opNone  },
    { "DSX", 0105761u, opNone  },
    { "JLY", 0105762u, opMA1I  },
    { "LBT", 0105763u, opNone  },
    { "SBT", 0105764u, opNone  },
    { "MBT", 0105765u, opMA1ZI },
    { "CBT", 0105766u, opMA1ZI },
    { "SFB", 0105767u, opNone  },
    { "ISY", 0105770u, opNone  },
    { "DSY", 0105771u, opNone  },
    { "JPY", 0105772u, opMA1I  },
    { "SBS", 0105773u, opMA2I  },
    { "CBS", 0105774u, opMA2I  },
    { "TBS", 0105775u, opMA2I  },
    { "CMW", 0105776u, opMA1ZI },
    { "MVW", 0105777u, opMA1ZI },

    { NULL }
    };


/* Parsing tables.

   Symbolic entry of instructions requires parsing the command line and matching
   the instruction mnemonic to an entry in an opcode table.  The parser table
   contains pointers to all of the opcode tables and their associated
   descriptors.  Parsing searches linearly in the order specified, so more
   common instructions should appear before less common ones.
*/

typedef struct {                                /* parser table entry */
    const OP_DESC  *descriptor;                 /*   opcode descriptor pointer */
    const OP_ENTRY *opcodes;                    /*   opcode table pointer */
    } PARSER_ENTRY;

static const PARSER_ENTRY parser_table [] = {   /* parser table array, searched linearly */
    { &mrg_desc,       mrg_ops       },
    { &srg1_desc,      srg1_ops      },
    { &srg_udesc,      srg_uops      },
    { &asg_udesc,      asg_uops      },
    { &iog_desc,       iog_ops       },
    { &eag_desc,       eag_ops       },
    { &eag_ef_desc,    eag_ef_ops    },
    { &iop_2100_desc,  iop_2100_ops  },
    { &fp_desc,        fp_ops        },
    { &fpp_desc,       fpp_ops       },
    { &ffp_2100_desc,  ffp_2100_ops  },
    { &ffp_m_desc,     ffp_m_ops     },
    { &ffp_e_desc,     ffp_e_ops     },
    { &ffp_f_desc,     ffp_f_ops     },
    { &ema_desc,       ema_ops       },
    { &vma_desc,       vma_ops       },
    { &dbi_desc,       dbi_ops       },
    { &sis_desc,       sis_ops       },
    { &os_desc,        os_ops        },
    { &trap_desc,      trap_ops      },
    { &iop1_1000_desc, iop1_1000_ops },
    { &iop2_1000_desc, iop2_1000_ops },
    { &vis_desc,       vis_ops       },
    { &sig_desc,       sig_ops       },
    { &dms_desc,       dms_ops       },
    { &eig_desc,       eig_ops       },

    { NULL,            NULL           }
    };


/* System interface local SCP support routines */

static void   one_time_init  (void);
static t_bool fprint_stopped (FILE *st, t_stat reason);
static void   fprint_addr    (FILE *st, DEVICE *dptr, t_addr addr);
static t_addr parse_addr     (DEVICE *dptr, CONST char *cptr, CONST char **tptr);

static t_stat hp_exdep_cmd (int32 arg, CONST char *buf);
static t_stat hp_run_cmd   (int32 arg, CONST char *buf);
static t_stat hp_brk_cmd   (int32 arg, CONST char *buf);
static t_stat hp_load_cmd  (int32 arg, CONST char *buf);


/* System interface local utility routines */

static t_stat  fprint_value       (FILE *ofile, t_value val,  uint32 radix, uint32 width, uint32 format);
static t_stat  fprint_instruction (FILE *ofile, t_addr addr, t_value *val, uint32 radix,
                                   const OP_DESC op_desc, const OP_TABLE ops);

static t_value parse_address     (CONST char *cptr, t_stat *status);
static t_value parse_value       (CONST char *cptr, uint32 radix, t_value max, t_stat *status);
static t_stat  parse_cpu         (CONST char *cptr, t_addr addr, t_value *val, uint32 radix, SYMBOL_SOURCE target);
static t_stat  parse_instruction (CONST char *cptr, t_addr addr, t_value *val, uint32 radix, const OP_ENTRY *optr);
static t_stat  parse_micro_ops   (const OP_ENTRY *optr, char *gbuf, t_value *val,
                                  CONST char **gptr, uint32 *accumulator);

static int fgetword (FILE *fileref);
static int fputword (int data, FILE *fileref);


/* System interface state */

static size_t device_size    = 0;               /* the maximum device name size */
static size_t flag_size      = 0;               /* the maximum trace flag name size */
static t_bool parse_physical = TRUE;            /* the address parser configuration */


/* System interface global data structures */

#define E                   0400u               /* parity bit for even parity */
#define O                   0000u               /* parity bit for odd parity */

const HP_WORD odd_parity [256] = {                      /* odd parity table */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /*   000-017 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /*   020-037 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /*   040-067 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /*   060-077 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /*   100-117 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /*   120-137 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /*   140-157 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /*   160-177 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /*   200-217 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /*   220-237 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /*   240-267 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /*   260-277 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E,     /*   300-317 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /*   320-337 */
    O, E, E, O, E, O, O, E, E, O, O, E, O, E, E, O,     /*   340-357 */
    E, O, O, E, O, E, E, O, O, E, E, O, E, O, O, E      /*   360-377 */
    };


/* System interface global SCP data definitions */

char sim_name [] = "HP 2100";                   /* the simulator name */

int32 sim_emax = MAX_INSTR_LENGTH;              /* the maximum number of words in any instruction */

void (*sim_vm_init) (void) = &one_time_init;    /* a pointer to the one-time initializer */

DEVICE *sim_devices [] = {                      /* an array of pointers to the simulated devices */
    &cpu_dev,                                   /*   CPU (must be first) */
    &mp_dev,                                    /*   Memory Protect */
    &dma1_dev, &dma2_dev,                       /*   DMA/DCPC */
    &ptr_dev,                                   /*   2748 Paper Tape Reader */
    &ptp_dev,                                   /*   2895 Paper Tape Punch */
    &tty_dev,                                   /*   2752 Teleprinter */
    &clk_dev,                                   /*   Time-Base Generator */
    &lps_dev,                                   /*   2767 Line Printer */
    &lpt_dev,                                   /*   2607 Line Printer */
    &baci_dev,                                  /*   Buffered Asynchronous Communications Interface */
    &mpx_dev,                                   /*   Eight-Channel Asynchronous Multiplexer */
    &dpd_dev,  &dpc_dev,                        /*   2870/7900 Disc Interface */
    &dqd_dev,  &dqc_dev,                        /*   2883 Disc Interface */
    &drd_dev,  &drc_dev,                        /*   277x Disc/Drum Interface */
    &ds_dev,                                    /*   7905/06/20/25 MAC Disc Interface */
    &mtd_dev,  &mtc_dev,                        /*   3030 Magnetic Tape Interface */
    &msd_dev,  &msc_dev,                        /*   7970B/E Magnetic Tape Interface */
    &muxl_dev, &muxu_dev, &muxc_dev,            /*   Sixteen-Channel Asynchronous Multiplexer */
    &ipli_dev, &iplo_dev,                       /*   Processor Interconnect Kit */
    &pif_dev,                                   /*   Privileged Interrupt Fence */
    &da_dev,                                    /*   7906H/20H/25H ICD Disc Interface */
    &dc_dev,                                    /*   Dummy Disc Interface for diagnostics */
    NULL
    };                                          /* end of the device list */

#define DEVICE_COUNT        (sizeof sim_devices / sizeof sim_devices [0] - 1)   /* the count excludes the NULL pointer */


const char *sim_stop_messages [] = {            /* an array of pointers to the stop messages in STOP_nnn order */
    "Impossible error",                         /*   0 (never returned) */
    "Unimplemented instruction",                /*   STOP_UNIMPL */
    "Unassigned select code",                   /*   STOP_UNSC */
    "Undefined instruction",                    /*   STOP_UNDEF */
    "Indirect address loop",                    /*   STOP_INDIR */
    "Programmed halt",                          /*   STOP_HALT */
    "Breakpoint",                               /*   STOP_BRKPNT */
    "Cable not connected to",                   /*   STOP_NOCONN */
    "No tape loaded in",                        /*   STOP_NOTAPE */
    "End of tape on"                            /*   STOP_EOT    */
    };


/* Local command table.

   This table defines commands and command behaviors that are specific to this
   simulator.  Specifically:

     * EXAMINE, DEPOSIT, IEXAMINE, and IDEPOSIT accept the page/offset physical
       address form.

     * RUN, GO, BREAK, and NOBREAK accept the logical address form and reject
       the page/offset physical address form.

   The table is initialized with only those fields that differ from the standard
   command table.  During one-time simulator initialization, the empty fields
   are filled in from the corresponding standard command table entries.  This
   ensures that the auxiliary table automatically picks up any changes to the
   standard commands that it modifies.


   Implementation notes:

    1. The RESET and BOOT commands are duplicated from the standard SCP command
       table so that entering "R" doesn't invoke the RUN command and entering
       "B" doesn't invoke the BREAK command.  This would otherwise occur because
       a VM-specific command table is searched before the standard command
       table.
*/

static CTAB aux_cmds [] = {
/*    Name        Action Routine  Argument   Help String */
/*    ----------  --------------  ---------  ----------- */
    { "RESET",    NULL,           0,         NULL        },
    { "BOOT",     NULL,           0,         NULL        },

    { "EXAMINE",  &hp_exdep_cmd,  0,         NULL        },
    { "IEXAMINE", &hp_exdep_cmd,  0,         NULL        },
    { "DEPOSIT",  &hp_exdep_cmd,  0,         NULL        },
    { "IDEPOSIT", &hp_exdep_cmd,  0,         NULL        },

    { "RUN",      &hp_run_cmd,    0,         NULL        },
    { "GO",       &hp_run_cmd,    0,         NULL        },

    { "BREAK",    &hp_brk_cmd,    0,         NULL        },
    { "NOBREAK",  &hp_brk_cmd,    0,         NULL        },

    { "LOAD",     &hp_load_cmd,   0,         NULL        },

    { NULL }
    };



/* System interface global SCP support routines */


/* Load and dump memory images from and to files.

   The LOAD and DUMP commands are intended to provide a basic method of loading
   and dumping programs into and from memory.  Typically, these commands operate
   on a simple, low-level format, e.g., a memory image.

   For this simulator, the LOAD command provides a way of installing bootstrap
   loaders into the last 64 words of memory.  The command format is:

     LOAD <image-filename> { <select-code> }

   ...where <image-filename> is an absolute binary file containing the loader,
   and <select-code> is an optional value from 10-77 octal used for configuring
   the loader's I/O instructions.  If the select code is omitted, the loader is
   used as-is.  When the command completes, the loader remains enabled so that
   it may be executed.  The loader should be protected if it will not be used
   immediately.

   The binary file must be targeted to addresses in the range x7700-x7777, where
   "x" is irrelevant.  The loaded program will be relocated to the last 64 words
   of memory, so the desired memory size must be set before issuing the LOAD
   command.  If the configuration select code is supplied, all I/O instructions
   in the program that reference select codes >= 10 octal will be changed by
   adding the supplied value minus 10 to the instruction.  The effect is that
   instructions that reference select code 10 + n will be changed to reference
   the supplied select code + n; this permits configuration of loaders that use
   two-card interfaces.

   The core-memory 21xx machines reserve the last 64 words in memory for a
   access-protected resident boot loader.  In hardware, the loaders could be
   changed only by entering the replacement loader via the front panel switch
   register.  In simulation, the LOAD command serves this purpose.

   The 1000-series machines used semiconductor memory, so the loaders were
   implemented in socketed ROMs that were read into the last 64 words of memory
   via the front-panel IBL (Initial Binary Loader) button.  For these machines,
   the LOAD command serves to install ROM images other than the ones included
   with the device simulators.  If the CPU is configured with more than 32K of
   memory, the loader is installed in the last 64 words of the 32K logical
   address space.


   The DUMP command writes the bootstrap loader currently residing in memory to
   an absolute binary file.  The command format is:

     DUMP <image-filename>

   The loader must be enabled before entering the DUMP command; if the loader
   is disabled, the output file will contain all zeros.  When the command
   completes, the loader remains enabled.  The resulting file may be used in a
   subsequent LOAD command to reload the bootstrap program.


   Implementation notes:

    1. Previous simulator versions did not restrict LOAD command addressing.
       However, the LOAD command is not a convenient replacement for the ATTACH
       PTR and BOOT PTR commands.  The bootstrap loaders clear the A and B
       registers at completion, and certain HP software depends on this behavior
       for proper operation.  The LOAD command alters nothing other than the
       memory occupied by the program, and its use as a general absolute binary
       paper tape loader would result in software failures.

    2. The absolute binary format is described in Appendix H of the RTE-IV
       Assembler Reference Manual.

    3. The LOADed absolute binary file may contain a leader of any length,
       including zero length (i.e., omitted).  The logical end of file occurs
       after reading ten null bytes or the physical EOF, whichever occurs first.

    4. The DUMP command writes the 64-word loader in two records of 57 and 7
       words, respectively, to conform with the format written by the HP
       Assembler.
*/

t_stat sim_load (FILE *fptr, CONST char *cptr, CONST char *fnam, int flag)
{
const int    reclen [2] = { TO_WORD (57, 0),            /* the two DUMP record length words */
                            TO_WORD (7, 0) };
const int    reccnt [2] = { 57, 7 };                    /* the two DUMP record word counts */
int          record, count, address, word, checksum;
t_stat       result;
int32        trailer = 1;                               /* > 0 while reading leader, < 0 while reading trailer */
HP_WORD      select_code = 0;                           /* select code to configure; 0 implies no configuration */
LOADER_ARRAY boot = {                                   /* an array of two BOOT_LOADER structures */
    { 000,       IBL_NA,  IBL_NA,  { 0 } },             /*   HP 21xx Loader */
    { IBL_START, IBL_DMA, IBL_FWA, { 0 } }              /*   HP 1000 Loader */
    };

if (flag == 0) {                                        /* if this is a LOAD command */
    if (*cptr != '\0') {                                /*   then if a parameter follows */
        select_code =                                   /*     then parse it as an octal number */
          (HP_WORD) get_uint (cptr, 8, MAXDEV, &result);

        if (result != SCPE_OK)                          /* if a parse error occurred */
            return result;                              /*   then report it */

        else if (select_code < VARDEV)                  /* otherwise if the select code is invalid */
            return SCPE_ARG;                            /*   then report a bad argument */
        }

    while (TRUE) {                                      /* read absolute binary records from the file */
        do {                                            /* skip any blank leader or trailer present */
            count = fgetc (fptr);                       /* get the next byte from the tape */

            if (count == EOF)                           /* if an EOF occurred */
                if (trailer > 0)                        /*   then if we are reading the leader */
                    return SCPE_FMT;                    /*     then the tape format is bad */
                else                                    /*   otherwise we are reading the trailer */
                    trailer = 0;                        /*     and now we are done */

            else if (count == 0)                        /* otherwise if this is a null value */
                trailer = trailer + 1;                  /*   then increment the trailer count */
            }
        while (count == 0 && trailer != 0);             /* continue if a null was read or trailer is exhausted */


        if (trailer == 0)                               /* if the physical EOF was seen */
            break;                                      /*   then the binary read is complete */

        else if (fgetc (fptr) == EOF)                   /* otherwise discard the unused byte after the record count */
            return SCPE_FMT;                            /* if it is not there, then the tape format is bad */

        address = fgetword (fptr);                      /* get the record load address word */

        if (address == EOF)                             /* if the load address is not present */
            return SCPE_FMT;                            /*   then the tape format is bad */
        else                                            /* otherwise */
            checksum = address;                         /*   start the record checksum with the load address */

        if ((address & 0007777u) < 0007700u)            /* if the address does not fall at the end of a 4K block */
            return SCPE_NXM;                            /*   then report the address error */
        else                                            /* otherwise mask the address */
            address = address & IBL_MASK;               /*   to form an index into the loader array */


        while (count-- > 0) {                           /* read the data record */
            word = fgetword (fptr);                     /* get the next data word from the file */

            if (word == EOF)                            /* if the word is not present */
                return SCPE_FMT;                        /*   then the tape format is bad */

            else {                                                  /* otherwise */
                boot [0].loader [address]   = (MEMORY_WORD) word;   /*   save the data word in */
                boot [1].loader [address++] = (MEMORY_WORD) word;   /*     both loader arrays */
                checksum = checksum + word;                         /*       and include it in the record checksum */
                }
            }


        word = fgetword (fptr);                         /* read the record checksum word */

        if (word == EOF)                                /* if it is not present */
            return SCPE_FMT;                            /*   then the tape format is bad */

        else if (word != (int) (checksum & D16_MASK))   /* otherwise if the checksums do not match */
            return SCPE_CSUM;                           /*   then report a checksum error */

        else                                            /* otherwise the record is good */
            trailer = -10;                              /*   so prepare for a potential trailer */
        }                                               /*     and loop until all records are read */

    cpu_copy_loader (boot, select_code,                 /* install the loader */
                     IBL_S_NOCLEAR, IBL_S_NOSET);       /*   and configure the select code if requested */
    }


else {                                                  /* otherwise this is a DUMP command */
    address = MEMSIZE - 1 & ~IBL_MASK & LA_MASK;        /* the loader occupies the last 64 words in memory */

    for (record = 0; record < 2; record++) {            /* write two absolute records */
        if (fputword (reclen [record], fptr) == EOF)    /*   starting with the record length; if it fails */
            return SCPE_IOERR;                          /*     then report an I/O error */

        if (fputword (address, fptr) == EOF)            /* write the starting address; if it fails */
            return SCPE_IOERR;                          /*   then report an I/O error */

        checksum = address;                             /* start the record checksum with the load address */

        for (count = 0; count < reccnt [record]; count++) { /* write a data record */
            word = mem_examine (address++);                 /* get the next data word from memory */

            if (fputword (word, fptr) == EOF)               /* write the data word; if it fails */
                return SCPE_IOERR;                          /*   then report an I/O error */
            else                                            /* otherwise */
                checksum = checksum + word;                 /*   include it in the record checksum */
            }                                               /*     and loop until all words are written */

        if (fputword (checksum & D16_MASK, fptr) == EOF)    /* write the checksum word; if it fails */
            return SCPE_IOERR;                              /*   then report an I/O error */
        }                                                   /* loop until both records are written */
    }

return SCPE_OK;
}


/* Print a value in symbolic format.

   This routine prints a data value in the format specified by the optional
   switches on the output stream provided.  On entry, "ofile" is the opened
   output stream, and the other parameters depend on the reason the routine was
   called, as follows:

     * To print the next instruction mnemonic when the simulator stops:
        - addr = the program counter
        - val  = a pointer to sim_eval [0]
        - uptr = NULL
        - sw   = "-M" | SIM_SW_STOP

     * To print the result of EXAMining a register with REG_VMIO or a user flag:
        - addr = the ORed register radix and user flags
        - val  = a pointer to a single t_value
        - uptr = NULL
        - sw   = the command line switches | SIM_SW_REG

     * To print the result of EXAMining a memory address:
        - addr = the memory address
        - val  = a pointer to sim_eval [0]
        - uptr = a pointer to the named unit
        - sw   = the command line switches

     * To print the result of EVALuating a symbol:
        - addr = the symbol index
        - val  = a pointer to sim_eval [addr]
        - uptr = a pointer to the default unit (cpu_unit)
        - sw   = the command line switches

   On exit, a status code is returned to the caller.  If the format requested is
   not supported, SCPE_ARG status is returned, which causes the caller to print
   the value in numeric format with the default radix.  Otherwise, SCPE_OK
   status is returned if a single-word value was consumed, or the negative
   number of extra words (beyond the first) consumed in printing the symbol is
   returned.  For example, printing a two-word symbol would return
   SCPE_OK_2_WORDS (= -1).

   The following symbolic modes are supported by including the indicated
   switches:

     Switch   Interpretation
     ------   -----------------------------------------
       -A     a single character in the right-hand byte
       -C     a two-character packed string
       -M     a CPU instruction mnemonic

   In the absence of a mode switch, the value is displayed in a numeric format.

   When displaying in the instruction mnemonic form, an additional format switch
   may be specified to indicate the desired operand radix, as follows:

     Switch   Interpretation
     ------   -----------------------------------------
       -B     a binary value
       -O     an octal value
       -D     a decimal value
       -H     a hexadecimal value

   These switches (except for -B) may be used without a mode switch to display a
   numeric value in the specified radix.  To summarize, the valid switch
   combinations are:

     -C
     -M [ -A | -B | -O | -D | -H ]
     -A | -O | -D | -H

   When displaying mnemonics, operand values by default are displayed in a radix
   suitable to the type of the value.  Address values are displayed in the CPU's
   address radix, which is octal, and data values are displayed in the CPU's
   data radix, which defaults to octal but may be set to a different radix or
   overridden by a switch on the command line.


   Implementation notes:

    1. If we are being called as a result of a VM stop to display the next
       instruction to be executed, a check is made to see if an interrupt is
       pending and not deferred.  If so, then the interrupt source and the trap
       cell instruction are displayed as the next instruction to be executed,
       rather than the instruction at the current PC.

    2. The trap cell instruction is read by calling "mem_fast_read" directly
       into the "val" array (which points to the "sim_eval" array), rather than
       by setting the VAL_EMPTY flag and letting the "fprint_instruction"
       routine load any required operands, because the instruction must be read
       from the system map instead of the current map (which may be the user map
       until the interrupt is actually processed).

    3. When we are called to format a register, the "val" parameter points at a
       single t_value containing the register value.  However, the instruction
       mnemonic formatter expects to receive a pointer to an array that holds
       at least the number of words for the instruction being decoded.  For
       example, if the register holds the MPY opcode, the formatter will expect
       the operand address in the second array word.  Multi-word instructions
       cannot be displayed correctly if they originate in a register, so the
       best we can do is copy the value to "sim_eval [0]" and zero the remaining
       words before calling the mnemonic formatter.  A call to "memset" is used
       because this can be optimized to a single REP STOSD instruction.

    4. The "fprint_cpu" routine needs to know whether the "addr" parameter value
       represents a CPU memory address in order to interpret MRG instructions
       correctly.  It does for simulator stops and CPU memory examinations but
       not for register or device memory examinations and symbol evaluations.
       The symbol source is set to "CPU_Symbol" or "Device_Symbol",
       respectively, to reflect these conditions.

    5. We return SCPE_INVSW when multiple modes or formats are specified, but
       the callers do not act on this; they use the fallback formatter if any
       status error is returned.  We could work around this by printing "Invalid
       switch" to the console and returning SCPE_OK, but this does not stop
       IEXAMINE from prompting for the replacement value(s) or EXAMINE from
       printing a range.

    6. Radix switches and the -C switch are conceptually mutually exclusive.
       However, if we return an error when "format" is non-zero, then -C will be
       ignored, and the fallback formatter will use the radix switch.  The other
       choice is to process -C and ignore the radix switch; this is the option
       implemented.
*/

t_stat fprint_sym (FILE *ofile, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
int32         formats, modes, i;
uint32        irq, radix;
SYMBOL_SOURCE source;

if ((sw & (SIM_SW_REG | ALL_SWITCHES)) == SIM_SW_REG)   /* if we are formatting a register without overrides */
    if (addr & REG_A)                                   /*   then if the default format is character */
        sw |= A_SWITCH;                                 /*     then set the -A switch */

    else if (addr & REG_C)                              /*   otherwise if the default mode is string */
        sw |= C_SWITCH;                                 /*     then set the -C switch */

    else if (addr & REG_M)                              /*   otherwise if the default mode is mnemonic */
        sw |= M_SWITCH;                                 /*     then set the -M switch */

if ((sw & SYMBOLIC_SWITCHES) == 0)                      /* if there are no symbolic overrides */
    return SCPE_ARG;                                    /*   then return an error to use the standard formatter */


formats = sw & FORMAT_SWITCHES;                         /* separate the format switches */
modes   = sw & MODE_SWITCHES;                           /*   from the mode switches */

if (formats == A_SWITCH)                                /* if the -A switch is specified */
    radix = 256;                                        /*   then override the radix to character */

else if (formats == B_SWITCH)                           /* otherwise if the -B switch is specified */
    radix = 2;                                          /*   then override the radix to binary */

else if (formats == D_SWITCH)                           /* otherwise if the -D switch is specified */
    radix = 10;                                         /*   then override the radix to decimal */

else if (formats == H_SWITCH)                           /* otherwise if the -H switch is specified */
    radix = 16;                                         /*   then override the radix to hexadecimal */

else if (formats == O_SWITCH)                           /* otherwise if the -O switch is specified */
    radix = 8;                                          /*   then override the radix to octal */

else if (formats == 0)                                  /* otherwise if no format switch is specified */
    radix = 0;                                          /*   then indicate that the default radix is to be used */

else                                                    /* otherwise more than one format is specified */
    return SCPE_INVSW;                                  /*   so return an error */

if (modes == M_SWITCH) {                                /* if mnemonic mode is specified */
    if (sw & SIM_SW_STOP) {                             /*   then if this is a simulator stop */
        source = CPU_Symbol;                            /*     then report as a CPU symbol */

        irq = calc_int ();                              /* check for a pending interrupt */

        if (irq && !ion_defer) {                        /* if a pending interrupt is present and not deferred */
            addr = irq;                                 /*   then set the display address to the trap cell */

            for (i = 0; i < sim_emax; i++)                              /* load the trap cell instruction */
                val [i] = mem_fast_read ((HP_WORD) (irq + i), SMAP);    /*   which might be multi-word (e.g., JLY) */

            fprintf (ofile, "IAK %2o: ", irq);          /* report that the interrupt will be acknowledged  */
            }
        }

    else if (sw & SIM_SW_REG) {                         /* otherwise if a register value is being formatted */
        source = Device_Symbol;                         /*   then report it as a device symbol */

        memset (sim_eval, 0,                            /* clear the sim_eval array */
                MAX_INSTR_LENGTH * sizeof (t_value));   /*   in case the instruction is multi-word */

        sim_eval [0] = *val;                            /* copy the register value */
        val = sim_eval;                                 /*   and point at the sim_eval array */
        }

    else if (uptr == &cpu_unit)                         /* otherwise if access is to CPU memory */
        source = CPU_Symbol;                            /*   then report as a CPU symbol */

    else                                                /* otherwise access is to device memory */
        source = Device_Symbol;                         /*   so report it as a device symbol */

    return fprint_cpu (ofile, addr, val, radix, source);    /* format and print the value in mnemonic format */
    }

else if (modes == C_SWITCH) {                           /* otherwise if ASCII string mode is specified */
    fputs (fmt_char (UPPER_BYTE (val [0])), ofile);     /*   then format and print the upper byte */
    fputc (',', ofile);                                 /*     followed by a separator */
    fputs (fmt_char (LOWER_BYTE (val [0])), ofile);     /*       followed by the lower byte */
    return SCPE_OK;
    }

else if (modes == 0)                                    /* otherwise if no mode was specified */
    return fprint_value (ofile, val [0], radix,         /*   then format and print it with the specified radix */
                         DV_WIDTH, PV_RZRO);            /*     and data width */

else                                                    /* otherwise the modes conflict */
    return SCPE_INVSW;                                  /*   so return an error */
}


/* Parse a value in symbolic format.

   Print the data value in the format specified by the optional switches on the
   output stream supplied.  This routine is called to print:

   Parse the input string in the format specified by the optional switches, and
   return the resulting value(s).  This routine is called to parse an input
   string when:

     - DEPOSITing into a register marked with REG_VMIO or a user flag
     - DEPOSITing into a memory address
     - EVALuating a symbol

   On entry, "cptr" points at the string to parse, "addr" is the register radix
   and flags, memory address, or 0 (respectively), "uptr" is NULL, a pointer to
   the named unit, or a pointer to the default unit (respectively), "val" is a
   pointer to an array of t_values of depth "sim_emax" representing the value(s)
   returned, and "sw" contains any switches passed on the command line.  "sw"
   also includes SIM_SW_REG for a register call.

   On exit, a status code is returned to the caller.  If the format requested is
   not supported or the parse failed, SCPE_ARG status is returned, which causes
   the caller to attempt to parse the value in numeric format.  Otherwise,
   SCPE_OK status is returned if the parse produced a single-word value, or the
   negative number of extra words (beyond the first) produced by parsing the
   symbol is returned.  For example, parsing a symbol that resulted in two words
   being stored (in val [0] and val [1]) would return SCPE_OK_2_WORDS (= -1).

   The following symbolic modes are supported by including the indicated
   switches:

     Switch   Interpretation
     ------   -----------------------------
       -C     a two-character packed string
       -M     a CPU instruction mnemonic

   When parsing in the instruction mnemonic form, an additional format switch
   may be specified to indicate the supplied operand radix, as follows:

     Switch   Interpretation
     ------   -----------------------------------------
       -A     a single character in the right-hand byte
       -B     a binary value
       -O     an octal value
       -D     a decimal value
       -H     a hexadecimal value

   These switches (except for -B) may be used without a mode switch to parse a
   numeric value in the specified radix.  To summarize, the valid switch
   combinations are:

     -C
     -M [ -A | -B | -O | -D | -H ]
     -A | -O | -D | -H

   When entering machine instruction mnemonics, operand values are parsed in a
   radix suitable to the type of the value.  Address values are parsed in the
   CPU's address radix, which is octal, and data values are parsed in the CPU's
   data radix, which defaults to octal but may be set to a different radix or
   overridden by a switch on the command line.

   In the absence of switches, a leading ' implies "-A", a leading " implies
   "-C", and a leading alphabetic or punctuation character implies "-M".  If a
   single character is supplied with "-C", the low byte of the resulting value
   will be zero; follow the character with a space if the low byte is to be
   padded with a space.

   The operand format for -A is a single (displayable) character.  The operand
   format for -C is two (displayable) characters.


   Implementation notes:

    1. The "cptr" post-increments are logically ANDed with the tests for ' and "
       so that the increments are performed only if the tests succeed.  The
       intent is to skip over the leading ' or " character.  The increments
       themselves always succeed, so they don't affect the outcome of the tests.

    2. A hex value that is also a machine instruction mnemonic (e.g., CCE) is
       parsed as the latter unless the -H switch is specified.  If both -M and
       -H are specified, the mnemonic is parsed as a machine instruction, and
       any operand is parsed as a hex value.

    3. We return SCPE_INVSW when multiple modes or formats are specified, but
       the callers do not act on this; they use the fallback parser if any
       status error is returned.  We could work around this by printing "Invalid
       switch" to the console and returning SCPE_OK, but this does not stop
       IDEPOSIT from prompting for the replacement value(s) or DEPOSIT from
       printing the error for each address in a range.

    4. Radix switches and the -C switch are conceptually mutually exclusive.
       However, if we return an error when "format" is non-zero, then -C will be
       ignored, and the fallback formatter will use the radix switch.  The other
       option is to process -C and ignore the radix switch; this is the option
       implemented.
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
int32         formats, modes;
uint32        radix;
t_stat        status;
SYMBOL_SOURCE target;

if ((sw & (SIM_SW_REG | ALL_SWITCHES)) == SIM_SW_REG)   /* if we are parsing a register without overrides */
    if (addr & REG_A)                                   /*   then if the default format is character */
        sw |= A_SWITCH;                                 /*     then set the -A switch */

    else if (addr & REG_C)                              /*   otherwise if the default mode is string */
        sw |= C_SWITCH;                                 /*     then set the -C switch */

    else if (addr & REG_M)                              /*   otherwise if the default mode is mnemonic */
        sw |= M_SWITCH;                                 /*     then set the -M switch */


if ((sw & ALL_SWITCHES) == 0)                           /* if there are no default or explicit overrides */
    if (*cptr == '\'' && cptr++)                        /*   then if a character parse is implied */
        sw |= A_SWITCH;                                 /*     then set the -A switch */

    else if (*cptr == '"' && cptr++)                    /*   otherwise if a character string parse is implied */
        sw |= C_SWITCH;                                 /*     then set the -C switch */

    else if (isalpha (*cptr) || ispunct (*cptr))        /*   otherwise if an instruction mnemonic parse is implied */
        sw |= M_SWITCH;                                 /*     then set the -M switch */


if ((sw & SYMBOLIC_SWITCHES) == 0)                      /* if there are no symbolic overrides */
    return SCPE_ARG;                                    /*   then return an error to use the standard parser */


formats = sw & FORMAT_SWITCHES;                         /* separate the format switches */
modes   = sw & MODE_SWITCHES;                           /*   from the mode switches */

if (formats == A_SWITCH)                                /* if the -A switch is specified */
    radix = 256;                                        /*   then override the radix to character */

else if (formats == B_SWITCH)                           /* otherwise if the -B switch is specified */
    radix = 2;                                          /*   then override the radix to binary */

else if (formats == D_SWITCH)                           /* otherwise if the -D switch is specified */
    radix = 10;                                         /*   then override the radix to decimal */

else if (formats == H_SWITCH)                           /* otherwise if the -H switch is specified */
    radix = 16;                                         /*   then override the radix to hexadecimal */

else if (formats == O_SWITCH)                           /* otherwise if the -O switch is specified */
    radix = 8;                                          /*   then override the radix to octal */

else if (formats == 0)                                  /* otherwise if no format switch is specified */
    radix = 0;                                          /*   then indicate that the default radix is to be used */

else                                                    /* otherwise more than one format is specified */
    return SCPE_INVSW;                                  /*   so return an error */

if (modes == M_SWITCH) {                                /* if instruction mnemonic mode is specified */
    if (uptr == NULL || uptr == &cpu_unit)              /*   then if access is to a register or CPU memory */
        target = CPU_Symbol;                            /*     then report as a CPU symbol */
    else                                                /*   otherwise access is to device memory */
        target = Device_Symbol;                         /*     so report it as a device symbol */

    return parse_cpu (cptr, addr, val, radix, target);  /* attempt a mnemonic instruction parse */
    }

else if (modes == C_SWITCH)                                 /* otherwise if string mode is specified */
    if (cptr [0] != '\0') {                                 /*   then if characters are present */
        val [0] = (t_value) TO_WORD (cptr [0], cptr [1]);   /*     then convert the character values */
        return SCPE_OK;                                     /*       and indicate success */
        }

    else                                                    /* otherwise */
        return SCPE_ARG;                                    /*   report that the line cannot be parsed */

else if (modes == 0) {                                      /* otherwise if no mode was specified */
    val [0] = parse_value (cptr, radix, DV_UMAX, &status);  /*   then parse using the specified radix */
    return status;                                          /*     and return the parsing status */
    }

else                                                    /* otherwise the modes conflict */
    return SCPE_INVSW;                                  /*   so return an error */
}


/* Attach a file for appending.

   This routine is called to attach a file to a specified unit and set the file
   position to the end for appending.  The routine returns the result of the
   operation, which will be SCPE_OK if the attach and optional EOF seek succeed,
   SCPE_IOERR if the attach succeeds but the EOF seek fails (in this case, the
   file will be detached before returning), or an appropriate status code if the
   attach fails.

   The standard "attach_unit" routine handles the "-N" switch to use an empty
   ("new") file.  If the file exists, "attach_unit" returns with the file
   positioned at the start.  For write-only devices, such as printers, this is
   almost never desirable, as it means that existing content may be only
   partially overwritten.  This routine provides the proper semantics for these
   devices, i.e., if the file exists, position it for writing to the end of the
   file.


   Implementation notes:

    1. "attach_unit" opens the file using one of the following modes:

         - "r" (reading) if the -R (read-only) switch or UNIT_RO is present or
           the file is marked read-only by the host file system

         - "w+" (truncate and update) if the -N (new) switch is present or the
           file does not exist

         - "r+" (update) otherwise

    2. Reopening with mode "a" or "a+" doesn't have the desired semantics.
       These modes permit writing only at the EOF, but we want to permit
       overwriting if the user explicitly sets POS.  The resulting "fseek" on
       resumption would be ignored if the mode is "a" or "a+".  Therefore, we
       accept mode "r+" and explicitly "fseek" to the end of file here.

    3. If we are called during a RESTORE command to reattach a file previously
       attached when the simulation was SAVEd, the file position is not altered.

    4. It is not necessary to report the device or unit if the 'fseek" fails, as
       this routine is called only is response to a user command that specifies
       the unit to attach.
*/

t_stat hp_attach (UNIT *uptr, CONST char *cptr)
{
t_stat result;

result = attach_unit (uptr, cptr);                      /* attach the specified image file */

if (result == SCPE_OK                                   /* if the attach was successful */
  && (sim_switches & SIM_SW_REST) == 0)                 /*   and we are not being called during a RESTORE command */
    if (fseek (uptr->fileref, 0, SEEK_END) == 0)        /*     then append by seeking to the end of the file */
        uptr->pos = (t_addr) ftell (uptr->fileref);     /*       and repositioning if the seek succeeded */

    else {                                              /* otherwise the seek failed */
        cprintf ("%s simulator seek error: %s\n",       /*   so report the error to the console */
                 sim_name, strerror (errno));

        detach_unit (uptr);                             /* detach the unit */
        result = SCPE_IOERR;                            /*   and report that the seek failed */
        }

return result;
}


/* Set a device select code.

   This validation routine is called to set a device's select code.  The "uptr"
   parameter points to the unit being configured, "count" gives the number of
   select codes to configure, "cptr" points to the first character of the value
   to be set, and "desc" points to the DIB associated with the device.

   If the supplied value is acceptable, it is stored in the DIB, and the routine
   returns SCPE_OK.  Otherwise, an error code is returned.

   Some devices (e.g., the DP disc device) use multiple interface cards.  These
   are assigned sequential select codes.  For these devices, "desc" must point
   at the first element of an array of DIBs to be assigned, and "count" must be
   set to the number of elements.  For single-card devices, "desc" points at the
   DIB, and "count" is 1.


   Implementation notes:

    1. The legacy modifier "DEVNO" is supported in addition to the preferred
       "SC" modifier, but the corresponding MTAB entry is tagged with MTAB_NMO
       to prevent its display.  To differentiate between the two MTAB entries,
       which is necessary for display, the "DEVNO" entry's "count" parameter is
       complemented (the corresponding MTAB "match" field that supplies the
       "count" parameter to this routine is unsigned).
*/

t_stat hp_set_dib (UNIT *uptr, int32 count, CONST char *cptr, void *desc)
{
DIB        *dibptr = (DIB *) desc;                      /* a pointer to the associated DIB array */
t_stat     status = SCPE_OK;
uint32     value;
int32      index;

if (cptr == NULL || *cptr == '\0')                      /* if the expected value is missing */
    status = SCPE_MISVAL;                               /*   then report the error */

else {                                                  /* otherwise a value is present */
    if (count < 0)                                      /* if the count has been complemented */
        count = ~count;                                 /*   then restore it to a positive value */

    value = (uint32) get_uint (cptr, SC_BASE,           /* parse the supplied device number */
                               SC_MAX + 1 - count, &status);

    if (status == SCPE_OK) {                            /* if it is valid */
        if (value < VARDEV)                             /*   then if it is an internal select code */
            return SCPE_ARG;                            /*     then reject it */

        for (index = 0; index < count; index++, dibptr++)   /* loop through the associated interfaces */
            dibptr->select_code = value + index;            /*   and set the select codes in order */
        }
    }

return status;                                          /* return the validation result */
}


/* Show a device select code.

   This display routine is called to show a device's select code.  The "st"
   parameter is the open output stream, "uptr" points to the unit being queried,
   "count" gives the number of additional select codes to display, and "desc"
   points to the DIB associated with the device.

   Some devices (e.g., the DP disc device) use multiple interface cards.  For
   these devices, "desc" must point at the first element of an array of DIBs to
   be assigned, and "count" must be set to the index of the last element (note:
   not the size) of the array.  The select codes will be printed together.

   For single-card devices, "desc" points at the DIB, and "count" is zero.


   Implementation notes:

    1. The legacy modifier "DEVNO" is supported in addition to the preferred
       "SC" modifier, but the corresponding MTAB entry is tagged with MTAB_NMO
       to prevent its display.  To differentiate between the two MTAB entries,
       which is necessary for display, the "DEVNO" entry's "count" parameter is
       complemented (the corresponding MTAB "match" field that supplies the
       "count" parameter to this routine is unsigned).


    1. The legacy modifier "DEVNO" is supported in addition to the preferred
       "SC" modifier, but the corresponding MTAB entry is tagged with MTAB_NMO
       to prevent its display.  When displaying an MTAB_NMO entry, a newline is
       not automatically added to the end of the line, so we must add it here.
       To differentiate between the two MTAB entries, the "DEVNO" entry's
       "count" parameter is complemented (the corresponding MTAB "match" field
       that supplies the "count" parameter to this routine is unsigned).
*/

t_stat hp_show_dib (FILE *st, UNIT *uptr, int32 count, CONST void *desc)
{
const DIB *dibptr = (const DIB *) desc;                 /* a pointer to the associated DIB array */
int32     index, limit;

if (count < 0)                                          /* if the count has been complemented */
    limit = ~count;                                     /*   then restore it to a positive value */
else                                                    /* otherwise */
    limit = count;                                      /*   the value is already positive */

fprintf (st, "select code=%o", dibptr++->select_code);  /* print the interface's select code */

for (index = 2; index <= limit; index++, dibptr++)      /* if the device uses more than one interface */
    fprintf (st, "/%o", dibptr->select_code);           /*   then append the additional select code(s) */

if (count < 0)                                          /* if this is a DEVNO request (MTAB_NMO) */
    fputc ('\n', st);                                   /*   then append a newline */

return SCPE_OK;                                         /* return the display result */
}



/* System interface global utility routines */


/* Print a CPU instruction in symbolic format.

   This routine is called to format and print an instruction in mnemonic form.
   The "ofile" parameter is the opened output stream, "val" is a pointer to an
   array of t_values of depth "sim_emax" containing the word(s) comprising the
   machine instruction to print, "radix" contains the desired operand radix or
   zero if the default radix is to be used, and "source" indicates the source of
   the instruction (device, CPU, or trace).

   The routine returns a status code to the caller.  SCPE_OK status is returned
   if a single-word instruction was printed, or the negative number of extra
   words (beyond the first) consumed in printing the instruction is returned.
   For example, printing a two-word instruction returns SCPE_OK_2_WORDS (= -1).
   If the supplied instruction is not a valid opcode, or is not valid for the
   current CPU feature set, SCPE_ARG is returned.

   The routine determines the instruction group that includes the supplied
   instruction value and calls the "fprint_instruction" routine with the
   associated opcode descriptor and table.  For MRG instructions, the
   instruction address is set to an invalid value if the source is not a CPU
   memory location.  This causes the instruction to be printed as a base-page or
   current-page offset, rather than as an absolute address.

   SRG instructions consist of two encoded micro-op fields, plus separate
   micro-ops for CLE and SLA/SLB.  These are printed separately.

   MAC instructions must be decoded into the indicated feature groups.  Some of
   these groups overlap and must be differentiated by the current CPU model or
   installed microcode options before the correct opcode descriptor and table
   may be determined.  Primary MAC decoding is bits 11-8, as follows (note that
   bit 10 = 0 for the MAC group):

     Bits
     11-8  Group
     ----  -----
     0000  EAU
     0001  EAU
     0010  EAU
     0011  UIG-1
     1000  EAU
     1001  EAU
     1010  UIG-0
     1011  UIG-1

   Bits 7-4 further decode the UIG instruction feature group.  UIG-0 pertains
   to the 2100 and 1000 M/E/F-Series, as follows:

     Instructions   IR 7-4  Option Name                  2100   1000-M  1000-E  1000-F
     -------------  ------  --------------------------  ------  ------  ------  ------
     105000-105362  00-17   2000 I/O Processor           opt      -       -       -
     105000-105137  00-05   Floating Point               opt     std     std     std
     105200-105237  10-11   Fast FORTRAN Processor       opt     opt     opt     std
     105240-105257    12    RTE-IVA/B Extended Memory     -       -      opt     opt
     105240-105257    12    RTE-6/VM Virtual Memory       -       -      opt     opt
     105300-105317    14    Distributed System            -       -      opt     opt
     105320-105337    15    Double Integer                -       -      opt      -
     105320-105337    15    Scientific Instruction Set    -       -       -      std
     105340-105357    16    RTE-6/VM Operating System     -       -      opt     opt

   UIG-1 pertains only to the 1000 M/E/F-Series machines, as follows:

     Instructions   IR 7-4  Option Name                  2100   1000-M  1000-E  1000-F
     -------------  ------  --------------------------  ------  ------  ------  ------
     10x400-10x437  00-01   2000 I/O Processor            -      opt     opt      -
     10x460-10x477    03    2000 I/O Processor            -      opt     opt      -
     10x460-10x477    03    Vector Instruction Set        -       -       -      opt
     10x520-10x537    05    Distributed System            -      opt      -       -
     10x600-10x617    10    SIGNAL/1000 Instruction Set   -       -       -      opt
     10x700-10x737  14-15   Dynamic Mapping System        -      opt     opt     std
     10x740-10x777  16-17   Extended Instruction Group    -      std     std     std


   Implementation notes:

    1. Indexing is employed where possible, but the 21xx/1000 instruction set is
       quite irregular, so conditionals and linear searches are needed to
       resolve many of the instructions.

    2. The no-operation opcode is handled as a special case within the SRG
       micro-ops table.
*/

t_stat fprint_cpu (FILE *ofile, t_addr addr, t_value *val, uint32 radix, SYMBOL_SOURCE source)
{
const  t_value opcode = val [0];                        /* the instruction opcode */
t_bool separator = FALSE;                               /* TRUE if a separator between multiple ops is needed */
t_stat status    = SCPE_ARG;                            /* initial return status is "invalid opcode" */

if (MRGOP (opcode)) {                                   /* if this is an MRG instruction */
    if (source == Device_Symbol)                        /*   then if the offset must be relative */
        addr = PA_MAX + 1;                              /*     then pass an invalid address */

    status = fprint_instruction (ofile, addr, val,      /* print the instruction */
                                  radix, mrg_desc, mrg_ops);
    }


else if (SRGOP (opcode)) {                              /* otherwise if this is an SRG instruction */
    if (opcode & SRG1_DE_MASK) {                        /*   then if the first shift is enabled */
        status = fprint_instruction (ofile, addr, val,  /*     then print the first shift operation */
                                     radix, srg1_desc, srg1_ops);

        separator = TRUE;                               /* we will need a separator */
        }

    if (opcode == SRG_NOP                               /* if this is a NOP */
      || (opcode & (SRG_CLE | SRG_SLx))) {              /*   or a micro-op is present */
        if (separator)                                  /*     then if a separator is needed */
            fputc (',', ofile);                         /*       then print it */

        status = fprint_instruction (ofile, addr, val,  /* print the micro-op(s) */
                                     radix, srg_udesc, srg_uops);

        separator = TRUE;                               /* we will need a separator */
        }

    if (opcode & SRG2_DE_MASK) {                        /* if the second shift is enabled */
        if (separator)                                  /*   then if a separator is needed */
            fputc (',', ofile);                         /*     then print it */

        status = fprint_instruction (ofile, addr, val,  /* print the second shift operation */
                                     radix, srg2_desc, srg2_ops);
        }
    }


else if (ASGOP (opcode))                                /* otherwise if this is an ASG instruction */
    status = fprint_instruction (ofile, addr, val,      /*   then print it */
                                 radix, asg_udesc, asg_uops);


else if (IOGOP (opcode))                                /* otherwise if this is an IOG instruction */
    status = fprint_instruction (ofile, addr, val,      /*   then print it */
                                 radix, iog_desc, iog_ops);

else {                                                  /* otherwise this is a MAC group instruction */
    if (source == CPU_Trace)                            /* if this is a CPU trace call */
        addr = addr | VAL_EMPTY;                        /*   then indicate that the value array has not been loaded */

    if (UIG_0_OP (opcode)) {                            /* if this is a UIG-0 instruction */
        status = fprint_instruction (ofile, addr, val,  /*   then try to print it as a 2100 IOP instruction */
                                     radix, iop_2100_desc, iop_2100_ops);

        if (status == SCPE_UNK)                         /* if it's not a 2100 IOP instruction */
            switch (UIG (opcode)) {                     /*   then dispatch on the feature group */

                case 000:                                           /* 105000-105017 */
                case 001:                                           /* 105020-105037 */
                case 002:                                           /* 105040-105057 */
                case 003:                                           /* 105060-105077 */
                case 004:                                           /* 105100-105117 */
                case 005:                                           /* 105120-105137 */
                    status = fprint_instruction (ofile, addr, val,  /* try to print as an FP instruction */
                                                 radix, fp_desc, fp_ops);

                    if (status == SCPE_UNK)                             /* if it's not an FP instruction */
                        status = fprint_instruction (ofile, addr, val,  /*   then try again as an FPP instruction */
                                                     radix, fpp_desc, fpp_ops);
                    break;


                case 010:                                               /* 105200-105217 */
                case 011:                                               /* 105220-105237 */
                    status = fprint_instruction (ofile, addr, val,      /* try to print as a 2100 FFP instruction */
                                                 radix, ffp_2100_desc, ffp_2100_ops);

                    if (status == SCPE_UNK)                             /* if it's not a 2100 FFP opcode */
                        status = fprint_instruction (ofile, addr, val,  /*   then try again as a 1000-F FFP opcode */
                                                     radix, ffp_f_desc, ffp_f_ops);

                    if (status == SCPE_UNK)                             /* if it's not a 1000-F FFP opcode */
                        status = fprint_instruction (ofile, addr, val,  /*   then try again as a 1000-E FFP opcode */
                                                     radix, ffp_e_desc, ffp_e_ops);

                    if (status == SCPE_UNK)                             /* if it's not a 1000-E FFP opcode */
                        status = fprint_instruction (ofile, addr, val,  /*   then try again as a 1000-M FFP opcode */
                                                     radix, ffp_m_desc, ffp_m_ops);
                    break;


                case 012:                                               /* 105240-105257 */
                    status = fprint_instruction (ofile, addr, val,      /* try to print as a VMA instruction */
                                                 radix, vma_desc, vma_ops);

                    if (status == SCPE_UNK)                             /* if it's not a VMA instruction */
                        status = fprint_instruction (ofile, addr, val,  /*   then try again as an EMA instruction */
                                                     radix, ema_desc, ema_ops);
                    break;


                case 014:                                           /* 105300-105317  Distributed System */
                    break;

                case 015:                                               /* 105320-105337 */
                    status = fprint_instruction (ofile, addr, val,      /* try to print as an SIS instruction */
                                                 radix, sis_desc, sis_ops);

                    if (status == SCPE_UNK)                             /* if it's not an SIS instruction */
                        status = fprint_instruction (ofile, addr, val,  /*   then try again as a DBI instruction */
                                                     radix, dbi_desc, dbi_ops);
                    break;


                case 016:                                               /* 105340-105357 */
                    if (opcode >= RTE_IRQ_RANGE                         /* if the opcode is in the interrupt use range */
                      && addr >= OPTDEV && addr <= MAXDEV)              /*   and it's located in a trap cell */
                        status = fprint_instruction (ofile, addr, val,  /*     then print as an IRQ instruction */
                                                     radix, trap_desc, trap_ops);
                    else                                                /* otherwise */
                        status = fprint_instruction (ofile, addr, val,  /*   print as an OS-assist instruction */
                                                     radix, os_desc, os_ops);
                    break;


                default:                                /* all other feature groups */
                    status = SCPE_ARG;                  /*   are unimplemented */
                }
        }


    else if (UIG_1_OP (opcode))                         /* otherwise if this is a UIG-1 instruction */
        switch (UIG (opcode)) {                         /*   then dispatch on the feature group */

            case 000:                                           /* 10x400-10x417 */
            case 001:                                           /* 10x420-10x437 */
                status = fprint_instruction (ofile, addr, val,  /* print as an IOP instruction */
                                             radix, iop1_1000_desc, iop1_1000_ops);
                break;

            case 003:                                           /* 10x460-10x477 */
                status = fprint_instruction (ofile, addr, val,  /* try to print as an IOP instruction */
                                             radix, iop2_1000_desc, iop2_1000_ops);

                if (status == SCPE_UNK) {                                       /* if it's not an IOP opcode */
                    if (source == CPU_Trace)                                    /*   then if this is a CPU trace call */
                        val [1] = mem_fast_read (addr + 1 & LA_MASK, dms_ump);  /*   then load the following word */

                    status = fprint_instruction (ofile, addr, val,  /* print as a VIS instruction */
                                                 radix, vis_desc, vis_ops);
                    }
                break;


            case 010:                                           /* 10x600-10x617 */
                status = fprint_instruction (ofile, addr, val,  /* print as a SIGNAL instruction */
                                             radix, sig_desc, sig_ops);
                break;


            case 014:
            case 015:                                           /* 10x700-10x737 */
                status = fprint_instruction (ofile, addr, val,  /* print as a DMS instruction */
                                             radix, dms_desc, dms_ops);
                break;


            case 016:
            case 017:                                           /* 10x740-10x777 */
                status = fprint_instruction (ofile, addr, val,  /* print as an EIG instruction */
                                             radix, eig_desc, eig_ops);
                break;


            default:                                    /* all other feature groups */
                status = SCPE_ARG;                      /*   are unimplemented */
            }


    else {                                              /* otherwise */
        status = fprint_instruction (ofile, addr, val,  /*   print as an EAG instruction */
                                     radix, eag_desc, eag_ops);

        if (status == SCPE_ARG)                             /* if it's not an EAG opcode but the EAU is present */
            status = fprint_instruction (ofile, addr, val,  /*   then try as a 1000-E/F extension instruction */
                                         radix, eag_ef_desc, eag_ef_ops);
        }
    }

return status;                                          /* return the consumption status */
}


/* Format a character for printing.

   This routine formats a single 8-bit character value into a printable string
   and returns a pointer to that string.  Printable characters retain their
   original form but are enclosed in single quotes.  Control characters are
   translated to readable strings.  Characters outside of the ASCII range but
   within the full character range (i.e., less than 377 octal) are presented as
   escaped octal values.  Characters outside of the full character range are
   masked to the lower eight bits and printed as a plain octal value.


   Implementation notes:

    1. The longest string to be returned is a five-character escaped string
       consisting of a backslash, three octal digits, and a trailing NUL.  The
       end-of-buffer pointer has an allowance to ensure that the string will
       fit.

    2. The routine returns a pointer to a static buffer containing the printable
       string.  To allow the routine to be called more than once per trace line,
       the null-terminated format strings are concatenated in the buffer, and
       each call returns a pointer that is offset into the buffer to point at
       the latest formatted string.

    3. There is no explicit buffer-free action.  Instead, newly formatted
       strings are appended to the buffer until there is no more space
       available.  At that point, the pointers are reset to the start of the
       buffer.  In effect, this provides a circular buffer, as previously
       formatted strings are overwritten by subsequent calls.

    4. The buffer is sized to hold the maximum number of concurrent strings
       needed for a single trace line.  If more concurrent strings are used, one
       or more strings from the earliest calls will be overwritten.
*/

const char *fmt_char (uint32 charval)
{
static const char *const control [] = {
    "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL",
    "BS",  "HT",  "LF",  "VT",  "FF",  "CR",  "SO",  "SI",
    "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB",
    "CAN", "EM",  "SUB", "ESC", "FS",  "GS",  "RS",  "US"
    };
static char fmt_buffer [64];                                /* the return buffer */
static char *freeptr = fmt_buffer;                          /* pointer to the first free character in the buffer */
static char *endptr  = fmt_buffer + sizeof fmt_buffer - 5;  /* pointer to the end of the buffer (less allowance) */
const  char *fmtptr;

if (charval <= 0037u)                                   /* if the value is an ASCII control character */
    return control [charval];                           /*   then return a readable representation */

else if (charval == 0177u)                              /* otherwise if the value is the delete character */
    return "DEL";                                       /*   then return a readable representation */

else {
    if (freeptr > endptr)                               /* if there is not enough room left to append the string */
        freeptr = fmt_buffer;                           /*   then reset to point at the start of the buffer */

    fmtptr = freeptr;                                   /* initialize the return pointer */
    *freeptr = '\0';                                    /*   and the format accumulator */

    if (charval > D8_UMAX)                                  /* if the value is beyond the 8-bit character range */
        freeptr = freeptr + sprintf (freeptr, "%03o",       /*   then format the lower byte as octal */
                                     LOWER_BYTE (charval)); /*     and update the free pointer */

    else if (charval > 0177u)                               /* otherwise if the value is beyond the printable range */
        freeptr = freeptr + sprintf (freeptr, "\\%03o",     /*   then format as an escaped octal value */
                                     charval);              /*     and update the free pointer */

    else {                                              /* otherwise it's a printable character */
        *freeptr++ = '\'';                              /*   so form a representation */
        *freeptr++ = (char) charval;                    /*     containing the character */
        *freeptr++ = '\'';                              /*       surrounded by single quotes */
        *freeptr   = '\0';
        }

    freeptr = freeptr + 1;                              /* advance past the NUL terminator */
    return fmtptr;                                      /*   and return the formatted string */
    }
}


/* Format a set of named bits.

   This routine formats a set of up to 32 named bits into a printable string and
   returns a pointer to that string.  The names of the active bits are
   concatenated and separated by vertical bars.  For example:

     ready | no error | unit 0

   On entry, "bitset" is a value specifying the bits to format, and "bitfmt" is
   a BITSET_FORMAT structure describing the format to use.  The structure
   contains a count and a pointer to an array of character strings specifying
   the names of the valid bits in "bitset", the offset in bits from the LSB to
   the least-significant named bit, the direction in which to process the bits
   (from MSB to LSB, or vice versa), whether or not alternate names are present
   in the name array, and whether or not to append a final separator.  The names
   in the name array appear in the order corresponding to the supplied
   direction; invalid bits are indicated by NULL character names.  The pointer
   returned points at a character buffer containing the names of the valid bits
   that are set in the supplied value.  If no valid bits are set, then the
   buffer contains "(none)" if a trailing separator is omitted, or a null string
   ("") if a trailing separator is requested.

   The name_count and names fields describe the separately defined name string
   array.  The array must start with the first valid bit but need only contain
   entries through the last valid bit; NULL entries for the remaining bits in
   the word are not necessary.  For example, if bits 14-12 of a word are valid,
   then the name string array would have three entries.  If bits 14-12 and 10
   are valid, then the array would have five entries, with the fourth entry set
   to NULL.

   The offset field specifies the number of unnamed bits to the right of the
   least-significant named bit.  Using the same examples as above, the offsets
   would be 12 and 10, respectively.

   The direction field specifies whether the bits are named from MSB to LSB
   (msb_first) or vice versa (lsb_first).  The order of the entries in the name
   string array must match the direction specified.  Continuing with the first
   example above, if the direction is msb_first, then the first name is for bit
   14; if the direction is lsb_first, then the first name is for bit 12.

   The alternate field specifies whether (has_alt) or not (no_alt) alternate
   conditions are represented by one or more bits.  Generally, bits represent
   Boolean conditions, e.g., a condition is present when the bit is 1 and absent
   when the bit is zero.  In these cases, the corresponding bit name is included
   or omitted, respectively, in the return string.

   Occasionally, bits will represent alternate conditions, e.g., where condition
   A is present when the bit is 1, and condition B is present when the bit is 0.
   For these, the bit name string should consist of both condition names in that
   order, with the "1" name preceded by the '\1' character and the "0" name
   preceded by the '\0' character.  For example, if 1 corresponds to "load" and
   0 to "store", then the bit name string would be "\1load\0store".  If
   alternate names are present, the has_alt identifier should be given, so that
   the indicated bits are checked for zero conditions.  If no_alt is specified,
   the routine stops as soon as all of the one-bits have been processed.

   The bar field specifies whether (append_bar) or not (no_bar) a vertical bar
   separator is appended to the formatted string.  Typically, a bitset
   represents a peripheral control or status word.  If the word also contains
   multiple-bit fields, a trailing separator should be requested, and the
   decoded fields should be concatenated by the caller with any named bits.  If
   the bitset is empty, the returned null string will present the proper display
   containing just the decoded fields.  If the bitset completely describes the
   word, then no appended separator is needed.

   Peripheral control and status words generally are decoded from MSB to LSB.  A
   bitset may also represent a set of inbound or outbound signals.  These should
   be decoded from LSB to MSB, as that is the order in which they are executed
   by the device interface routines.

   The implementation first generates a mask for the significant bits and
   positions the mask with the offset specified.  Then a test bit mask is
   generated; the bit is either the most- or least-significant bit of the
   bitset, depending on the direction indicated.

   For each name in the array of names, if the name is defined (not NULL), the
   corresponding bit in the bitset is tested.  If it is set, the name is
   appended to the output buffer; otherwise, it is omitted (unless the name has
   an alternate, in which case the alternate is appended).  The bitset is then
   shifted in the indicated direction, remasking to just the significant bits.
   Processing continues until there are no remaining significant bits (if no
   alternates are specified), or until there are no remaining names in the array
   (if alternates are specified).


   Implementation notes:

    1. The routine returns a pointer to a static buffer containing the printable
       string.  To allow the routine to be called more than once per trace line,
       the null-terminated format strings are concatenated in the buffer, and
       each call returns a pointer that is offset into the buffer to point at
       the latest formatted string.

    2. There is no explicit buffer-free action.  Instead, newly formatted
       strings are appended to the buffer until there is no more space
       available.  At that point, the string currently being assembled is moved
       to the start of the buffer, and the pointers are reset.  In effect, this
       provides a circular buffer, as previously formatted strings are
       overwritten by subsequent calls.

    3. The buffer is sized to hold the maximum number of concurrent strings
       needed for a single trace line.  If more concurrent strings are used, one
       or more strings from the earliest calls will be overwritten.  If an
       attempt is made to format a string larger than the buffer, an error
       indication string is returned.

    4. The location of the end of the buffer used to determine if the next name
       will fit includes an allowance for two separators that might be placed on
       either side of the name and a terminating NUL character.
*/

const char *fmt_bitset (uint32 bitset, const BITSET_FORMAT bitfmt)
{
static const char separator [] = " | ";                     /* the separator to use between names */
static char fmt_buffer [1024];                              /* the return buffer */
static char *freeptr = fmt_buffer;                          /* pointer to the first free character in the buffer */
static char *endptr  = fmt_buffer + sizeof fmt_buffer       /* pointer to the end of the buffer */
                         - 2 * (sizeof separator - 1) - 1;  /*   less allowance for two separators and a terminator */
const char *bnptr, *fmtptr;
uint32     test_bit, index, bitmask;
size_t     name_length;

if (bitfmt.name_count < D32_WIDTH)                      /* if the name count is the less than the mask width */
    bitmask = (1 << bitfmt.name_count) - 1;             /*   then create a mask for the name count specified */

else                                                    /* otherwise use a predefined value for the mask */
    bitmask = D32_MASK;                                 /*   to prevent shifting the bit off the MSB end */

bitmask = bitmask << bitfmt.offset;                     /* align the mask to the named bits */
bitset = bitset & bitmask;                              /*   and mask to just the significant bits */

if (bitfmt.direction == msb_first)                          /* if the examination is left-to-right */
    test_bit = 1 << bitfmt.name_count + bitfmt.offset - 1;  /*   then create a test bit for the MSB */
else                                                        /* otherwise */
    test_bit = 1 << bitfmt.offset;                          /*   create a test bit for the LSB */


fmtptr = freeptr;                                       /* initialize the return pointer */
*freeptr = '\0';                                        /*   and the format accumulator */
index = 0;                                              /*     and the name index */

while ((bitfmt.alternate || bitset)                     /* while more bits */
  && index < bitfmt.name_count) {                       /*   and more names exist */
    bnptr = bitfmt.names [index];                       /*     point at the name for the current bit */

    if (bnptr)                                          /* if the name is defined */
        if (*bnptr == '\1' && bitfmt.alternate)         /*   then if this name has an alternate */
            if (bitset & test_bit)                      /*     then if the bit is asserted */
                bnptr++;                                /*       then point at the name for the "1" state */
            else                                        /*     otherwise */
                bnptr = bnptr + strlen (bnptr) + 1;     /*       point at the name for the "0" state */

        else                                            /*   otherwise the name is unilateral */
            if ((bitset & test_bit) == 0)               /*     so if the bit is denied */
                bnptr = NULL;                           /*       then clear the name pointer */

    if (bnptr) {                                        /* if the name pointer is set */
        name_length = strlen (bnptr);                   /*   then get the length needed */

        if (freeptr + name_length > endptr) {           /* if there is not enough room left to append the name */
            strcpy (fmt_buffer, fmtptr);                /*   then move the partial string to the start of the buffer */

            freeptr = fmt_buffer + (freeptr - fmtptr);  /* point at the new first free character location */
            fmtptr = fmt_buffer;                        /*   and reset the return pointer */

            if (freeptr + name_length > endptr)         /* if there is still not enough room left to append */
                return "(buffer overflow)";             /*   then this call is requires a larger buffer! */
            }

        if (*fmtptr != '\0') {                          /* if this is not the first name added */
            strcpy (freeptr, separator);                /*   then add a separator to the string */
            freeptr = freeptr + strlen (separator);     /*     and move the free pointer */
            }

        strcpy (freeptr, bnptr);                        /* append the bit's mnemonic to the accumulator */
        freeptr = freeptr + name_length;                /*   and move the free pointer */
        }

    if (bitfmt.direction == msb_first)                  /* if formatting is left-to-right */
        bitset = bitset << 1 & bitmask;                 /*   then shift the next bit to the MSB and remask */
    else                                                /* otherwise formatting is right-to-left */
        bitset = bitset >> 1 & bitmask;                 /*   so shift the next bit to the LSB and remask */

    index = index + 1;                                  /* bump the bit name index */
    }


if (*fmtptr == '\0')                                    /* if no names were output */
    if (bitfmt.bar == append_bar)                       /*   then if concatenating with more information */
        return "";                                      /*     then return an empty string */
    else                                                /*   otherwise it's a standalone format */
        return "(none)";                                /*     so return a placeholder */

else if (bitfmt.bar == append_bar) {                    /* otherwise if a trailing separator is specified */
    strcpy (freeptr, separator);                        /*   then add a separator to the string */
    freeptr = freeptr + strlen (separator) + 1;         /*     and account for it plus the trailing NUL */
    }

else                                                    /* otherwise */
    freeptr = freeptr + 1;                              /*   just account for the trailing NUL */

return fmtptr;                                          /* return a pointer to the formatted string */
}


/* Format and print a trace line to the debug log file.

   A formatted line is assembled and sent to the previously opened debug output
   stream.  On entry, "dptr" points to the device issuing the trace, "flag" is
   the trace flag that has enabled the trace, and the remaining parameters
   consist of the format string and associated values.

   This routine is usually not called directly but rather via the "tprintf"
   macro, which tests that tracing is enabled for the specified flag before
   calling this function.  This eliminates the calling overhead if tracing is
   disabled.

   This routine prints a prefix before the supplied format string consisting of
   the device name (in upper case) and the trace flag name (in lower case),
   e.g.:

     >>MPX state: Channel SR 3 entered State A
     ~~~~~~~~~~~~ ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        prefix       supplied format string

   The names are padded to the lengths of the largest device name and trace flag
   name among the devices enabled for tracing to ensure that all trace lines
   will align for easier reading.


   Implementation notes:

    1. ISO C99 allows assignment expressions as the bounds for array
       declarators.  VC++ 2008 requires constant expressions.  To accommodate
       the latter, we must allocate "sufficiently large" arrays for the flag
       name and format, rather than arrays of the exact size required by the
       call parameters.
*/

#define FLAG_SIZE           32                          /* sufficiently large to accommodate all flag names */
#define FORMAT_SIZE         1024                        /* sufficiently large to accommodate all format strings */

void hp_trace (DEVICE *dptr, uint32 flag, ...)
{
const char *nptr;
va_list    argptr;
DEBTAB     *debptr;
char       *format, *fptr;
char       flag_name [FLAG_SIZE];                       /* desired size is [flag_size + 1] */
char       header_fmt [FORMAT_SIZE];                    /* desired size is [device_size + flag_size + format_size + 6] */

if (sim_deb != NULL && dptr != NULL) {                  /* if the output stream and device pointer are valid */
    debptr = dptr->debflags;                            /*   then get a pointer to the trace flags table */

    if (debptr != NULL)                                 /* if the trace table exists */
        while (debptr->name != NULL)                    /*   then search it for an entry with the supplied flag */
            if (debptr->mask & flag) {                  /* if the flag matches this entry */
                nptr = debptr->name;                    /*   then get a pointer to the flag name */
                fptr = flag_name;                       /*     and the buffer */

                do
                    *fptr++ = (char) tolower (*nptr);   /* copy and downshift the flag name */
                while (*nptr++ != '\0');

                sprintf (header_fmt, ">>%-*s %*s: ",            /* format the prefix and store it */
                         (int) device_size, sim_dname (dptr),   /*   while padding the device and flag names */
                         (int) flag_size, flag_name);           /*     as needed for proper alignment */

                va_start (argptr, flag);                        /* set up the argument list */

                format = va_arg (argptr, char *);               /* get the format string parameter */
                strcat (header_fmt, format);                    /* append the supplied format */

                vfprintf (sim_deb, header_fmt, argptr);         /* format and print to the debug stream */

                va_end (argptr);                                /* clean up the argument list */
                break;                                          /*   and exit with the job complete */
                }

            else                                        /* otherwise */
                debptr++;                               /*   look at the next trace table entry */
    }

return;
}


/* Check for device conflicts.

   The device information blocks (DIBs) for the set of enabled devices are
   checked for consistency.  Each select code must be unique among the enabled
   devices.  This requirement is checked as part of the instruction execution
   prelude; this allows the user to exchange two select codes simply by setting
   each device to the other's select code.  If conflicts were enforced instead
   at the time the codes were entered, the first device would have to be set to
   an unused select code before the second could be set to the first device's
   code.

   The routine begins by filling in a DIB value table from all of the device
   DIBs to allow indexed access to the values to be checked.  Unused DIB values
   and values corresponding to devices that have no DIBs or are disabled are set
   to the corresponding UNUSED constants.

   As part of the device scan, the sizes of the largest device name and active
   trace flag name among the devices enabled for tracing are accumulated for use
   in aligning the trace statements.

   After the DIB value table is filled in, a conflict check is made by building
   a conflict table, where each array element is set to the count of devices
   that contain DIB value equal to the element index.  For example, conflict
   table element 6 is set to the count of devices that have dibptr->select_code
   set to 6.  If any conflict table element is set more than once, the
   "conflict_is" variable is set.

   If any conflicts exist, the conflict table is scanned.  A conflict table
   element value greater than 1 indicates a conflict.  For each such value, the
   DIB value table is scanned to find matching values, and the device names
   associated with the matching values are printed.

   This routine returns TRUE if any conflicts exist and FALSE there are none.


   Implementation notes:

    1. When this routine is called, the console and optional log file have
       already been put into "raw" output mode.  Therefore, newlines are not
       translated to the correct line ends on systems that require it.  Before
       reporting a conflict, "sim_ttcmd" is called to restore the console and
       log file translation.  This is OK because a conflict will abort the run
       and return to the command line anyway.

    2. sim_dname is called instead of using dptr->name directly to ensure that
       we pick up an assigned logical device name.

    3. Only the names of active trace (debug) options are accumulated to produce
       the most compact trace log.  However, if the CPU device's EXEC option is
       enabled, then all of the CPU option names are accumulated, as EXEC
       enables all trace options for a given instruction or instruction class.

    4. Even though the routine is called only from the sim_instr routine in the
       CPU simulator module, it must be located here to use the DEVICE_COUNT
       constant to allocate the dib_val matrix.  If it were located in the CPU
       module, the matrix would have to be allocated dynamically after a
       run-time determination of the count of simulator devices.
*/

t_bool hp_device_conflict (void)
{
const DIB     *dibptr;
const DEBTAB  *tptr;
DEVICE        *dptr;
size_t        name_length, flag_length;
uint32        dev, val;
int32         count;
int32         dib_val   [DEVICE_COUNT];
int32         conflicts [MAXDEV + 1];
t_bool        is_conflict = FALSE;

device_size = 0;                                        /* reset the device and flag name sizes */
flag_size = 0;                                          /*   to those of the devices actively tracing */

memset (conflicts, 0, sizeof conflicts);                /* zero the conflict table */

for (dev = 0; dev < DEVICE_COUNT; dev++) {              /* fill in the DIB value table */
    dptr = sim_devices [dev];                           /*   from the device table */
    dibptr = (DIB *) dptr->ctxt;                        /*     and the associated DIBs */

    if (dibptr && !(dptr->flags & DEV_DIS)) {           /* if the DIB is defined and the device is enabled */
        dib_val [dev] = dibptr->select_code;            /*   then copy the values to the DIB table */

        if (++conflicts [dibptr->select_code] > 1)      /* increment the count of references; if more than one */
            is_conflict = TRUE;                         /*   then a conflict occurs */
        }

    if (sim_deb && dptr->dctrl) {                       /* if tracing is active for this device */
        name_length = strlen (sim_dname (dptr));        /*   then get the length of the device name */

        if (name_length > device_size)                  /* if it's greater than the current maximum */
            device_size = name_length;                  /*   then reset the size */

        if (dptr->debflags)                                 /* if the device has a trace flags table */
            for (tptr = dptr->debflags;                     /*   then scan the table */
                 tptr->name != NULL; tptr++)
                if (dev == 0 && dptr->dctrl & TRACE_EXEC    /* if the CPU device is tracing executions */
                  || tptr->mask & dptr->dctrl) {            /*   or this trace option is active */
                    flag_length = strlen (tptr->name);      /*     then get the flag name length */

                    if (flag_length > flag_size)            /* if it's greater than the current maximum */
                        flag_size = flag_length;            /*   then reset the size */
                    }
        }
    }

if (is_conflict) {                                      /* if a conflict exists */
    sim_ttcmd ();                                       /*   then restore the console and log I/O mode */

for (val = 0; val <= MAXDEV; val++)                     /* search the conflict table for the next conflict */
    if (conflicts [val] > 1) {                          /* if a conflict is present for this value */
        count = conflicts [val];                        /*   then get the number of conflicting devices */

        cprintf ("Select code %o conflict (", val);     /* report the multiply-assigned select code */

        dev = 0;                                        /* search for the devices that conflict */

        while (count > 0) {                             /* search the DIB value table */
            if (dib_val [dev] == (int32) val) {         /*   to find the conflicting entries */
                if (count < conflicts [val])            /*     and report them to the console */
                    cputs (" and ");

                cputs (sim_dname (sim_devices [dev]));  /* report the conflicting device name */
                count = count - 1;                      /*   and drop the count of remaining conflicts */
                }

            dev = dev + 1;                              /* move to the next device */
            }                                           /*   and loop until all conflicting devices are reported */

        cputs (")\n");                                  /* tie off the line */
        }                                               /*   and continue to look for other conflicting select codes */
    }

return (is_conflict);                                   /* return TRUE if any conflicts exist */
}


/* Make a pair of devices consistent */

void hp_enbdis_pair (DEVICE *ccptr, DEVICE *dcptr)
{
if (ccptr->flags & DEV_DIS)
    dcptr->flags |= DEV_DIS;
else
    dcptr->flags &= ~DEV_DIS;

return;
}



/* System interface local SCP support routines */


/* One-time initialization.

   This routine is called once by the SCP startup code.  It fills in the
   auxiliary command table from the corresponding system command table entries,
   sets up the VM-specific routine pointers, and registers the supported
   breakpoint types.
*/

static void one_time_init (void)
{
CTAB *systab, *auxtab = aux_cmds;

while (auxtab->name != NULL) {                          /* loop through the auxiliary command table */
    systab = find_cmd (auxtab->name);                   /* find the corresponding system command table entry */

    if (systab != NULL) {                               /* if it is present */
        if (auxtab->action == NULL)                     /*   then if the action routine field is empty */
            auxtab->action = systab->action;            /*     then fill it in */

        if (auxtab->arg == 0)                           /* if the command argument field is empty */
            auxtab->arg = systab->arg;                  /*   then fill it in */

        if (auxtab->help == NULL)                       /* if the help string field is empty */
            auxtab->help = systab->help;                /*   then fill it in */

        auxtab->help_base = systab->help_base;          /* fill in the help base and message fields */
        auxtab->message   = systab->message;            /*   as we never override them */
        }

    auxtab++;                                           /* point at the next table entry */
    }

sim_vm_cmd            = aux_cmds;                       /* set up the auxiliary command table */
sim_vm_fprint_stopped = &fprint_stopped;                /* set up the simulation-stop printer */
sim_vm_fprint_addr    = &fprint_addr;                   /* set up the address printer */
sim_vm_parse_addr     = &parse_addr;                    /* set up the address parser */
sim_vm_post           = &cpu_post_cmd;                  /* set up the command post-processor */

sim_brk_types = BP_SUPPORTED;                           /* register the supported breakpoint types */
sim_brk_dflt  = BP_ENONE;                               /* the default breakpoint type is "execution" */

return;
}


/* Format and print a VM simulation stop message.

   When the instruction loop is exited, a simulation stop message is printed and
   control returns to SCP.  An SCP stop prints a message with this format:

     <reason>, P: <addr> (<inst>)

   For example:

     SCPE_STOP prints "Simulation stopped, P: 24713 (CLA)"
     SCPE_STEP prints "Step expired, P: 24713 (CLA)"

   For VM stops, this routine is called after the message has been printed and
   before the comma and program counter label and value are printed.  Depending
   on the reason for the stop, the routine may insert additional information,
   and it may request omission of the PC value by returning FALSE instead of
   TRUE.

   This routine modifies the default output for these stop codes:

     STOP_HALT   prints "Programmed halt, T: 102077 (HLT 77), P: 24713 (CLA)"
     STOP_NOTAPE prints "Tape not loaded in the <dev> device, P: 24713 (CLA)"
     STOP_EOT    prints "End of tape on the <dev> device, P: 24713 (CLA)"
     STOP_NOCONN prints "Cable not connected to the <dev> device, P: 24713 (CLA)"

   The HP 21xx/1000 halt instruction opcode includes select code and device flag
   hold/clear bit fields.  In practice, these are not used to affect the device
   interface; rather, they communicate to the operator the significance of the
   particular halt encountered.

   Under simulation, the halt opcode must be communicated to the user as part of
   the stop message.  To so do, we define a sim_vm_fprint_stopped handler that
   is called for all VM stops.  When called for a STOP_HALT, the halt message
   has been printed, and we add the opcode value in the T register before
   returning TRUE, so that SCP will add the program counter value.

   For unreported I/O error stops, the message must include the device name, so
   the user will know how to correct the error.  For these stops, the global
   variable "cpu_ioerr_uptr" will point at the unit that encountered the error.


   Implementation notes:

    1. Normally, the "sim_eval" global value array either is preloaded with the
       instruction and its operand addresses, or the first element is set to the
       instruction and the address parameter includes the VAL_EMPTY flag to
       indicate that the operands have not yet been loaded.  In the STOP_HALT
       case, the instruction is always a HLT, which is a single-word
       instruction.  Therefore, the VAL_EMPTY flag will not be checked and need
       not be included.
*/

static t_bool fprint_stopped (FILE *st, t_stat reason)
{
DEVICE *dptr;

if (reason == STOP_HALT) {                              /* if this is a halt instruction stop */
    sim_eval [0] = (t_value) TR;                        /*   then save the opcode for display */

    fprintf (st, ", T: %06o (", TR);                    /* print the T register value */
    fprint_cpu (st, MR, sim_eval, 0, CPU_Symbol);       /*   then format and print the halt mnemonic */
    fputc (')', st);                                    /*     and finally close the parenthesis */
    }

else if (cpu_ioerr_uptr) {                              /* otherwise if this is an I/O error stop */
    dptr = find_dev_from_unit (cpu_ioerr_uptr);         /*   then get the device pointer from the unit */

    if (dptr)                                               /* if the search succeeded */
        fprintf (st, " the %s device", sim_dname (dptr));   /*   then report the device */
    else                                                    /* otherwise */
        fputs (" an unknown device", st);                   /*   report that the device is unknown */
    }

return TRUE;                                            /* return TRUE to append the program counter */
}


/* Format and print a memory address.

   This routine is called by SCP to print memory addresses.  It is also called
   to print the contents of registers tagged with the REG_VMAD flag.

   On entry, the "st" parameter is the opened output stream, "dptr" points to
   the device to which the address refers, and "addr" contains the address to
   print.  The routine prints the linear address in <page>.<offset> form for CPU
   addresses > 32K and as a scalar value for CPU addresses <= 32K and all other
   devices.
*/

static void fprint_addr (FILE *st, DEVICE *dptr, t_addr addr)
{
uint32 page, offset;

if (dptr == &cpu_dev && addr > LA_MAX) {                /* if a CPU address is outside of the logical address space */
    page = PAGE (addr);                                 /*   then separate page and offset */
    offset = OFFSET (addr);                             /*     from the linear address */

    fprint_val (st, page, dptr->aradix, PG_WIDTH, PV_RZRO);     /* print the page address */
    fputc ('.', st);                                            /*   followed by a period */
    fprint_val (st, offset, dptr->aradix, OF_WIDTH, PV_RZRO);   /*     and concluding with the offset */
    }

else                                                            /* otherwise print the value */
    fprint_val (st, addr, dptr->aradix, dptr->awidth, PV_LEFT); /*   as a scalar for all other devices */

return;
}


/* Parse a memory address.

   This routine is called by SCP to parse memory addresses.  It is also called
   to parse values to be stored in registers tagged with the REG_VMAD flag.

   On entry, the "dptr" parameter points to the device to which the address
   refers, and "cptr" points to the first character of the address operand on
   the command line.  On exit, the linear address is returned, and the pointer
   pointed to by "tptr" is set to point at the first character after the parsed
   address.  Parsing errors, including use of features disallowed by the command
   in process, are indicated by the "tptr" pointer being set to "cptr".

   The HP 1000 divides memory into 1K-word pages within a 32K logical address
   space.  With memory expansion disabled, only the first 32 pages are
   accessible.  With memory expansion enabled, memory maps are used to translate
   logical to physical addresses.  Each translation uses one of four possible
   maps.  Program accesses use the system map or the user map, depending on
   which one is designated the "current map."  DCPC accesses use the port A or
   port B map, depending on whether channel 1 or channel 2, respectively, is
   performing the access.  Each map translates the 32 logical pages to 32 of the
   1024 physical pages.

   The HP 2114, 2115, 2116, and 2100 provide up to 32 pages of memory.  Memory
   expansion is not provided with these machines.

   The simulator supports only linear addresses for all devices other than the
   CPU.  For the CPU, two forms of address entries are allowed:

     - a logical address consisting of a 15-bit offset within the 32K logical
       address space (e.g., 77777).

     - a physical address consisting of a 10-bit page number and a 10-bit
       offset within the page, separated by a period (e.g., 1777.1777)

   Command line switches modify the interpretation of logical addresses as
   follows:

     Switch   Interpretation
     ------   --------------------------------------------------
       -N     Use no mapping
       -S     If memory expansion is enabled, use the system map
       -U     If memory expansion is enabled, use the user map
       -P     If memory expansion is enabled, use the port A map
       -Q     If memory expansion is enabled, use the port B map

   If no switch is specified, the address is interpreted using the current map
   if memory expansion is enabled; otherwise, the address is not mapped.  If the
   current or specified map is used, then the address must lie within the 32K
   logical address space.

   The "parse_physical" global specifies whether or not a physical address is
   permitted.  Addresses used by the RUN, GO, BREAK, and NOBREAK commands must
   resolve to logical addresses, whereas EXAMINE and DEPOSIT commands allow both
   physical and logical address specifications.
*/

static t_addr parse_addr (DEVICE *dptr, CONST char *cptr, CONST char **tptr)
{
CONST char *sptr;
t_addr     page;
t_addr     address = 0;

if (dptr != &cpu_dev)                                   /* if this is not a CPU memory address */
    return (t_addr) strtotv (cptr, tptr, dptr->aradix); /*   then parse a scalar and return the value */

address = strtotv (cptr, tptr, dptr->aradix);           /* parse the address */

if (cptr != *tptr)                                      /* if the parse succeeded */
    if (**tptr == '.')                                  /*   then if this a paged address */
        if (address > PG_MAX)                           /*     then if the page number is out of range */
            *tptr = cptr;                               /*       then report a parse error */

        else {                                              /* otherwise the <bank>.<offset> form is allowed */
            sptr = *tptr + 1;                               /* point to the offset */
            page = address;                                 /* save the first part as the bank address */
            address = strtotv (sptr, tptr, dptr->aradix);   /* parse the offset */

            if (address > OF_MAX)                       /* if the offset is too large */
                *tptr = cptr;                           /*   then report a parse error */
            else                                        /* otherwise it is in range */
                address = TO_PA (page, address);        /*   so form the linear address */
            }

    else if (address > LA_MAX)                          /* otherwise if the non-paged offset is too large */
        *tptr = cptr;                                   /*   then report a parse error */

if (parse_physical == FALSE                             /* if only logical addresses are permitted */
  && address > LA_MAX)                                  /*   and the parsed address is out of range */
    *tptr = cptr;                                       /*     then report a parse error */

return address;                                         /* return the linear address */
}


/* Execute the EXAMINE, DEPOSIT, IEXAMINE, and IDEPOSIT commands.

   These commands are intercepted to configure address parsing.  The following
   address forms are valid:

     EXAMINE <page>.<offset>
     EXAMINE <logical-address>

   This routine configures the address parser and calls the standard command
   handler.
*/

static t_stat hp_exdep_cmd (int32 arg, CONST char *buf)
{
parse_physical = TRUE;                                  /* allow the <page.<offset> address form */

return exdep_cmd (arg, buf);                            /* return the result of the standard handler */
}


/* Execute the RUN and GO commands.

   These commands are intercepted to configure address parsing.  The following
   address form is valid:

     RUN { <logical-address> }
     GO  { <logical-address> }

   If no argument is specified, the breakpoint address defaults to the current
   value of P.  This routine configures the address parser and calls the
   standard command handler.
*/

static t_stat hp_run_cmd (int32 arg, CONST char *buf)
{
parse_physical = FALSE;                                 /* allow the <logical-address> address form only */

return run_cmd (arg, buf);                              /* return the result of the standard handler */
}


/* Execute the BREAK and NOBREAK commands.

   These commands are intercepted to configure address parsing.  The following
   address forms are valid:

     BREAK
     BREAK <logical-address>

   If no argument is specified, the breakpoint address defaults to the current
   value of P.  This routine configures the address parser and calls the
   standard command handler.
*/

static t_stat hp_brk_cmd (int32 arg, CONST char *buf)
{
parse_physical = FALSE;                                 /* allow the <logical-address> address form only */

return brk_cmd (arg, buf);                              /* return the result of the standard handler */
}


/* Execute the LOAD command.

   This command is intercepted to permit a device boot routine to be loaded
   into memory.  The following command forms are valid:

     LOAD <dev>
     LOAD <filename> [ <select-code> ]

   If the first form is used, and the device name is valid and bootable, the
   corresponding boot loader is copied into memory in the highest 64 locations
   of the logical address space.  Upon return, the loader will have been
   configured for the device's select code or for the device and paper tape
   reader select codes, in the case of a dual-use 21xx boot loader, and the P
   register will have been set to point at the start of the loader.  The loader
   then may be executed by a RUN command.  If the device name is valid, but the
   device is not bootable, or no boot loader exists for the current CPU
   configuration, the command is rejected.

   If the second form is used, the file containing a loader in absolute binary
   form is read into memory and configured as above.  See the "sim_load"
   comments for details of this operation.


   Implementation notes:

    1. The "find_dev" routine requires that the device name be in upper case to
       match.  The "get_glyph" routine performs case-shifting on the input
       buffer.
*/

static t_stat hp_load_cmd (int32 arg, CONST char *buf)
{
CONST char *cptr;
char cbuf [CBUFSIZE];
DEVICE *dptr = NULL;

GET_SWITCHES (buf);                                     /* parse any switches present */

cptr = get_glyph (buf, cbuf, '\0');                     /* parse a potential device name */

if (cbuf [0] != '\0') {                                 /* if the name is present */
    dptr = find_dev (cbuf);                             /*   then see if it matches a device */

    if (dptr != NULL)                                   /* if it does */
        if (dptr->boot == NULL)                         /*   then if the device is not bootable */
            return SCPE_NOFNC;                          /*     then report "Command not allowed" */
        else if (*cptr != '\0')                         /*   otherwise if more characters follow */
            return SCPE_2MARG;                          /*     then report "Too many arguments" */
        else                                            /*   otherwise the device name stands alone */
            return dptr->boot (0, dptr);                /*     so load the corresponding boot loader */
    }

return load_cmd (arg, buf);                             /* if it's not a device name, then try loading a file */
}



/* System interface local utility routines */


/* Print a numeric value in a given radix.

   This routine prints a numeric value using the specified radix, width, and
   output format.  If the radix is 256, then the value is printed as a single
   character.  Otherwise, it is printed as a numeric value.

   On entry, the "ofile" parameter is the opened output stream, "val" is the
   value to print, "radix" is the desired print radix, "width" is the number of
   significant bits in the value, and "format" is a format specifier (PV_RZRO,
   PV_RSPC, or PV_LEFT).  On exit, the routine returns SCPE_OK if the value was
   printed successfully, or SCPE_ARG if the value could not be printed.
*/

static t_stat fprint_value (FILE *ofile, t_value val, uint32 radix, uint32 width, uint32 format)
{
if (radix == 256)                                       /* if ASCII character display is requested */
    if (val <= D8_SMAX) {                               /*   then if the value is a single character */
        fputs (fmt_char ((uint32) val), ofile);         /*     then format and print it */
        return SCPE_OK;                                 /*       and report success */
        }

    else                                                /* otherwise */
        return SCPE_ARG;                                /*   report that it cannot be displayed */

else                                                        /* otherwise format and print the value */
    return fprint_val (ofile, val, radix, width, format);   /*   using the supplied radix and format */
}


/* Print a CPU instruction in symbolic format.

   This routine is called to decode and print a CPU instruction mnemonic and any
   associated operand(s).  On entry, "ofile" is the opened output stream, "addr"
   is the memory address location of the instruction, "val" is a pointer to an
   array of t_values of depth "sim_emax" containing the instruction to be
   printed, "radix" is zero to use the default radix for the operand(s) or a
   value indicating a requested radix override, "op_desc" is a structure that
   describes the characteristics of the instruction opcode, and "ops" is the
   table of opcodes that pertain to the class to which the supplied instruction
   belongs.

   On exit, a status code is returned to the caller.  If the instruction is
   provided by a firmware option that is not installed, SCPE_UNK status is
   returned; this permits the caller to try a different opcode table if the
   opcode is in a shared address space (e.g., the EMA and VMA firmware).  If the
   instruction is not present in the supplied opcode table, SCPE_ARG status is
   returned, which causes the caller to print the instruction in numeric format
   with the default radix.  Otherwise, SCPE_OK status is returned if a
   single-word instruction was printed, or the negative number of extra words
   (beyond the first) consumed in printing the instruction is returned. For
   example, printing a two-word instruction returns SCPE_OK_2_WORDS (= -1).

   The "addr" parameter is used in two cases: to supply the operand address(es)
   that follow the instruction when the "val" array has not been fully
   populated, and to supply the current page for memory reference instructions
   that have the C bit set.

   If the routine is called to examine a memory location, the "val" array will
   be fully populated with the instruction and the values that follow.  However,
   if the routine is called by an instruction trace, only the first word is set;
   this is to avoid the overhead of loading extra operand words for instructions
   that do not have operands (the vast majority).  In this case, the "addr"
   parameter will contain the address of the instruction word ORed with the
   VAL_EMPTY flag.  Once the instruction is decoded, the routine knows the
   number of operand words required, and these are loaded before printing the
   operands.

   When printing a current-page memory reference instruction residing in CPU
   memory, the instruction word supplies the 10-bit offset from the page
   containing the instruction.  This is merged with the page number from the
   "addr" parameter to form the operand address.

   However, if the instruction is not in memory (e.g., it resides in a disc data
   buffer or device register), then the final load address is indeterminate.  In
   this case, the "addr" parameter is set to a value greater than the maximum
   possible memory address as an indication that only the current-page offset is
   valid.

   All machine instruction opcodes are single-word, with the exception of the
   vector arithmetic instructions in the Vector Instruction Set; these use
   two-word opcodes.  If the routine is entered to print a VIS instruction, "val
   [0]" and "val [1]" must both be set, even if the VAL_EMPTY flag is set.

   Upon entry, the required feature set from the opcode description is checked
   against the current CPU feature set.  If the required option is not
   installed, the routine exits with SCPE_UNK.  Otherwise, the instruction is
   masked and shifted to obtain the primary index into the opcode table.  For
   instructions that use the A/B-selector bit (bit 11), the index is shifted to
   the second half of the table if the bit is set.

   If the primary entry exists, then the opcode mnemonic is obtained from the
   entry and printed.  Otherwise, the secondary table entries must be searched
   linearly.  Each entry is compared to the instruction with the operand bits
   masked off; if a match occurs, then the opcode mnemonic is checked.  If it is
   empty, then this is a two-word instruction whose second word is invalid.  In
   this case, the routine prints the two words in numeric format, separated by a
   comma, and returns success to indicate that the instruction was printed.
   Otherwise the mnemonic is present, so it is printed.  If the opcode
   descriptor indicates that only a single match is permitted, the loop is
   exited to print the operand(s), if any.  Otherwise, multiple matches are
   allowed (e.g., for SRG instructions), so the secondary entry search is
   continued until the end of the table.

   If the instruction does not match either a primary or a secondary entry, the
   routine exits with SCPE_ARG status.  The caller will then print the value in
   numeric form.

   The type and count of operand(s), if any, are indicated by the "type" and
   "count" values in the matched opcode table entry.  The entry also indicates
   which operand(s) are addresses and which are data values.

   If operands are needed but not present in the "val" array, they are loaded
   now using the current DMS map.  The operand type dispatches to one of several
   handlers that obtain the operand(s) from the instruction.  Operands that are
   extracted from the instruction are placed in "val [1]" for printing.  Each
   operand is printed using the address radix for addresses and the supplied
   data radix for data.

   After printing the operand(s), the routine returns the number of words
   consumed.


   Implementation notes:

    1. If the opcode descriptor mask is zero, then the opcode table contains
       only secondary entries.  The descriptor shift value then determines
       whether a single match is allowed (zero) or multiple matches are allowed
       (non-zero).

    2. If the opcode descriptor A/B-register selector indicates that bit 11 is
       decoded, then the opcode primary table will be twice the size implied by
       the opcode mask width.  The first half of the table decodes instructions
       with bit 11 equal to zero; the second half decodes instruction with bit
       11 equal to 1.

    3. Only the SRG and ASG instructions contain multiple micro-opcodes, and
       none of these take operands.
*/

static t_stat fprint_instruction (FILE *ofile, t_addr addr, t_value *val, uint32 radix,
                                  const OP_DESC op_desc, const OP_TABLE ops)
{
OP_TYPE    op_type;
uint32     op_index, op_size, op_count, op_radix, op_address_set;
t_value    instruction, op_value;
t_stat     status;
const char *prefix   = NULL;                            /* label to print before the operand */
t_bool     clear     = FALSE;                           /* TRUE if the instruction contains a CLF micro-op */
t_bool     separator = FALSE;                           /* TRUE if a separator between multiple ops is needed */
uint32     op_start  = 1;                               /* the "val" array index of the first operand */

if (!(cpu_configuration & op_desc.feature & OPTION_MASK /* if the required feature set is not enabled */
  && cpu_configuration & op_desc.feature & CPU_MASK))   /*   for the current CPU configuration */
    return SCPE_UNK;                                    /*     then we cannot decode the instruction */

instruction = TO_DWORD (val [1], val [0]);              /* merge the two supplied values */

op_size = (op_desc.mask >> op_desc.shift)               /* determine the size of the primary table part */
            + (op_desc.mask != 0);                      /*   if it is present */

op_index = ((uint32) instruction & op_desc.mask) >> op_desc.shift;  /* extract the opcode primary index */

if (op_desc.ab_selector) {                              /* if the A/B-register selector is significant */
    if (op_desc.ab_selector & instruction)              /*   then if the A/B-register selector bit is set */
        op_index = op_index + op_size;                  /*     then use the second half of the table */

    op_size = op_size * 2;                              /* the primary table is twice the indicated size */
    }

if (op_desc.mask && ops [op_index].mnemonic [0])        /* if a primary entry is defined */
    fputs (ops [op_index].mnemonic, ofile);             /*   then print the mnemonic */

else {                                                  /* otherwise search through the secondary entries */
    for (op_index = op_size;                            /*   starting after the primary entries */
      ops [op_index].mnemonic != NULL;                  /*     until the NULL entry at the end */
      op_index++)
        if (ops [op_index].opcode ==                    /* if the opcode in this table entry */
          (instruction                                  /*   matches the instruction */
          & ops [op_index].op_bits                      /*     masked to the significant opcode bits */
          & op_props [ops [op_index].type].mask))       /*       and with the operand bits masked off */
            if (ops [op_index].mnemonic [0]) {          /*         then if the entry is defined */
                if (separator)                          /*           then if a separator is needed */
                    fputc (',', ofile);                 /*             then print it */

                fputs (ops [op_index].mnemonic, ofile); /* print the opcode mnemonic */

                if (op_desc.mask == OP_LINEAR           /* if multiple matches */
                  && op_desc.shift == OP_MULTIPLE)      /*   are allowed */
                    separator = TRUE;                   /*     then separators will be needed between mnemonics */

                else                                    /* otherwise */
                    break;                              /*   the search terminates on the first match */
                }

            else {                                          /* otherwise this two-word instruction is unimplemented */
                fprint_val (ofile, val [0], cpu_dev.dradix, /*   so print the first word */
                            cpu_dev.dwidth, PV_RZRO);       /*     with the default radix */

                fputc (',', ofile);                         /* add a separator */

                fprint_val (ofile, val [1], cpu_dev.dradix, /* print the second word */
                            cpu_dev.dwidth, PV_RZRO);

                return SCPE_OK_2_WORDS;                     /* return success to indicate printing is complete */
                }

    if (separator)                                      /* if one or more micro-ops was found */
        return SCPE_OK;                                 /*   then return, as there are no operands */

    else if (ops [op_index].mnemonic == NULL)           /* otherwise if the opcode was not found */
        return SCPE_ARG;                                /*   then return error status to print it in octal */
    }

op_type = ops [op_index].type;                          /* get the type of the instruction operand(s) */

op_value = val [0] & ~op_props [op_type].mask;          /* mask the first instruction word to the operand value */

op_count = (uint32) op_props [op_type].count;           /* get the number of operands */
status = (t_stat) - op_props [op_type].count;           /*   and set the initial number of words consumed */

op_address_set = op_props [op_type].address_set;        /* get the address/data-selector bit set */

op_radix = (radix ? radix : cpu_dev.dradix);            /* assume that the (only) operand is data */

if (ops [op_index].op_bits > D16_MASK) {                /* if this is a two-word instruction */
    op_start = 2;                                       /*   then the operands start after the second word */
    op_count = op_count + 1;                            /*     and extend for an extra word */
    status = status - 1;                                /*       and consume an additional word */
    }

if (op_count > 0 && (addr & VAL_EMPTY)) {               /* if operand words are needed but not loaded */
    addr = addr & LA_MASK;                              /*   then restore the logical address */

    for (op_index = op_start; op_index <= op_count; op_index++)                 /* starting with the first operand */
        val [op_index] = mem_fast_read ((HP_WORD) (addr + op_index), dms_ump);  /*   load the operands from memory */
    }


switch (op_type) {                                      /* dispatch by the operand type */

    /* no operand */

    case opNone:
        break;                                          /* no formatting needed */


    /* MRG page bit 10, offset 0000-1777 octal, indirect bit 15 */

    case opMPOI:
        if (addr > LA_MAX)                              /* if the instruction location is indeterminate */
            if (instruction & I_CP)                     /*   then if the current-page bit is set */
                prefix = " C ";                         /*     then prefix the offset with "C" */
            else                                        /*   otherwise it's a base-page reference */
                prefix = " Z ";                         /*     so prefix the offset with "Z" */

        else {                                          /* otherwise the address is valid */
            prefix = " ";                               /*   so use a blank separator */

            if (instruction & I_CP)                     /* if the current-page bit is set */
                op_value |= addr & I_PAGENO;            /*   then merge the offset with the current page address */
            }

        val [1] = op_value;                             /* set the operand value */
        op_count = 1;                                   /*   and print only one operand */
        break;


    /* IOG hold/clear bit 9 */

    case opHC:
        if (op_value)                                   /* if the clear-flag bit is set */
            fputs (" C", ofile);                        /*   then add the "C" to the mnemonic */
        break;


    /* IOG select code range 00-77 octal, hold/clear bit 9 */
    /* IOG optional select code range 00-77 octal, hold/clear bit 9 */

    case opSCHC:
    case opSCOHC:
        clear = (instruction & I_HC);                   /* set TRUE if the clear-flag bit is set */

    /* fall into the opSC case */

    /* IOG select code range 00-77 octal */

    case opSC:
        prefix = " ";                                   /* add a separator */
        val [1] = op_value;                             /*   and set the operand value */
        op_count = 1;                                   /*     and print only one operand */
        break;


    /* EAU shift/rotate count range 1-16 */

    case opShift:
        prefix = " ";                                   /* add a separator */
        op_radix = (radix ? radix : 10);                /*   and default the shift counts to decimal */

        if (op_value == 0)                              /* if the operand is zero */
            val [1] = 16;                               /*   then the shift count is 16 */
        else                                            /* otherwise */
            val [1] = op_value;                         /*   then shift count is the operand value */

        op_count = 1;                                   /* print only one operand */
        break;


    /* IOP index negative offset range 1-20 octal */

    case opIOPON:
        prefix = " -";                                  /* prefix the operand with the sign */
        val [1] = 16 - op_value;                        /*   and set the (absolute) offset */

        op_radix = (radix ? radix : 10);                /* default the offset to decimal */
        op_count = 1;                                   /*   and print only one operand */
        break;


    /* IOP index positive offset range 0-17 octal */

    case opIOPOP:
        prefix = " ";                                   /* add a separator */
        val [1] = op_value;                             /*   and set the (positive) offset */

        op_radix = (radix ? radix : 10);                /* default the offset to decimal */
        op_count = 1;                                   /*   and print only one operand */
        break;


    /* IOP index offset range 0-37 octal (+20 bias) */

    case opIOPO:
        if (op_value >= 020) {                          /* if the operand is positive */
            prefix = " ";                               /*   then omit the sign */
            val [1] = op_value - 020;                   /*     and remove the bias */
            }

        else {                                          /* otherwise the operand is negative */
            prefix = " -";                              /*   so prefix a minus sign */
            val [1] = 020 - op_value;                   /*     and remove the bias to get the absolute value */
            }

        op_radix = (radix ? radix : 10);                /* default the offset to decimal */
        op_count = 1;                                   /*   and print only one operand */
        break;


    /* UIG zero word, four (indirect) memory addresses */
    /* UIG zero word, five (indirect) memory addresses */
    /* UIG zero word, six (indirect) memory addresses */
    /* UIG zero word, eight (indirect) memory addresses */

    case opZA4:
    case opZA5:
    case opZA6:
    case opZA8:
        prefix = " ";                                   /* add a separator */
        op_start = 2;                                   /*   and skip the all-zeros word */
        break;


    /* One memory address range 00000-77777 octal, zero word, indirect bit 15 */

    case opMA1ZI:
        prefix = " ";                                   /* add a separator */
        op_count = 1;                                   /*   and print only one operand */
        break;


    /* One to seven memory addresses range 00000-77777 octal, indirect bit 15 */

    case opMA1I:
    case opMA2I:
    case opMA3I:
    case opMA4I:
    case opMA5I:
    case opMA6I:
    case opMA7I:

    /* UIG one data value */
    /* UIG two data values */
    /* UIG one (indirect) memory address, one data value */
    /* UIG two data values, one (indirect) memory address */
    /* UIG one data value, five (indirect) memory addresses */

    case opV1:
    case opV2:
    case opA1V1:
    case opV2A1:
    case opV1A5:
        prefix = " ";                                   /* add a separator */
        break;
    }                                                   /* end of the operand type dispatch */


if (prefix)                                             /* if an operand is present */
    for (op_index = op_start;                           /*   then format the operands */
      op_index <= op_count;                             /*     starting with the first operand word */
      op_index++) {
        fputs (prefix, ofile);                          /* print the operand prefix or separator */

        if (op_address_set & 1) {                           /* if this operand is an address */
            fprint_val (ofile, val [op_index] & LA_MASK,    /*   then print the logical address */
                        cpu_dev.aradix, LA_WIDTH, PV_LEFT); /*     using the address radix */

            if (val [op_index] & I_IA)                  /* add an indirect indicator */
                fputs (",I", ofile);                    /*   if specified by the operand */
            }

        else                                            /* otherwise it's a data value */
            fprint_value (ofile, val [op_index],        /*  so print the full value */
                          op_radix, DV_WIDTH, PV_LEFT); /*    using the specified radix */

        op_address_set = op_address_set >> 1;           /* shift the next address/data flag bit into place */
        }                                               /*   and loop until all operands are printed */

if (clear)                                              /* add a clear-flag indicator */
    fputs (",C", ofile);                                /*   if specified by the instruction */

return status;                                          /* return the number of words consumed */
}


/* Parse an address operand.

   Address operands of the form:

     <octal value>[,I]

  ...are parsed.  On entry, "cptr" points at the first character of the octal
  value, and "status" points at a variable to hold the return status.  If the
  parse succeeds, the address with the optional indirect bit (bit 15) is
  returned, and the status is set to SCPE_OK.  If the parse fails, the value is
  out of the logical range, or extraneous characters follow the operand,
  then zero is returned, and the status is set to SCPE_ARG.


  Implementation notes:

   1. The string that "cptr" points to is trimmed of trailing spaces before this
      routine is called.  Therefore, the trailing NUL test is sufficient to
      determine if there are more characters in the input buffer.
*/


static t_value parse_address (CONST char *cptr, t_stat *status)
{
CONST char *iptr;
t_value address;

address = strtotv (cptr, &iptr, cpu_dev.aradix);        /* parse the address */

if (cptr == iptr || address > LA_MAX) {                 /* if a parse error occurred or the value is too big */
    *status = SCPE_ARG;                                 /*   then return an invalid argument error */
    return 0;
    }

else if (*iptr == '\0') {                               /* otherwise if there is no indirect indicator */
    *status = SCPE_OK;                                  /*   then the parse succeeds */
    return address;                                     /*     and return the address */
    }

else if (strcmp (iptr, ",I") == 0) {                    /* otherwise if there is an indirect indicator */
    *status = SCPE_OK;                                  /*   then the parse succeeds */
    return address | I_IA;                              /*     and return the address with the indirect bit set */
    }

else {                                                  /* otherwise there are extraneous characters following */
    *status = SCPE_ARG;                                 /*   so return an invalid argument error */
    return 0;
    }
}


/* Parse a character or numeric value.

   This routine parses a token pointed to by "cptr" using the supplied "radix"
   and with the maximum allowed value "max".  If the parse succeeds, the routine
   sets "status" to SCPE_OK and returns the parsed value.

   Single-character ASCII parsing is performed if the specified radix is 256.
   Otherwise, a numeric parse is attempted.
*/

static t_value parse_value (CONST char *cptr, uint32 radix, t_value max, t_stat *status)
{
if (radix == 256)                                       /* if ASCII character parsing is requested */
    if (cptr [0] != '\0' && (t_value) cptr [0] < max) { /*   then if a character is present and within range */
        *status = SCPE_OK;                              /*     then indicate success */
        return (t_value) cptr [0];                      /*       and convert the character value */
        }

    else {                                              /* otherwise */
        *status = SCPE_ARG;                             /*   report that the character parse failed */
        return 0;
        }

else                                                    /* otherwise parse as a number */
    return get_uint (cptr, radix, max, status);         /*   with the specified radix and maximum value */
}


/* Parse a CPU instruction.

   This routine parses the command line for a CPU instruction and its
   operand(s), if any.  On entry, "cptr" points at the instruction mnemonic,
   "addr" is the address where the instruction will be stored, "val" points to
   an array of t_values of depth "sim_emax" where the word(s) comprising the
   machine instruction will be saved, "radix" contains the desired operand radix
   or zero if the default radix is to be used, and "target" indicates the target
   of the instruction (device, CPU, or trace).

   The routine returns a status code to the caller.  SCPE_OK status is returned
   if a single-word instruction was parsed, or the negative number of extra
   words (beyond the first) occupied by the instruction is returned.  For
   example, parsing a two-word instruction returns SCPE_OK_2_WORDS (= -1).  If
   "cptr" does not point at a valid mnemonic, or the mnemonic is not valid for
   the current CPU feature set, or an operand error is present, SCPE_ARG is
   returned.

   The routine first separates the instruction mnemonic from its operands, if
   any.  If the first token is not present or starts with a digit, SCPE_ARG is
   returned.  If the target for the parsed value is not a CPU memory location,
   the address is reset to an invalid value to indicate that a relative offset
   parse should be performed for Memory Reference Group instructions.  Then the
   parser table is scanned to search for a match.

   The parser table contains entries pointing to pairs of opcode descriptors and
   opcode tables, in order of decreasing frequency of appearance in the typical
   instruction mix.  For each entry, if the required firmware option is
   currently installed in the CPU, then the table is searched linearly for a
   match with the mnemonic token.  If a match is found, the "parse_instruction"
   routine is called to assemble the instruction opcode and any associated
   operands into the "val" array for return to the caller.

   If the token does not match any legal instruction mnemonic, the routine
   returns SCPE_ARG.


   Implementation notes:

    1. The mnemonic token is delineated by either a space or a comma; the latter
       is used to separate the multiple mnemonics of SRG and ASG instructions.
*/

static t_stat parse_cpu (CONST char *cptr, t_addr addr, t_value *val, uint32 radix, SYMBOL_SOURCE target)
{
CONST char *gptr;
const PARSER_ENTRY *pptr;
const OP_ENTRY     *eptr;
char  gbuf [CBUFSIZE];

gptr = get_glyph (cptr, gbuf, ',');                     /* parse the opcode and shift to uppercase */

if (gbuf [0] == '\0' || isdigit (gbuf [0]))             /* if the glyph is missing or is numeric */
    return SCPE_ARG;                                    /*   then it is not an instruction mnemonic */

if (target == Device_Symbol)                            /* if the target is not main memory */
    addr = PA_MAX + 1;                                  /*   then invalidate the target address */

for (pptr = parser_table; pptr->descriptor != NULL; pptr++)                 /* search the parser table */
    if (cpu_configuration & pptr->descriptor->feature & OPTION_MASK         /* if the required feature set is enabled */
      && cpu_configuration & pptr->descriptor->feature & CPU_MASK)          /*   for the current CPU configuration */
        for (eptr = pptr->opcodes; eptr->mnemonic != NULL; eptr++)          /*     then search the opcode table */
            if (strcmp (eptr->mnemonic, gbuf) == 0)                         /* if a matching mnemonic is found */
                return parse_instruction (gptr, addr, val, radix, eptr);    /*  then parse the operands */

return SCPE_ARG;                                        /* no match was found, so return failure */
}


/* Parse a CPU instruction in symbolic format.

   This routine is called to parse and encode a CPU instruction mnemonic and any
   associated operand(s).  On entry, "cptr" points at the input buffer after the
   first mnemonic token, "addr" is the address where the instruction will be
   stored, "val" points to an array of t_values of depth "sim_emax" where the
   word(s) comprising the machine instruction will be saved, "radix" contains
   the desired operand parsing radix or zero if the default radix is to be used,
   and "optr" points at the entry in an opcode table corresponding to the
   initial instruction mnemonic.  If "optr" points at an SRG or ASG opcode
   entry, then "cptr" may point to additional micro-ops to be merged with the
   original opcode.  Otherwise, "cptr" will point to any needed instruction
   operands.

   On exit, a status code is returned to the caller.  SCPE_OK status is returned
   if a single-word instruction was parsed, or the negative number of extra
   words (beyond the first) occupied by the instruction is returned.  For
   example, parsing a two-word instruction returns SCPE_OK_2_WORDS (= -1).  If
   the remainder of the input buffer does not parse correctly, or does not
   contain the operands required by the instruction, SCPE_ARG is returned.

   The routine begins by saving the initial opcode word(s) from the opcode table
   entry.  If the opcode belongs to the SRG or ASG, it may be the first of
   several micro-ops that must be parsed individually.  To ensure that each
   micro-op is specified only once and that any A/B-register usage is consistent
   between micro-ops, the significant bits of each parsed micro-op are
   accumulated.  The accumulated bits are passed to the "parse_micro_op"
   routine, where the checks are made.

   If the initial opcode is from the ASG, any additional micro-op mnemonics in
   the input buffer must be in the order they appear in the "asg_uops" table.
   The scan starts with the opcode table entry after the one corresponding to
   the initial opcode.

   Parsing SRG opcodes is more involved.  The first opcode may be a shift or
   rotate instruction, or it may be an SRG micro-op (CLE and/or SLA/B).  In
   either case, parsing continues with the (remaining) micro-ops and then with a
   second shift or rotate instruction.

   A complication is that an initial CLE, SLA, and SLB may be either an SRG or
   an ASG micro-op.  The determination depends on whether they appear with a
   shift or rotate instruction or with a micro-op belonging only to the ASG.
   The decision must be deferred until a mnemonic from one of these two groups
   is parsed.  If CLE and/or SLA/B appears alone, then it is encoded as an SRG
   instruction.

   Examples of SRG and ASG parsing follow:

     Input String     Parsed as  Reason
     ---------------  ---------  ---------------
     CLE                 SRG
     CLE,SLA             SRG
     CLE,SLA,SSA         ASG
     CME,SLA             ASG
     SLA,SSB            error    mixed A/B
     SEZ,ERA            error    mixed ASG/SRG
     SLA,CLE            error    out of order

     RAL                 SRG
     RAL,RAL             SRG
     RAL,CLE,SLA,ERA     SRG
     RAL,CLE,SLA,ERB    error    mixed A/B
     RAL,SLB            error    mixed A/B
     RAL,CLE,SSA        error    mixed SRG/ASG
     CLA,CCA            error    bad combination
     CLA,CLA            error    duplicate

   If the initial opcode is not an SRG or ASG opcode, then only a single
   mnemonic is allowed, and the operand(s), if any, are now examined.  The type
   and count of operand(s), if any, are indicated by the "type" and "count"
   values in the matched opcode table entry.  The entry also indicates which
   operand(s) are addresses and which are data values.  These values drive the
   parsing of the operands.  Parsed operand values are placed in the "val" array
   in order of appearance after the instruction word.

   An MRG memory address operand may be specified either as an absolute address
   or as an explicit page indicator ("Z" or "C") and a page offset.  An absolute
   address is converted into an offset and zero/current page indicator by
   examining the address value.  If the value is less than 2000 octal, a
   base-page reference is encoded.  If the value falls within the same page as
   the "addr" parameter value, a current-page reference is encoded.  If neither
   condition is true, then SCPE_ARG is returned.

   IOG instructions parse the select code and an optional hold-or-clear-flag
   indicator.  These values are encoded into the instruction.  Parsing is
   complicated in that specification of the select code is optional for the HLT
   instruction, defaulting to select code 0.  If the select code is omitted, the
   comma that separates it and the "C" indicator is also omitted.

   EAG shift and rotate counts are specified as 1-16 but are encoded in the
   instruction as 1-15 and 0, respectively.  IOP index instructions offsets are
   specified as -16 to 15 (or +15) but are encoded in the instruction in
   excess-16 format.

   The remaining instructions encode operands in separate memory words following
   the opcode.  A few instructions that are interruptible require emission of an
   all-zeros word following the instruction (either preceding or following the
   operands).  Address operands are parsed as absolute logical addresses with
   optional indirect indicators.  Data operands are parsed as absolute values.

   Parsing concludes with a check for extraneous characters on the line;
   presence causes an SCPE_ARG return.


   Implementation notes:

    1. The "repeated instruction" test for the second shift/rotate op is needed
       to catch the "NOP,ALS" sequence.

    2. The select code may be omitted when entering the HLT instruction (it
       defaults to 0), so "HLT" and "HLT C" are permissible entries.  For the
       latter, if the radix is overridden to hexadecimal, the "C" is interpreted
       as select code 14 (octal) rather than as a "halt and clear flag"
       instruction.

    3. Parsing the 2100 IOP instruction mnemonics "LAI" and "SAI" always matches
       the "opIOPON" entries.  Therefore, the opcodes are those of the negative
       index instructions.  However, because the negative and positive opcode
       values are adjacent, adding (not ORing) the offset value produces the
       correct instruction encoding for both (index -16 => encoded 0, -1 => 15,
       0 => 16, and +15 => 31).
*/

static t_stat parse_instruction (CONST char *cptr, t_addr addr, t_value *val, uint32 radix, const OP_ENTRY *optr)
{
CONST char *gptr;
const char *mptr;
char       gbuf [CBUFSIZE];
OP_TYPE    op_type;
uint32     accumulator, op_index, op_count, op_radix, op_address_set;
t_stat     status, consumption;
t_value    op_value;
t_bool     op_implicit;
t_bool     op_flag  = FALSE;
uint32     op_start = 1;                                /* the "val" array index of the first operand */

val [0] = LOWER_WORD (optr->opcode);                    /* set the (initial) opcode */
val [1] = UPPER_WORD (optr->opcode);                    /*   and the subopcode if it is a two-word instruction */

if (*cptr != '\0'                                       /* if there is more to parse */
  && (SRGOP (optr->opcode) || ASGOP (optr->opcode))) {  /*   and the first opcode is SRG or ASG */
    accumulator = (uint32) optr->op_bits;               /*     then accumulate the significant opcode bits */

    gptr = get_glyph (cptr, gbuf, ',');                 /* parse the next opcode, if present */

    if (ASGOP (optr->opcode))                           /* if the initial opcode is ASG */
        optr++;                                         /*   then point at the next table entry for parsing */

    else {                                              /* otherwise this is an SRG opcode */
        if (optr->opcode & SRG1_DE_MASK) {              /* if it is a shift or rotate instruction */
            mptr = NULL;                                /*   then it cannot be an ASG instruction */
            optr = srg_uops;                            /*     and parsing continues with the micro-ops */
            }

        else {                                          /* otherwise it's a micro-op that could be ASG */
            mptr = optr->mnemonic;                      /*   so save the initial mnemonic pointer */
            optr++;                                     /*     and start with the next micro-op */
            }

        status = parse_micro_ops (optr, gbuf, val,      /* parse the SRG micro-ops */
                                  &gptr, &accumulator);

        if (status != SCPE_INCOMP)                      /* if the parse is complete */
            return status;                              /*   then return success or failure as appropriate */

        optr = srg2_ops;                                /* the mnemonic is not a micro-op, so try a shift/rotate op */

        status = parse_micro_ops (optr, gbuf, val,      /* parse the SRG shift-rotate opcode */
                                  NULL, &accumulator);

        if (status == SCPE_OK && *gptr != '\0')         /* if the opcode was found but there is more to parse */
            return SCPE_ARG;                            /*   then return failure as only one opcode is allowed */

        else if (status != SCPE_INCOMP)                 /* otherwise if the parse is complete */
            return status;                              /*   then return success or failure as appropriate */

        if (mptr == NULL)                               /* the mnemonic is not an SRG, and if it cannot be an ASG */
            return SCPE_ARG;                            /*   then fail with an invalid mnemonic */

        else {                                          /* otherwise attempt to reparse as ASG instructions */
            val [0] = 0;                                /* clear the assembled opcode */
            accumulator = 0;                            /*   and the accumulated significant opcode bits */

            strcpy (gbuf, mptr);                        /* restore the original mnemonic to the buffer */
            gptr = cptr;                                /*   and the original remainder-of-line pointer */

            optr = asg_uops;                            /* set to search the ASG micro-ops table */
            }                                           /*   and fall into the ASG parser */
        }


    status = parse_micro_ops (optr, gbuf, val,          /* parse the ASG micro-ops */
                              &gptr, &accumulator);

    if (status != SCPE_INCOMP)                          /* if the parse is complete */
        return status;                                  /*   then return success or failure as appropriate */
    }

else {                                                  /* otherwise, it's a single opcode */
    op_type = optr->type;                               /*   so get the type of the instruction operand(s) */

    op_count = (uint32) op_props [op_type].count;       /* get the number of operands */
    consumption = (t_stat) - op_props [op_type].count;  /*   and set the initial number of words consumed */

    op_address_set = op_props [op_type].address_set;    /* get the address/data-selector bit set */

    op_radix = (radix ? radix : cpu_dev.dradix);        /* assume that the (only) operand is data */

    if (optr->op_bits > D16_MASK) {                     /* if this is a two-word instruction */
        op_start = 2;                                   /*   then the operands start after the second word */
        op_count = op_count + 1;                        /*     and extend for an extra word */
        consumption = consumption - 1;                  /*       and consume an additional word */
        }


    switch (op_type) {                                  /* dispatch by the operand type */

        /* no operand */

        case opNone:
            break;                                      /* no parsing needed */


        /* MRG page bit 10, offset 0000-1777 octal, indirect bit 15 */

        case opMPOI:
            cptr = get_glyph (cptr, gbuf, '\0');            /* get the next token */

            if (gbuf [0] == 'C' && gbuf [1] == '\0') {      /* if the "C" modifier was specified */
                val [0] = val [0] | I_CP;                   /*   then add the current-page flag */
                cptr = get_glyph (cptr, gbuf, '\0');        /* get the address */
                op_implicit = FALSE;                        /*   and clear the implicit-page flag */
                }

            else if (gbuf [0] == 'Z' && gbuf [1] == '\0') { /* otherwise if the "Z" modifier was specified */
                cptr = get_glyph (cptr, gbuf, '\0');        /*   then get the address */
                op_implicit = FALSE;                        /*     and clear the implicit-page flag */
                }

            else                                            /* otherwise neither modifier is present */
                op_implicit = TRUE;                         /*   so set the flag to allow implicit-page addressing */

            op_value = parse_address (gbuf, &status);       /* parse the address and optional indirection indicator */

            if (status != SCPE_OK)                          /* if a parse error occurred */
                return status;                              /*   then return an invalid argument error */

            if ((op_value & VAMASK) <= I_DISP)              /* if a base-page address was given */
                val [0] |= op_value;                        /*   then merge the offset into the instruction */

            else if (addr <= LA_MAX && op_implicit              /* otherwise if an implicit-page address is allowed */
              && ((addr ^ op_value) & I_PAGENO) == 0)           /*   and the target is in the current page */
                val [0] |= I_CP | op_value & (I_IA | I_DISP);   /*     then merge the offset with the current-page flag */

            else                                            /* otherwise the address cannot be reached */
                return SCPE_ARG;                            /*   from the current instruction's location */
            break;


        /* IOG select code range 00-77 octal, hold/clear bit 9 */
        /* IOG optional select code range 00-77 octal, hold/clear bit 9 */

        case opSCHC:
        case opSCOHC:
            op_flag = TRUE;                             /* set a flag to enable an optional ",C" */

        /* fall into the opSC case */

        /* IOG select code range 00-77 octal */

        case opSC:
            cptr = get_glyph (cptr, gbuf, (op_flag ? ',' : '\0'));      /* get the next glyph */
            op_value = get_uint (gbuf, op_radix, I_DEVMASK, &status);   /*   and parse the select code */

            if (status == SCPE_OK)                      /* if the select code is good */
                val [0] |= op_value;                    /*   then merge it into the opcode */

            else if (op_type == opSCOHC)                /* otherwise if the select code is optional */
                if (gbuf [0] == '\0')                   /*   then if it is not supplied */
                    break;                              /*     then use the base instruction value */

                else if (gbuf [0] == 'C' && gbuf [1] == '\0') { /*   otherwise if the "C" modifier was specified */
                    val [0] |= I_HC;                            /*     then merge the H/C bit */
                    break;                                      /*       into the base instruction value */
                    }

            else                                        /* otherwise the select code is bad */
                return SCPE_ARG;                        /*   so report failure for a bad argument */

        /* fall into opHC case */

        /* IOG hold/clear bit 9 */

        case opHC:
            if (*cptr != '\0')                                  /* if there is more */
                if (op_type != opSC) {                          /*   and it is expected */
                    cptr = get_glyph (cptr, gbuf, '\0');        /*     then get the glyph */

                    if (gbuf [0] == 'C' && gbuf [1] == '\0')    /* if the "C" modifier was specified */
                        val [0] |= I_HC;                        /*   then merge the H/C bit */
                    else                                        /* otherwise it's something else */
                        return SCPE_ARG;                        /*   so report failure for a bad argument */
                    }

                else                                    /* otherwise it's not expected */
                    return SCPE_ARG;                    /*   so report failure for a bad argument */
            break;


        /* EAU shift/rotate count range 1-16 */

        case opShift:
            op_radix = (radix ? radix : 10);            /* shift counts default to decimal */

            cptr = get_glyph (cptr, gbuf, '\0');                    /* get the next glyph */
            op_value = parse_value (gbuf, op_radix, 16, &status);   /*   and parse the shift count */

            if (status != SCPE_OK || op_value == 0)     /* if a parsing error occurred or the count is zero */
                return SCPE_ARG;                        /*   then report failure for a bad argument */
            else if (op_value < 16)                     /* otherwise the count is good */
                val [0] |= op_value;                    /*   so merge it into the opcode (0 encodes 16) */
            break;


        /* IOP index negative offset range 1-16 */
        /* IOP index positive offset range 0-15 */
        /* IOP index offset range -16 to +15 (+16 bias) */

        case opIOPON:
        case opIOPOP:
        case opIOPO:
            op_radix = (radix ? radix : 10);            /* index offsets default to decimal */

            if (*cptr == '+')                           /* if there is a leading plus sign */
                cptr++;                                 /*   then skip it */

            else if (*cptr == '-') {                    /* otherwise if there is a leading minus sign */
                op_flag = TRUE;                         /*   then set the negative flag */
                cptr++;                                 /*     and skip it */
                }

            cptr = get_glyph (cptr, gbuf, '\0');        /* get the next glyph */
            op_value = parse_value (gbuf, op_radix,     /*   and parse the index offset with the appropriate range */
                                    15 + op_flag, &status);

            if (status != SCPE_OK)                      /* if a parsing error or value out of range occurred */
                return SCPE_ARG;                        /*   then report failure for a bad argument */

            else if (op_flag)                           /* otherwise the offset is good; if it is negative */
                val [0] = val [0] + 16 - op_value;      /*   then scale and merge the offset */

            else                                        /* otherwise it is positive */
                val [0] = val [0] + op_value + 16;      /*   so shift to the positive opcode and merge the offset */
            break;


        /* UIG zero word, four (indirect) memory addresses */
        /* UIG zero word, five (indirect) memory addresses */
        /* UIG zero word, six (indirect) memory addresses */
        /* UIG zero word, eight (indirect) memory addresses */

        case opZA4:
        case opZA5:
        case opZA6:
        case opZA8:
            val [1] = 0;                                /* add the zero word */
            op_start = 2;                               /*   and bump the operand starting index */
            break;


        /* One memory address range 00000-77777 octal, zero word, indirect bit 15 */

        case opMA1ZI:
            val [2] = 0;                                /* add the zero word */
            op_count = 1;                               /*   and reduce the operand count to 1 */
            break;


        /* One to seven memory addresses range 00000-77777 octal, indirect bit 15 */

        case opMA1I:
        case opMA2I:
        case opMA3I:
        case opMA4I:
        case opMA5I:
        case opMA6I:
        case opMA7I:

        /* UIG one data value */
        /* UIG two data values */
        /* UIG one (indirect) memory address, one data value */
        /* UIG two data values, one (indirect) memory address */
        /* UIG one data value, five (indirect) memory addresses */

        case opV1:
        case opV2:
        case opA1V1:
        case opV2A1:
        case opV1A5:
            break;                                      /* no special operand handling needed */
        }                                               /* end of the operand type dispatch */


    if (op_count > 0)                                   /* if one or more operands are required */
        for (op_index = op_start;                       /*   then parse and load them */
          op_index <= op_count;                         /*     in to the return array */
          op_index++) {
            cptr = get_glyph (cptr, gbuf, '\0');        /* get the next glyph */

            if (op_address_set & 1)                         /* if this operand is an address */
                op_value = parse_address (gbuf, &status);   /*   then parse it with an optional indirection indicator */
            else                                            /* otherwise it's a data value */
                op_value = parse_value (gbuf, op_radix,     /*   so parse it as an unsigned number */
                                        DV_UMAX, &status);

            if (status != SCPE_OK)                      /* if a parse error occurred */
                return status;                          /*   then return an invalid argument error */
            else                                        /* otherwise */
                val [op_index] = op_value;              /*   store the operand value */

            op_address_set = op_address_set >> 1;       /* shift the next type bit into place */
            }                                           /*   and loop until all operands are parsed */

    if (*cptr == '\0')                                  /* if parsing is complete */
        return consumption;                             /*   then return the number of instruction words */
    }

return SCPE_ARG;                                        /* return an error for extraneous characters present */
}


/* Parse micro-ops.

   This routine parses a set of micro-operations that form a single instruction.
   It is called after a token has matched an entry in the SRG or ASG opcode
   tables.

   On entry, "optr" points at the opcode table entry after the first token
   match, "gbuf" points at the next mnemonic token in the input buffer, "val"
   points at the array of t_values that will hold the assembled instruction and
   its operands, "gptr" points at the pointer that is set to the next character
   to parse, and "accumulator" points at the variable that holds the accumulated
   significant opcode bits for the instruction.  On exit, the routine returns
   SCPE_OK if the input buffer was parsed correctly, SCPE_ARG if conflicting
   A/B-register selectors were encountered (e.g., "CLA,CMB"), or SCPE_INCOMP if
   the opcode table was exhausted without consuming the input buffer.  The
   pointer "gptr" points at is updated to point at the first character after the
   last token parsed, and the value pointed at by "accumulator" contains the
   accumulated significant opcode bits for the set of parsed micro-ops.  The
   former is used by the caller to continue parsing, while the latter is used to
   track the micro-ops that have been specified to ensure that each appears only
   once.

   The routine searches the supplied table for mnemonics that match the tokens
   parsed from the input buffer.  For each match, checks are made to ensure that
   the same mnemonic has not been specified before and that opcodes that specify
   an accumulator are consistent throughout the instruction.  The opcode from
   each matching entry is then logically ORed into the instruction word, and the
   significant opcode bits are accumulated to provide a check for duplicates.
   When the input buffer or opcode table is exhausted, or a check fails, the
   routine exits with the appropriate status.


   Implementation notes:

    1. The "repeated operation" significant-bits test is necessary only to catch
       two cases.  One is the NOP,RAL and RAL,NOP case where NOP must be
       specified alone and yet it decodes as an SRG instruction, which allows
       micro-ops.  The other is the CLA,CMA case where multiple encoded ASG
       operations are specified.  All other repeated operation cases (e.g.,
       CLE,SLA,CLE) are prohibited by the single pass through a table arranged
       in the required order (so the second CLE will not be found because SLA
       comes after CLE in the table).

       The first case could be handled by substituting an operand test for the
       group test (e.g., optr->type == opSRG || optr->type == opASG and marking
       NOP as opNone and all other SRG opcodes as opSRG, so that NOP will not be
       detected as an SRG instruction) in "parse_instruction", placing the NOP
       entry first, and starting the search with entry 2 in the SRG micro-ops
       table.

       The second case could be handled by treating the encoded operations
       (CLx/CMx/CCx and CLE/CME/CCE) as indexed searches of separate primary
       tables.

       The added complexity of these alternates is not worth the effort, given
       the relative simplicity of the "repeated operation" test.

    2. The A/B-register selector state must be the same for all micro-ops in a
       given instruction.  That is, all micro-ops that use the A or B registers
       must reference the same accumulator.  The check is performed by logically
       ANDing the accumulated and current significant opcode bits with the
       AB_SELECT mask (to determine if the A/B-register select is significant
       and has been set), and then with the exclusive-OR of the accumulated and
       current micro-ops (to determine if the current A/B-register select is the
       same as the prior A/B-register selects).  If there is a discrepancy, then
       the current micro-op does not use the same register as a previously
       specified micro-op.
*/

static t_stat parse_micro_ops (const OP_ENTRY *optr, char *gbuf, t_value *val, CONST char **gptr, uint32 *accumulator)
{
while (optr->mnemonic != NULL)                                  /* search the table until the NULL entry at the end */
    if (strcmp (optr->mnemonic, gbuf) == 0) {                   /* if the mnemonic matches this entry */
        if (*accumulator & optr->op_bits & ~(AB_SELECT | ASG))  /*   then if this opcode has been entered before */
            return SCPE_ARG;                                    /*     then fail with a repeated instruction error */

        if (*accumulator & optr->op_bits & AB_SELECT    /* if the A/B-register selector is significant */
          & (optr->opcode ^ val [0]))                   /*   and the new opcode disagrees with the prior ones */
            return SCPE_ARG;                            /*     then fail with an A/B inconsistent error */

        val [0] |= optr->opcode;                        /* include the opcode */
        *accumulator |= optr->op_bits;                  /*   and accumulate the significant opcode bits */

        if (gptr == NULL || **gptr == '\0')             /* if the line is fully parsed or only one match allowed */
            return SCPE_OK;                             /*   then we are done */
        else                                            /* otherwise there is more */
            *gptr = get_glyph (*gptr, gbuf, ',');       /*   so parse the next opcode */
        }

    else                                                /* otherwise this entry does not match */
        optr++;                                         /*   so point at the next one */

return SCPE_INCOMP;
}


/* Get a 16-bit word from a file.

   This routine gets the next two bytes from the open file stream "fileref" and
   returns a 16-bit word with the first byte in the upper half and the second
   byte in the lower half.  If a file error occurs (either a read error or an
   unexpected end-of-file), the routine returns EOF.
*/

static int fgetword (FILE *fileref)
{
int c1, c2;

c1 = fgetc (fileref);                                   /* get the first byte from the file */

if (c1 == EOF)                                          /* if the read failed */
    return c1;                                          /*   then return EOF */

c2 = fgetc (fileref);                                   /* get the second byte from the file */

if (c2 == EOF)                                          /* if the read failed */
    return c2;                                          /*   then return EOF */
else                                                    /* otherwise */
    return (int) TO_WORD (c1, c2);                      /*   return the word formed by the two bytes */
}


/* Put a 16-bit word to a file.

   This routine writes two bytes from the 16-bit word "data" into the open file
   stream "fileref".  The upper half of the word supplies the first byte, and
   the lower half supplies the second byte.  If a write error occurs, the
   routine returns EOF.  Otherwise, it returns the second byte written.
*/

static int fputword (int data, FILE *fileref)
{
int status;

status = fputc (UPPER_BYTE (data), fileref);            /* write the first byte */

if (status != EOF)                                      /* if the write succeeded */
    status = fputc (LOWER_BYTE (data), fileref);        /*   then write the second byte */

return status;                                          /* return the result of the write */
}
