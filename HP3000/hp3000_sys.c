/* hp3000_sys.c: HP 3000 system common interface

   Copyright (c) 2016, J. David Bryan

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
   AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be used
   in advertising or otherwise to promote the sale, use or other dealings in
   this Software without prior written authorization from the author.

   13-May-16    JDB     Modified for revised SCP API function parameter types
   23-Nov-15    JDB     First release version
   11-Dec-12    JDB     Created

   References:
     - Machine Instruction Set Reference Manual
         (30000-90022, February 1980)
     - Systems Programming Language Reference Manual
         (30000-90024, December 1976)


   This module provides the interface between the Simulation Control Program
   (SCP) and the HP 3000 simulator.  It includes the required global VM
   interface data definitions (e.g., the simulator name, device array, etc.),
   symbolic display and parsing routines, utility routines for tracing and
   execution support, and SCP command replacement routines.
*/



#include <ctype.h>
#include <stdarg.h>

#include "hp3000_defs.h"
#include "hp3000_cpu.h"
#include "hp3000_cpu_ims.h"
#include "hp3000_io.h"



/* External I/O data structures */

extern DEVICE cpu_dev;                          /* Central Processing Unit */
extern DEVICE iop_dev;                          /* I/O Processor */
extern DEVICE mpx_dev;                          /* Multiplexer Channel */
extern DEVICE sel_dev;                          /* Selector Channel */
extern DEVICE scmb_dev [2];                     /* Selector Channel Maintenance Boards */
extern DEVICE atcd_dev;                         /* Asynchronous Terminal Controller TDI */
extern DEVICE atcc_dev;                         /* Asynchronous Terminal Controller TCI */
extern DEVICE clk_dev;                          /* System Clock */
extern DEVICE ds_dev;                           /* 79xx MAC Disc */
extern DEVICE ms_dev;                           /* 7970 Magnetic Tape */


/* Program constants */


/* Symbolic production/consumption values */

#define SCPE_OK_2_WORDS     ((t_stat) -1)       /* two words produced or consumed */
#define SCPE_OK_3_WORDS     ((t_stat) -2)       /* three words produced or consumed */


/* Address parsing configuration flags */

typedef enum {
    apcNone          = 000,                     /* no configuration */
    apcBank_Offset   = 001,                     /* <bank>.<offset> address form allowed */
    apcBank_Override = 002,                     /* bank override switches allowed */
    apcDefault_DBANK = 004,                     /* default bank is DBANK */
    apcDefault_PBANK = 010                      /* default bank is PBANK */
    } APC_FLAGS;


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

    1. Immediate values, displacements, and decrements are assumed to be
       right-justified in the instruction word, i.e., extend from bits n-15,
       unless otherwise noted.

    2. Operand masks for signed values must include both signs and magnitudes.
*/

typedef enum {
    opNone,                                     /* no operand */
    opU1,                                       /* unsigned value range 0-1 */
    opU1515,                                    /* unsigned value pair range 0-15 */
    opU63,                                      /* unsigned value range 0-63 */
    opU63X,                                     /* unsigned value range 0-63, index bit 4 */
    opU255,                                     /* unsigned value range 0-255 */
    opC15,                                      /* CIR display value range 0-15 */
    opR255L,                                    /* register selection value range 0-255 left-to-right */
    opR255R,                                    /* register selection value range 0-255 right-to-left */
    opPS31I,                                    /* P +/- displacement range 0-31, indirect bit 4 */
    opPS255,                                    /* P +/- displacement range 0-255 */
    opPU255,                                    /* P unsigned displacement range 0-255 */
    opPS255IX,                                  /* P +/- displacement range 0-255, indirect bit 5, index bit 4 */
    opS,                                        /* S decrement bit 11 */
    opSCS,                                      /* sign control bits 9-10, S decrement bit 11 */
    opSU2,                                      /* S decrement range 0-2 bits 10-11 */
    opSU3,                                      /* S decrement range 0-3 */
    opSU3B,                                     /* S decrement range 0-3, base bit 11 */
    opSU3NAS,                                   /* S decrement range 0-3, N/A/S bits 11-13 */
    opSU7,                                      /* S decrement range 0-7 */
    opSU15,                                     /* S decrement range 0-15 */
    opD255IX,                                   /* DB+/Q+/Q-/S- displacements, indirect bit 5, index bit 4 */
    opPD255IX,                                  /* P+/P-/DB+/Q+/Q-/S- displacements, indirect bit 5, index bit 4 */
    opX                                         /* index bit 4 */
    } OP_TYPE;

static const t_value op_mask [] = {             /* operand masks, indexed by OP_TYPE */
    0177777,                                    /*   opNone */
    0177776,                                    /*   opU1 */
    0177400,                                    /*   opU1515 */
    0177700,                                    /*   opU63 */
    0173700,                                    /*   opU63X */
    0177400,                                    /*   opU255 */
    0177760,                                    /*   opC15 */
    0177400,                                    /*   opR255L */
    0177400,                                    /*   opR255R */
    0173700,                                    /*   opPS31I */
    0177000,                                    /*   opPS255 */
    0177400,                                    /*   opPU255 */
    0171000,                                    /*   opPS255IX */
    0177757,                                    /*   opS */
    0177617,                                    /*   opSCS */
    0177717,                                    /*   opSU2 */
    0177774,                                    /*   opSU3 */
    0177754,                                    /*   opSU3B */
    0177740,                                    /*   opSU3NAS */
    0177770,                                    /*   opSU7 */
    0177760,                                    /*   opSU15 */
    0171000,                                    /*   opD255IX */
    0170000,                                    /*   opPD255IX */
    0173777                                     /*   opX */
    };


/* Instruction classifications.

   Machine instructions on the HP 3000 are identified by a varying number of
   bits.  In general, the four most-significant bits identify the general class
   of instruction, and additional bits form a sub-opcode within a class to
   identify an instruction uniquely.  However, some instructions are irregular
   or have reserved bits.  These bits are defined to be zero, but correct
   hardware decoding may or may not depend on the value being zero.

   Each instruction is classified by a mnemonic, a base operation code (without
   the operand), an operand type, and a mask for the reserved bits, if any.
   Classifications are grouped by class of instruction into arrays that are
   indexed by sub-opcodes, if applicable.

   An operation table consists of two parts, either of which is optional.  If a
   given class has a sub-opcode that fully or almost fully decodes the class,
   the first (primary) part of the table contains the appropriate number of
   classification elements.  This allows rapid access to a specific instruction
   based on its bit pattern.  In this primary part, the reserved bits masks are
   not used.

   If some instructions in a class have reserved bits, or if the sub-opcode
   decoding is not regular, the second (secondary) part of the table contains
   classification elements that specify reserved bits masks.  This part is
   searched linearly.

   As an example, the stack instructions have bits 0-3 = 0.  The remaining
   twelve bits are broken into two six-bit fields.  Each field encodes one of 64
   stack operations (actually, 63 operations, as one is reserved).  As the stack
   operations are fully decoded, the table consists only of 64 primary elements,
   corresponding one-for-one to the 64 operations.

   As as contrasting example, the shift and branch operations have bits 0-3 = 1
   and are fully decoded by bits 5-9, except for two instructions that have
   reserved bit fields (SCAN and TNSL), and two instructions that require one
   more bit for full decoding (QASL and QASR).  The table consists of a primary
   part of 32 elements and a secondary part of four elements.  The three primary
   elements corresponding to the four partially-decoded instructions are
   indicated by zero-length mnemonics.  The four secondary elements contain an
   entry for each instruction that requires additional masking before unique
   identification is possible.

   Some instructions contain reserved bits that may or may not affect hardware
   instruction decoding.  For example, the MOVE instruction defines bits 12-13
   as 00, but the bits are not decoded, so MOVE will result regardless of the
   values.  IXIT also defines bits 12-13 as 00, but in this case they must be 00
   for the instruction to execute; any other value executes a PCN instruction.

   For those instructions dependent on their reserved bits for interpretation,
   the operations table has two entries for each opcode.  The first entry
   specifies a reserved bits mask of all-ones; this entry matches the canonical
   opcode.  The second entry specifies a mask that matches the opcode to the
   range of opcodes that decode to the instruction; this entry presents the
   opcode mnemonic in lowercase as an indicator that it is not the canonical
   representation.

   The end of an operations table is indicated by a NULL mnemonic pointer.
*/

typedef struct {
    const char  *mnemonic;                      /* symbolic name */
    t_value     opcode;                         /* base opcode */
    OP_TYPE     operand;                        /* operand type */
    t_value     rsvd_mask;                      /* reserved bits mask */
    } INST_CLASS;

typedef INST_CLASS OP_TABLE [];                 /* an array of classifications */


/* Stack operations.

   The stack instructions are fully decoded by bits 4-9 or 10-15.  The table
   consists of 64 primary entries.

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   0 |   1st stack opcode    |   2nd stack opcode    |  Stack
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Implementation notes:

    1. Opcode 072 is undefined and will cause an Unimplemented Instruction trap
       if it is executed.  Normally, an unimplemented instruction is printed in
       numeric form during mnemonic display.  However, as two stack operations
       are contained in a single word, the entry for opcode 072 has "072" as the
       mnemonic to allow the other stack op to be decoded for printing.
*/

static const OP_TABLE stack_ops = {
    { "NOP",  0000000, opNone },
    { "DELB", 0000100, opNone },
    { "DDEL", 0000200, opNone },
    { "ZROX", 0000300, opNone },
    { "INCX", 0000400, opNone },
    { "DECX", 0000500, opNone },
    { "ZERO", 0000600, opNone },
    { "DZRO", 0000700, opNone },
    { "DCMP", 0001000, opNone },
    { "DADD", 0001100, opNone },
    { "DSUB", 0001200, opNone },
    { "MPYL", 0001300, opNone },
    { "DIVL", 0001400, opNone },
    { "DNEG", 0001500, opNone },
    { "DXCH", 0001600, opNone },
    { "CMP",  0001700, opNone },
    { "ADD",  0002000, opNone },
    { "SUB",  0002100, opNone },
    { "MPY",  0002200, opNone },
    { "DIV",  0002300, opNone },
    { "NEG",  0002400, opNone },
    { "TEST", 0002500, opNone },
    { "STBX", 0002600, opNone },
    { "DTST", 0002700, opNone },
    { "DFLT", 0003000, opNone },
    { "BTST", 0003100, opNone },
    { "XCH",  0003200, opNone },
    { "INCA", 0003300, opNone },
    { "DECA", 0003400, opNone },
    { "XAX",  0003500, opNone },
    { "ADAX", 0003600, opNone },
    { "ADXA", 0003700, opNone },
    { "DEL",  0004000, opNone },
    { "ZROB", 0004100, opNone },
    { "LDXB", 0004200, opNone },
    { "STAX", 0004300, opNone },
    { "LDXA", 0004400, opNone },
    { "DUP",  0004500, opNone },
    { "DDUP", 0004600, opNone },
    { "FLT",  0004700, opNone },
    { "FCMP", 0005000, opNone },
    { "FADD", 0005100, opNone },
    { "FSUB", 0005200, opNone },
    { "FMPY", 0005300, opNone },
    { "FDIV", 0005400, opNone },
    { "FNEG", 0005500, opNone },
    { "CAB",  0005600, opNone },
    { "LCMP", 0005700, opNone },
    { "LADD", 0006000, opNone },
    { "LSUB", 0006100, opNone },
    { "LMPY", 0006200, opNone },
    { "LDIV", 0006300, opNone },
    { "NOT",  0006400, opNone },
    { "OR",   0006500, opNone },
    { "XOR",  0006600, opNone },
    { "AND",  0006700, opNone },
    { "FIXR", 0007000, opNone },
    { "FIXT", 0007100, opNone },
    { "072",  0007200, opNone },                /* unassigned opcode */
    { "INCB", 0007300, opNone },
    { "DECB", 0007400, opNone },
    { "XBX",  0007500, opNone },
    { "ADBX", 0007600, opNone },
    { "ADXB", 0007700, opNone },
    { NULL }
    };


/* Shift, branch, and bit test operations.

   The shift, branch, and bit test instructions are fully decoded by bits 5-9,
   except for SCAN and TNSL, whose reserved bits are don't cares, and QASL and
   QASR, which depend on bit 4.  The table consists of 32 primary entries and
   four secondary entries.

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   1 | X |   shift opcode    |      shift count      |  Shift
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   1 | I |   branch opcode   |+/-|  P displacement   |  Branch
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   0   1 | X |  bit test opcode  |     bit position      |  Bit Test
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

static const OP_TABLE sbb_ops = {
    { "ASL",  0010000, opU63X  },
    { "ASR",  0010100, opU63X  },
    { "LSL",  0010200, opU63X  },
    { "LSR",  0010300, opU63X  },
    { "CSL",  0010400, opU63X  },
    { "CSR",  0010500, opU63X  },
    { "",     0010600, opNone  },               /* SCAN */
    { "IABZ", 0010700, opPS31I },
    { "TASL", 0011000, opU63X  },
    { "TASR", 0011100, opU63X  },
    { "IXBZ", 0011200, opPS31I },
    { "DXBZ", 0011300, opPS31I },
    { "BCY",  0011400, opPS31I },
    { "BNCY", 0011500, opPS31I },
    { "",     0011600, opNone  },               /* TNSL */
    { "",     0011700, opNone  },               /* QASL, QASR */
    { "DASL", 0012000, opU63X  },
    { "DASR", 0012100, opU63X  },
    { "DLSL", 0012200, opU63X  },
    { "DLSR", 0012300, opU63X  },
    { "DCSL", 0012400, opU63X  },
    { "DCSR", 0012500, opU63X  },
    { "CPRB", 0012600, opPS31I },
    { "DABZ", 0012700, opPS31I },
    { "BOV",  0013000, opPS31I },
    { "BNOV", 0013100, opPS31I },
    { "TBC",  0013200, opU63X  },
    { "TRBC", 0013300, opU63X  },
    { "TSBC", 0013400, opU63X  },
    { "TCBC", 0013500, opU63X  },
    { "BRO",  0013600, opPS31I },
    { "BRE",  0013700, opPS31I },
    { "SCAN", 0010600, opX,   0177700 },
    { "TNSL", 0011600, opX,   0177700 },
    { "QASL", 0011700, opU63, 0177777 },
    { "QASR", 0015700, opU63, 0177777 },
    { NULL }
    };


/* Move, special, firmware, immediate, bit field, and register operations.

   The move and special instructions are partially decoded by bits 8-10.  Only
   MABS, MTDS, MDS, MFDS, and MVBW are fully decoded; the other 19 instructions
   are not.  Therefore, it's easier to treat all of the instructions as
   potentially containing reserved bits and use secondary table entries.

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   0 |  move op  | opts/S decrement  |  Move
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   0 |  special op   | 0   0 | sp op |  Special
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The firmware extension instructions, including DMUL and DDIV in the base set,
   have generally unique encodings.  They are rare, so it's easier to use
   secondary entries for all of them.

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | 0   0   0   1 |      firmware option op       |  Firmware
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The immediate, bit field, and register instructions are fully decoded by bits
   4-7.

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | immediate op  |       immediate operand       |  Immediate
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 | field opcode  |    J field    |    K field    |  Field
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   0 |  register op  | SK| DB| DL| Z |STA| X | Q | S |  Register
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

   The table consists of 16 primary entries for the immediate, bit field, and
   register instructions, followed by the secondary entries for the remaining
   instructions.


   Implementation notes:

    1. The IXIT, PCN, LOCK, and UNLK instructions specify bits 12-15 as
       0000-0011.  However, values other than zero in bits 12-13 will decode to
       one of these instructions.  Specifically, the value of bits 12-15 for
       IXIT = 0000, PCN = nnn0, LOCK = nn01, and UNLK = nn11, where n is any
       collective value other than 0.

    2. By convention, SIMH simulators always decode all supported instructions,
       regardless of whether or not they are enabled by currents CPU firmware
       configurations.  So the EIS, APL, and COBOL-II instructions are present
       in the table and are not conditional on the CPU options set.
*/

static const OP_TABLE msfifr_ops = {
    { "",     0020000, opNone  },               /* move and special ops */
    { "",     0020400, opNone  },               /* DMUL, DDIV, and firmware extension opcodes */
    { "LDI",  0021000, opU255  },
    { "LDXI", 0021400, opU255  },
    { "CMPI", 0022000, opU255  },
    { "ADDI", 0022400, opU255  },
    { "SUBI", 0023000, opU255  },
    { "MPYI", 0023400, opU255  },
    { "DIVI", 0024000, opU255  },
    { "PSHR", 0024400, opR255R },
    { "LDNI", 0025000, opU255  },
    { "LDXN", 0025400, opU255  },
    { "CMPN", 0026000, opU255  },
    { "EXF",  0026400, opU1515 },
    { "DPF",  0027000, opU1515 },
    { "SETR", 0027400, opR255L },
    { "MOVE", 0020000, opSU3B,   0177763 },
    { "MVB",  0020040, opSU3B,   0177763 },
    { "MVBL", 0020100, opSU3,    0177773 },
    { "MABS", 0020110, opSU7,    0177777 },
    { "SCW",  0020120, opSU3,    0177773 },
    { "MTDS", 0020130, opSU7,    0177777 },
    { "MVLB", 0020140, opSU3,    0177773 },
    { "MDS",  0020150, opSU7,    0177777 },
    { "SCU",  0020160, opSU3,    0177773 },
    { "MFDS", 0020170, opSU7,    0177777 },
    { "MVBW", 0020200, opSU3NAS, 0177777 },
    { "CMPB", 0020240, opSU3B,   0177763 },
    { "RSW",  0020300, opNone,   0177761 },
    { "LLSH", 0020301, opNone,   0177761 },
    { "PLDA", 0020320, opNone,   0177761 },
    { "PSTA", 0020321, opNone,   0177761 },
    { "LSEA", 0020340, opNone,   0177763 },
    { "SSEA", 0020341, opNone,   0177763 },
    { "LDEA", 0020342, opNone,   0177763 },
    { "SDEA", 0020343, opNone,   0177763 },
    { "IXIT", 0020360, opNone,   0177777 },
    { "LOCK", 0020361, opNone,   0177777 },
    { "lock", 0020361, opNone,   0177763 },     /* decodes bits 12-15 as nn01 */
    { "PCN",  0020362, opNone,   0177777 },
    { "pcn",  0020360, opNone,   0177761 },     /* decodes bits 12-15 as nnn0 */
    { "UNLK", 0020363, opNone,   0177777 },
    { "unlk", 0020363, opNone,   0177763 },     /* decodes bits 12-15 as nn11 */
    { "EADD", 0020410, opNone,   0177777 },
    { "ESUB", 0020411, opNone,   0177777 },
    { "EMPY", 0020412, opNone,   0177777 },
    { "EDIV", 0020413, opNone,   0177777 },
    { "ENEG", 0020414, opNone,   0177777 },
    { "ECMP", 0020415, opNone,   0177777 },
    { "DMUL", 0020570, opNone,   0177777 },
    { "DDIV", 0020571, opNone,   0177777 },
    { "DMPY", 0020601, opNone,   0177617 },
    { "CVAD", 0020602, opS,      0177637 },
    { "CVDA", 0020603, opSCS,    0177777 },
    { "CVBD", 0020604, opS,      0177637 },
    { "CVDB", 0020605, opS,      0177637 },
    { "SLD",  0020606, opSU2,    0177677 },
    { "NSLD", 0020607, opSU2,    0177677 },
    { "SRD",  0020610, opSU2,    0177677 },
    { "ADDD", 0020611, opSU2,    0177677 },
    { "CMPD", 0020612, opSU2,    0177677 },
    { "SUBD", 0020613, opSU2,    0177677 },
    { "MPYD", 0020614, opSU2,    0177677 },
    { NULL }
    };


/* I/O and control operations.

   The I/O instructions are fully decoded by bits 8-11.  The control
   instructions are partially decoded and require additional decoding by bits
   14-15.  The table consists of 16 primary entries, followed by the secondary
   entries for the instructions that are partially decoded or have reserved
   bits.

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 | 0   0   0   0 |  I/O opcode   |    K field    |  I/O
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 | 0   0   0   0 |  cntl opcode  | 0   0 | cn op |  Control
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Implementation notes:

    1. The SED instruction specifies bits 12-14 as 000, and the instruction only
       works correctly if opcodes 030040 and 030041 are used.  Values other than
       000 will decode and execute as SED, but the status register is set
       improperly (the I bit is cleared, bits 12-15 are rotated right twice and
       then ORed into the status register).

    2. The XCHD, DISP, PSDB, and PSEB instructions specify bits 12-15 as
       0000-0011.  However, values other than zero in bits 12-13 will decode to
       one of these instructions.  Specifically, the value of bits 12-15 for
       XCHD = 0000, DISP = nnn0, PSDB = nn01, and PSEB = nn11, where n is any
       collective value other than 0.

    3. The SMSK, SCLK, RMSK, and RCLK instructions specify bits 12-15 as
       0000-0011.  However, values other than zero in bits 12-14 will decode to
       one of these instructions.  Specifically, the value of bits 12-15 for
       SMSK and RMSK = 0000, and SCLK and SMSK = nnnn, where n is any collective
       value other than 0.

    4. The double entries for DISP, SCLK, and RCLK ensure that their full ranges
       decode to the indicated instructions for printing but only the primary
       opcode is encoded when entering instructions in symbolic form.
*/

static const OP_TABLE ioc_ops = {
    { "LST",  0030000, opSU15 },
    { "PAUS", 0030020, opC15  },
    { "",     0030040, opNone },                /* SED */
    { "",     0030060, opNone },                /* XCHD, PSDB, DISP, PSEB */
    { "",     0030100, opNone },                /* SMSK, SCLK */
    { "",     0030120, opNone },                /* RMSK, RCLK */
    { "XEQ",  0030140, opSU15 },
    { "SIO",  0030160, opSU15 },
    { "RIO",  0030200, opSU15 },
    { "WIO",  0030220, opSU15 },
    { "TIO",  0030240, opSU15 },
    { "CIO",  0030260, opSU15 },
    { "CMD",  0030300, opSU15 },
    { "SST",  0030320, opSU15 },
    { "SIN",  0030340, opSU15 },
    { "HALT", 0030360, opC15  },
    { "SED",  0030040, opU1,   0177777 },
    { "sed",  0030040, opU1,   0177760 },       /* decodes bits 12-14 as nnn */
    { "XCHD", 0030060, opNone, 0177777 },
    { "PSDB", 0030061, opNone, 0177777 },
    { "psdb", 0030061, opNone, 0177763 },       /* decodes bits 12-15 as nn01 */
    { "DISP", 0030062, opNone, 0177777 },
    { "disp", 0030060, opNone, 0177761 },       /* decodes bits 12-15 as nnn0 */
    { "PSEB", 0030063, opNone, 0177777 },
    { "pseb", 0030063, opNone, 0177763 },       /* decodes bits 12-15 as nn11 */
    { "SMSK", 0030100, opNone, 0177777 },
    { "SCLK", 0030101, opNone, 0177777 },
    { "sclk", 0030100, opNone, 0177760 },       /* decodes bits 12-15 as nnnn */
    { "RMSK", 0030120, opNone, 0177777 },
    { "RCLK", 0030121, opNone, 0177777 },
    { "rclk", 0030120, opNone, 0177760 },       /* decodes bits 12-15 as nnnn */
    { NULL }
    };


/* Program, immediate, and memory operations.

   The program, immediate, and memory instructions are fully decoded by bits
   4-7.  The table consists of 16 primary entries.  Entry 0 is a placeholder for
   the separate I/O and control instructions table.

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 |  program op   |            N field            |  Program
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 | immediate op  |       immediate operand       |  Immediate
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   0   1   1 |   memory op   |        P displacement         |  Memory
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

static const OP_TABLE pmi_ops = {
    { "",     0000000, opNone  },               /* placeholder for subop 00 */
    { "SCAL", 0030400, opPU255 },
    { "PCAL", 0031000, opPU255 },
    { "EXIT", 0031400, opPU255 },
    { "SXIT", 0032000, opPU255 },
    { "ADXI", 0032400, opU255  },
    { "SBXI", 0033000, opU255  },
    { "LLBL", 0033400, opPU255 },
    { "LDPP", 0034000, opPU255 },
    { "LDPN", 0034400, opPU255 },
    { "ADDS", 0035000, opU255  },
    { "SUBS", 0035400, opU255  },
    { "",     0036000, opNone  },               /* unassigned opcode */
    { "ORI",  0036400, opU255  },
    { "XORI", 0037000, opU255  },
    { "ANDI", 0037400, opU255  },
    { NULL }
    };


/* Memory, loop, and branch operations.

   The memory and loop instructions are fully decoded by bits 0-3, except for
   TBA, MTBA, TBX, MTBX, STOR, INCM, DECM, LDB, LDD, STB, and STD, which depend
   on bits 4-6.  The branch instructions also depend on 4-6, except for BCC,
   which also depends on bits 7-9.  The table consists of 16 primary entries,
   followed by the secondary entries for the instructions that are partially
   decoded or have reserved bits.  Entries 0-3 are placeholders for the other
   instruction tables.

       0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |   memory op   | X | I |         mode and displacement         |  Memory
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                             | 0 | 0 |     P+ displacement 0-255     |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 0 | 1 |     P- displacement 0-255     |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 0 |    DB+ displacement 0-255     |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 1 | 0 |   Q+ displacement 0-127   |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 1 | 1 | 0 | Q- displacement 0-63  |
                             +---+---+---+---+---+---+---+---+---+---+
                             | 1 | 1 | 1 | 1 | S- displacement 0-63  |
                             +---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     |   memory op   | X | I | s |     mode and displacement         |   Memory
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
                                 | 0 |    DB+ displacement 0-255     |
                                 +---+---+---+---+---+---+---+---+---+
                                 | 1 | 0 |   Q+ displacement 0-127   |
                                 +---+---+---+---+---+---+---+---+---+
                                 | 1 | 1 | 0 | Q- displacement 0-63  |
                                 +---+---+---+---+---+---+---+---+---+
                                 | 1 | 1 | 1 | S- displacement 0-63  |
                                 +---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 0   1   0   1 |loop op| 0 |+/-|    P-relative displacement    |  Loop
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     | 1   1   0   0 | I | 0   1 | > | = | < | P+- displacement 0-31 |  Branch
     +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+


   Implementation notes:

    1. The BCC instruction specifies the branch condition in bits 7-9.  There
       are separate secondary entries for each of the conditions.

    2. The BR (Branch) instruction has two forms.  When bit 6 = 0, it has a
       P-relative displacement with optional indexing and indirection.  When bit
       6 = 1, it has an indirect DB/Q/S-relative displacement with optional
       indexing.  Two secondary entries are needed for the two operand types.
       The opcode for BR DB/Q/S,I is 143000, i.e., with the I bit forced on.
       The second opcode entry is 141000 to put the I bit with the operand for
       proper decoding.

    3. Signed displacements are in sign-magnitude form, not two's complement.
*/

static const OP_TABLE mlb_ops = {
    { "",     0000000, opNone    },             /* placeholder for opcode 00 */
    { "",     0010000, opNone    },             /* placeholder for opcode 01 */
    { "",     0020000, opNone    },             /* placeholder for opcode 02 */
    { "",     0030000, opNone    },             /* placeholder for opcode 03 */
    { "LOAD", 0040000, opPD255IX },
    { "",     0050000, opNone    },             /* TBA, MTBA, TBX, MTBX, STOR */
    { "CMPM", 0060000, opPD255IX },
    { "ADDM", 0070000, opPD255IX },
    { "SUBM", 0100000, opPD255IX },
    { "MPYM", 0110000, opPD255IX },
    { "",     0120000, opNone    },             /* INCM, DECM */
    { "LDX",  0130000, opPD255IX },
    { "",     0140000, opNone    },             /* BR, BCC */
    { "",     0150000, opNone    },             /* LDB, LDD */
    { "",     0160000, opNone    },             /* STB, STD */
    { "LRA",  0170000, opPD255IX },
    { "TBA",  0050000, opPS255,   0177777 },
    { "MTBA", 0052000, opPS255,   0177777 },
    { "TBX",  0054000, opPS255,   0177777 },
    { "MTBX", 0056000, opPS255,   0177777 },
    { "STOR", 0051000, opD255IX,  0177777 },
    { "INCM", 0120000, opD255IX,  0177777 },
    { "DECM", 0121000, opD255IX,  0177777 },
    { "BR",   0140000, opPS255IX, 0177777 },    /* P-relative displacement */
    { "BN",   0141000, opPS31I,   0177777 },    /* branch never */
    { "BL",   0141100, opPS31I,   0177777 },    /* branch on less than */
    { "BE",   0141200, opPS31I,   0177777 },    /* branch on equal */
    { "BLE",  0141300, opPS31I,   0177777 },    /* branch on less than or equal */
    { "BG",   0141400, opPS31I,   0177777 },    /* branch on greater than  */
    { "BNE",  0141500, opPS31I,   0177777 },    /* branch on not equal */
    { "BGE",  0141600, opPS31I,   0177777 },    /* branch on greater than or equal */
    { "BA",   0141700, opPS31I,   0177777 },    /* branch always */
    { "BR",   0141000, opD255IX,  0177777 },    /* indirect DB/Q/S-relative displacement */
    { "LDB",  0150000, opD255IX,  0177777 },
    { "LDD",  0151000, opD255IX,  0177777 },
    { "STB",  0160000, opD255IX,  0177777 },
    { "STD",  0161000, opD255IX,  0177777 },
    { NULL }
    };


/* System interface local SCP support routines */

static void   one_time_init  (void);
static t_bool fprint_stopped (FILE   *st,   t_stat     reason);
static void   fprint_addr    (FILE   *st,   DEVICE     *dptr, t_addr     addr);
static t_addr parse_addr     (DEVICE *dptr, CONST char *cptr, CONST char **tptr);

static t_stat hp_cold_cmd  (int32 arg, CONST char *buf);
static t_stat hp_exdep_cmd (int32 arg, CONST char *buf);
static t_stat hp_run_cmd   (int32 arg, CONST char *buf);
static t_stat hp_brk_cmd   (int32 arg, CONST char *buf);

/* System interface local utility routines */

static void   fprint_value       (FILE *ofile, t_value val,  uint32 radix, uint32 width, uint32 format);
static t_stat fprint_order       (FILE *ofile, t_value *val, uint32 radix);
static t_stat fprint_instruction (FILE *ofile, const OP_TABLE ops, t_value *instruction,
                                  t_value mask, uint32 shift, uint32 radix);

static t_stat parse_cpu          (CONST char *cptr, t_addr address, UNIT *uptr, t_value *value, int32 switches);


/* System interface state */

static size_t device_size = 0;                  /* maximum device name size */
static size_t flag_size   = 0;                  /* maximum debug flag name size */

static APC_FLAGS parse_config = apcNone;        /* address parser configuration  */


/* System interface global data structures */

#define E                   0400                /* parity bit for even parity */
#define O                   0000                /* parity bit for odd parity */

const uint16 odd_parity [256] = {                       /* odd parity table */
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


static const BITSET_NAME inbound_names [] = {   /* Inbound signal names, in INBOUND_SIGNAL order */
    "DSETINT",                                  /*   000000000001 */
    "DCONTSTB",                                 /*   000000000002 */
    "DSTARTIO",                                 /*   000000000004 */
    "DWRITESTB",                                /*   000000000010 */
    "DRESETINT",                                /*   000000000020 */
    "DSTATSTB",                                 /*   000000000040 */
    "DSETMASK",                                 /*   000000000100 */
    "DREADSTB",                                 /*   000000000200 */
    "ACKSR",                                    /*   000000000400 */
    "TOGGLESR",                                 /*   000000001000 */
    "SETINT",                                   /*   000000002000 */
    "PCMD1",                                    /*   000000004000 */
    "PCONTSTB",                                 /*   000000010000 */
    "SETJMP",                                   /*   000000020000 */
    "PSTATSTB",                                 /*   000000040000 */
    "PWRITESTB",                                /*   000000100000 */
    "PREADSTB",                                 /*   000000200000 */
    "EOT",                                      /*   000000400000 */
    "TOGGLEINXFER",                             /*   000001000000 */
    "TOGGLEOUTXFER",                            /*   000002000000 */
    "READNEXTWD",                               /*   000004000000 */
    "TOGGLESIOOK",                              /*   000010000000 */
    "DEVNODB",                                  /*   000020000000 */
    "INTPOLLIN",                                /*   000040000000 */
    "XFERERROR",                                /*   000100000000 */
    "CHANSO",                                   /*   000200000000 */
    "PFWARN"                                    /*   000400000000 */
    };

const BITSET_FORMAT inbound_format =            /* names, offset, direction, alternates, bar */
    { FMT_INIT (inbound_names, 0, lsb_first, no_alt, no_bar) };


static const BITSET_NAME outbound_names [] = {  /* Outbound signal names, in OUTBOUND_SIGNAL order */
    "INTREQ",                                   /*   000000200000 */
    "INTACK",                                   /*   000000400000 */
    "INTPOLLOUT",                               /*   000001000000 */
    "DEVEND",                                   /*   000002000000 */
    "JMPMET",                                   /*   000004000000 */
    "CHANACK",                                  /*   000010000000 */
    "CHANSR",                                   /*   000020000000 */
    "SRn"                                       /*   000040000000 */
    };

const BITSET_FORMAT outbound_format =           /* names, offset, direction, alternates, bar */
    { FMT_INIT (outbound_names, 16, lsb_first, no_alt, no_bar) };


/* System interface global SCP data definitions */

char sim_name [] = "HP 3000";                   /* the simulator name */

int32 sim_emax = 2;                             /* the maximum number of words in any instruction */

void (*sim_vm_init) (void) = &one_time_init;    /* a pointer to the one-time initializer */

DEVICE *sim_devices [] = {                      /* an array of pointers to the simulated devices */
    &cpu_dev,                                   /*   CPU (must be first) */
    &iop_dev,                                   /*   I/O Processor */
    &mpx_dev,                                   /*   Multiplexer Channel */
    &sel_dev,                                   /*   Selector Channel */
    &scmb_dev [0], &scmb_dev [1],               /*   Selector Channel Maintenance Boards */
    &atcd_dev,     &atcc_dev,                   /*   Asynchronous Terminal Controller (TDI and TCI) */
    &clk_dev,                                   /*   System Clock */
    &ds_dev,                                    /*   7905/06/20/25 MAC Disc Interface */
    &ms_dev,                                    /*   7970B/E Magnetic Tape Interface */
    NULL                                        /* end of the device list */
    };

#define DEVICE_COUNT        (sizeof sim_devices / sizeof sim_devices [0] - 1)


const char *sim_stop_messages [] = {            /* an array of pointers to the stop messages in STOP_nnn order */
    "Impossible error",                         /*   0 (never returned) */
    "System halt",                              /*   STOP_SYSHALT */
    "Unimplemented instruction",                /*   STOP_UNIMPL */
    "Undefined instruction",                    /*   STOP_UNDEF */
    "CPU paused",                               /*   STOP_PAUS */
    "Programmed halt",                          /*   STOP_HALT */
    "Breakpoint",                               /*   STOP_BRKPNT */
    "Infinite loop",                            /*   STOP_INFLOOP */
    "Cold load complete",                       /*   STOP_CLOAD */
    "Cold dump complete"                        /*   STOP_CDUMP */
    };


/* Local command table.

   This table defines commands and command behaviors that are specific to this
   simulator.  No new commands are defined, but several commands are repurposed
   or extended.  Specifically:

     * EXAMINE, DEPOSIT, IEXAMINE, and IDEPOSIT accept bank/offset form, implied
       DBANK offsets, and memory bank override switches.

     * RUN and GO accept implied PBANK offsets and reject bank/offset form and
       memory bank override switches.

     * BREAK and NOBREAK accept bank/offset form and implied PBANK offsets and
       reject memory bank override switches.

     * LOAD and DUMP invoke the CPU cold load/cold dump facility, rather than
       loading or dumping binary files.

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
/*    Name        Action Routine  Argument   Help String                                          */
/*    ----------  --------------  ---------  ---------------------------------------------------- */
    { "RESET",    NULL,           0,         NULL                                                 },
    { "BOOT",     NULL,           0,         NULL                                                 },

    { "EXAMINE",  &hp_exdep_cmd,  0,         NULL                                                 },
    { "IEXAMINE", &hp_exdep_cmd,  0,         NULL                                                 },
    { "DEPOSIT",  &hp_exdep_cmd,  0,         NULL                                                 },
    { "IDEPOSIT", &hp_exdep_cmd,  0,         NULL                                                 },
    { "RUN",      &hp_run_cmd,    0,         NULL                                                 },
    { "GO",       &hp_run_cmd,    0,         NULL                                                 },
    { "BREAK",    &hp_brk_cmd,    0,         NULL                                                 },
    { "NOBREAK",  &hp_brk_cmd,    0,         NULL                                                 },
    { "LOAD",     &hp_cold_cmd,   Cold_Load, "l{oad} {cntlword}        cold load from a device\n" },
    { "DUMP",     &hp_cold_cmd,   Cold_Dump, "du{mp} {cntlword}        cold dump to a device\n"   },
    { NULL }
    };



/* System interface global SCP support routines */


/* Load and dump memory images from and to files.

   The LOAD and DUMP commands are intended to provide a basic method of loading
   and dumping programs into and from memory.  Typically, these commands operate
   on a simple, low-level format, e.g., a memory image.

   However, the HP 3000 requires the bank and segment registers being set up
   appropriately before execution.  In addition, the CPU microcode depends on
   segment tables being present in certain fixed memory locations as part of a
   program load.  These actions will not take place unless the system cold load
   facility is employed.

   Consequently, the LOAD and DUMP commands are repurposed to invoke the cold
   load and cold dump facilities, respectively, and this is a dummy routine that
   will never be called.  It is present only to satisfy the external declared in
   the SCP module.
*/

t_stat sim_load (FILE *fptr, CONST char *cptr, CONST char *fnam, int flag)
{
return SCPE_ARG;                                        /* return an error if called inadvertently */
}


/* Print a value in symbolic format.

   Print the data value in the format specified by the optional switches on the
   output stream supplied.  This routine is called to print:

     - the next instruction mnemonic when the simulator stops
     - the result of EXAMining a register marked with a user flag
     - the result of EXAMining a memory address
     - the result of EVALuating a symbol

   On entry, "ofile" is the opened output stream, "addr" is respectively the
   program counter, register radix and flags, memory address, or symbol index,
   "val" is a pointer to an array of t_values of depth "sim_emax" representing
   the value to be printed, "uptr" is respectively NULL, NULL, a pointer to the
   named unit, or a pointer to the default unit, and "sw" contains any switches
   passed on the command line.  "sw" also includes SIM_SW_STOP for a simulator
   stop call or SIM_SW_REG for a register call.

   On exit, a status code is returned to the caller.  If the format requested is
   not supported, SCPE_ARG status is returned, which causes the caller to print
   the value in numeric format.  Otherwise, SCPE_OK status is returned if a
   single-word value was consumed, or the negative number of extra words (beyond
   the first) consumed in printing the symbol is returned.  For example,
   printing a two-word symbol would return SCPE_OK_2_WORDS (= -1).

   The following symbolic formats are supported by the listed switches:

     Switch   Interpretation
     ------   -----------------------------------
       -a     a single character in the low byte
       -b     a 16-bit binary value
       -c     a two-character packed string
       -i     an I/O program instruction mnemonic
       -m     a CPU instruction mnemonic
       -s     a CPU status mnemonic
       -o     override numeric output to octal
       -d     override numeric output to decimal
       -h     override numeric output to hex

   Memory may be displayed in any format.  All registers may be overridden to
   display in octal, decimal, or hexadecimal numeric format.  Only registers
   marked with the REG_A flag may be displayed in any format.  Registers marked
   with REG_B may be displayed in binary format.  Registers marked with REG_M
   will default to CPU instruction mnemonic display.  Registers marked with
   REG_S will default to CPU status mnemonic display.

   When displaying mnemonics, operand values are displayed in a radix suitable
   to the type of the value.  Address values are displayed in the CPU's address
   radix, which is octal, and data values are displayed in the CPU's data radix,
   which defaults to octal but may be set to a different radix or overridden by
   a switch on the command line.


   Implementation notes:

    1. Because mnemonics are specific to the CPU/MPX/SEL, the CPU's radix
       settings are used, even if the unit is a peripheral.  For example,
       displaying disc sector data as CPU instructions uses the CPU's address
       and data radix values, rather than the disc's values.

    2. Displaying a register having a symbolic default format (e.g., CIR) will
       use the default unless the radix is overridden on the command line.  For
       example, "EXAMINE CIR" displays the CIR value as an instruction mnemonic,
       whereas "EXAMINE -O CIR" displays the value as octal.  Adding "-M" will
       force mnemonic display and allow the radix switch to override the operand
       display.  For example, "EXAMINE -M -O CIR" displays the value as mnemonic
       and overrides the operand radix to octal.
*/

t_stat fprint_sym (FILE *ofile, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
const t_bool is_reg = (sw & SIM_SW_REG) != 0;               /* TRUE if this is a register access */
uint32 radix_override;

if (sw & SWMASK ('A') && (!is_reg || addr & REG_A))         /* if ASCII character display is requested and permitted */
    if (val [0] <= D8_SMAX) {                               /*     then if the value is a single character */
        fputs (fmt_char (val [0]), ofile);                  /*       then format and print it */
        return SCPE_OK;
        }

    else                                                    /*   otherwise */
        return SCPE_ARG;                                    /*     report that it cannot be displayed */

else if (sw & SWMASK ('C') && (!is_reg || addr & REG_A)) {  /* if ASCII string display is requested and permitted */
    fputs (fmt_char (UPPER_BYTE (val [0])), ofile);         /*     then format and print the upper byte */
    fputc (',', ofile);                                     /*       followed by a separator */
    fputs (fmt_char (LOWER_BYTE (val [0])), ofile);         /*         then format and print the lower byte */
    return SCPE_OK;
    }

else if (sw & SWMASK ('B')                                  /* if binary display is requested */
  && (!is_reg || addr & (REG_A | REG_B))) {                 /*   and is permitted */
    fprint_val (ofile, val [0], 2, DV_WIDTH, PV_RZRO);      /*     then format and print the value */
    return SCPE_OK;
    }

else {                                                      /* otherwise display as numeric or mnemonic */
    if (sw & SWMASK ('O'))                                  /* if an octal override is present */
        radix_override = 8;                                 /*   then print the value in base 8 */
    else if (sw & SWMASK ('D'))                             /* otherwise if a decimal override is present */
        radix_override = 10;                                /*   then print the value in base 10 */
    else if (sw & SWMASK ('H'))                             /* otherwise if a hex override is present */
        radix_override = 16;                                /*   then print the value in base 16 */
    else                                                    /* otherwise */
        radix_override = 0;                                 /*   use the default radix setting */

    if (sw & SWMASK ('I') && !is_reg)                       /* if I/O channel order memory display is requested */
        return fprint_order (ofile, val, radix_override);   /*   then format and print it */

    else if (sw & SWMASK ('M')                              /* otherwise if CPU instruction display is requested */
      && (!is_reg || addr & (REG_A | REG_M))                /*   and is permitted */
      || is_reg && addr & REG_M && radix_override == 0)     /* or if displaying a register that defaults to mnemonic */
        return fprint_cpu (ofile, val, radix_override, sw); /*   then format and print it */

    else if (sw & SWMASK ('S')                              /* otherwise if status display is requested */
      && (!is_reg || addr & (REG_A | REG_S))                /*   and is permitted */
      || is_reg && addr & REG_S && radix_override == 0) {   /* or if displaying a register that defaults to status */
        fputs (fmt_status ((uint32) val [0]), ofile);       /*   then format the status flags and condition code */
        fputc (' ', ofile);                                 /*     and add a separator */

        fprint_value (ofile, val [0] & STATUS_CS_MASK,      /* print the code segment number */
                      (radix_override ? radix_override : cpu_dev.dradix),
                      STATUS_CS_WIDTH, PV_RZRO);

        return SCPE_OK;
        }

    else                                                    /* otherwise */
        return SCPE_ARG;                                    /*   request that the value be printed numerically */
    }
}


/* Parse a string in symbolic format.

   Parse the input string using the interpretation specified by the optional
   switches, and return the resulting value(s).  This routine is called to
   parse an input string when:

     - DEPOSITing into a register marked with a user flag
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

   The following symbolic formats are supported by the listed switches:

     Switch   Interpretation
     ------   ----------------------------------
       -a     a single character the in low byte
       -c     a two-character packed string
       -o     override numeric input to octal
       -d     override numeric input to decimal
       -h     override numeric input to hex

   In the absence of switches, a leading ' implies "-a", a leading " implies
   "-c", and a leading alphabetic character implies an instruction mnemonic.  If
   a single character is supplied with "-c", the low byte of the resulting value
   will be zero; follow the character with a space if the low byte is to be
   padded  with a space.

   Caution must be exercised when entering hex values without a leading digit.
   A value that is the same as an instruction mnemonic will be interpreted as
   the latter unless overridden by the "-h" switch.  For example, "ADD" is an
   instruction mnemonic, but "ADE" is a hex value.  To avoid confusion, always
   enter hex values with the "-h" switch or with a leading zero (i.e., "0ADD").

   When entering mnemonics, operand values are parsed in a radix suitable to the
   type of the value.  Address values are parsed in the CPU's address radix,
   which is octal, and data values are parsed in the CPU's data radix, which
   defaults to octal but may be set to a different radix or overridden by a
   switch on the command line.


   Implementation notes:

    1. Because the mnemonics are specific to the CPU/MPX/SEL, the CPU's radix
       settings are used, even if the unit is a peripheral.  For example,
       entering disc sector data as CPU instructions uses the CPU's address and
       data radix values, rather than the disc's values.
*/

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
while (isspace ((int) *cptr))                           /* skip over any leading spaces */
    cptr++;                                             /*   that are present in the line */

if (sw & SWMASK ('A') || *cptr == '\'' && cptr++)       /* if an ASCII character parse is requested */
    if (cptr [0] != '\0') {                             /*   then if a character is present */
        val [0] = (t_value) cptr [0];                   /*     then convert the character value */
        return SCPE_OK;                                 /*       and indicate success */
        }

    else                                                /* otherwise */
        return SCPE_ARG;                                /*   report that the line cannot be parsed */

else if (sw & SWMASK ('C') || *cptr == '"' && cptr++)       /* otherwise if a character string parse is requested */
    if (cptr [0] != '\0') {                                 /*   then if characters are present */
        val [0] = (t_value) TO_WORD (cptr [0], cptr [1]);   /*     then convert the character value(s) */
        return SCPE_OK;                                     /*       and indicate success */
        }

    else                                                    /* otherwise */
        return SCPE_ARG;                                    /*   report that the line cannot be parsed */


else                                                    /* otherwise */
    return parse_cpu (cptr, addr, uptr, val, sw);       /*   attempt a mnemonic instruction parse */
}


/* Set a device configuration value.

   This validation routine is called to set a device's I/O configuration (device
   number, interrupt mask, interrupt priority, and service request number).  The
   "uptr" parameter points to the unit being configured, "code" is a validation
   constant (VAL_DEVNO, VAL_INTMASK, VAL_INTPRI, or VAL_SRNO), "cptr" points to
   the first character of the value to be set, and "desc" points to the DIB
   associated with the device.

   If the supplied value is acceptable, it is stored in the DIB, and the routine
   returns SCPE_OK.  Otherwise, an error code is returned.

   For the following validation constants, the acceptable ranges of values are:

     VAL_DEVNO   -- 0-127
     VAL_INTMASK -- 0-15 | E | D
     VAL_INTPRI  -- 0-31
     VAL_SRNO    -- 0-15


   Implementation notes:

    1. For a numeric interrupt mask entry value <n>, the value stored in the DIB
       is 2^<n>.  For mask entry values "D" and "E", the stored values are 0 and
       0177777, respectively.

    2. The SCMB is the only device that may or may not have a service request
       number, depending on whether or not it is connected to the multiplexer
       channel bus.  Therefore, the current request number must be valid before
       it may be changed.
*/

t_stat hp_set_dib (UNIT *uptr, int32 code, CONST char *cptr, void *desc)
{
DIB *const dibptr = (DIB *) desc;                       /* a pointer to the associated DIB */
t_stat     status = SCPE_OK;
t_value    value;

if (cptr == NULL || *cptr == '\0')                      /* if the expected value is missing */
    status = SCPE_MISVAL;                               /*   then report the error */

else                                                    /* otherwise a value is present */
    switch (code) {                                     /*   and parsing depends on the value expected */

        case VAL_DEVNO:                                 /* DEVNO=0-127 */
            value = get_uint (cptr, DEVNO_BASE,         /* parse the supplied device number */
                              DEVNO_MAX, &status);

            if (status == SCPE_OK)                      /* if it is valid */
                dibptr->device_number = (uint32) value; /*   then save it in the DIB */
            break;

        case VAL_INTMASK:                                   /* INTMASK=0-15/E/D */
            if (*cptr == 'E')                               /* if the mask value is "E" (enable) */
                dibptr->interrupt_mask = INTMASK_E;         /*   then set all mask bits on */

            else if (*cptr == 'D')                          /* otherwise if the mask value is "D" (disable) */
                dibptr->interrupt_mask = INTMASK_D;         /*   then set all mask bits off */

            else {                                          /* otherwise */
                value = get_uint (cptr, INTMASK_BASE,       /*   parse the supplied numeric mask value */
                                  INTMASK_MAX, &status);

                if (status == SCPE_OK)                      /* if it is valid */
                    dibptr->interrupt_mask = 1 << value;    /*   then set the corresponding mask bit in the DIB */
                }
            break;

        case VAL_INTPRI:                                    /* INTPRI=0-31 */
            value = get_uint (cptr, INTPRI_BASE,            /* parse the supplied priority number */
                              INTPRI_MAX, &status);

            if (status == SCPE_OK)                              /* if it is valid */
                dibptr->interrupt_priority = (uint32) value;    /*   then save it in the DIB */
            break;

        case VAL_SRNO:                                          /* SRNO=0-15 */
            if (dibptr->service_request_number == SRNO_UNUSED)  /* if the current setting is "unused" */
                status = SCPE_NOFNC;                            /*   then report that it cannot be set */

            else {                                              /* otherwise */
                value = get_uint (cptr, SRNO_BASE,              /*   parse the supplied service request number */
                                  SRNO_MAX, &status);

                if (status == SCPE_OK)                                  /* if it is valid */
                    dibptr->service_request_number = (uint32) value;    /*   then save it in the DIB */
                }
            break;

        default:                                        /* if an illegal code was passed */
            status = SCPE_IERR;                         /*   then report an internal coding error */
        }

return status;                                          /* return the validation result */
}


/* Show the device configuration values.

   This display routine is called to show a device's I/O configuration (device
   number, interrupt mask, interrupt priority, or service request number).  The
   "st" parameter is the open output stream, "uptr" points to the unit being
   queried, "code" is a validation constant (VAL_DEVNO, VAL_INTMASK, VAL_INTPRI,
   or VAL_SRNO), and "desc" points at the DIB associated with the device.

   If the code is acceptable, the routine prints the DIB value for the specified
   characteristic and returns SCPE_OK.  Otherwise, an error code is returned.

   For the following validation constants, the configuration values printed are:

     VAL_DEVNO   -- DEVNO=0-127
     VAL_INTMASK -- INTMASK=0-15 | E | D
     VAL_INTPRI  -- INTPRI=0-31
     VAL_SRNO    -- SRNO=0-15


   Implementation notes:

    1. For a numeric interrupt mask entry value <n>, the value stored in the DIB
       is 2^<n>.  For mask entry values "D" and "E", the stored values are 0 and
       0177777, respectively.
*/

t_stat hp_show_dib (FILE *st, UNIT *uptr, int32 code, CONST void *desc)
{
const DIB *const dibptr = (const DIB *) desc;           /* a pointer to the associated DIB */
uint32           mask, value;

switch (code) {                                         /* display the requested value */

    case VAL_DEVNO:                                     /* show the device number */
        fprintf (st, "DEVNO=%d", dibptr->device_number);
        break;

    case VAL_INTMASK:                                   /* show the interrupt mask */
        fputs ("INTMASK=", st);

        if (dibptr->interrupt_mask == INTMASK_D)        /* if the mask is disabled */
            fputc ('D', st);                            /*   then display "D" */

        else if (dibptr->interrupt_mask == INTMASK_E)   /* otherwise if the mask is enabled */
            fputc ('E', st);                            /*   then display "E" */

        else {                                          /* otherwise */
            mask = dibptr->interrupt_mask;              /*   display a specific mask value */

            for (value = 0; !(mask & 1); value++)       /* count the number of mask bit shifts */
                mask = mask >> 1;                       /*   until the correct one is found */

            fprintf (st, "%d", value);                  /* display the mask bit number */
            }
        break;

    case VAL_INTPRI:                                    /* show the interrupt priority */
        fprintf (st, "INTPRI=%d", dibptr->interrupt_priority);
        break;

    case VAL_SRNO:                                          /* show the service request number */
        if (dibptr->service_request_number == SRNO_UNUSED)  /* if the current setting is "unused" */
            fprintf (st, "SRNO not used");                  /*   then report it */
        else                                                /* otherwise report the SR number */
            fprintf (st, "SRNO=%d", dibptr->service_request_number);
        break;

    default:                                            /* if an illegal code was passed */
        return SCPE_IERR;                               /*   then report an internal coding error */
    }

return SCPE_OK;                                         /* return the display result */
}



/* System interface global utility routines */


/* Check for device conflicts.

   The device information blocks (DIBs) for the set of enabled devices are
   checked for consistency.  Each device number, interrupt priority number, and
   service request number must be unique among the enabled devices.  These
   requirements are checked as part of the instruction execution prelude; this
   allows the user to exchange two device numbers (e.g.) simply by setting each
   device to the other's device number.  If conflicts were enforced instead at
   the time the numbers were entered, the first device would have to be set to
   an unused number before the second could be set to the first device's number.

   The routine begins by filling in a DIB value table from all of the device
   DIBs to allow indexed access to the values to be checked.  Unused DIB values
   and values corresponding to devices that have no DIBs or are disabled are set
   to the corresponding UNUSED constants.

   As part of the device scan, the sizes of the largest device name and debug
   flag name among the devices enabled for debugging are accumulated for use in
   printing debug tracing statements.

   After the DIB value table is filled in, a conflict check is made for each
   conflict type (i.e., device number, interrupt priority, or service request
   number).  For each check, a conflict table is built, where each array element
   is set to the count of devices that contain DIB values equal to the element
   index.  For example, when processing device number values, conflict table
   element 6 is set to the count of devices that have dibptr->device_number set
   to 6.  If any conflict table element is set more than once, the "conflict_is"
   variable is set to the type of conflict.

   If any conflicts exist for the current type, the conflict table is scanned.
   A conflict table element value (i.e., device count) greater than 1 indicates
   a conflict.  For each such value, the DIB value table is scanned to find
   matching values, and the device names associated with the matching values are
   printed.

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
*/

t_bool hp_device_conflict (void)
{
typedef enum {                                          /* conflict types */
    Device,                                             /*   device number conflict */
    Interrupt,                                          /*   interrupt priority conflict */
    Service,                                            /*   service request number conflict */
    None                                                /*   no conflict */
    } CONFLICT_TYPE;

#define CONFLICT_COUNT      3                           /* the number of conflict types to check */

static const uint32 max_number [CONFLICT_COUNT] = {     /* the last element index, in CONFLICT_TYPE order */
    DEVNO_MAX,
    INTPRI_MAX,
    SRNO_MAX
    };

static const char *conflict_label [CONFLICT_COUNT] = {  /* the conflict names, in CONFLICT_TYPE order */
    "Device number",
    "Interrupt priority",
    "Service request number"
    };

const DIB     *dibptr;
const DEBTAB  *tptr;
DEVICE        *dptr;
size_t        name_length, flag_length;
uint32        dev, val;
CONFLICT_TYPE conf, conflict_is;
int32         count;
int32         dib_val   [DEVICE_COUNT] [CONFLICT_COUNT];
int32         conflicts [DEVNO_MAX + 1];

device_size = 0;                                        /* reset the device and flag name sizes */
flag_size = 0;                                          /*   to those of the devices actively debugging */

for (dev = 0; dev < DEVICE_COUNT; dev++) {              /* fill in the DIB value table */
    dptr = (DEVICE *) sim_devices [dev];                /*   from the device table */
    dibptr = (DIB *) dptr->ctxt;                        /*      and their associated DIBs */

    if (dibptr && !(dptr->flags & DEV_DIS)) {                   /* if the DIB is defined and the device is enabled */
        dib_val [dev] [Device]    = dibptr->device_number;      /*   then copy the values to the DIB table */
        dib_val [dev] [Interrupt] = dibptr->interrupt_priority;
        dib_val [dev] [Service]   = dibptr->service_request_number;
        }

    else {                                                      /* otherwise the device will not participate in I/O */
        dib_val [dev] [Device]    = DEVNO_UNUSED;               /*   so set this table entry */
        dib_val [dev] [Interrupt] = INTPRI_UNUSED;              /*     to the "unused" values */
        dib_val [dev] [Service]   = SRNO_UNUSED;
        }

    if (sim_deb && dptr->dctrl) {                       /* if debugging is active for this device */
        name_length = strlen (sim_dname (dptr));        /*   then get the length of the device name */

        if (name_length > device_size)                  /* if it's greater than the current maximum */
            device_size = name_length;                  /*   then reset the size */

        if (dptr->debflags)                             /* if the device has a debug flags table */
            for (tptr = dptr->debflags;                 /*   then scan the table */
                 tptr->name != NULL; tptr++) {          /*     to check the length */
                flag_length = strlen (tptr->name);      /*       of each flag name */

                if (flag_length > flag_size)            /* if it's greater than the current maximum */
                    flag_size = flag_length;            /*   then reset the size */
                }
        }
    }

conflict_is = None;                                     /* assume that no conflicts exist */

for (conf = Device; conf <= Service; conf++) {          /* check for conflicts for each type */
    memset (conflicts, 0, sizeof conflicts);            /* zero the conflict table for each check */

    for (dev = 0; dev < DEVICE_COUNT; dev++)            /* populate the conflict table from the DIB value table */
        if (dib_val [dev] [conf] >= 0)                  /* if this device has an assigned value */
            if (++conflicts [dib_val [dev] [conf]] > 1) /*   then increment the count of references */
                conflict_is = conf;                     /* if there is more than one reference, a conflict occurs */

    if (conflict_is == conf) {                          /* if a conflict exists for this type */
        sim_ttcmd ();                                   /*   then restore the console and log I/O mode */

        for (val = 0; val <= max_number [conf]; val++)  /* search the conflict table for the next conflict */
            if (conflicts [val] > 1) {                  /* if a conflict is present for this value */
                count = conflicts [val];                /*   then get the number of conflicting devices */

                cprintf ("%s %d conflict (", conflict_label [conf], val);

                dev = 0;                                        /* search for the devices that conflict */

                while (count > 0) {                             /* search the DIB value table */
                    if (dib_val [dev] [conf] == (int32) val) {  /*   to find the conflicting entries */
                        if (count < conflicts [val])            /*     and report them to the console */
                            cputs (" and ");

                        cputs (sim_dname ((DEVICE *) sim_devices [dev]));
                        count = count - 1;
                        }

                    dev = dev + 1;
                    }

                cputs (")\n");
                }
        }
    }

return (conflict_is != None);                           /* return TRUE if any conflicts exist */
}


/* Print a CPU instruction in symbolic format.

   This routine is called to format and print an instruction in mnemonic form.
   The "ofile" parameter is the opened output stream, "val [*]" contains the
   word(s) comprising the machine instruction to print, "radix" contains the
   desired operand radix or zero if the default radix is to be used, and
   "switches" includes the SIM_SW_STOP switch if the routine was called as part
   of a simulation stop.

   The routine returns a status code to the caller.  SCPE_OK status is returned
   if the print consumed a single-word value, or the negative number of extra
   words (beyond the first) consumed by printing the instruction is returned.
   For example, printing a symbol that resulted in two words being consumed
   (from val [0] and val [1]) would return SCPE_OK_2_WORDS (= -1).

   HP 3000 machine instructions are generally classified by the first four bits.
   Within each class, additional bits identify sub-classes or individual
   instructions.

   Most of the decoding work is handled by the "fprint_instruction" routine,
   which prints mnemonics and operands and returns a status code indicating the
   number of words consumed for the current instruction.


   Implementation notes:

    1. For a stack instruction, if the R (right stack-op pending) bit in the
       status word is set, and the request is for a simulation stop, the
       left-hand opcode will print as dashes to indicate that it has already
       been executed.
*/

t_stat fprint_cpu (FILE *ofile, t_value *val, uint32 radix, int32 switches)
{
const char *dashes = "----,";
t_stat status = SCPE_OK;

switch (SUBOP (val [0])) {                                  /* dispatch based on the instruction sub-opcode */

    case 000:                                               /* stack operations */
        if (STA & STATUS_R && switches & SIM_SW_STOP)       /* if right stack-op pending and this is a simulation stop */
            fputs (dashes + 4                               /*   then indicate that the left stack-op has completed */
               - strlen (stack_ops [STACKOP_A (val [0])].mnemonic), ofile);

        else {                                              /* otherwise */
            status = fprint_instruction (ofile, stack_ops,  /*   print the left operation */
                                         val, STACKOP_A_MASK,
                                         STACKOP_A_SHIFT, radix);
            fputc (',', ofile);                             /* add a separator */
            }

        status = fprint_instruction (ofile, stack_ops,      /* print the right operation */
                                     val, STACKOP_B_MASK,
                                     STACKOP_B_SHIFT, radix);
        break;

    case 001:                                               /* shift/branch/bit operations */
        status = fprint_instruction (ofile, sbb_ops,        /* print the operation */
                                     val, SBBOP_MASK,
                                     SBBOP_SHIFT, radix);
        break;

    case 002:                                               /* move/special/firmware/immediate/field/register operations */
        status = fprint_instruction (ofile, msfifr_ops,     /* print the operation */
                                     val, MSFIFROP_MASK,
                                     MSFIFROP_SHIFT, radix);
        break;

    case 003:                                               /* I/O/control/program/immediate/memory operations */
        if (val [0] & IOCPIMOP_MASK)                        /* if it is a program, immediate, or memory instruction */
            status = fprint_instruction (ofile, pmi_ops,    /*   then print the operation */
                                         val, IOCPIMOP_MASK,
                                         IOCPIMOP_SHIFT, radix);

        else                                                /* otherwise it is an I/O or control operation */
            status = fprint_instruction (ofile, ioc_ops,    /*   so print the operation */
                                         val, IOCSUBOP_MASK,
                                         IOCSUBOP_SHIFT, radix);
        break;


    default:                                                /* memory, loop, and branch operations */
        status = fprint_instruction (ofile, mlb_ops,        /* print the operation */
                                     val, MLBOP_MASK,
                                     MLBOP_SHIFT, radix);
        break;
    }

return status;                                              /* return the consumption status */
}


/* Format the status register flags and condition code.

   This routine formats the flags and condition code part of the status register
   and returns a pointer to the formatted string.  It does not format the
   current code segment number part of the register.

   The six status flags are represented by letters.  If the flag is set, an
   uppercase letter is used; if it is clear, a lowercase letter is used.  The
   condition code is represented by the strings "CCL", "CCE", or "CCG" for the
   less than, equal to, or greater than conditions.  If the condition code is
   the invalid value, "CC?" is used.
*/

const char *fmt_status (uint32 status)
{
static const char conditions [] = "GLE?";
static const char flags [] = "m i t r o c CCx";
static char formatted [sizeof flags];
uint32 index;

strcpy (formatted, flags);                              /* copy the initial flags template */

formatted [14] = conditions [TO_CCN (status)];          /* set the condition code representation */

for (index = 0; index < 6 * 2; index = index + 2) {     /* loop through the six MSBs (the flags) */
    if (status & D16_SIGN)                              /* if the bit is set */
        formatted [index] =                             /*   then convert the corresponding flag */
           (char) toupper (formatted [index]);          /*     to upper case */

    status = status << 1;                               /* position the next flag for testing */
    }

return formatted;                                       /* return a pointer to the formatted string */
}


/* Format a character for printing.

   This routine formats single 8-bit character value into a printable string and
   returns a pointer to that string.  Printable characters retain their original
   form but are enclosed in single quotes.  Control characters are translated to
   readable strings.  Characters outside of the ASCII range are presented as
   escaped octal values.


   Implementation notes:

    1. The longest string to be returned is a five-character escaped octal
       string, consisting of a backslash, three digits, and a trailing NUL.
*/

const char *fmt_char (uint32 charval)
{
static const char *const control [] = {
    "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL",
    "BS",  "HT",  "LF",  "VT",  "FF",  "CR",  "SO",  "SI",
    "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB",
    "CAN", "EM",  "SUB", "ESC", "FS",  "GS",  "RS",  "US"
    };
static char printable [5];

if (charval <= '\037')                                  /* if the value is an ASCII control character */
    return control [charval];                           /*   then return a readable representation */

else if (charval == '\177')                             /* otherwise if the value is the delete character */
    return "DEL";                                       /*   then return a readable representation */

else if (charval > '\177') {                            /* otherwise if the value is beyond the printable range */
    sprintf (printable, "\\%03o", charval & D8_MASK);   /*   then format the value */
    return printable;                                   /*     as an escaped octal code */
    }

else {                                                  /* otherwise it's a printable character */
    printable [0] = '\'';                               /*   so form a representation */
    printable [1] = (char) charval;                     /*     containing the character */
    printable [2] = '\'';                               /*       surrounded by single quotes */
    printable [3] = '\0';
    return printable;
    }
}


/* Format a set of named bits.

   This routine formats a set of up to 32 named bits into a printable string and
   returns a pointer to that string.  The names of the active bits are
   concatenated and separated by vertical bars.  For example:

     SIO OK | ready | no error | unit 0

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
   the word are not necessary.  For example, if bits 1-3 of a word are valid,
   then the name string array would have three entries.  If bits 1-3 and 5 are
   valid, then the array would have five entries, with the fourth entry set to
   NULL.

   The offset field specifies the number of unnamed bits to the right of the
   least-significant named bit.  Using the same examples as above, the offsets
   would be 12 and 10, respectively.

   The direction field specifies whether the bits are named from MSB to LSB
   (msb_first) or vice versa (lsb_first).  The order of the entries in the name
   string array must match the direction specified.  Continuing with the first
   example above, if the direction is msb_first, then the first name is for bit
   1; if the direction is lsb_first, then the first name is for bit 3.

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
*/

const char *fmt_bitset (uint32 bitset, const BITSET_FORMAT bitfmt)
{
static char formatted_set [1024];                       /* the return buffer */

const char *bnptr;
uint32 test_bit, index, bitmask;
char   *fsptr = formatted_set;

*fsptr = '\0';                                          /* initialize the format accumulator */
index = 0;                                              /*   and the name index */

if (bitfmt.name_count < D32_WIDTH)                      /* if the bit count is the less than the mask variable width */
    bitmask = (1 << bitfmt.name_count) - 1;             /*   then create a mask for the bit count specified */
else                                                    /* otherwise use a predefined value for the mask */
    bitmask = D32_MASK;                                 /*   to prevent shifting the bit off the MSB end */

bitmask = bitmask << bitfmt.offset;                     /* align the mask to the named bits */
bitset = bitset & bitmask;                              /*   and mask to just the significant bits */

if (bitfmt.direction == msb_first)                          /* if the examination is left-to-right */
    test_bit = 1 << bitfmt.name_count + bitfmt.offset - 1;  /*   then create a test bit for the MSB */
else                                                        /* otherwise */
    test_bit = 1 << bitfmt.offset;                          /*   create a test bit for the LSB */


while ((bitfmt.alternate || bitset) && index < bitfmt.name_count) { /* while more bits and more names exist */
    bnptr = bitfmt.names [index];                                   /*   point at the name for the current bit */

    if (bnptr)                                          /* if the name is defined */
        if (*bnptr == '\1')                             /*   then if this name has an alternate */
            if (bitset & test_bit)                      /*     then if the bit is asserted */
                bnptr++;                                /*       then point at the name for the "1" state */
            else                                        /*     otherwise */
                bnptr = bnptr + strlen (bnptr) + 1;     /*       point at the name for the "0" state */

        else                                            /*   otherwise the name is unilateral */
            if ((bitset & test_bit) == 0)               /*     so if the bit is denied */
                bnptr = NULL;                           /*       then clear the name pointer */

    if (bnptr) {                                        /* if the name pointer is set */
        if (formatted_set [0] != '\0')                  /*   then if it is not the first one added */
            fsptr = strcat (fsptr, " | ");              /*     then add a separator to the string */

        strcat (fsptr, bnptr);                          /* append the bit's mnemonic to the accumulator */
        }

    if (bitfmt.direction == msb_first)                  /* if formatting is left-to-right */
        bitset = bitset << 1 & bitmask;                 /*   then shift the next bit to the MSB and remask */
    else                                                /* otherwise formatting is right-to-left */
        bitset = bitset >> 1 & bitmask;                 /*   so shift the next bit to the LSB and remask */

    index = index + 1;                                  /* bump the bit name index */
    }


if (formatted_set [0] == '\0')                          /* if the set is empty */
    if (bitfmt.bar == append_bar)                       /*   then if concatenating with more information */
        return "";                                      /*     then return an empty string */
    else                                                /*   otherwise it's a standalone format */
        return "(none)";                                /*     so return a placeholder */

else if (bitfmt.bar == append_bar)                      /* otherwise if a trailing separator is specified */
    fsptr = strcat (fsptr, " | ");                      /*   then add it to the string */

return formatted_set;                                   /* return a pointer to the accumulator */
}


/* Format and print a debugging trace line to the debug log.

   A formatted line is assembled and sent to the previously opened debug output
   stream.  On entry, "dptr" points to the device issuing the trace, "flag" is
   the debug flag that has enabled the trace, and the remaining parameters
   consist of the format string and associated values.

   This routine is usually not called directly but rather via the "dprintf"
   macro, which tests that debugging is enabled for the specified flag before
   calling this function.  This eliminates the calling overhead if debugging is
   disabled.

   This routine prints a prefix before the supplied format string consisting of
   the device name (in upper case) and the debug flag name (in lower case),
   e.g.:

     >>MPX state: Channel SR 3 entered State A
     ~~~~~~~~~~~~ ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        prefix       supplied format string

   The names are padded to the lengths of the largest device name and debug flag
   name among the devices enabled for debugging to ensure that all trace lines
   will align for easier reading.


   Implementation notes:

    1. ISO C99 allows assignment expressions as the bounds for array
       declarators.  VC++ 2008 requires constant expressions.  To accommodate
       the latter, we must allocate "sufficiently large" arrays for the flag
       name and format, rather than arrays of the exact size required by the
       call parameters.
*/

#define FLAG_SIZE           50                          /* sufficiently large to accommodate all flag names */
#define FORMAT_SIZE         1000                        /* sufficiently large to accommodate all format strings */

void hp_debug (DEVICE *dptr, uint32 flag, ...)
{
va_list argptr;
DEBTAB  *debptr;
char    *format, *fptr;
const char *nptr;
char    flag_name [FLAG_SIZE];                          /* desired size is [flag_size + 1] */
char    header_fmt [FORMAT_SIZE];                       /* desired size is [device_size + flag_size + format_size + 6] */

if (sim_deb != NULL && dptr != NULL) {                  /* if the output stream and device pointer are valid */
    debptr = dptr->debflags;                            /*   then get a pointer to the debug flags table */

    if (debptr != NULL)                                 /* if the debug table exists */
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
                debptr++;                               /*   look at the next debug table entry */
    }

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

sim_brk_types = BP_SUPPORTED;                           /* register the supported breakpoint types */
sim_brk_dflt = BP_EXEC;                                 /* the default breakpoint type is "execution" */

return;
}


/* Format and print a VM simulation stop message.

   When the instruction loop is exited, a simulation stop message is printed and
   control returns to SCP.  An SCP stop prints a message with this format:

     <reason>, P: <addr> (<inst>)

   For example:

     SCPE_STOP prints "Simulation stopped, P: 24713 (LOAD 1)"
     SCPE_STEP prints "Step expired, P: 24713 (LOAD 1)"

   For VM stops, this routine is called after the message has been printed and
   before the comma and program counter label and value are printed.  Depending
   on the reason for the stop, the routine may insert additional information,
   and it may request omission of the PC value by returning FALSE instead of
   TRUE.

   This routine modifies the default output for these stop codes:

     STOP_SYSHALT prints "System halt 3, P: 24713 (LOAD 1)"
     STOP_HALT    prints "Programmed halt, CIR: 030365 (HALT 5), P: 24713 (LOAD 1)"
     STOP_CDUMP   prints "Cold dump complete, CIR: 000020"


   Implementation notes:

    1. HALT instructions are always one word in length, so only sim_eval [0]
       needs to be set up before calling fprint_cpu.

    2. The system halt reason is present in RA.
*/

static t_bool fprint_stopped (FILE *st, t_stat reason)
{
if (reason == STOP_HALT) {                              /* if this is a halt instruction stop */
    sim_eval [0] = CIR;                                 /*   then save the instruction for evaluation */

    fputs (", CIR: ", st);                              /* print the register label */
    fprint_val (st, CIR, cpu_dev.dradix,                /*   and the numeric value */
                cpu_dev.dwidth, PV_RZRO);

    fputs (" (", st);                                   /* print the halt mnemonic */
    fprint_cpu (st, sim_eval, 0, SIM_SW_STOP);          /*   (which cannot fail) */
    fputc (')', st);                                    /*     within parentheses */

    return TRUE;                                        /* return TRUE to append the program counter */
    }

else if (reason == STOP_CDUMP) {                        /* otherwise if this is a cold dump completion stop */
    fputs (", CIR: ", st);                              /*   then print the register label */
    fprint_val (st, CIR, cpu_dev.dradix,                /*     and the numeric value */
                cpu_dev.dwidth, PV_RZRO);

    return FALSE;                                       /* return FALSE to omit the program counter */
    }

else if (reason == STOP_SYSHALT) {                      /* otherwise if this is a system halt stop */
    fprintf (st, " %d", RA);                            /*   then print the halt reason */
    return TRUE;                                        /*     and return TRUE to append the program counter */
    }

else                                                    /* otherwise all other stops */
    return TRUE;                                        /*   return TRUE to append the program counter */
}


/* Format and print a memory address.

   This routine is called by SCP to print memory addresses.  It is also called
   to print the contents of registers tagged with the REG_VMAD flag.

   On entry, the "st" parameter is the opened output stream, "dptr" points to
   the device to which the address refers, and "addr" contains the address to
   print.  The routine prints the linear address in <bank>.<offset> form for the
   CPU and as a scalar value for all other devices.
*/

static void fprint_addr (FILE *st, DEVICE *dptr, t_addr addr)
{
uint32 bank, offset;

if (dptr == &cpu_dev) {                                 /* if the address originates in the CPU */
    bank = TO_BANK (addr);                              /*   then separate bank and offset */
    offset = TO_OFFSET (addr);                          /*     from the linear address */

    fprint_val (st, bank, dptr->aradix, BA_WIDTH, PV_RZRO);     /* print the bank address */
    fputc ('.', st);                                            /*   followed by a period */
    fprint_val (st, offset, dptr->aradix, LA_WIDTH, PV_RZRO);   /*     and concluding with the offset */
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

   The HP 3000 divides memory into 64K-word banks.  Each bank is identified by a
   bank address from 0-15.  The current bank addresses for the program, data,
   and stack segments are kept in the PBANK, DBANK, and SBANK registers.

   The simulator supports only linear addresses for all devices other than the
   CPU.  For the CPU, two forms of address entries are allowed:

     - an absolute address consisting of a 4-bit bank address and a 16-bit
       offset within the bank, separated by a period (e.g., 17.177777)

     - a relative address consisting of a 16-bit offset within a bank specified
       by a bank register (e.g., 177777).

   Command line switches modify the interpretation of relative addresses as
   follows:

     * -P specifies an implied bank address obtained from PBANK
     * -S specifies an implied bank address obtained from SBANK
     * no switch specifies an implied bank address obtained from DBANK

   The "parse_config" global specifies the allowed parse configurations.  For
   example, the memory examine/deposit commands allow both absolute addresses
   and offsets from any of the three bank registers, whereas the run command
   only allows an implied offset from PBANK.
*/

static t_addr parse_addr (DEVICE *dptr, CONST char *cptr, CONST char **tptr)
{
CONST char *sptr;
uint32     overrides;
t_addr     bank;
t_addr     address = 0;

if (dptr != &cpu_dev)                                   /* if this is not a CPU memory address */
    return (t_addr) strtotv (cptr, tptr, dptr->aradix); /*   then parse a scalar and return the value */

overrides = sim_switches & (SWMASK ('P') | SWMASK ('S'));       /* mask to just the bank address overrides */

if (overrides && !(parse_config & apcBank_Override)             /* if overrides are present but not allowed */
  || overrides & ~SWMASK ('P') && overrides & ~SWMASK ('S'))    /*   or multiple overrides are specified */
    *tptr = cptr;                                               /*     then report a parse error */

else                                                            /* otherwise the switches are consistent */
    address = strtotv (cptr, tptr, dptr->aradix);               /*   so parse the address */

if (cptr != *tptr)                                      /* if the parse succeeded */
    if (**tptr == '.')                                  /*   then if this a banked address */
        if (! (parse_config & apcBank_Offset))          /*     but it is not allowed */
            *tptr = cptr;                               /*       then report a parse error */

        else {                                              /* otherwise the <bank>.<offset> form is allowed */
            sptr = *tptr + 1;                               /* point to the offset */
            bank = address;                                 /* save the first part as the bank address */
            address = strtotv (sptr, tptr, dptr->aradix);   /* parse the offset */

            address = TO_PA (bank, address);                /* form the linear address */
            }

    else if (address > LA_MAX)                          /* otherwise if the non-banked offset is too large */
        *tptr = cptr;                                   /*   then report a parse error */

    else if (overrides & SWMASK ('S'))                  /* otherwise if the stack-bank override is specified */
        address = TO_PA (SBANK, address);               /*   then base the address on SBANK */

    else if (overrides & SWMASK ('P'))                  /* otherwise if the program-bank override is specified */
        address = TO_PA (PBANK, address);               /*   then base the address on PBANK */

    else if (parse_config & apcDefault_PBANK)           /* otherwise if PBANK is the default */
        if (PB <= address && address <= PL)             /*   then if the address lies within the segment limits */
            address = TO_PA (PBANK, address);           /*     then base the address on PBANK */
        else                                            /*   otherwise it is outside of the segment */
            *tptr = cptr;                               /*     so report a parse error */

    else if (parse_config & apcDefault_DBANK)           /* otherwise if the default is DBANK */
        address = TO_PA (DBANK, address);               /*   then base the address on DBANK */

return address;                                         /* return the linear address */
}


/* Execute the LOAD and DUMP commands.

   This routine implements the cold load and cold dump commands.  The syntax is:

     LOAD { <control/devno> }
     DUMP { <control/devno> }

   The <control/devno> is a number that is deposited into the SWCH register
   before invoking the CPU cold load or cold dump facility.  The CPU radix is
   used to interpret the number; it defaults to octal.  If the number is
   omitted, the SWCH register value is not altered before loading or dumping.

   On entry, the "arg" parameter is "Cold_Load" for a LOAD command and
   "Cold_Dump" for a DUMP command, and "buf" points at the remainder of the
   command line.  If characters exist on the command line, they are parsed,
   converted to a numeric value, and stored in the SWCH register.  Then the
   CPU's cold load/dump routine is called to set up the CPU state.  Finally, the
   CPU is started to begin the requested action.


   Implementation notes:

    1. The run command invocation prepares the simulator for execution, which
       includes a CPU and I/O reset.  However, the cpu_reset routine does not
       reset the CPU state if the cold dump switch is set.  This allows the
       value of the CPX2 register to be saved as part of the dump.
*/

static t_stat hp_cold_cmd (int32 arg, CONST char *buf)
{
const char *cptr;
char       gbuf [CBUFSIZE];
t_stat     status;
t_value    value;

if (*buf != '\0') {                                     /* if more characters exist on the command line */
    cptr = get_glyph (buf, gbuf, 0);                    /*   then get the next glyph */

    if (*cptr != '\0')                                  /* if that does not exhaust the input */
        return SCPE_2MARG;                              /*   then report that there are too many arguments */

    value = get_uint (gbuf, cpu_dev.dradix,             /* get the parameter value */
                      D16_UMAX, &status);

    if (status == SCPE_OK)                              /* if a valid number was present */
        SWCH = value;                                   /*   then set it into the switch register */
    else                                                /* otherwise */
        return status;                                  /*   return the error status */
    }

cpu_front_panel (SWCH, arg);                            /* set up the cold load or dump microcode */

return run_cmd (RU_RUN, "");                            /* reset and execute the halt-mode routine */
}


/* Execute the EXAMINE, DEPOSIT, IEXAMINE, and IDEPOSIT commands.

   These commands are intercepted to configure address parsing.  The following
   address forms are valid:

     EXAMINE <bank>.<offset>
     EXAMINE <dbank-offset>
     EXAMINE -P <pbank-offset>
     EXAMINE -S <sbank-offset>

   This routine configures the address parser and calls the standard command
   handler.
*/

static t_stat hp_exdep_cmd (int32 arg, CONST char *buf)
{
parse_config = apcBank_Offset |                         /* allow the <bank>.<offset> address form */
               apcBank_Override |                       /* allow bank override switches */
               apcDefault_DBANK;                        /* set the default bank register to DBANK */

return exdep_cmd (arg, buf);                            /* return the result of the standard handler */
}


/* Execute the RUN and GO commands.

   These commands are intercepted to configure address parsing.  The following
   address form is valid:

     RUN { <pbank-offset> }
     GO  { <pbank-offset> }

   This routine configures the address parser and calls the standard command
   handler.  The <pbank-offset>, if specified, must lie between PB and PL, or
   the command will be rejected when the offset is parsed.


   Implementation notes:

    1. The RUN command uses the RU_GO argument instead of RU_RUN so that the
       run_cmd SCP routine will not reset all devices before entering the
       instruction executor.  As is done in hardware, resetting the CPU clears
       the ICS flag, which corrupts the CPU state set up after a cold load.  A
       CPU reset is only valid prior to a cold load -- never when a program is
       resident in memory.
*/

static t_stat hp_run_cmd (int32 arg, CONST char *buf)
{
parse_config = apcDefault_PBANK;                        /* set the default bank register to PBANK */

cpu_front_panel (SWCH, Run);                            /* set up run request */

return run_cmd (RU_GO, buf);                            /* return the result of the standard handler */
}


/* Execute the BREAK and NOBREAK commands.

   These commands are intercepted to configure address parsing.  The following
   address forms are valid:

     BREAK
     BREAK <bank>.<offset>
     BREAK <pbank-offset>

   If no argument is specified, the breakpoint address defaults to the current
   values of PBANK and P.  The standard command handler will accommodate this,
   but only if the program counter contains a physical address.  Therefore, for
   the duration of the call, the SCP pointer to the P register structure is
   changed to point at a temporary register structure that contains the physical
   address.

   The <pbank-offset>, if specified, must lie between PB and PL, or the command
   will be rejected by the parse_addr routine when it is called by the brk_cmd
   routine to parse the offset.
*/

static t_stat hp_brk_cmd (int32 arg, CONST char *buf)
{
static uint32 PC;
static REG PR = { ORDATA (PP, PC, 32) };
REG    *save_PC;
t_stat status;

save_PC = sim_PC;                                       /* temporarily change the P-register pointer */
sim_PC  = & PR;                                         /*   to point at a structure holding the physical address */

PC = TO_PA (PBANK, P);                                  /* set the physical address from the program counter */

parse_config = apcBank_Offset | apcDefault_PBANK;       /* allow the <bank>.<offset> form with a PBANK default */

status = brk_cmd (arg, buf);                            /* call the standard breakpoint command handler */

sim_PC = save_PC;                                       /* restore the P-register pointer */

return status;                                          /* return the handler status */
}



/* System interface local utility routines */


/* Print a numeric value with a radix identifier.

   This routine prints a numeric value with a leading radix indicator if the
   specified print radix is not the same as the current CPU data radix.  It uses
   the HP 3000 convention of a leading "%", "#", or "!" character to indicate
   an octal, decimal, or hexadecimal number.

   On entry, the "ofile" parameter is the opened output stream, "val" is the
   value to print, "radix" is the desired print radix, "width" is the number of
   significant bits in the value, and "format" is a format specifier (PV_RZRO,
   PV_RSPC, or PV_LEFT).  On exit, the status of the print operation is
   returned.
*/

static void fprint_value (FILE *ofile, t_value val, uint32 radix, uint32 width, uint32 format)
{
if (radix != cpu_dev.dradix)                            /* if the requested radix is not the current data radix */
    if (radix == 8)                                     /*   then if the requested radix is octal */
        fputc ('%', ofile);                             /*     then print the octal indicator */

    else if (radix == 10)                               /*   otherwise if it is decimal */
        fputc ('#', ofile);                             /*     then print the decimal indicator */

    else if (radix == 16)                               /*   otherwise if it is hexadecimal */
        fputc ('!', ofile);                             /*     then print the hexadecimal indicator */

    else                                                /*   otherwise it must be some other radix */
        fputc ('?', ofile);                             /*     with no defined indicator */

fprint_val (ofile, val, radix, width, format);          /* print the value in the radix specified */

return;
}


/* Print an I/O program instruction in symbolic format.

   This routine prints a pair of data words as an I/O channel order and the
   associated operand(s) on the output stream supplied.

   On entry, the "ofile" parameter is the opened output stream, "val [0]"
   contains the I/O Control Word, "val [1]" contains the I/O Address Word, and
   "radix" contains the desired operand radix or zero if the default radix is to
   be used.  The control and address words are decoded as follows:

       IOCW        IOCW            IOAW
     0 1 2 3       4-15            0-15              Action
     -------  --------------  --------------  ---------------------
     0 0 0 0  0 XXXXXXXXXXX   Jump Address    Unconditional Jump
     0 0 0 0  1 XXXXXXXXXXX   Jump Address    Conditional Jump
     0 0 0 1  0 XXXXXXXXXXX   Residue Count   Return Residue
     0 0 0 1  1 XXXXXXXXXXX   Bank Address    Set Bank
     0 0 1 0  X XXXXXXXXXXX   (don't care)    Interrupt
     0 0 1 1  0 XXXXXXXXXXX   Status Value    End
     0 0 1 1  1 XXXXXXXXXXX   Status Value    End with Interrupt
     0 1 0 0  Control Word 1  Control Word 2  Control
     0 1 0 1  X XXXXXXXXXXX   Status Value    Sense
     C 1 1 0  Neg Word Count  Write Address   Write
     C 1 1 1  Neg Word Count  Read Address    Read

   Operand values are printed in a radix suitable to the type of the value, as
   follows:

     - Address values are printed in the CPU's address radix, which is octal.

     - Counts are printed in decimal.

     - Control and status values are printed in the CPU's data radix, which
       defaults to octal but may be set to a different radix with SET CPU
       OCT|DEC|HEX.

   The radix for operand values other than addresses may be overridden by a
   switch on the command line.  A value printed in a radix other than the
   current data radix is preceded by a radix identifier ("%" for octal, "#" for
   decimal, or "!" for hexadecimal).

   The routine returns SCPE_OK_2_WORDS to indicate that two words were consumed.


   Implementation notes:

    1. The Return Residue value is printed as a positive number, even though
       the value in memory is either negative or zero.
*/

static const char *const order_names [] = {             /* indexed by SIO_ORDER */
    "JUMP   ",                                          /*   sioJUMP   -- Jump unconditionally */
    "JUMPC  ",                                          /*   sioJUMPC  -- Jump conditionally */
    "RTNRES ",                                          /*   sioRTRES  -- Return residue */
    "SETBNK ",                                          /*   sioSBANK  -- Set bank */
    "INTRPT",                                           /*   sioINTRP  -- Interrupt */
    "END    ",                                          /*   sioEND    -- End */
    "ENDINT ",                                          /*   sioENDIN  -- End with interrupt */
    "CONTRL ",                                          /*   sioCNTL   -- Control */
    "SENSE  ",                                          /*   sioSENSE  -- Sense */
    "WRITE  ",                                          /*   sioWRITE  -- Write */
    "WRITEC ",                                          /*   sioWRITEC -- Write (chained) */
    "READ   ",                                          /*   sioREAD   -- Read */
    "READC  "                                           /*   sioREADC  -- Read (chained) */
    };

static t_stat fprint_order (FILE *ofile, t_value *val, uint32 radix)
{
t_value   iocw, ioaw;
SIO_ORDER order;

iocw = val [0];                                         /* get the I/O control word */
ioaw = val [1];                                         /*   and I/O address word */

order = IOCW_ORDER (iocw);                              /* get the SIO I/O order from the IOCW */

fputs (order_names [order], ofile);                     /* print the I/O order mnemonic */

switch (order) {                                        /* dispatch operand printing based on the order */

    case sioJUMP:
    case sioJUMPC:                                      /* print the jump target address */
        fprint_value (ofile, ioaw, cpu_dev.aradix,
                      LA_WIDTH, PV_RZRO);
        break;

    case sioRTRES:                                      /* print the residue count */
        fprint_value (ofile, - SEXT (ioaw),
                      (radix ? radix : 10),
                      DV_WIDTH, PV_LEFT);
        break;

    case sioSBANK:                                      /* print the bank address */
        fprint_value (ofile, ioaw & BA_MASK,
                      cpu_dev.aradix, BA_WIDTH, PV_RZRO);
        break;

    case sioINTRP:                                      /* no operand to print */
        break;

    case sioEND:
    case sioENDIN:
    case sioSENSE:                                      /* print the status value */
        fprint_value (ofile, ioaw,
                      (radix ? radix : cpu_dev.dradix),
                      DV_WIDTH, PV_RZRO);
        break;

    case sioCNTL:                                       /* print control words 1 and 2 */
        fprint_value (ofile, IOCW_CNTL (iocw),
                      (radix ? radix : cpu_dev.dradix),
                      DV_WIDTH, PV_RZRO);

        fputc (',', ofile);

        fprint_value (ofile, ioaw,
                      (radix ? radix : cpu_dev.dradix),
                      DV_WIDTH, PV_RZRO);
        break;

    case sioWRITE:
    case sioWRITEC:
    case sioREAD:
    case sioREADC:                                      /* print the count and address */
        fprint_value (ofile, - IOCW_COUNT (iocw),
                      (radix ? radix : 10), DV_WIDTH, PV_LEFT);

        fputc (',', ofile);

        fprint_value (ofile, ioaw, cpu_dev.aradix,
                      LA_WIDTH, PV_RZRO);
        break;
    }

return SCPE_OK_2_WORDS;                                 /* indicate that each instruction uses one extra word */
}


/* Print a CPU instruction opcode and operand in symbolic format.

   This routine prints a CPU instruction and its operand, if any, using the
   mnemonics specified in the Machine Instruction Set and Systems Programming
   Language Reference manuals.  Specified bits in the instruction word are used
   as an index into a supplied classification table.  The entry corresponding to
   the instruction gives the mnemonic string, operand type, and reserved bits
   (if any).

   On entry, the "ofile" parameter is the opened output stream, "ops" is the
   table of classifications containing the instruction, "instruction" is the
   machine instruction to print, "mask" is the opcode mask to apply to get the
   index bits, "shift" is the right-shift count to align the index, and "radix"
   contains the desired operand radix or zero if the default radix is to be
   used.

   On exit, a status code is returned to the caller.  SCPE_OK status is returned
   if the print consumed a single-word value, or the negative number of extra
   words (beyond the first) consumed by printing the instruction is returned.
   For example, printing a symbol that resulted in two words being consumed
   (from val [0] and val [1]) would return SCPE_OK_2_WORDS (= -1).

   The classification table consists of a set of entries that are indexed by
   opcode, followed optionally by a set of entries that are searched linearly.
   Empty mnemonics, i.e., "", are used in the indexed part to indicate that the
   linear part must be searched.  A NULL mnemonic ends the array (this allows
   string searches for parsing to fail without aborting).

   The supplied instruction is ANDed with the "mask" parameter and then
   right-shifted by the "shift" parameter to produce an index into the "ops"
   table.  If the entry contains a non-empty mnemonic string, it is printed.
   Otherwise, starting at the index implied by the size of the mask, i.e., at
   mask + 1, a linear search of the entries is performed.  For each entry, the
   instruction is masked to remove the operand and optionally the reserved bits,
   and the result is compared to the base opcode.  If it matches, the associated
   mnemonic is printed.  If the table is exhausted without a match, the
   instruction is undefined, and it is printed in octal, regardless of the data
   radix.

   For defined instructions, the operand, if any, is printed after the mnemonic.
   Operand values are printed in a radix suitable to the type of the value, as
   follows:

     - Register-relative displacements, S-register decrements, and K fields are
       printed in the CPU's address radix, which is octal.

     - Shift counts, bit positions, and starting bits and counts are printed in
       decimal.

     - CIR values for the PAUS and HALT instructions are printed in octal.

     - Immediate values are printed in the CPU's data radix, which defaults to
       octal but may be set to a different radix with SET CPU OCT|DEC|HEX.

   The radix for operand values other than addresses may be overridden by a
   switch on the command line.  A value printed in a radix other than the
   current data radix is preceded by a radix identifier ("%" for octal, "#" for
   decimal, or "!" for hexadecimal).


   Implementation notes:

    1. All instructions in the base set are single words.  However, some
       extension instructions, including instructions for later-series CPUs,
       e.g., Series 33, use two or more words.  For example, the WIOC (write I/O
       channel) instruction is the two-word sequence 020302 000003, and the SIOP
       (start I/O program) sequence is 020302 000000.  Currently, this routine
       is not set up to handle this.

    2. The operand type dispatch handlers either set up operand printing by
       assigning the prefix, indirect, and index values, or print the operand(s)
       directly if special formatting is required.

    3. Register flags for the PSHR and SETR instructions are printed using the
       SPL register names.
*/

static const char *const register_name [] = {   /* PSHR/SETR register names corresponding to bits 8-15 */
    "SBANK",                                    /*   bit  8 */
    "DB",                                       /*   bit  9 */
    "DL",                                       /*   bit 10 */
    "Z",                                        /*   bit 11 */
    "STATUS",                                   /*   bit 12 */
    "X",                                        /*   bit 13 */
    "Q",                                        /*   bit 14 */
    "S"                                         /*   bit 15 */
    };

static t_stat fprint_instruction (FILE *ofile, const OP_TABLE ops, t_value *instruction,
                                  t_value mask, uint32 shift, uint32 radix)
{
uint32  op_index, op_radix;
int32   reg_index;
t_bool  reg_first;
t_value op_value;
char    *prefix  = NULL;                                /* base register label to print before the operand */
t_bool  index    = FALSE;                               /* TRUE if the instruction is indexed */
t_bool  indirect = FALSE;                               /* TRUE if the instruction is indirect */

op_index = (instruction [0] & mask) >> shift;           /* extract the opcode index */

if (ops [op_index].mnemonic [0])                        /* if a primary entry is defined */
    fputs (ops [op_index].mnemonic, ofile);             /*   then print the mnemonic */

else {                                                  /* otherwise search through the secondary entries */
    for (op_index = (mask >> shift) + 1;                /* search the table starting after the primary entries */
         ops [op_index].mnemonic != NULL;               /*   until the NULL entry at the end */
         op_index++)
        if (ops [op_index].opcode ==                    /* if the opcode in this table entry */
          (instruction [0] & ops [op_index].rsvd_mask   /*   matches the instruction with the reserved bits */
          & op_mask [ops [op_index].operand])) {        /*     and operand bits masked off */
            fputs (ops [op_index].mnemonic, ofile);     /*       then print it */
            break;                                      /*         and terminate the search */
            }

    if (ops [op_index].mnemonic == NULL)                /* if the opcode was not found */
        return SCPE_ARG;                                /*   then return error status to print it in octal */
    }


op_value =                                              /* mask the instruction to the operand value */
   instruction [0] & ~op_mask [ops [op_index].operand];

op_radix = cpu_dev.aradix;                              /* assume that operand is an address */

switch (ops [op_index].operand) {                       /* dispatch by the operand type */

    /* no operand */

    case opNone:
        break;                                          /* no formatting needed */


    /* unsigned value pair range 0-15 */

    case opU1515:
        fputc (' ', ofile);                                     /* print a separator */

        fprint_value (ofile, START_BIT (op_value),              /* print the starting bit position */
                      (radix ? radix : 10), DV_WIDTH, PV_LEFT);

        fputc (':', ofile);                                     /* print a separator */

        fprint_value (ofile, BIT_COUNT (op_value),              /* print the bit count */
                     (radix ? radix : 10), DV_WIDTH, PV_LEFT);
        break;


    /* P +/- displacement range 0-31, indirect bit 4 */

    case opPS31I:
        indirect = (instruction [0] & I_FLAG_BIT_4) != 0;       /* save the indirect condition */
        prefix = (op_value & DISPL_31_SIGN ? " P-" : " P+");    /* set the base register and sign label */
        op_value = op_value & DISPL_31_MASK;                    /*   and remove the sign from the displacement value */
        break;


    /* P +/- displacement range 0-255, indirect bit 5, index bit 4 */

    case opPS255IX:
        index = (instruction [0] & X_FLAG) != 0;            /* save the index condition */
        indirect = (instruction [0] & I_FLAG_BIT_5) != 0;   /*   and the indirect condition */

    /* fall into the P-relative displacement case */

    /* P +/- displacement range 0-255 */

    case opPS255:
        prefix = (op_value & DISPL_255_SIGN ? " P-" : " P+");   /* set the base register and sign label */
        op_value = op_value & DISPL_255_MASK;                   /*   and remove the sign from the displacement value */
        break;


    /* S decrement range 0-3, base register bit 11 */

    case opSU3B:
        prefix = (instruction [0] & DB_FLAG) ? " " : " PB,";    /* set the base register label */
        op_value = op_value & ~op_mask [opSU3];                 /*   and remove the base flag from the S decrement value */
        break;


    /* S decrement range 0-3, N/A/S bits 11-13 */

    case opSU3NAS:
        if (instruction [0] & MVBW_CCF)                 /* if any flags are present */
            fputc (' ', ofile);                         /*   then print a space as a separator */

        if (instruction [0] & MVBW_A_FLAG)              /* if the alphabetic flag is present */
            fputc ('A', ofile);                         /*   then print an "A" as the indicator */

        if (instruction [0] & MVBW_N_FLAG)              /* if the numeric flag is present */
            fputc ('N', ofile);                         /*   then print an "N" as the indicator */

        if (instruction [0] & MVBW_S_FLAG)              /* if the upshift flag is present */
            fputc ('S', ofile);                         /*   then print an "S" as the indicator */

        prefix = ",";                                   /* separate the value from the flags */
        op_value = op_value & ~op_mask [opSU3];         /*   and remove the flags from the S decrement value */
        break;


    /* register selection bits 8-15, execution from left-to-right */

    case opR255L:
        if (op_value != 0) {                                    /* if any registers are to be output */
            fputc (' ', ofile);                                 /*   then print a space as a separator */

            reg_first = TRUE;                                   /* set the first-time-through flag */

            for (reg_index = 0; reg_index <= 7; reg_index++) {  /* loop through the register bits */
                if (op_value & PSR_LR_MASK) {                   /* if the register selection bit is set */
                    if (reg_first)                              /*   then if this is the first time */
                        reg_first = FALSE;                      /*     then clear the flag */
                    else                                        /* otherwise */
                        fputc (',', ofile);                     /*   output a comma separator */

                    fputs (register_name [reg_index], ofile);   /* output the register name */
                    }

                op_value = op_value << 1;                       /* position the next register selection bit */
                }
            }
        break;


    /* register selection bits 8-15, execution from right-to-left */

    case opR255R:
        if (op_value != 0) {                                    /* if any registers are to be output */
            fputc (' ', ofile);                                 /*   then print a space as a separator */

            reg_first = TRUE;                                   /* set the first-time-through flag */

            for (reg_index = 7; reg_index >= 0; reg_index--) {  /* loop through the register bits */
                if (op_value & PSR_RL_MASK) {                   /* if the register selection bit is set */
                    if (reg_first)                              /*   then if this is the first time */
                        reg_first = FALSE;                      /*     then clear the flag */
                    else                                        /* otherwise */
                        fputc (',', ofile);                     /*   output a comma separator */

                    fputs (register_name [reg_index], ofile);   /* output the register name */
                    }

                op_value = op_value >> 1;                       /* position the next register selection bit */
                }
            }
        break;


    /* P+/P-/DB+/Q+/Q-/S- displacements, indirect bit 5, index bit 4 */

    case opPD255IX:
        if ((instruction [0] & DISPL_P_FLAG) == 0) {                /* if this a P-relative displacement */
            prefix = (op_value & DISPL_255_SIGN ? " P-" : " P+");   /*   then set the base register and sign label */
            op_value = op_value & DISPL_255_MASK;                   /*     and remove the sign from the displacement value */

            index = (instruction [0] & X_FLAG) != 0;                /* save the index condition */
            indirect = (instruction [0] & I_FLAG_BIT_5) != 0;       /*   and the indirect condition */
            break;
            }

    /* otherwise the displacement is not P-relative, so fall into the data-relative handler */

    /* DB+/Q+/Q-/S- displacements, indirect bit 5, index bit 4 */

    case opD255IX:
        if ((instruction [0] & DISPL_DB_FLAG) == 0) {       /* if this a DB-relative displacement */
            prefix = " DB+";                                /*   then set the base register label */
            op_value = op_value & DISPL_255_MASK;           /*     and remove the base flag from the displacement value */
            }

        else if ((instruction [0] & DISPL_QPOS_FLAG) == 0) {    /* otherwise if this a positive Q-relative displacement */
            prefix = " Q+";                                     /*   then set the base register label */
            op_value = op_value & DISPL_127_MASK;               /*     and remove the base flag from the displacement value */
            }

        else if ((instruction [0] & DISPL_QNEG_FLAG) == 0) {    /* otherwise if this a negative Q-relative displacement */
            prefix = " Q-";                                     /*   then set the base register label */
            op_value = op_value & DISPL_63_MASK;                /*     and remove the base flag from the displacement value */
            }

        else {                                              /* otherwise it must be a negative S-relative displacement */
            prefix = " S-";                                 /*   so set the base register label */
            op_value = op_value & DISPL_63_MASK;            /*     and remove the base flag from the displacement value */
            }

        indirect = (instruction [0] & I_FLAG_BIT_5) != 0;   /* save the indirect condition */

    /* fall into the index case */

    /* index bit 4 */

    case opX:
        index = (instruction [0] & X_FLAG) != 0;        /* save the index condition */
        break;


    /* unsigned value range 0-63, index bit 4 */

    case opU63X:
        index = (instruction [0] & X_FLAG) != 0;        /* save the index condition */
        op_value = op_value & DISPL_63_MASK;            /*   and mask to the operand value */

    /* fall into the unsigned value case */

    /* unsigned value range 0-63 */

    case opU63:
        op_radix = (radix ? radix : 10);                /* set the print radix */
        prefix = " ";                                   /*   and add a separator */
        break;


    /* sign control bits 9-10, S decrement bit 11 */

    case opSCS:
        if (instruction [0] & NABS_FLAG) {              /* if the negative absolute flag is present */
            fputs (" NABS", ofile);                     /*   then print "NABS" as the indicator */
            prefix = ",";                               /* we will need to separate the flag and value */
            }

        else if (instruction [0] & ABS_FLAG) {          /* otherwise if the absolute flag is present */
            fputs (" ABS", ofile);                      /*   then print "ABS" as the indicator */
            prefix = ",";                               /* we will need to separate the flag and value */
            }

        else                                            /* otherwise neither flag is present */
            prefix = " ";                               /*   so just use a space to separate the value */

        op_value = (op_value & ~op_mask [opS]) >> EIS_SDEC_SHIFT;   /* remove the flags from the S decrement value */
        op_radix = (radix ? radix : cpu_dev.dradix);                /*   and set the print radix */
        break;


    /* S decrement bit 11 */
    /* S decrement range 0-2 bits 10-11 */

    case opS:
    case opSU2:
        op_value = op_value >> EIS_SDEC_SHIFT;          /* align the S decrement value */

    /* fall into the unsigned operand case */

    /* unsigned value range 0-1 */
    /* unsigned value range 0-255 */

    case opU1:
    case opU255:
        op_radix = (radix ? radix : cpu_dev.dradix);    /* set the print radix */
        prefix = " ";                                   /*   and add a separator */
        break;


    /* CIR display bits 12-15 */

    case opC15:
        op_radix = (radix ? radix : 8);                 /* set the print radix */
        prefix = " ";                                   /*   and add a separator */
        break;


    /* P unsigned displacement range 0-255 */
    /* S decrement range 0-3 */
    /* S decrement range 0-7 */
    /* S decrement range 0-15 */

    case opPU255:
    case opSU3:
    case opSU7:
    case opSU15:
        prefix = " ";                                   /* add a separator */
        break;

    }                                                   /* end of the operand type dispatch */


if (prefix) {                                           /* if an operand is present */
    fputs (prefix, ofile);                              /*   then label it */
    fprint_value (ofile, op_value, op_radix,            /*     and then print the value */
                  DV_WIDTH, PV_LEFT);
    }

if (indirect)                                           /* add an indirect indicator */
    fputs (",I", ofile);                                /*   if specified by the instruction */

if (index)                                              /* add an index indicator */
    fputs (",X", ofile);                                /*   if specified by the instruction */

return SCPE_OK;
}


/* Parse a CPU instruction */

static t_stat parse_cpu (CONST char *cptr, t_addr address, UNIT *uptr, t_value *value, int32 switches)
{
return SCPE_ARG;                                        /* mnemonic support is not present in this release */
}
