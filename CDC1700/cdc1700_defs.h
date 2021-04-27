/*

   Copyright (c) 2015-2017, John Forecast

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
   JOHN FORECAST BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of John Forecast shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from John Forecast.

*/

/* cdc1700_defs.h:      CDC1700 simulator definitions
 */

#ifndef _CDC1700_DEFS_H
#define _CDC1700_DEFS_H

#include "sim_defs.h"
#include "sim_tape.h"

#define DBGOUT  (sim_deb != NULL ? sim_deb : stdout)

/*
 * Private status codes
 */
#define SCPE_LOOP       1               /* Indirect addressing loop */
#define SCPE_SSTOP      2               /* Selective stop */
#define SCPE_INVEXI     3               /* Invalid bit in EXI delta */
#define SCPE_IBKPT      4               /* Breakpoint */
#define SCPE_REJECT     5               /* Stop on reject */
#define SCPE_UNIMPL     6               /* Unimplemented instruction */

/*
 * Private device flags
 */
#define DEV_V_REJECT    (DEV_V_UF + 1)  /* Stop on reject enabled */
#define DEV_V_NOEQUIP   (DEV_V_UF + 2)  /* Not an equipment device */
#define DEV_V_INDEV     (DEV_V_UF + 3)  /* Input device for IO_DEVICE */
#define DEV_V_OUTDEV    (DEV_V_UF + 4)  /* Output device for IO_DEVICE */
#define DEV_V_PROTECT   (DEV_V_UF + 5)  /* Device supports protection */
#define DEV_V_PROTECTED (DEV_V_UF + 6)  /* Device protection enabled */
#define DEV_V_REVERSE   (DEV_V_UF + 7)  /* DP reverse addressing */
#define DEV_V_FIXED     (DEV_V_UF + 7)  /* CDD fixed drive first addressing */

#define DEV_REJECT      (1 << DEV_V_REJECT)
#define DEV_NOEQUIP     (1 << DEV_V_NOEQUIP)
#define DEV_INDEV       (1 << DEV_V_INDEV)
#define DEV_OUTDEV      (1 << DEV_V_OUTDEV)
#define DEV_PROTECT     (1 << DEV_V_PROTECT)
#define DEV_PROTECTED   (1 << DEV_V_PROTECTED)
#define DEV_REVERSE     (1 << DEV_V_REVERSE)
#define DEV_FIXED       (1 << DEV_V_FIXED)

/*
 * CPU debug flags
 */
#define DBG_V_DISASS    0               /* Disassemble on execution */
#define DBG_V_IDISASS   1               /* Disassemble during interrupt */
#define DBG_V_INTR      2               /* Indicate interrupt execution */
#define DBG_V_TRACE     3               /* Trace register content */
#define DBG_V_ITRACE    4               /* Trace during interrupt */
#define DBG_V_TARGET    5               /* Output target address */
#define DBG_V_INPUT     6               /* Trace input operations */
#define DBG_V_OUTPUT    7               /* Trace output operations */
#define DBG_V_FULL      8               /* Full Trace (emulator debug) */
#define DBG_V_INTLVL    9               /* Prefix with interrupt level */
#define DBG_V_PROTECT   10              /* Display protect fault information */
#define DBG_V_MISSING   11              /* Display access to missing devices */
#define DBG_V_ENH       12              /* Display enh. instructions ignored */
                                        /*   in basic instruction mode */
#define DBG_V_MSOS5     13              /* Trace MSOS5 system requests */

#define DBG_DISASS      (1 << DBG_V_DISASS)
#define DBG_IDISASS     (1 << DBG_V_IDISASS)
#define DBG_INTR        (1 << DBG_V_INTR)
#define DBG_TRACE       (1 << DBG_V_TRACE)
#define DBG_ITRACE      (1 << DBG_V_ITRACE)
#define DBG_TARGET      (1 << DBG_V_TARGET)
#define DBG_INPUT       (1 << DBG_V_INPUT)
#define DBG_OUTPUT      (1 << DBG_V_OUTPUT)
#define DBG_FULL        (1 << DBG_V_FULL)
#define DBG_INTLVL      (1 << DBG_V_INTLVL)
#define DBG_PROTECT     (1 << DBG_V_PROTECT)
#define DBG_MISSING     (1 << DBG_V_MISSING)
#define DBG_ENH         (1 << DBG_V_ENH)
#define DBG_MSOS5       (1 << DBG_V_MSOS5)

/*
 * Default device radix
 */
#define DEV_RDX         16

/*
 * Private unit flags
 */
#define UNIT_V_7TRACK   (MTUF_V_UF + 0) /* 7-track tape transport */

#define UNIT_V_854      (UNIT_V_UF + 0) /* 854 vs. 853 disk pack drive */
#define UNIT_V_856_4    (UNIT_V_UF + 0) /* 856_4 vs. 856_2 drive */
#define UNIT_V_DRMSIZE  (UNIT_V_UF + 0) /* 1752 drum memory assignment */

#define UNIT_7TRACK     (1 << UNIT_V_7TRACK)
#define UNIT_854        (1 << UNIT_V_854)
#define UNIT_856_4      (1 << UNIT_V_856_4)
#define UNIT_DRMSIZE    (1 << UNIT_V_DRMSIZE)

/*
 * CPU
 */

/* The currently supported instruction set is held in u3. */
#define INSTR_ORIGINAL  0                       /* Original instruction set */
#define INSTR_BASIC     1                       /* Basic instruction set */
#define INSTR_ENHANCED  2                       /* Enhanced instruction set */

#define INSTR_SET       (cpu_unit.u3)

#define MAXMEMSIZE      65536
#define DEFAULTMEMSIZE  32768

/*
 * Compute the actual memory address based on the amount of memory installed
 * on the system. Note we only support power of 2 memories like the real
 * hardware - the system reference manual says that 12K and 24K systems are
 * possible but not supported by standard software.
 */
#define MEMADDR(p)      ((p) & (cpu_unit.capac - 1))

/*
 * Protect bit access
 */
#define SETPROTECT(a)   P[MEMADDR(a)] = 1
#define CLRPROTECT(a)   P[MEMADDR(a)] = 0
#define ISPROTECTED(a)  P[MEMADDR(a)]

/*
 * Max count of indirect addressing. Used to avoid infinite loops.
 */
#define MAXINDIRECT     10000

/*
 * Register access
 */
#define INCP            Preg = MEMADDR(Preg + 1)

/*
 * I/O operations
 */
enum IOstatus {
  IO_REPLY,                             /* Device sent a reply */
  IO_REJECT,                            /* Device sent a reject */
  IO_INTERNALREJECT                     /* I/O rejected internally */
};

#define SIGN            0x8000
#define MAXPOS          0x7FFF
#define MAXNEG          0xFFFF
#define ABS(v)          (((v) & SIGN) ? ~(v) : (v))

/*
 * Instruction layout:
 *
 * Storage reference instructions:
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |    op     |re|in|i1|i2|        delta          |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * Register reference instructions:
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * | 0  0  0  0|    F1     |       modifier        |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * Inter-register instructions:
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * | 0  0  0  0| 1  0  0  0|lp|xr| origin |  dest  |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * Shift instructions:
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * | 0  0  0  0| 1  1  1  1|LR| A| Q|    count     |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * Skip instructions:
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * | 0  0  0  0| 0  0  0  1|   type    |   count   |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 */
#define MOD_RE          0x800
#define MOD_IN          0x400
#define MOD_I1          0x200
#define MOD_I2          0x100

#define OPC_MASK        0xF000
#define OPC_ADQ         0xF000
#define OPC_LDQ         0xE000
#define OPC_RAO         0xD000
#define OPC_LDA         0xC000
#define OPC_EOR         0xB000
#define OPC_AND         0xA000
#define OPC_SUB         0x9000
#define OPC_ADD         0x8000
#define OPC_SPA         0x7000
#define OPC_STA         0x6000
#define OPC_RTJ         0x5000
#define OPC_STQ         0x4000
#define OPC_DVI         0x3000
#define OPC_MUI         0x2000
#define OPC_JMP         0x1000
#define OPC_SPECIAL     0x0000

#define OPC_SPECIALMASK 0x0F00

#define OPC_SLS         0x0000

#define OPC_SKIPS       0x0100
#define OPC_SKIPMASK    0x00F0
#define OPC_SKIPCOUNT   0x000F

#define OPC_SAZ         (OPC_SKIPS | 0x00)
#define OPC_SAN         (OPC_SKIPS | 0x10)
#define OPC_SAP         (OPC_SKIPS | 0x20)
#define OPC_SAM         (OPC_SKIPS | 0x30)
#define OPC_SQZ         (OPC_SKIPS | 0x40)
#define OPC_SQN         (OPC_SKIPS | 0x50)
#define OPC_SQP         (OPC_SKIPS | 0x60)
#define OPC_SQM         (OPC_SKIPS | 0x70)
#define OPC_SWS         (OPC_SKIPS | 0x80)
#define OPC_SWN         (OPC_SKIPS | 0x90)
#define OPC_SOV         (OPC_SKIPS | 0xA0)
#define OPC_SNO         (OPC_SKIPS | 0xB0)
#define OPC_SPE         (OPC_SKIPS | 0xC0)
#define OPC_SNP         (OPC_SKIPS | 0xD0)
#define OPC_SPF         (OPC_SKIPS | 0xE0)
#define OPC_SNF         (OPC_SKIPS | 0xF0)

#define OPC_INP         0x0200
#define OPC_OUT         0x0300
#define OPC_EIN         0x0400
#define OPC_IIN         0x0500
#define OPC_ECA         0x0580
#define OPC_DCA         0x05C0
#define OPC_SPB         0x0600
#define OPC_CPB         0x0700

#define OPC_INTER       0x0800
 #define MOD_LP         0x80
 #define MOD_XR         0x40
 #define MOD_O_A        0x20
 #define MOD_O_Q        0x10
 #define MOD_O_M        0x08
 #define MOD_D_A        0x04
 #define MOD_D_Q        0x02
 #define MOD_D_M        0x01
#define OPC_AAM         (OPC_INTER | MOD_O_A | MOD_O_M)
#define OPC_AAQ         (OPC_INTER | MOD_O_A | MOD_O_Q)
#define OPC_AAB         (OPC_INTER | MOD_O_A | MOD_O_Q | MOD_O_M)
#define OPC_CLR         (OPC_INTER | MOD_XR)
#define OPC_TCM         (OPC_INTER | MOD_XR | MOD_O_M)
#define OPC_TCQ         (OPC_INTER | MOD_XR | MOD_O_Q)
#define OPC_TCB         (OPC_INTER | MOD_XR | MOD_O_Q | MOD_O_M)
#define OPC_TCA         (OPC_INTER | MOD_XR | MOD_O_A)
#define OPC_EAM         (OPC_INTER | MOD_XR | MOD_O_A | MOD_O_M)
#define OPC_EAQ         (OPC_INTER | MOD_XR | MOD_O_A | MOD_O_Q)
#define OPC_EAB         (OPC_INTER | MOD_XR | MOD_O_A | MOD_O_Q | MOD_O_M)
#define OPC_SET         (OPC_INTER | MOD_LP)
#define OPC_TRM         (OPC_INTER | MOD_LP | MOD_O_M)
#define OPC_TRQ         (OPC_INTER | MOD_LP | MOD_O_Q)
#define OPC_TRB         (OPC_INTER | MOD_LP | MOD_O_Q | MOD_O_M)
#define OPC_TRA         (OPC_INTER | MOD_LP | MOD_O_A)
#define OPC_LAM         (OPC_INTER | MOD_LP | MOD_O_A | MOD_O_M)
#define OPC_LAQ         (OPC_INTER | MOD_LP | MOD_O_A | MOD_O_Q)
#define OPC_LAB         (OPC_INTER | MOD_LP | MOD_O_A | MOD_O_Q | MOD_O_M)
#define OPC_CAM         (OPC_INTER | MOD_LP | MOD_XR | MOD_O_A | MOD_O_M)
#define OPC_CAQ         (OPC_INTER | MOD_LP | MOD_XR | MOD_O_A | MOD_O_Q)
#define OPC_CAB         (OPC_INTER | MOD_LP | MOD_XR | MOD_O_A | MOD_O_Q | MOD_O_M)

#define OPC_INA         0x0900
#define OPC_ENA         0x0A00
#define OPC_NOP         0x0B00
#define OPC_ENQ         0x0C00
#define OPC_INQ         0x0D00
#define OPC_EXI         0x0E00

#define OPC_MODMASK     0x00FF
#define EXTEND16(v)     (((v) & 0x8000) ? (v) | 0xFFFF0000 : (v))
#define EXTEND8(v)      (((v) & 0x80) ? (v) | 0xFF00 : (v))
#define EXTEND4(v)      (((v) & 0x8) ? (v) | 0xFFF0 : (v))
#define TRUNC16(v)      ((v) & 0xFFFF)
#define CANEXTEND8(v)   (((v) & 0xFF80) == 0xFF80)

#define OPC_SHIFTS      0x0F00
#define OPC_SHIFTMASK   0x00E0
#define MOD_LR          0x80
#define MOD_S_A         0x40
#define MOD_S_Q         0x20
#define OPC_SHIFTCOUNT  0x001F

#define OPC_QRS         (OPC_SHIFTS | MOD_S_Q)
#define OPC_ARS         (OPC_SHIFTS | MOD_S_A)
#define OPC_LRS         (OPC_SHIFTS | MOD_S_A | MOD_S_Q)
#define OPC_QLS         (OPC_SHIFTS | MOD_LR | MOD_S_Q)
#define OPC_ALS         (OPC_SHIFTS | MOD_LR | MOD_S_A)
#define OPC_LLS         (OPC_SHIFTS | MOD_LR | MOD_S_A | MOD_S_Q)

#define OPC_ADDRMASK    0x00FF
#define ISCONSTANT(i)   ((i & (MOD_RE | MOD_IN | OPC_ADDRMASK)) == 0)

/*
 * Enhanced instruction layout.
 *
 * Enhanced storage reference instructions:
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |   F = 0   |   F1 = 4  |r |i |   Ra   |   Rb   |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |    F4     |    F5     |        delta          |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |   16-bit address, only present if delta = 0   |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * Field reference instructions:
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |   F = 0   |   F1 = 5  |r |i |   Ra   |   Rb   |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |   FLDSTR  |  FLDLTH-1 |        delta          |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |   16-bit address, only present if delta = 0   |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * Enhanced inter-register instructions:
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |   F = 0   |   F1 = 7  |   Ra   | F2a |   Rb   |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * Enhanced skip instructions:
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |   F = 0   |   F1 = 0  |     F2    |     SK    |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * Decrement and repeat instructions:
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |   F = 0   |   F1 = 6  |   Ra   |0 |     SK    |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 * Miscellaneous instructions:
 *
 *  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 * |   F = 0   |   F1 = B  |   Ra   |0 |     F3    |
 * +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
 *
 */
#define MOD_ENHRE       0x0080
#define MOD_ENHIN       0x0040
#define MOD_ENHRA       0x0038
#define MOD_ENHRB       0x0007

#define REG_NOREG       0x0
#define REG_R1          0x1
#define REG_R2          0x2
#define REG_R3          0x3
#define REG_R4          0x4
#define REG_Q           0x5
#define REG_A           0x6
#define REG_I           0x7

#define OPC_ENHF4       0xF000
#define OPC_ENHF5       0x0F00

#define WORD_REG        0x0
#define WORD_MEM        0x1
#define CHAR_REG        0x2
#define CHAR_MEM        0x3

#define OPC_STOSJMP     0x5000
#define OPC_STOADD      0x8000
#define OPC_STOSUB      0x9000
#define OPC_STOAND      0xA000
#define OPC_STOLOADST   0xC000
#define OPC_STOOR       0xD000
#define OPC_STOCRE      0xE000

#define OPC_FLDF3A      0x07
#define OPC_FLDRSV1     0x0
#define OPC_FLDRSV2     0x1
#define OPC_FLDSFZ      0x2
#define OPC_FLDSFN      0x3
#define OPC_FLDLOAD     0x4
#define OPC_FLDSTORE    0x5
#define OPC_FLDCLEAR    0x6
#define OPC_FLDSET      0x7
#define OPC_FLDSTR      0xF000
#define OPC_FLDLTH      0x0F00

#define OPC_ENHXFRRA    0xE0
#define OPC_ENHXFRF2A   0x18
#define OPC_ENHXFRRB    0x7

#define OPC_ENHSKIPTY   0x30
#define OPC_ENHSKIPREG  0xC0
#define OPC_ENHSKIPCNT  0xF

#define OPC_DRPMBZ      0x10
#define OPC_DRPRA       0xE0
#define OPC_DRPSK       0xF

#define OPC_MISCRA      0xE0
#define OPC_MISCF3      0xF

#define OPC_ENHLMM      0x1
#define OPC_ENHLRG      0x2
#define OPC_ENHSRG      0x3
#define OPC_ENHSIO      0x4
#define OPC_ENHSPS      0x5
#define OPC_ENHDMI      0x6
#define OPC_ENHCBP      0x7
#define OPC_ENHGPE      0x8
#define OPC_ENHGPO      0x9
#define OPC_ENHASC      0xA
#define OPC_ENHAPM      0xB
#define OPC_ENHPM0      0xC
#define OPC_ENHPM1      0xD

#define OPC_ENHLUB      0x0
#define OPC_ENHLLB      0x1
#define OPC_ENHEMS      0x2
#define OPC_ENHWPR      0x3
#define OPC_ENHRPR      0x4
#define OPC_ENHECC      0x5

/*
 * Interrupt vector definitions
 */
#define INTERRUPT_BASE  0x100
#define INTERRUPT_00    (INTERRUPT_BASE + 0x00)
#define INTERRUPT_01    (INTERRUPT_BASE + 0x04)
#define INTERRUPT_02    (INTERRUPT_BASE + 0x08)
#define INTERRUPT_03    (INTERRUPT_BASE + 0x0C)
#define INTERRUPT_04    (INTERRUPT_BASE + 0x10)
#define INTERRUPT_05    (INTERRUPT_BASE + 0x14)
#define INTERRUPT_06    (INTERRUPT_BASE + 0x18)
#define INTERRUPT_07    (INTERRUPT_BASE + 0x1C)
#define INTERRUPT_08    (INTERRUPT_BASE + 0x20)
#define INTERRUPT_09    (INTERRUPT_BASE + 0x24)
#define INTERRUPT_10    (INTERRUPT_BASE + 0x28)
#define INTERRUPT_11    (INTERRUPT_BASE + 0x2C)
#define INTERRUPT_12    (INTERRUPT_BASE + 0x30)
#define INTERRUPT_13    (INTERRUPT_BASE + 0x34)
#define INTERRUPT_14    (INTERRUPT_BASE + 0x38)
#define INTERRUPT_15    (INTERRUPT_BASE + 0x3C)

#define INTR_BASIC      2
#define INTR_1705       16

/*
 * I/O definitions.
 */
#define IO_CONTINUE     0x8000
#define IO_W            0x7800
#define IO_EQUIPMENT    0x0780
#define IO_COMMAND      0x007F
/*
 * Standard director functions
 */
#define IO_DIR_STOP     0x0040                  /* Stop motion */
#define IO_DIR_START    0x0020                  /* Start motion */
#define IO_DIR_ALARM    0x0010                  /* Alarm int req. */
#define IO_DIR_EOP      0x0008                  /* End of operation int req. */
#define IO_DIR_DATA     0x0004                  /* Data int req. */
#define IO_DIR_CINT     0x0002                  /* Clear interrupts */
#define IO_DIR_CCONT    0x0001                  /* Clear controller */

/*
 * Illegal combination of functions - Start + Stop
 */
#define STARTSTOP(v)    (((v) & \
                          (IO_DIR_START | IO_DIR_STOP)) == \
                          (IO_DIR_START | IO_DIR_STOP))
/*
 * Standard status bits
 */
#define IO_ST_PARITY    0x0100                  /* Parity error */
#define IO_ST_PROT      0x0080                  /* Protected */
#define IO_ST_LOST      0x0040                  /* Lost data */
#define IO_ST_ALARM     0x0020                  /* Alarm */
#define IO_ST_EOP       0x0010                  /* End of operation */
#define IO_ST_DATA      0x0008                  /* Data */
#define IO_ST_INT       0x0004                  /* Interrupt */
#define IO_ST_BUSY      0x0002                  /* Device busy */
#define IO_ST_READY     0x0001                  /* Device ready */

/*
 * The following type/values may be used to differentiate processing when
 * a single device driver is used to emulate multiple controller types (e.g.
 * the LP device driver emulates both 1740 and 1742-30/-120 controllers).
 */
enum IOdevtype {
  IOtype_default,                               /* Initial value */
  IOtype_dev1,                                  /* Device specific values */
  IOtype_dev2,
  IOtype_dev3,
  IOtype_dev4,
  IOtype_dev5,
  IOtype_dev6,
  IOtype_dev7,
  IOtype_dev8
};

/*
 * I/O framework device structure
 */
struct io_device {
  const char            *iod_name;
  const char            *iod_model;
  enum IOdevtype        iod_type;
  uint8                 iod_equip;
  uint8                 iod_station;
  uint16                iod_interrupt;
  uint16                iod_dcbase;
  DEVICE                *iod_indev;
  DEVICE                *iod_outdev;
  UNIT                  *iod_unit;
  t_bool                (*iod_reject)(struct io_device *, t_bool, uint8);
  enum IOstatus         (*iod_IOread)(struct io_device *, uint8);
  enum IOstatus         (*iod_IOwrite)(struct io_device *, uint8);
  enum IOstatus         (*iod_BDCread)(struct io_device *, uint16 *, uint8);
  enum IOstatus         (*iod_BDCwrite)(struct io_device *, uint16 *, uint8);
  void                  (*iod_state)(const char *, DEVICE *, struct io_device *);
  t_bool                (*iod_intr)(struct io_device *);
  uint16                (*iod_raised)(DEVICE *);
  void                  (*iod_clear)(DEVICE *);
  uint8                 (*iod_decode)(struct io_device *, t_bool, uint8);
  t_bool                (*iod_chksta)(t_bool, uint8);
  uint16                iod_ienable;
  uint16                iod_oldienable;
  uint16                iod_imask;
  uint16                iod_dmask;
  uint16                iod_smask;
  uint16                iod_cmask;
  uint16                iod_rmask;
  uint8                 iod_regs;
  uint16                iod_validmask;
  uint16                iod_readmap;
  uint16                iod_rejmapR;
  uint16                iod_rejmapW;
  uint8                 iod_flags;
  uint8                 iod_dc;
  uint16                iod_readR[16];
  uint16                iod_writeR[16];
  uint16                iod_prevR[16];
  uint16                iod_forced;
  t_uint64              iod_event;
  uint16                iod_private;
  void                  *iod_private2;
  uint16                iod_private3;
  t_bool                iod_private4;
  const char            *iod_private5;
  uint16                iod_private6;
  uint16                iod_private7;
  uint16                iod_private8;
  uint8                 iod_private9;
  t_bool                iod_private10;
  uint16                iod_private11;
  uint16                iod_private12;
  uint8                 iod_private13;
  uint8                 iod_private14;
};
#define STATUS          iod_readR[1]
#define DEVSTATUS(iod)  ((iod)->iod_readR[1])
#define DCSTATUS(iod)   ((iod)->iod_readR[2])
#define FUNCTION        iod_writeR[1]
#define IENABLE         iod_ienable
#define OLDIENABLE      iod_oldienable
#define ENABLED(iod)    ((iod)->iod_ienable)
#define ISENABLED(iod, mask) \
  (((iod)->iod_ienable & mask) != 0)

#define SETSTICKY(iod, reg, value) \
  ((iod)->iod_sticky[reg] |= value)
#define CLRSTICKY(iod, reg, value) \
  ((iod)->iod_sticky[reg] &= ~value)

#define MASK_REGISTER0  0x0001
#define MASK_REGISTER1  0x0002
#define MASK_REGISTER2  0x0004
#define MASK_REGISTER3  0x0008
#define MASK_REGISTER4  0x0010
#define MASK_REGISTER5  0x0020
#define MASK_REGISTER6  0x0040
#define MASK_REGISTER7  0x0080
#define MASK_REGISTER8  0x0100
#define MASK_REGISTER9  0x0200
#define MASK_REGISTER10 0x0400
#define MASK_REGISTER11 0x0800
#define MASK_REGISTER12 0x1000
#define MASK_REGISTER13 0x2000
#define MASK_REGISTER14 0x4000
#define MASK_REGISTER15 0x8000

#define STATUS_ZERO     0x01
#define DEVICE_DC       0x02
#define AQ_ONLY         0x04

#define IODEV(name, model, id, equ, sta, base, busy, ior, iow, bdcr, bdcw, dump, intr, raised, clear, decode, chksta, mask, regs, valid, map, rejR, rejW, flags, dc, devspec) \
  { name, model, IOtype_default, equ, sta, 0, base, \
    NULL, NULL, NULL, \
    busy, ior, iow, bdcr, bdcw, dump, intr, raised, clear, decode, chksta, \
    0, 0, IO_##id##_INTR, IO_##id##_DIRMSK, IO_##id##_STMSK, \
    IO_##id##_STCINT | IO_ST_INT, \
    mask, regs, valid, map, rejR, rejW, flags, dc, \
    { 0, 0, 0, 0, 0, 0, 0, 0, }, \
    { 0, 0, 0, 0, 0, 0, 0, 0, }, \
    { 0, 0, 0, 0, 0, 0, 0, 0, }, \
    0, 0, 0, devspec, 0, FALSE, NULL, 0, 0, 0, 0, FALSE \
  }

typedef struct io_device IO_DEVICE;

#define IODEVICE(dev)   ((IO_DEVICE *)dev->ctxt)

#define CHANGED(iod, n) ((iod)->iod_writeR[n] ^ (iod)->iod_prevR[n])
#define ICHANGED(iod)   ((iod)->iod_ienable ^ (iod)->iod_oldienable)

#define DEVRESET(iod) \
  (iod)->iod_ienable = 0; \
  (iod)->iod_oldienable = 0; \
  (iod)->iod_forced = 0

/*
 * Routine type to return interrupt mask for a device.
 */
typedef uint16 devINTR(DEVICE *);

/*
 * Generic device flags
 */
#define DBG_V_DTRACE    0                       /* Trace device ops */
#define DBG_V_DSTATE    1                       /* Dump device state */
#define DBG_V_DINTR     2                       /* Trace device interrupts */
#define DBG_V_DERROR    3                       /* Trace device errors */
#define DBG_V_LOC       4                       /* Dump instruction location */
#define DBG_V_FIRSTREJ  5                       /* Dump only first Reject */
#define DBG_SPECIFIC    6                       /* Start of device-specific */
                                                /* flags */

#define DBG_DTRACE      (1 << DBG_V_DTRACE)
#define DBG_DSTATE      (1 << DBG_V_DSTATE)
#define DBG_DINTR       (1 << DBG_V_DINTR)
#define DBG_DERROR      (1 << DBG_V_DERROR)
#define DBG_DLOC        (1 << DBG_V_LOC)
#define DBG_DFIRSTREJ   (1 << DBG_V_FIRSTREJ)

/*
 * Device specific values
 */

/*
 * CPU is treated as a device but has no interrupts, director functions or
 * status bits.
 */
#define IO_CPU_INTR     0
#define IO_CPU_DIRMSK   0
#define IO_CPU_STMSK    0
#define IO_CPU_STCINT   0

/*
 * 1706-A Buffered Data Channel
 */
#define IO_1706_1_A     0x1000                  /* 1706-A #1 */
#define IO_1706_1_B     0x1800
#define IO_1706_1_C     0x2000
#define IO_1706_1_D     0x2800
#define IO_1706_2_A     0x3800                  /* 1706-A #2 */
#define IO_1706_2_B     0x4000
#define IO_1706_2_C     0x4800
#define IO_1706_2_D     0x5000
#define IO_1706_3_A     0x6000                  /* 1706-A #3 */
#define IO_1706_3_B     0x6800
#define IO_1706_3_C     0x7000
#define IO_1706_3_D     0x7800

#define IO_1706_SET     0x8000                  /* Set/clear for ones */
#define IO_1706_EOP     0x0001                  /* Enable interrupt on EOP */

#define IO_1706_PROT    0x0040                  /* Program protect fault */
#define IO_1706_REPLY   0x0200                  /* Device reply */
#define IO_1706_REJECT  0x0100                  /* Device reject */
#define IO_1706_STMSK   (IO_1706_REPLY | IO_1706_REJECT | IO_1706_PROT | \
                         IO_ST_EOP | IO_ST_BUSY | IO_ST_READY)

/*
 * A 1706-A has no interrupts, director functions or status bits of it's own
 * that are accessible through normal I/O requests.
 */
#define IO_DC_INTR      0
#define IO_DC_DIRMSK    0
#define IO_DC_STMSK     0
#define IO_DC_STCINT    0

#define IO_1706_MAX     3                       /* Max # of 1706's allowed */
#define IO_1706_DEVS    8                       /* Max devices per channel */

#define IDX_FROM_CHAN(c)        (c - 1)

/*
 * 1711-A/B, 1712-A Teletypewriter
 */
#define IO_1711_A       0x0090
#define IO_1711_B       0x0091

#define IO_1711_SREAD   0x0200                  /* Select read mode */
#define IO_1711_SWRITE  0x0100                  /* Select write mode */
#define IO_1711_DIRMSK  (IO_1711_SREAD | IO_1711_SWRITE | IO_DIR_ALARM | \
                         IO_DIR_EOP | IO_DIR_DATA | IO_DIR_CINT | IO_DIR_CCONT)
#define IO_1711_INTR    (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA)

#define IO_1711_MANUAL  0x0800                  /* Manual interrupt */
#define IO_1711_MON     0x0400                  /* Motor on */
#define IO_1711_RMODE   0x0200                  /* Read mode */
#define IO_1711_STMSK   (IO_1711_MANUAL | IO_1711_MON | IO_1711_RMODE | \
                         IO_ST_LOST | IO_ST_ALARM | IO_ST_EOP \
                         | IO_ST_DATA | IO_ST_INT | IO_ST_BUSY | IO_ST_READY)
#define IO_1711_STCINT  (IO_ST_ALARM | IO_ST_EOP | IO_ST_DATA)

/*
 * 1721-A/B/C/D, 1722-A/B Paper Tape Reader
 */
#define IO_1721_A       0x00A0
#define IO_1721_B       0x00A1

#define IO_1721_DIRMSK  (IO_DIR_STOP | IO_DIR_START | IO_DIR_ALARM | \
                         IO_DIR_DATA | IO_DIR_CINT | IO_DIR_CCONT)
#define IO_1721_INTR    (IO_DIR_ALARM | IO_DIR_DATA)

#define IO_1721_POWERON 0x0400                  /* Power on */
#define IO_1721_MOTIONF 0x0200                  /* Paper motion failure */
#define IO_1721_EXIST   0x0100                  /* Existence code */
#define IO_1721_STMSK   (IO_1721_POWERON | IO_1721_MOTIONF | IO_1721_EXIST | \
                         IO_ST_PROT | IO_ST_LOST | IO_ST_ALARM | \
                         IO_ST_DATA | IO_ST_INT | IO_ST_BUSY | IO_ST_READY)
#define IO_1721_STCINT  (IO_ST_ALARM | IO_ST_DATA)

/*
 * 1723-A/B, 1724-A/B Paper Tape Punch
 */
#define IO_1723_A       0x00C0
#define IO_1723_B       0x00C1

#define IO_1723_DIRMSK  (IO_DIR_STOP | IO_DIR_START | IO_DIR_ALARM | \
                         IO_DIR_DATA | IO_DIR_CINT | IO_DIR_CCONT)
#define IO_1723_INTR    (IO_DIR_ALARM | IO_DIR_DATA)

#define IO_1723_TAPELOW 0x0800                  /* Tape supply low */
#define IO_1723_POWERON 0x0400                  /* Power on */
#define IO_1723_BREAK   0x0200                  /* Tape break */
#define IO_1723_EXIST   0x0100                  /* Existence code */
#define IO_1723_STMSK   (IO_1723_TAPELOW | IO_1723_POWERON | IO_1723_BREAK | \
                         IO_1723_EXIST | IO_ST_PROT | IO_ST_ALARM | \
                         IO_ST_DATA | IO_ST_INT | IO_ST_BUSY | IO_ST_READY)
#define IO_1723_STCINT  (IO_ST_ALARM | IO_ST_DATA)

/*
 * 1726 Card Reader
 */
#define IO_1726_GATE    0x0200                  /* gate card */
#define IO_1726_NHOL    0x0400                  /* Negate hollerith to acsii */
#define IO_1726_RHOL    0x0800                  /* Rel. hollerith to ascii */
#define IO_1726_RELOAD  0x1000                  /* Reload memory */

#define IO_1726_DIRMSK  (IO_1726_RELOAD | IO_1726_RHOL | IO_1726_NHOL | \
                         IO_1726_GATE | IO_DIR_ALARM | IO_DIR_EOP | \
                         IO_DIR_DATA | IO_DIR_CINT | IO_DIR_CCONT)
#define IO_1726_INTR    (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA)

#define IO_1726_ERROR   0x0100                  /* Error */
#define IO_1726_BINARY  0x0200                  /* Binary card */
#define IO_1726_SEP     0x0400                  /* Separator card */
#define IO_1726_FEED    0x0800                  /* Fail to feed */
#define IO_1726_JAM     0x1000                  /* Stacker full or jam */
#define IO_1726_EMPTY   0x2000                  /* Input tray empty */
#define IO_1726_EOF     0x4000                  /* End of file */
#define IO_1726_PWROFF  0x8000                  /* Motor power off */
#define IO_1726_STMSK   (IO_1726_PWROFF | IO_1726_EOF | IO_1726_EMPTY | \
                         IO_1726_JAM | IO_1726_FEED | IO_1726_SEP | \
                         IO_1726_BINARY | IO_1726_ERROR | IO_ST_PROT | \
                         IO_ST_ALARM | IO_ST_EOP | IO_ST_DATA | \
                         IO_ST_INT | IO_ST_BUSY | IO_ST_READY)
#define IO_1726_STCINT  (IO_ST_ALARM | IO_ST_EOP | IO_ST_DATA)

/*
 * 1728-A/B Card Reader/Punch
 */
#define IO_1728_MASK    0x0060                  /* Station mask */
#define IO_1728_CR      0x0020                  /* Card reader select */
#define IO_1728_CP      0x0040                  /* Card puch select */

#define IO_1728_OFFSET  0x0100                  /* Offset request */
#define IO_1728_FEED    0x0080                  /* Initiate feed cycle */

#define IO_1728_DIRMSK  (IO_1728_OFFSET | IO_1728_FEED | IO_DIR_ALARM | \
                         IO_DIR_EOP | IO_DIR_DATA | IO_DIR_CINT | \
                         IO_DIR_CCONT)
#define IO_1728_INTR    (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA)

#define IO_1728_CBFULL  0x0800                  /* Chip box full */
#define IO_1728_EOF     0x0400                  /* End of file */
#define IO_1728_FEEDAL  0x0200                  /* Feed alert */
#define IO_1728_ERROR   0x0100                  /* Pre-read or punch error */
#define IO_1728_STMSK   (IO_1728_CBFULL | IO_1728_EOF | IO_1728_FEEDAL | \
                         IO_1728_ERROR | IO_ST_PROT | IO_ST_LOST | \
                         IO_ST_EOP | IO_ST_DATA | IO_ST_INT | IO_ST_BUSY | \
                         IO_ST_READY)
#define IO_1728_STCINT  (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA)

#define IO_ST2_INTLOCK  0x0400                  /* Door interlock */
#define IO_ST2_PUNINH   0x0200                  /* Punch inhibit set */
#define IO_ST2_MANUAL   0x0100                  /* Manual mode */
#define IO_ST2_PUNERR   0x0080                  /* Punch error */
#define IO_ST2_PREERR   0x0040                  /* Pre-read error */
#define IO_ST2_STKJAM   0x0020                  /* Stacker area jam */
#define IO_ST2_PUNJAM   0x0010                  /* Punch area jam */
#define IO_ST2_READJAM  0x0008                  /* Read area jam */
#define IO_ST2_FFEED    0x0004                  /* Fail to feed */
#define IO_ST2_FULL     0x0002                  /* Stacker full */
#define IO_ST2_EMPTY    0x0001                  /* Hopper empty */
#define IO_1728_ST2MSK  (IO_ST2_INTLOCK | IO_ST2_PUNINH | IO_ST2_MANUAL | \
                         IO_ST2_PUNERR | IO_ST2_PREERR | IO_ST2_STKJAM | \
                         IO_ST2_PUNJAM | IO_ST2_READJAM | IO_ST2_FFEED | \
                         IO_ST2_FULL | IO_ST2_EMPTY)

/*
 * 1729-A/B Card Reader
 */
#define IO_1729_A       0x00E0
#define IO_1729_B       0x00E1

#define IO_1729_IEOR    0x0008                  /* Interrupt on End of Record */

#define IO_1729_DIRMSK  (IO_DIR_STOP | IO_DIR_START | IO_DIR_ALARM | \
                         IO_1729_IEOR | IO_DIR_DATA | IO_DIR_CINT | \
                         IO_DIR_CCONT)
#define IO_1729_INTR    (IO_DIR_ALARM | IO_1729_IEOR | IO_DIR_DATA)

#define IO_1729_EMPTY   0x0200                  /* Read station empty */
#define IO_1729_EXIST   0x0100                  /* Existence code */
#define IO_1729_EOR     0x0010                  /* End of Record */
#define IO_1729_STMSK   (IO_1729_EMPTY | IO_1729_EXIST | IO_ST_PROT | \
                         IO_ST_LOST | IO_ST_ALARM | IO_1729_EOR | \
                         IO_ST_DATA | IO_ST_INT | IO_ST_BUSY | \
                         IO_ST_READY)
#define IO_1729_STCINT  (IO_ST_ALARM | IO_1729_EOR | IO_ST_DATA)

/*
 * 1732-3 Magnetic Tape Controller
 */
#define IO_1732_WRITE   0x0080                  /* Write motion */
#define IO_1732_READ    0x0100                  /* Read motion */
#define IO_1732_BACKSP  0x0180                  /* Backspace */
#define IO_1732_WFM     0x0280                  /* Write file mark/tape mark */
#define IO_1732_SFWD    0x0300                  /* Search FM/TM forward */
#define IO_1732_SBACK   0x0380                  /* Search FM/TM backward */
#define IO_1732_REWL    0x0400                  /* Rewind load */
#define IO_1732_MOTION  0x0780                  /* Motion field mask */

#define IO_1732_LRT     0x1000                  /* Select low read threshold */
#define IO_1732_DESEL   0x0800                  /* Deselect tape unit */
#define IO_1732_SEL     0x0400                  /* Select tape unit */
#define IO_1732_UNIT    0x0180                  /* Unit mask */
#define IO_1732_ASSEM   0x0040                  /* Assembly/Disassembly */
#define IO_1732_1600    0x0020                  /* Select 1600 BPI */
#define IO_1732_556     0x0010                  /* Select 556 BPI */
#define IO_1732_800     0x0008                  /* Select 800 BPI */
#define IO_1732_BINARY  0x0004                  /* Binary mode */
#define IO_1732_BCD     0x0002                  /* BCD mode */
#define IO_1732_PARITY  0x0006                  /* Parity mask */
#define IO_1732_CHAR    0x0001                  /* Character mode */

                                                /* 1732-A/B changes */
#define IO_1732A_REWU   0x0600                  /* Rewind and unload */
#define IO_1732A_UNIT   0x0380                  /* Unit mask */
#define IO_1732A_200    0x0020                  /* Select 200 BPI */

#define IO_1732_DIRMSK  (IO_1732_MOTION | IO_DIR_ALARM | IO_DIR_EOP | \
                         IO_DIR_DATA | IO_DIR_CINT | IO_DIR_CCONT)

#define IO_1732_INTR    (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA)

#define IO_1732_PROT    0x8000                  /* Protect fault */
#define IO_1732_SPE     0x4000                  /* Storage parity error */
#define IO_1732_FILL    0x2000                  /* Odd byte record in */
                                                /* assembly mode */
#define IO_1732_ACTIVE  0x1000                  /* Controller active */
#define IO_1732_FMARK   0x0800                  /* At file mark */
#define IO_1732_BOT     0x0400                  /* At BOT */
#define IO_1732_EOT     0x0200                  /* At EOT */
#define IO_1732_STMSK   (IO_1732_PROT | IO_1732_SPE | IO_1732_FILL | \
                         IO_1732_ACTIVE | IO_1732_FMARK | IO_1732_BOT | \
                         IO_1732_EOT | IO_ST_PARITY | IO_ST_PROT | \
                         IO_ST_LOST | IO_ST_ALARM | IO_ST_EOP | \
                         IO_ST_DATA | IO_ST_INT | IO_ST_BUSY | \
                         IO_ST_READY)
#define IO_1732_STCINT  (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA)

#define IO_ST2_LRT      0x0200                  /* Low read threshold */
#define IO_ST2_IDABORT  0x0100                  /* ID abort */
#define IO_ST2_PETRANS  0x0080                  /* PE capable transport */
#define IO_ST2_PELOST   0x0040                  /* PE - lost data */
#define IO_ST2_PEWARN   0x0020                  /* PE - error warning */
#define IO_ST2_WENABLE  0x0010                  /* Write enable */
#define IO_ST2_7TRACK   0x0008                  /* Seven track transport */
#define IO_ST2_1600     0x0004                  /* 1600 BPI */
#define IO_ST2_800      0x0002                  /* 800 BPI */
#define IO_ST2_556      0x0001                  /* 556 BPI */
#define IO_1732_ST2MSK  (IO_ST2_LRT | IO_ST2_IDABORT | IO_ST2_PETRANS | \
                         IO_ST2_PELOST | IO_ST2_PEWARN | IO_ST2_WENABLE | \
                         IO_ST2_7TRACK | IO_ST2_1600 | IO_ST2_800 | \
                         IO_ST2_556)
#define IO_1732A_ST2MSK (IO_ST2_WENABLE | IO_ST2_7TRACK | IO_ST2_800 | \
                         IO_ST2_556)

/*
 * 1733-2 Cartridge Disk Drive Controller
 */
#define IO_1733_USC     0x0600                  /* Unit select code */
#define IO_1733_USEL    0x0100                  /* Unit select */
#define IO_1733_UDSEL   0x0080                  /* Unit de-select */
#define IO_1733_RBINT   0x0004                  /* Ready and not busy int */
#define IO_1733_DIRMSK  (IO_1733_USC | IO_1733_USEL | IO_1733_UDSEL | \
                         IO_DIR_ALARM | IO_DIR_EOP | IO_1733_RBINT | \
                         IO_DIR_CINT)
#define IO_1733_INTR    (IO_DIR_ALARM | IO_DIR_EOP | IO_1733_RBINT)

#define IO_1733_DSEEK   0x8000                  /* Drive seek error */
#define IO_1733_SPROT   0x4000                  /* Storage protect fault */
#define IO_1733_SPAR    0x2000                  /* Storage parity error */
#define IO_1733_SINGLE  0x1000                  /* Single density */
#define IO_1733_CSEEK   0x0800                  /* Controller seek error */
#define IO_1733_ADDRERR 0x0400                  /* Address error */
#define IO_1733_LOST    0x0200                  /* Lost data */
#define IO_1733_CWERR   0x0100                  /* Checkword error */
#define IO_1733_NOCOMP  0x0040                  /* No compare */
#define IO_1733_ONCYL   0x0008                  /* On cylinder */
#define IO_1733_STMSK   (IO_1733_DSEEK | IO_1733_SPROT | IO_1733_SPAR | \
                         IO_1733_SINGLE | IO_1733_CSEEK | IO_1733_ADDRERR | \
                         IO_1733_LOST | IO_1733_CWERR | IO_ST_PROT | \
                         IO_1733_NOCOMP | IO_ST_ALARM | IO_ST_EOP | \
                         IO_1733_ONCYL | IO_ST_INT | IO_ST_BUSY | \
                         IO_ST_READY)
#define IO_1733_STCINT  (IO_ST_ALARM | IO_ST_EOP)

/*
 * 1738-A/B Disk Pack Controller
 */
#define IO_1738_USC     0x0200                  /* Unit select code */
#define IO_1738_USEL    0x0100                  /* Unit select */
#define IO_1738_REL     0x0080                  /* Release */
#define IO_1738_RBINT   0x0004                  /* Ready and not busy int */
#define IO_1738_DIRMSK  (IO_1738_USC | IO_1738_USEL | IO_1738_REL | \
                         IO_DIR_ALARM | IO_DIR_EOP | IO_1738_RBINT | \
                         IO_DIR_CINT)
#define IO_1738_INTR    (IO_DIR_ALARM | IO_DIR_EOP | IO_1738_RBINT)

#define IO_1738_SPROT   0x4000                  /* Storage protect fault */
#define IO_1738_SPAR    0x2000                  /* Storage parity error */
#define IO_1738_DEFECT  0x1000                  /* Defective track */
#define IO_1738_ADDRERR 0x0800                  /* Address error */
#define IO_1738_SKERR   0x0400                  /* Seek error */
#define IO_1738_LOST    0x0200                  /* Lost data */
#define IO_1738_CWERR   0x0100                  /* Checkword error */
#define IO_1738_NOCOMP  0x0040                  /* No compare */
#define IO_1738_ONCYL   0x0008                  /* On cylinder */
#define IO_1738_STMSK   (IO_1738_SPROT | IO_1738_SPAR | IO_1738_DEFECT | \
                         IO_1738_ADDRERR | IO_1738_SKERR | IO_1738_LOST | \
                         IO_1738_CWERR | IO_ST_PROT | IO_1738_NOCOMP | \
                         IO_ST_ALARM | IO_ST_EOP | IO_1738_ONCYL | \
                         IO_ST_INT | IO_ST_BUSY | IO_ST_READY)
#define IO_1738_STCINT  (IO_ST_ALARM | IO_ST_EOP)

/*
 * 1740 - Line Printer Controller
 */
#define IO_1740_CPRINT  0x0001                  /* Clear printer */
#define IO_1740_DIRMSK  (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA | \
                         IO_DIR_CINT | IO_1740_CPRINT)
#define IO_1740_INTR    (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA)

#define IO_1740_L12     0x4000                  /* Level 12 */
#define IO_1740_L7      0x0200                  /* Level 7 */
#define IO_1740_L6      0x0100                  /* Level 6 */
#define IO_1740_L5      0x0080                  /* Level 5 */
#define IO_1740_L4      0x0040                  /* Level 4 */
#define IO_1740_L3      0x0020                  /* Level 3 */
#define IO_1740_L2      0x0010                  /* Level 2 */
#define IO_1740_L1      0x0008                  /* Level 1 */
#define IO_1740_DSP     0x0004                  /* Double space */
#define IO_1740_SSP     0x0002                  /* Single space */
#define IO_1740_PRINT   0x0001                  /* Print */
#define IO_1740_DIR2MSK (IO_1740_L12 | IO_1740_L7 | IO_1740_L6 | \
                         IO_1740_L5 | IO_1740_L4 | IO_1740_L3 | \
                         IO_1740_L2 | IO_1740_L1 | IO_1740_DSP | \
                         IO_1740_SSP | IO_1740_PRINT)

#define IO_1740_LEVELS  (IO_1740_L1 | IO_1740_L2 | IO_1740_L3 | \
                         IO_1740_L4 | IO_1740_L5 | IO_1740_L6 | \
                         IO_1740_L7 | IO_1740_L12)

#define IO_1740_MOTION  (IO_1740_SSP | IO_1740_DSP | IO_1740_LEVELS)

#define IO_1740_STMSK   (IO_ST_PROT | IO_ST_ALARM | IO_ST_EOP | \
                         IO_ST_DATA | IO_ST_INT | IO_ST_BUSY | \
                         IO_ST_READY)
#define IO_1740_STCINT  (IO_ST_ALARM | IO_ST_EOP | IO_ST_DATA)

/*
 * 1742-30/-120 Line Printer
 */
#define IO_1742_PRINT   0x0020                  /* Print */
#define IO_1742_DIRMSK  (IO_1742_PRINT | IO_DIR_ALARM | IO_DIR_EOP | \
                         IO_DIR_DATA | IO_DIR_CINT | IO_DIR_CCONT)
#define IO_1742_INTR    (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_DATA)

#define IO_1742_LIMAGE  0x0100                  /* Load image (-120 only) */
#define IO_1742_ERROR   0x0040                  /* Device error */

#define IO_1742_STMSK   (IO_1742_LIMAGE | IO_ST_PROT | IO_1742_ERROR | \
                         IO_ST_ALARM | IO_ST_EOP | IO_ST_DATA | \
                         IO_ST_INT | IO_ST_BUSY | IO_ST_READY)

#define IO_1742_STCINT  (IO_ST_ALARM | IO_ST_EOP | IO_ST_DATA)

/*
 * 1752-1/2/3/4 Drum
 */
#define IO_1752_DIRMSK  (IO_DIR_ALARM | IO_DIR_EOP | IO_DIR_CINT | \
                         IO_DIR_CCONT)
#define IO_1752_INTR    (IO_DIR_ALARM | IO_DIR_EOP)

#define IO_1752_OVERR   0x8000                  /* Sector overrange error */
#define IO_1752_GUARDED 0x4000                  /* Guarded address error */
#define IO_1752_SECCMP  0x2000                  /* Sector compare */
#define IO_1752_POWERF  0x1000                  /* Power failure */
#define IO_1752_TIMERR  0x0800                  /* Timing track error */
#define IO_1752_GUARDE  0x0400                  /* Guarded address enabled */
#define IO_1752_PROTF   0x0200                  /* Protect fault */
#define IO_1752_CHECKW  0x0100                  /* Checkword error */
#define IO_1752_STMSK   (IO_1752_OVERR | IO_1752_GUARDED | IO_1752_SECCMP | \
                         IO_1752_POWERF | IO_1752_TIMERR | IO_1752_GUARDE | \
                         IO_1752_PROTF | IO_1752_CHECKW | IO_ST_PROT | \
                         IO_ST_LOST | IO_ST_ALARM | IO_ST_EOP | IO_ST_DATA | \
                         IO_ST_INT | IO_ST_BUSY | IO_ST_READY)

#define IO_1752_STCINT  (IO_ST_ALARM | IO_ST_EOP)

/*
 * 10336-1 Real-Time Clock
 */
#define IO_10336_ACK    0x0002                  /* Acknowledge interrupt */
#define IO_10336_STOP   0x0040                  /* Stop clock */
#define IO_10336_START  0x0080                  /* Start clock */
#define IO_10336_DIS    0x4000                  /* Disable interrupt */
#define IO_10336_ENA    0x8000                  /* Enable interrupt */

#define IO_10336_DIRMSK (IO_10336_ENA | IO_10336_DIS | IO_10336_START | \
                         IO_10336_STOP | IO_10336_ACK | IO_DIR_CCONT)

#define IO_10336_INTR   0
#define IO_10336_STMSK  0
#define IO_10336_STCINT 0

/*
 * M05 addressing scheme used by Cyber-18 enhanced instruction set for
 * magtape access.
 */
#define M05_SAMPLE      0x0000                  /* Sample (peripheral input) */
#define M05_SET         0x0008                  /* Set (peripheral output) */
#define M05_DEVICE      0x0070                  /* Device selection */
#define M05_CONTR       0x0380                  /* Controller selection */

/*
 * Timing parameters
 */
#define TT_OUT_WAIT      200                    /* TTY output wait time */
#define TT_IN_XFER        60                    /* TTY input transfer delay */
#define TT_IN_MOTION     500                    /* TTY paper motion delay */
#define PTP_OUT_WAIT     500                    /* Punch output wait time */
#define PTR_IN_WAIT      450                    /* Reader input wait time */
#define LP_OUT_WAIT       15                    /* Line printer output wait time */
#define LP_PRINT_WAIT   3000                    /* Print line wait time */
#define LP_CC_WAIT       300                    /* Control char wait time */

#define MT_MOTION_WAIT   150                    /* Magtape motion delay */
#define MT_RDATA_DELAY    46                    /* Read data avail. period */
#define MT_WDATA_DELAY    46                    /* Write data ready period */

#define MT_200_WAIT      134                    /* I/O latency (200 bpi) */
#define MT_556_WAIT       48                    /* I/O latency (556 bpi) */
#define MT_800_WAIT       33                    /* I/O latency (800 bpi) */
#define MT_1600_WAIT      16                    /* I/O latency (1600 bpi) */

#define MT_MIN_WAIT       10                    /* Minimum I/O latency */
#define MT_REC_WAIT      100                    /* Magtape record latency */
#define MT_TM_WAIT       200                    /* Magtape write TM latency */
#define MT_EOP_WAIT      100                    /* Magtape EOP latency */

#define DP_XFER_WAIT     300                    /* Control info xfer time */
#define DP_SEEK_WAIT    2000                    /* Seek wait time */
#define DP_IO_WAIT      1000                    /* Sector I/O wait time */

#define CD_SEEK_WAIT    1100                    /* Seek wait time */
#define CD_IO_WAIT       800                    /* Sector I/O wait time */
#define CD_RTZS_WAIT     200                    /* Return to zero seek wait */

#define DRM_ACCESS_WAIT 5800                    /* Average access latency */
#define DRM_SECTOR_WAIT  350                    /* Sector I/O time */

#define DC_START_WAIT      4                    /* Startup delay */
#define DC_IO_WAIT         4                    /* I/O transfer delay */
#define DC_EOP_WAIT        5                    /* EOP delay */

#define RDR_IN_WAIT      200                    /* Card reader feed wait */
#define PUN_OUT_WAIT     200                    /* Card punch feed wait */

#endif
