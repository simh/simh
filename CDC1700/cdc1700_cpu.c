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

/* cdc1700_cpu.c: CDC1700 CPU simulator
 */

/*
 * Implementation notes:
 *
 * 1. Interrupts. There is very little technical details about the interrupt
 *    system available in the documentation. The following information has
 *    been deduced from the SMM diagnostic routines.
 *
 *     - Device interrupts
 *
 *       Device interrupts are level-triggered. A device driver may lower the
 *       the interrupt trigger by:
 *
 *        - Issue a "Clear Controller" command
 *        - Issue a "Clear Interrupts" command
 *        - Issue a device-dependent command
 *          (e.g. on PTP, output a new value)
 *
 *     - CPU interrupts (Power fail, parity and protect fault)
 *
 *       CPU interrupts are edge-triggered. The interrupt trigger is
 *       automatically lowered when the CPU starts processing interrupt 0.
 *
 * 2. Interrupts - undocumented feature
 *
 *    The 1704 and 1784 processor doucmentation has a section describing
 *    interrupt handling. There is a sub-section titled "Sharing subroutines
 *    between interrupt levels" which indicates that a subroutine such as:
 *
 *        SUBR    ADC     0
 *                IIN
 *                <code>
 *                EIN
 *                JMP*    (SUBR)
 *
 *    may be shared between interrupt levels. It include the text "Interrupts
 *    occuring after the execution of the RTJ are blocked because the IIN is
 *    executed. These interrupts are not recognized until after the jump is
 *    executed, because one instruction must be executed after an EIN before
 *    the interrupt system is active".
 *
 *    The implication of this is that interrupts must be deferred for one
 *    instruction following an RTJ. And indeed, deferring interrupts after
 *    an RTJ fixed a crash I was seeing on a customized version of MSOS 5.0.
 *
 * 3. There is no documention on relative timing. For example, the paper tape
 *    punch diagnostic enables Alarm+Data interrupts and assumes that it will
 *    be able to execute some number of instructions before the interrupt
 *    occurs. How many instructions should we delay if interrupts are enabled
 *    and all conditions are met to deliver the interrupt immediately?
 *
 * 4. Some peripherals, notably the teletypewriter, do not have a protected
 *    status bit. Does this mean that any application can directly affect
 *    them?
 *
 *      -  The teletypewriter may be addressed by either a protected or a
 *         nonprotected instruction (see SC17 Reference Manual).
 *
 * 5. The 1740/1742 line printer controllers are incorrectly documented as
 *    having the status register at offset 3, it is at offset 1 like all
 *    other peripherals.
 *
 * 6. For the 1738 disk pack controller, what is the correct response if an
 *    operation is initiated with no drive selected? For now, we'll reject
 *    the request.
 *
 * 7. For the 1706-A buffered data channel, what interrupt is used to signal
 *    "End of Operation"? A channel-specific interrupt or a pass-through
 *    interrupt from the device being controlled or some other?
 *
 * Instruction set evolution
 *
 * Over time the instruction set for the CDC 1700/Cyber 18 was extended in,
 * sometime incompatible, ways. The emulator will attempt to implement the
 * various discreet instructions sets that were available within the
 * constraints of the emulator environment:
 *
 * 1. Original
 *
 *      This was the original instruction set defined when the 1700 series
 *      was first released. The instruction set encoding wasted a number of
 *      bits (e.g. IIN, EIN, SPB and CPB each had 8 unused bits which were
 *      ignored during execution).
 *
 *      Character addressing was an optional extension to the 1774 (and maybe
 *      the 1714) which was enabled/disabled by new instructions which made
 *      use of the used bits of the IIN instruction. Note that the encoding
 *      of these instructions is incompatible with the enhanced instruction
 *      set (see below).
 *
 * 2. Basic
 *
 *      The basic instruction set is identical to the original instruction set
 *      but limits the encoding of the unused bits in some instructions. For
 *      example, IIN will only execute the IIN functionality if the low-order
 *      8 bits are encoded as zero, any other value will cause the instruction
 *      to execute as a NOP.
 *
 * 3. Enhanced (Unimplemented)
 *
 *      The enhanced instruction set makes use of the unused bits of the basic
 *      instruction set to add new functionality:
 *
 *              - Additional 4 registers
 *              - Character addressing mode
 *              - Field references
 *              - Multi-register save/restore
 *              - etc
 *
 */

#include "cdc1700_defs.h"

uint16 M[MAXMEMSIZE];
uint8 P[MAXMEMSIZE];

t_uint64 Instructions;
uint16 Preg, Areg, Qreg, Mreg, CAenable, OrigPreg, Pending, IOAreg, IOQreg;
uint16 R1reg, R2reg, R3reg, R4reg;
uint8 Pfault, Protected, lastP, Oflag, INTflag, DEFERflag;

t_bool ExecutionStarted = FALSE;
uint16 CharAddrMode[16];

uint16 INTlevel;

char INTprefix[8];

t_bool FirstRejSeen = FALSE;
uint32 CountRejects = 0;

t_bool FirstAddr = TRUE;

/*
 * Memory location holding MSOS5 system request routine address
 */
#define NMON    0x00F4

extern void MSOS5request(uint16, uint16);

extern int disassem(char *, uint16, t_bool, t_bool, t_bool);

extern enum IOstatus doIO(t_bool, DEVICE **);
extern void fw_init(void);
extern void VMinit(void);
extern void rebuildPending(void);

extern void dev1Interrupts(char *);

t_stat cpu_set_instr(UNIT *, int32, CONST char *, void *);
t_stat cpu_show_instr(FILE *, UNIT *, int32, CONST void *);

t_stat cpu_reset(DEVICE *);
t_stat cpu_set_size(UNIT *, int32, CONST char *, void *);
t_stat cpu_ex(t_value *, t_addr, UNIT *, int32);
t_stat cpu_dep(t_value, t_addr, UNIT *uptr, int32 sw);

t_stat cpu_help(FILE *, DEVICE *, UNIT *, int32, const char *);

#define UNIT_V_STOPSW   (UNIT_V_UF + 1)         /* Selective STOP switch */
#define UNIT_STOPSW     (1 << UNIT_V_STOPSW)
#define UNIT_V_SKIPSW   (UNIT_V_UF + 2)         /* Selective SKIP switch */
#define UNIT_SKIPSW     (1 << UNIT_V_SKIPSW)
#define UNIT_V_MODE65K  (UNIT_V_UF + 3)         /* 32K/65K mode switch */
#define UNIT_MODE65K    (1 << UNIT_V_MODE65K)
#define UNIT_V_CHAR     (UNIT_V_UF + 4)         /* Character addressing */
#define UNIT_CHAR       (1 << UNIT_V_CHAR)
#define UNIT_V_PROT     (UNIT_V_UF + 5)         /* Protect mode */
#define UNIT_PROT       (1 << UNIT_V_PROT)
#define UNIT_V_MSIZE    (UNIT_V_UF + 6)         /* Memory size */
#define UNIT_MSIZE      (1 << UNIT_V_MSIZE)

IO_DEVICE CPUdev = IODEV(NULL, "1714", CPU, 0, 0xFF, 0,
                         NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                         NULL, NULL, 0, 0, 0, 0, 0, 0, 0, 0, NULL);

/* CPU data structures

   cpu_dev      CPU device descriptor
   cpu_unit     CPU unit
   cpu_reg      CPU register list
   cpu_mod      CPU modifier list
*/
UNIT cpu_unit = { UDATA(NULL, UNIT_FIX+UNIT_BINK, DEFAULTMEMSIZE) };

REG cpu_reg[] = {
  { HRDATAD(P, Preg, 16, "Program address counter") },
  { HRDATAD(A, Areg, 16, "Principal arithmetic register") },
  { HRDATAD(Q, Qreg, 16, "Index register") },
  { HRDATAD(M, Mreg, 16, "Interrupt mask register") },
  { HRDATAD(O, Oflag, 1, "Overflow flag") },
  { HRDATAD(CH, CAenable, 1, "Character addressing enable flag") },
  { HRDATAD(INT, INTflag, 1, "Interrupt enable flag") },
  { HRDATAD(DEFER, DEFERflag, 1, "Interrupt deferred flag") },
  { HRDATAD(PENDING, Pending, 16, "Pending interrupt flags") },
  { HRDATAD(PFAULT, Pfault, 1, "Protect fault pending flag") },
  { NULL }
};

MTAB cpu_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0, "1714 CDC 1700 series CPU", NULL, NULL, NULL },
  { MTAB_XTD|MTAB_VDV, 0, "INSTR", "INSTR={ORIGINAL|BASIC|ENHANCED}",
    &cpu_set_instr, &cpu_show_instr, NULL, "Set CPU instruction set" },
  { UNIT_STOPSW, UNIT_STOPSW, "Selective Stop", "SSTOP",
    NULL, NULL, NULL, "Enable Selective Stop" },
  { UNIT_STOPSW, 0, "No Selective Stop", "NOSSTOP",
    NULL, NULL, NULL, "Disable Selective Stop" },
  { UNIT_SKIPSW, UNIT_SKIPSW, "Selective Skip", "SSKIP",
    NULL, NULL, NULL, "Enable Selective Skip" },
  { UNIT_SKIPSW, 0, "No Selective Skip", "NOSSKIP",
    NULL, NULL, NULL, "Disable Selective Skip" },
  { UNIT_MODE65K, UNIT_MODE65K, "65K Addressing Mode", "MODE65K",
    NULL, NULL, NULL, "Enable 65K Indirect Addressing Mode" },
  { UNIT_MODE65K, 0, "32K Addressing Mode", "MODE32K",
    NULL, NULL, NULL, "Enable 32K Indirect Addressing Mode" },
  { UNIT_CHAR, UNIT_CHAR, NULL, "CHAR",
    NULL, NULL, NULL, "Enable Character Addressing Extensions" },
  { UNIT_CHAR, 0, NULL, "NOCHAR",
    NULL, NULL, NULL, "Disable Character Addressing Extensions" },
  { UNIT_PROT, UNIT_PROT, "Program Protect", "PROTECT",
    NULL, NULL, NULL, "Enable Protect Mode Operation" },
  { UNIT_PROT, 0, "", "NOPROTECT",
    NULL, NULL, NULL, "Disable Protect Mode Operation" },
  { UNIT_MSIZE, 4096, NULL, "4K",
    &cpu_set_size, NULL, NULL, "Set Memory Size to 4KW" },
  { UNIT_MSIZE, 8192, NULL, "8K",
    &cpu_set_size, NULL, NULL, "Set Memory Size to 8KW" },
  { UNIT_MSIZE, 16384, NULL, "16K",
    &cpu_set_size, NULL, NULL, "Set Memory Size to 16KW" },
  { UNIT_MSIZE, 32768, NULL, "32K",
    &cpu_set_size, NULL, NULL, "Set Memory Size to 32KW" },
#if MAXMEMSIZE > 32768
  { UNIT_MSIZE, 65536, NULL, "64K",
    &cpu_set_size, NULL, NULL, "Set Memory Size to 64KW" },
#endif
  { 0 }
};

#define DBG_ALL \
  (DBG_DISASS | DBG_TRACE | DBG_TARGET | DBG_INPUT | DBG_OUTPUT | DBG_FULL)

DEBTAB cpu_deb[] = {
  { "DISASSEMBLE",  DBG_DISASS,    "Disassemble instructions while tracing" },
  { "IDISASSEMBLE", DBG_IDISASS,   "Disassemble while interrupts active" },
  { "INTERRUPT",    DBG_INTR,      "Display interrupt entry/exit" },
  { "TRACE",        DBG_TRACE,     "Trace instruction execution" },
  { "ITRACE",       DBG_ITRACE,    "Trace while interrupts active" },
  { "TARGET",       DBG_TARGET,    "Display target address of instructions" },
  { "INPUT",        DBG_INPUT,     "Display INP instruction execution" },
  { "OUTPUT",       DBG_OUTPUT,    "Display OUT instruction execution" },
  { "IO",           DBG_INPUT | DBG_OUTPUT, "Display INP and OUT execution" },
  { "INTLVL",       DBG_INTLVL,    "Add interrupt level to all displays" },
  { "PROTECT",      DBG_PROTECT,   "Display protect faults" },
  { "MISSING",      DBG_MISSING,   "Display info about missing devices" },
  { "ENHANCED",     DBG_ENH,       "Display enh. instructions in basic mode" },
  { "MSOS5",        DBG_MSOS5,     "Display MSOS5 requests" },
  { "FULL",         DBG_ALL },
  { NULL }
};

DEVICE cpu_dev = {
  "CPU", &cpu_unit, cpu_reg, cpu_mod,
  1, 16, 16, 1, 16, 16,
  &cpu_ex, &cpu_dep, &cpu_reset,
  NULL, NULL, NULL,
  &CPUdev,
  DEV_DEBUG | DEV_NOEQUIP, 0, cpu_deb,
  NULL, NULL, &cpu_help, NULL, NULL, NULL
};

/*
 * Table of instructions which store to memory
 */
static t_bool storagemode[] = {
  FALSE, FALSE, FALSE, FALSE,                   /* SPECIAL, JMP, MUI, DVI */
  TRUE, FALSE, TRUE, TRUE,                      /* STQ, RTJ, STA, SPA */
  FALSE, FALSE, FALSE, FALSE,                   /* ADD, SUB, AND, EOR */
  FALSE, TRUE, FALSE, FALSE                     /* LDA, RAO, LDQ, ADQ */
};

/*
 * Table of parity values
 */
static uint8 parity[256] = {
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
  0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0
};

/*
 * Table of interrupt bits
 */
static uint16 interruptBit[] = {
  0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
  0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000
};

t_stat cpu_set_instr(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  if (!cptr)
    return SCPE_IERR;

  if (!strcmp(cptr, "ORIGINAL")) {
    INSTR_SET = INSTR_ORIGINAL;
  } else if (!strcmp(cptr, "BASIC")) {
    INSTR_SET = INSTR_BASIC;
  } else if (!strcmp(cptr, "ENHANCED")) {
    INSTR_SET = INSTR_ENHANCED;
  } else return SCPE_ARG;

  return SCPE_OK;
}

t_stat cpu_show_instr(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  switch (INSTR_SET) {
    case INSTR_ORIGINAL:
      fprintf(st, "\n\tOriginal instruction set");
      if ((cpu_unit.flags & UNIT_CHAR) != 0)
        fprintf(st, " + character addressing");
      break;

    case INSTR_BASIC:
      fprintf(st, "\n\tBasic instruction set");
      break;

    case INSTR_ENHANCED:
      fprintf(st, "\n\tEnhanced instruction set (Unimplemented)");
      break;

    default:
      return SCPE_IERR;
  }
  return SCPE_OK;
}

/*
 * Reset routine
 */
t_stat cpu_reset(DEVICE *dptr)
{
  int i;

  INTlevel = 0;
  CAenable = 0;

  Pending = 0;

  fw_init();

  sim_brk_types = sim_brk_dflt = SWMASK('E');
  Pfault = FALSE;

  FirstRejSeen = FALSE;
  CountRejects = 0;

  /*
   * Reset the saved character addressing mode for each interrupt level.
   */
  for (i = 0; i < 16; i++)
    CharAddrMode[i] = 0;

  VMinit();

  return SCPE_OK;
}

/*
 * Memory size change
 */
t_stat cpu_set_size(UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  uint16 mc = 0;
  uint32 i;

  if ((val <= 0) || (val > MAXMEMSIZE))
    return SCPE_ARG;

  for (i = val; i < cpu_unit.capac; i++)
    mc |= M[i];

  if ((mc != 0) && (!get_yn("Really truncate memory [N]?", FALSE)))
    return SCPE_OK;

  cpu_unit.capac = val;
  for (i = cpu_unit.capac; i < MAXMEMSIZE; i++)
    M[i] = 0;
  return SCPE_OK;
}

/*
 * Memory examine
 */
t_stat cpu_ex(t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
  if (addr >= cpu_unit.capac)
    return SCPE_NXM;
  if (vptr != NULL)
    *vptr = M[addr];
  return SCPE_OK;
}

/*
 * Memory deposit
 */
t_stat cpu_dep(t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
  if (addr >= cpu_unit.capac)
    return SCPE_NXM;
  M[addr] = TRUNC16(val);
  return SCPE_OK;
}

/*
 * Dump the current register contents on debugging output.
 */
void dumpRegisters(void)
{
  fprintf(DBGOUT,
          "%s[A: %04X, Q: %04X, M: %04X, Ovf: %d, Pfault: %d, I: %d, D: %d]",
          INTprefix, Areg, Qreg, Mreg, Oflag, Pfault, INTflag, DEFERflag);
}

/*
 * Indicate processor is running in protected mode.
 */
t_bool inProtectedMode(void)
{
  return (cpu_unit.flags & UNIT_PROT) != 0;
}

/*
 * Returns CPU interrupt status. This always returns 0 since the interrupt
 * has already been set in the Pending register.
 */
uint16 cpuINTR(DEVICE *dptr)
{
  return 0;
}

/*
 * Raise an internal interrupt. Used for Power Fail, Parity Error and
 * Program Protect Fault. Only Program Protect Fault can occur in emulation.
 */
void RaiseInternalInterrupt(void)
{
  if ((cpu_dev.dctrl & DBG_INTR) != 0) {
    fprintf(DBGOUT,
            "%sINT(0)[A: %04X, Q: %04X, M: %04X, Ovf: %d, Pfault: %d, I: %d, D: %d]\r\n",
            INTprefix, Areg, Qreg, Mreg, Oflag, Pfault, INTflag, DEFERflag);
  }
  Pending |= 1;
}

/*
 * Raise an external interrupt associated with a peripheral device.
 */
void RaiseExternalInterrupt(DEVICE *dev)
{
  IO_DEVICE *iod = IODEVICE(dev);
  uint16 Opending = Pending;

  /*
   * Don't touch the STATUS register if the device has completely
   * non-standard interrupts.
   */
  if (iod->iod_raised == NULL)
    iod->STATUS |= IO_ST_INT;

  rebuildPending();

  if ((cpu_dev.dctrl & DBG_INTR) != 0) {
    uint16 level = iod->iod_equip;

    fprintf(DBGOUT,
            "%sINT(%d, %s)[A: %04X, Q: %04X, M: %04X, P: %04x->%04x, Ovf: %d, I: %d, D: %d]\r\n",
            INTprefix, level, dev->name, Areg, Qreg, Mreg, Opending, Pending,
            Oflag, INTflag, DEFERflag);
  }
}

/*
 * Memory reference routines
 */

/*
 * Reads are always allowed
 */
uint16 LoadFromMem(uint16 addr)
{
  return M[MEMADDR(addr)];
}

/*
 * Writes require checking for protected mode. This routine returns TRUE
 * if the write succeeded and FALSE if the write failed and an interrupt
 * has been scheduled.
 */
t_bool StoreToMem(uint16 addr, uint16 value)
{
  if (inProtectedMode()) {
    if (!Protected) {
      if (P[MEMADDR(addr)]) {
        if ((cpu_dev.dctrl & DBG_PROTECT) != 0) {
          fprintf(DBGOUT,
                  "%sProtect fault storing to memory at %04x => %04X\r\n",
                  INTprefix, OrigPreg, addr);
        }
        Pfault = TRUE;
        RaiseInternalInterrupt();
        return FALSE;
      }
    }
  }
  M[MEMADDR(addr)] = value;
  return TRUE;
}

/*
 * I/O devices can maintain their own protected status. Perform similar
 * checking as StoreToMem() using the device protected status but do not
 * generate a "protect fault" since the error will be reported back through
 * the device status. Return TRUE if the write succeeded and FALSE if the
 * write failed due to a protect failure.
 */
t_bool IOStoreToMem(uint16 addr, uint16 value, t_bool prot)
{
  if (inProtectedMode()) {
    if (!prot) {
      if (P[MEMADDR(addr)]) {
        return FALSE;
      }
    }
  }
  M[MEMADDR(addr)] = value;
  return TRUE;
}

/*
 * The 1700 adder is a 16-bit one's complement subtractive adder which
 * eliminates minus zero in all but one case (the only case is when minus zero
 * is added to minus zero).
 */
uint16 doSUB(uint16 a, uint16 b)
{
  uint32 ea = EXTEND16(a);
  uint32 eb = EXTEND16(b);
  uint32 result = ea - eb;

  if (((a - b) & 0x10000) != 0)
    result -= 1;

  if (((result & 0x18000) != 0x18000) &&
      ((result & 0x18000) != 0x00000))
    Oflag = 1;

  return TRUNC16(result);
}
uint16 doADD(uint16 a, uint16 b)
{
  return doSUB(a, TRUNC16(~b));
}

/*
 * Internal operations such as address computations do not modify the
 * overflow flag.
 */
uint16 doADDinternal(uint16 a, uint16 b)
{
  uint32 result = a - TRUNC16(~b);

  if ((result & 0x10000) != 0)
    result -= 1;

  return TRUNC16(result);
}

/*
 * For multiply, we do the actual multiply in the positive domain and adjust
 * the resulting sign based on the input values.
 */
void doMUL(uint16 a)
{
  uint32 val1, result = 0;
  uint16 sign = Areg ^ a;
  int i;

  val1 = ABS(Areg) & 0xFFFF;
  a = ABS(a);

  /*
   * Accumulate the result via shift and add.
   */
  for (i = 0; i < 15; i++) {
    if ((a & 1) != 0)
      result += val1;
    val1 <<= 1;
    a >>= 1;
  }

  if ((sign & SIGN) != 0)
    result = ~result;

  Qreg = result >> 16;
  Areg = TRUNC16(result);
}

/*
 * For divide, we once again do the actual division in the positive domain
 * and adjust the resulting signs based on the input values.
 */
void doDIV(uint16 a)
{
  uint32 result = 0, divisor, remainder = (Qreg << 16) | Areg;
  uint32 mask = 1;
  uint8 sign = 0, rsign = 0;

  if ((Qreg & SIGN) != 0) {
    remainder = ~remainder;
    sign++;
    rsign++;
  }

  divisor = ABS(a) & 0xFFFF;

  if ((a & SIGN) != 0)
    sign++;

  /*
   * Handle divide by 0 (plus or minus) as documented in the 1784 reference
   * manual.
   */
  if (divisor == 0) {
    Oflag = 1;
    Qreg = Areg;
    Areg = (sign & 1) != 0 ? 0 : ~0;
    return;
  }

  /*
   * Special case check for zero dividend.
   */
  if (remainder == 0) {
    Areg = Qreg = 0;

    if ((sign & 1) != 0)
      Areg = ~Areg;
    if (rsign)
      Qreg = ~Qreg;
    return;
  }

  while (divisor < remainder) {
    divisor <<= 1;
    mask <<= 1;
  }

  do {
    if (remainder >= divisor) {
      remainder -= divisor;
      result += mask;
    }
    divisor >>= 1;
    mask >>= 1;
  } while (mask != 0);

  /*
   * Again the documentation does not specify whether the result/remainder
   * can be negative zero. For now I'm going to assume that they cannot.
   */
  if ((result & 0xFFFF8000) != 0)
    Oflag = 1;

  if ((sign & 1) != 0)
    result = ~result;

  if (rsign != 0)
    remainder = ~remainder;

  Areg = TRUNC16(result);
  Qreg = TRUNC16(remainder);
}

/*
 * Compute the effective address of an instruction
 */
t_stat getEffectiveAddr(uint16 p, uint16 instr, uint16 *addr)
{
  uint16 count = MAXINDIRECT;
  uint16 delta = instr & OPC_ADDRMASK;
  uint32 result = delta;

  if (delta == 0) {
    result = Preg;
    INCP;

    switch (instr & (MOD_RE | MOD_IN)) {
      /*
       * Mode 0, delta == 0 does not follow the regular addressing model
       * of the other modes.
       */
      case 0:
        if (!storagemode[(instr & OPC_MASK) >> 12] ||
            ((instr & (MOD_I1 | MOD_I2)) != 0))
          result = LoadFromMem(result);
        break;

      case MOD_RE:
        result = doADDinternal(result, LoadFromMem(result));
        break;

      case MOD_RE | MOD_IN:
        result = doADDinternal(result, LoadFromMem(result));
        /* FALLTHROUGH */

      case MOD_IN:
        result = LoadFromMem(result);

        if ((cpu_unit.flags & UNIT_MODE65K) == 0) {
          while (result & 0x8000) {
            if (--count == 0)
              return SCPE_LOOP;
            result = LoadFromMem(result & 0x7FFF);
          }
        }
        break;
    }
  } else {
    switch (instr & (MOD_RE | MOD_IN)) {
    case 0:
      break;

    case MOD_RE:
      result = doADDinternal(EXTEND8(result), p);
      break;

    case MOD_RE | MOD_IN:
      result = doADDinternal(EXTEND8(result), p);
      /* FALLTHROUGH */

    case MOD_IN:
      result = LoadFromMem(result);

      if ((cpu_unit.flags & UNIT_MODE65K) == 0) {
        while (result & 0x8000) {
          if (--count == 0)
            return SCPE_LOOP;
          result = LoadFromMem(result & 0x7FFF);
        }
      }
      break;
    }
  }

  /*
   * Handle indexing
   */
  if ((instr & MOD_I1) != 0)
    result = doADDinternal(result, Qreg);
  if ((instr & MOD_I2) != 0)
    result = doADDinternal(result, LoadFromMem(0xFF));

  *addr = result;
  return SCPE_OK;
}

/*
 * Similar effective address calculation routines without modifying the
 * CPU registers.
 */

/*
 * Compute the effective address of an instruction
 */
t_stat disEffectiveAddr(uint16 p, uint16 instr, uint16 *base, uint16 *addr)
{
  uint16 count = MAXINDIRECT;
  uint16 delta = instr & OPC_ADDRMASK;
  uint32 result = delta;

  if (delta == 0) {
    result = MEMADDR(p + 1);

    switch (instr & (MOD_RE | MOD_IN)) {
      case 0:
        if ((instr & (MOD_I1 | MOD_I2)) != 0)
          result = LoadFromMem(result);
        break;

      case MOD_RE:
        result = doADDinternal(result, LoadFromMem(result));
        break;

      case MOD_RE | MOD_IN:
        result = doADDinternal(result, LoadFromMem(result));
        /* FALLTHROUGH */

      case MOD_IN:
        result = LoadFromMem(result);

        if ((cpu_unit.flags & UNIT_MODE65K) == 0) {
          while (result & 0x8000) {
            if (--count == 0)
              return SCPE_LOOP;
            result = LoadFromMem(result & 0x7FFF);
          }
        }
        break;
    }
  } else {
    switch (instr & (MOD_RE | MOD_IN)) {
      case 0:
        break;

      case MOD_RE:
        result = doADDinternal(EXTEND8(result), p);
        break;

      case MOD_RE | MOD_IN:
        result = doADDinternal(EXTEND8(result), p);
        /* FALLTHROUGH */

      case MOD_IN:
        result = LoadFromMem(result);

        if ((cpu_unit.flags & UNIT_MODE65K) == 0) {
          while (result & 0x8000) {
            if (--count == 0)
              return SCPE_LOOP;
            result = LoadFromMem(result & 0x7FFF);
          }
        }
        break;
    }
  }

  *base = result;

  /*
   * Handle indexing
   */
  if ((instr & MOD_I1) != 0)
    result = doADDinternal(result, Qreg);
  if ((instr & MOD_I2) != 0)
    result = doADDinternal(result, LoadFromMem(0xFF));

  *addr = result;
  return SCPE_OK;
}

/*
 * Execute a single instruction on the current CPU. Register P must be
 * pointing at the instruction to execute.
 */
t_stat executeAnInstruction(void)
{
  DEVICE *dev;
  uint16 instr, operand, operand1, operand2, from;
  uint32 temp;
  t_stat status;

  INTprefix[0] = '\0';

  if ((cpu_dev.dctrl & DBG_INTLVL) != 0)
    sprintf(INTprefix, "%02d> ", INTlevel);

  if (INTflag && !DEFERflag) {
    if ((operand = Pending & Mreg) != 0) {
      int i, maxIntr = INTR_1705;

      for (i = 0; i < maxIntr; i++) {
        if ((operand & interruptBit[i]) != 0) {
          operand1 = INTERRUPT_BASE + (4 * i);
          operand2 = from = Preg;
          if ((cpu_unit.flags & UNIT_MODE65K) == 0) {
            operand2 = (operand2 & 0x7FFF) | (Oflag ? 0x8000 : 0);
            Oflag = 0;
          }

          Protected = TRUE;
          StoreToMem(operand1, operand2);
          Preg = operand1 + 1;
          INTflag = 0;
          INTlevel++;

          if ((cpu_unit.flags & UNIT_CHAR) != 0) {
            CharAddrMode[i] = CAenable;
            CAenable = 0;
          }

          if (FirstRejSeen) {
            fprintf(DBGOUT,
                    "%s %u Rejects terminated by interrupt\r\n",
                    INTprefix, CountRejects);
            FirstRejSeen = FALSE;
            CountRejects = 0;
          }

          if ((cpu_dev.dctrl & DBG_INTR) != 0) {
            if (i == 1) {
              char intbuf[32];
              char *buf = &intbuf[0];

              dev1Interrupts(buf);
              if (buf[0] == ' ')
                buf++;

              fprintf(DBGOUT,
                      "%s===> Device 1 Stations [%s]\n",
                      INTprefix, buf);
            }
            fprintf(DBGOUT,
                    "%s===> Interrupt %d entered at 0x%04X, from %04X, Inst: %llu\r\n",
                    INTprefix, i, Preg, from, Instructions);
          }

          if (i == 0)
            Pending &= 0xFFFE;

          if ((cpu_dev.dctrl & DBG_INTLVL) != 0)
            sprintf(INTprefix, "%02d> ", INTlevel);

          if (sim_brk_summ && sim_brk_test(Preg, SWMASK('E'))) {
            /*
             * This was not really an instruction execution.
             */
            sim_interval++;
            return SCPE_IBKPT;
          }
          break;
        }
      }
    }
  }

  DEFERflag = 0;

  if (((cpu_dev.dctrl & DBG_TRACE) != 0) ||
      (((cpu_dev.dctrl & DBG_ITRACE) != 0) && (INTlevel != 0))) {
    fprintf(DBGOUT, 
            "%sA:%04X Q:%04X I:%04X M:%04X Ovf:%d Pfault: %d Inst:%llu\r\n",
            INTprefix, Areg, Qreg, LoadFromMem(0xFF),
            Mreg, Oflag, Pfault, Instructions);
  }

  if (((cpu_dev.dctrl & DBG_DISASS) != 0) ||
      (((cpu_dev.dctrl & DBG_IDISASS) != 0) && (INTlevel != 0))) {
    char buf[128];
    t_bool target = (cpu_dev.dctrl & DBG_TARGET) != 0;

    disassem(buf, Preg, TRUE, target, TRUE);
    fprintf(DBGOUT, "%s%s\r\n", INTprefix, buf);
  }

  /*
   * Get the next instruction, moving the current PC to the next word address.
   * We need to save the PC of the current instruction so that we can pass it
   * into the operand calculation routine(s).
   */
  OrigPreg = Preg;
  lastP = Protected;
  Protected = P[MEMADDR(OrigPreg)];

  instr = LoadFromMem(OrigPreg);
  INCP;

  /*
   * Check for protected mode operation where we are about to execute a
   * protected instruction and the previous instruction was unprotected.
   */
  if (inProtectedMode()) {
    if (!lastP && Protected) {
      if ((cpu_dev.dctrl & DBG_PROTECT) != 0) {
        fprintf(DBGOUT,
                "%sProtect fault, protected after unprotected at %04X\r\n",
                INTprefix, OrigPreg);
      }
      Pfault = TRUE;
      RaiseInternalInterrupt();

      /*
       * The exact semantics of a protected fault are not documented in any
       * the hardware references. The code in this simulator was created
       * by examining the source code of MSOS 5.0. In the case of a 2 word
       * instruction causing the trap, P is left pointing at the second word
       * of the instruction. Note that the SMM diagnostics do not check for
       * this case.
       */

      /* 
       * Execute this instruction as an unprotected Selective Stop. If a
       * stop occurs, P may not point to a valid instruction (see above).
       * A subsequent "continue" command will cause a trap to the protect
       * fault processor.
       */
      if ((cpu_unit.flags & UNIT_STOPSW) != 0) {
        dumpRegisters();
        return SCPE_SSTOP;
      }
      return SCPE_OK;
    }
  }

  Instructions++;

  switch (instr & OPC_MASK) {
    case OPC_ADQ:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      if (!ISCONSTANT(instr))
        operand = LoadFromMem(operand);
      Qreg = doADD(Qreg, operand);
      break;

    case OPC_LDQ:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      if (!ISCONSTANT(instr))
        operand = LoadFromMem(operand);
      Qreg = operand;
      break;

    case OPC_RAO:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      StoreToMem(operand, doADD(LoadFromMem(operand), 1));
      break;

    case OPC_LDA:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      if (!ISCONSTANT(instr))
        operand = LoadFromMem(operand);

      if ((cpu_unit.flags & UNIT_CHAR) != 0) {
        uint16 xxx = operand;
        if (CAenable != 0) {
          if ((LoadFromMem(0xFF) & 0x01) == 0)
            operand >>= 8;
          operand = (Areg & 0xFF00) | (operand & 0xFF);
          fprintf(DBGOUT,
                  "CM LDA at P: %04X, A: %04X, I: %04X, SRC: %04X, Result: %04X\r\n",
                  OrigPreg, Areg, LoadFromMem(0xFF), xxx, operand);
        }
      }
      Areg = operand;      
      break;

    case OPC_EOR:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      if (!ISCONSTANT(instr))
        operand = LoadFromMem(operand);
      Areg ^= operand;
      break;

    case OPC_AND:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      if (!ISCONSTANT(instr))
        operand = LoadFromMem(operand);
      Areg &= operand;
      break;

    case OPC_SUB:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      if (!ISCONSTANT(instr))
        operand = LoadFromMem(operand);
      Areg = doSUB(Areg, operand);
      break;

    case OPC_ADD:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      if (!ISCONSTANT(instr))
        operand = LoadFromMem(operand);
      Areg = doADD(Areg, operand);
      break;
      
    case OPC_SPA:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      if (StoreToMem(operand, Areg)) {
        temp = parity[Areg & 0xFF] + parity[(Areg >> 8) & 0xFF];
        if ((temp & 1) != 0)
          Areg = 0;
        else Areg = 1;
      }
      break;

    case OPC_STA:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;

      if ((cpu_unit.flags & UNIT_CHAR) != 0) {
        if (CAenable != 0) {
          operand1 = LoadFromMem(operand);
          if ((LoadFromMem(0xFF) & 0x01) == 0)
            operand1 = (operand1 & 0xFF) | ((Areg << 8) & 0xFF00);
          else operand1 = (operand1 & 0xFF00) | (Areg & 0xFF);
          StoreToMem(operand, operand1);
          break;
        }
      }
      StoreToMem(operand, Areg);
      break;

    case OPC_RTJ:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      StoreToMem(operand, Preg);
      Preg = operand;
      INCP;
      DEFERflag = 1;
      break;

    case OPC_STQ:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      StoreToMem(operand, Qreg);
      break;
    
    case OPC_DVI:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      if (!ISCONSTANT(instr))
        operand = LoadFromMem(operand);
      doDIV(operand);
      break;

    case OPC_MUI:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      if (!ISCONSTANT(instr))
        operand = LoadFromMem(operand);
      doMUL(operand);
      break;

    case OPC_JMP:
      if ((status = getEffectiveAddr(OrigPreg, instr, &operand)) != SCPE_OK)
        return status;
      Preg = operand;
      break;
      
    case OPC_SPECIAL:
      switch (instr & OPC_SPECIALMASK) {
        case OPC_SLS:
          switch (INSTR_SET) {
            case INSTR_BASIC:
              if ((instr & OPC_MODMASK) != 0) {
                if ((cpu_dev.dctrl & DBG_ENH) != 0)
                  fprintf(DBGOUT, "%s Possible Enh. Instruction (%04X) at %04x\r\n",
                          INTprefix, instr, OrigPreg);
              }
              /* FALLTHROUGH */

            case INSTR_ORIGINAL:
              if ((cpu_unit.flags & UNIT_STOPSW) != 0) {
                dumpRegisters();
                return SCPE_SSTOP;
              }
              break;

            case INSTR_ENHANCED:
              if ((instr & OPC_MODMASK) == 0) {
                if ((cpu_unit.flags & UNIT_STOPSW) != 0) {
                  dumpRegisters();
                  return SCPE_SSTOP;
                }
                break;
              }
              Preg = OrigPreg;
              return SCPE_UNIMPL;
              break;
          }
          break;

        case OPC_SKIPS:
          switch (instr & (OPC_SKIPS | OPC_SKIPMASK)) {
            case OPC_SAZ:
              if (Areg == 0)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              break;

            case OPC_SAN:
              if (Areg != 0)    
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              break;
              
            case OPC_SAP:
              if ((Areg & SIGN) == 0)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              break;

            case OPC_SAM:
              if ((Areg & SIGN) != 0)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              break;

            case OPC_SQZ:
              if (Qreg == 0)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              break;
              
            case OPC_SQN:
              if (Qreg != 0)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              break;

            case OPC_SQP:
              if ((Qreg & SIGN) == 0)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              break;

            case OPC_SQM:
              if ((Qreg & SIGN) != 0)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              break;

            case OPC_SWS:
              if ((cpu_unit.flags & UNIT_SKIPSW) != 0)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              break;

            case OPC_SWN:
              if ((cpu_unit.flags & UNIT_SKIPSW) == 0)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              break;

            case OPC_SOV:
              if (Oflag)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              Oflag = 0;
              break;

            case OPC_SNO:
              if (!Oflag)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              Oflag = 0;
              break;

              /*
               * The emulator does not generate/check storage parity, so these
               * skips always operate as though parity is valid.
               */
            case OPC_SPE:
              break;

            case OPC_SNP:
              Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              break;

            case OPC_SPF:
              if (Pfault)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              Pfault = FALSE;
              rebuildPending();
              break;

            case OPC_SNF:
              if (!Pfault)
                Preg = doADDinternal(Preg, instr & OPC_SKIPCOUNT);
              Pfault = FALSE;
              rebuildPending();
              break;
          }
          break;

        case OPC_INP:
          if ((cpu_dev.dctrl & DBG_INPUT) != 0)
            if (!FirstRejSeen)
              fprintf(DBGOUT,
                      "%sINP:[A: %04X, Q: %04X, M: %04X, Ovf: %d, I: %d, D: %d]\r\n",
                      INTprefix, Areg, Qreg, Mreg, Oflag, INTflag, DEFERflag);

          switch (doIO(FALSE, &dev)) {
            case IO_REPLY:
              if (FirstRejSeen) {
                fprintf(DBGOUT,
                        "%s %u Rejects terminated by a Reply\r\n",
                        INTprefix, CountRejects);
                FirstRejSeen = FALSE;
                CountRejects = 0;
              }
              if ((cpu_dev.dctrl & DBG_INPUT) != 0)
                fprintf(DBGOUT, "%sINP: ==> REPLY, A: %04X\r\n",
                        INTprefix, Areg);
              break;
              
            case IO_REJECT:
              if ((cpu_dev.dctrl & DBG_INPUT) != 0)
                if (!FirstRejSeen)
                  fprintf(DBGOUT, "%sINP: ==> REJECT\r\n", INTprefix);
              Preg = doADDinternal(Preg, EXTEND8(instr & OPC_MODMASK));
              if ((dev != NULL) && ((dev->flags & DEV_REJECT) != 0))
                return SCPE_REJECT;

              /*
               * Check if reject forces the instruction to restart. If so,
               * reduce a sequence of Reject logs into a single entry.
               */
              if (Preg == OrigPreg) {
                if ((dev  != NULL) && ((dev->dctrl & DBG_DFIRSTREJ) != 0)) {
                  if (!FirstRejSeen) {
                    FirstRejSeen = TRUE;
                    CountRejects = 1;
                  }
                } else CountRejects++;
              }
              break;

            case IO_INTERNALREJECT:
              if ((cpu_dev.dctrl & DBG_INPUT) != 0)
                fprintf(DBGOUT, "%sINP: ==> INTERNALREJECT\r\n", INTprefix);
              Preg = doADDinternal(OrigPreg, EXTEND8(instr & OPC_MODMASK));
              if ((dev != NULL) && ((dev->flags & DEV_REJECT) != 0))
                return SCPE_REJECT;
              break;
          }
          break;

        case OPC_OUT:
          if ((cpu_dev.dctrl & DBG_OUTPUT) != 0)
            if (!FirstRejSeen)
              fprintf(DBGOUT,
                      "%sOUT:[A: %04X, Q: %04X, M: %04X, Ovf: %d, I: %d, D: %d]\r\n",
                      INTprefix, Areg, Qreg, Mreg, Oflag, INTflag, DEFERflag);

          switch (doIO(TRUE, &dev)) {
            case IO_REPLY:
              if (FirstRejSeen) {
                fprintf(DBGOUT,
                        "%s %u Rejects terminated by a Reply\r\n",
                        INTprefix, CountRejects);
                FirstRejSeen = FALSE;
                CountRejects = 0;
              }
              if ((cpu_dev.dctrl & DBG_OUTPUT) != 0)
                fprintf(DBGOUT, "%sOUT: ==> REPLY\r\n", INTprefix);
              break;

            case IO_REJECT:
              if ((cpu_dev.dctrl & DBG_OUTPUT) != 0)
                fprintf(DBGOUT, "%sOUT: ==> REJECT\r\n", INTprefix);
              Preg = doADDinternal(Preg, EXTEND8(instr & OPC_MODMASK));
              if ((dev != NULL) && ((dev->flags & DEV_REJECT) != 0))
                return SCPE_REJECT;

              /*
               * Check if reject forces the instruction to restart. If so,
               * reduce a sequence of Reject logs into a single entry.
               */
              if (Preg == OrigPreg) {
                if ((dev  != NULL) && ((dev->dctrl & DBG_DFIRSTREJ) != 0)) {
                  if (!FirstRejSeen) {
                    FirstRejSeen = TRUE;
                    CountRejects = 1;
                  }
                } else CountRejects++;
              }
              break;

            case IO_INTERNALREJECT:
              if ((cpu_dev.dctrl & DBG_OUTPUT) != 0)
                fprintf(DBGOUT, "%sOUT: ==> INTERNALREJECT\r\n", INTprefix);
              Preg = doADDinternal(OrigPreg, EXTEND8(instr & OPC_MODMASK));
              if ((dev != NULL) && ((dev->flags & DEV_REJECT) != 0))
                return SCPE_REJECT;
              break;
          }
          break;

          /*
           * EIN, IIN, SPB and CPB operate differently depending on the
           * currently selected instruction set.
           */
        case OPC_IIN:
        case OPC_EIN:
        case OPC_SPB:
        case OPC_CPB:
          switch (INSTR_SET) {
            case INSTR_ORIGINAL:
              /*
               * Character addressing enable/disable is only available as
               * an extension to the original instruction set.
               */
              if ((instr & OPC_SPECIALMASK) == OPC_IIN) {
                if ((cpu_unit.flags & UNIT_CHAR) != 0) {
                  if ((instr & OPC_MODMASK) != 0) {
                    switch (instr) {
                      case OPC_ECA:
                        CAenable = 1;
                        break;

                      case OPC_DCA:
                        CAenable = 0;
                        break;
                    }
                    goto done;
                  }
                }
              }
              break;

            case INSTR_BASIC:
              if ((instr & OPC_MODMASK) != 0) {
                if ((cpu_dev.dctrl & DBG_ENH) != 0)
                  fprintf(DBGOUT, "%s Possible Enh. Instruction (%04X) at %04x\r\n",
                          INTprefix, instr, OrigPreg);
              }
              break;

            case INSTR_ENHANCED:
              if ((instr & OPC_MODMASK) != 0) {
                Preg = OrigPreg;
                return SCPE_UNIMPL;
              }
              break;
          }
          /* FALLTHROUGH */

          /*
           * The following instructions (EIN, IIN, SPB, CPB and EXI)
           * generate a protect fault if the protect switch is set and
           * the instruction is not protected. If the system is unable
           * to handle the interrupt (interrupts disabled or interrupt 0
           * masked), the instruction executes as a "Selective Stop".
           */
        case OPC_EXI:
          if (inProtectedMode()) {
            if (!Protected) {
              if ((cpu_dev.dctrl & DBG_PROTECT) != 0) {
                fprintf(DBGOUT,
                        "%sProtect fault EIN/SPB/CPB/EXI at %04X\r\n",
                        INTprefix, OrigPreg);
              }
              Pfault = TRUE;
              RaiseInternalInterrupt();

              /*
               * Execute this instruction as though it was a "Selective Stop".
               */
              if ((cpu_unit.flags & UNIT_STOPSW) != 0) {
                dumpRegisters();
                return SCPE_SSTOP;
              }
              break;
            }
          }

          /*
           * Execute the instruction.
           */
          switch (instr & OPC_SPECIALMASK) {
            case OPC_EIN:
              if ((cpu_dev.dctrl & DBG_INTR) != 0) {
                fprintf(DBGOUT,
                        "%sEIN:[A: %04X, Q: %04X, M: %04X, Ovf: %d, I: %d, D: %d]\r\n",
                        INTprefix, Areg, Qreg, Mreg, Oflag, INTflag, DEFERflag);
              }

              INTflag = DEFERflag = 1;
              break;

          case OPC_IIN:
            if ((cpu_dev.dctrl & DBG_INTR) != 0) {
              fprintf(DBGOUT,
                      "%sIIN:[A: %04X, Q: %04X, M: %04X, Ovf: %d, I: %d, D: %d]\r\n",
                      INTprefix, Areg, Qreg, Mreg, Oflag, INTflag, DEFERflag);
            }

            /*
             * Check for MSOS5 system requests. If we are executing the first
             * instruction of the MSOS5 request processor (which is also an
             * IIN instruction), dump information about the current request.
             * This test will work correctly independent of whether a 1 or 2
             * word RTJ is used to call the request processor.
             */
            if ((cpu_dev.dctrl & DBG_MSOS5) != 0) {
              if (OrigPreg == (M[NMON] + 1))
                MSOS5request(M[M[NMON]], 0);
            }

            INTflag = 0;
          done:
            break;

          case OPC_SPB:
            SETPROTECT(Qreg);
            break;

          case OPC_CPB:
            CLRPROTECT(Qreg);
            break;

          case OPC_EXI:
            operand = instr & OPC_MODMASK;
            if ((operand & 0xC3) != 0) {
              Preg = OrigPreg;
              return SCPE_INVEXI;
            }

            if ((cpu_dev.dctrl & DBG_INTR) != 0)
              fprintf(DBGOUT, "%s<=== Interrupt %d exit [M: %04X]\r\n", 
                      INTprefix, (operand >> 2) & 0xF, Mreg);

            Preg = operand2 = LoadFromMem(INTERRUPT_BASE + operand);
            if ((cpu_unit.flags & UNIT_MODE65K) == 0) {
              Preg &= 0x7FFF;
              Oflag = operand2 & 0x8000 ? 1 : 0;
            }
            if (INTlevel != 0)
              INTlevel--;
            INTflag = 1;

            if ((cpu_unit.flags & UNIT_CHAR) != 0) {
              CAenable = CharAddrMode[(operand >> 2) & 0xF];
              CharAddrMode[(operand >> 2) & 0xF] = 0;
            }
            break;
          }
          break;

        case OPC_INTER:
          /*
           * Protection fault if the instruction is not protected and 
           * modifies M
           */
          if (inProtectedMode()) {
            if ((instr & MOD_D_M) != 0) {
              if (!Protected) {
                if ((cpu_dev.dctrl & DBG_PROTECT) != 0) {
                  fprintf(DBGOUT,
                          "%sProtect fault INTER to M at %04X\r\n",
                          INTprefix, OrigPreg);
                }
                Pfault = TRUE;
                RaiseInternalInterrupt();

                /*
                 * Execute the instruction as a "Selective Stop".
                 */
                if ((cpu_unit.flags & UNIT_STOPSW) != 0) {
                  dumpRegisters();
                  return SCPE_SSTOP;
                }
                break;
              }
            }
          }
          operand1 = instr & MOD_O_A ? Areg : 0xFFFF;
          switch (instr & (MOD_O_Q | MOD_O_M)) {
            case 0:
              operand2 = 0xFFFF;
              break;

            case MOD_O_M:
              operand2 = Mreg;
              break;

            case MOD_O_Q:
              operand2 = Qreg;
              break;

            case MOD_O_M | MOD_O_Q:
              operand2 = Qreg | Mreg;
              break;

            default:
              ASSURE(0);
          }

          switch (instr & (MOD_LP | MOD_XR)) {
            case 0:
              operand = doADD(operand1, operand2);
              break;

            case MOD_XR:
              operand = operand1 ^ operand2;
              break;

            case MOD_LP:
              operand = operand1 & operand2;
              break;

            case MOD_XR | MOD_LP:
              operand = ~(operand1 & operand2);
              break;
          }

          if ((instr & MOD_D_A) != 0)
            Areg = operand;
          if ((instr & MOD_D_Q) != 0)
            Qreg = operand;
          if ((instr & MOD_D_M) != 0) {
            if ((cpu_dev.dctrl & DBG_INTR) != 0)
              fprintf(DBGOUT, "%s<=== M changed from %04X to %04X\r\n",
                      INTprefix, Mreg, operand);
            Mreg = operand;
          }
          break;

        case OPC_INA:
          Areg = doADD(Areg, EXTEND8(instr & OPC_MODMASK));
          break;

        case OPC_ENA:
          Areg = EXTEND8(instr & OPC_MODMASK);
          break;

        case OPC_NOP:
          switch (INSTR_SET) {
            case INSTR_ORIGINAL:
              break;

            case INSTR_BASIC:
              if ((instr & OPC_MODMASK) != 0) {
                if ((cpu_dev.dctrl & DBG_ENH) != 0)
                  fprintf(DBGOUT, "%s Possible Enh. Instruction (%04X) at %04x\r\n",
                          INTprefix, instr, OrigPreg);
              }
              break;

            case INSTR_ENHANCED:
              if ((instr & OPC_MODMASK) != 0) {
                Preg = OrigPreg;
                return SCPE_UNIMPL;
              }
              break;
          }
          break;

        case OPC_ENQ:
          Qreg = EXTEND8(instr & OPC_MODMASK);
          break;

        case OPC_INQ:
          Qreg = doADD(Qreg, EXTEND8(instr & OPC_MODMASK));
          break;

        case OPC_SHIFTS:
          /* Assume shifts without A or Q are a NOP */
          if ((instr & (MOD_S_A | MOD_S_Q)) != 0) {
            int i, count = instr & OPC_SHIFTCOUNT;
            uint32 temp32;

            if (count) {
              switch (instr & (OPC_SHIFTS | OPC_SHIFTMASK)) {
                case OPC_QRS:
                  temp32 = Qreg;
                  for (i = 0; i < count; i++) {
                    temp32 >>= 1;
                    if ((temp32 & 0x4000) != 0)
                      temp32 |= SIGN;
                  }
                  Qreg = TRUNC16(temp32);
                  break;

                case OPC_ARS:
                  temp32 = Areg;
                  for (i = 0; i < count; i++) {
                    temp32 >>= 1;
                    if ((temp32 & 0x4000) != 0)
                      temp32 |= SIGN;
                  }
                  Areg = TRUNC16(temp32);
                  break;

                case OPC_LRS:
                  temp32 = (Qreg << 16) | Areg;
                  for (i = 0; i < count; i++) {
                    temp32 >>= 1;
                    if ((temp32 & 0x40000000) != 0)
                      temp32 |= 0x80000000;
                  }
                  Areg = TRUNC16(temp32);
                  Qreg = TRUNC16(temp32 >> 16);
                  break;
              
                case OPC_QLS:
                  temp32 = Qreg;
                  for (i = 0; i < count; i++) {
                    temp32 <<= 1;
                    if ((temp32 & 0x10000) != 0)
                      temp32 |= 1;
                  }
                  Qreg = TRUNC16(temp32);
                  break;

                case OPC_ALS:
                  temp32 = Areg;
                  for (i = 0; i < count; i++) {
                    temp32 <<= 1;
                    if ((temp32 & 0x10000) != 0)
                      temp32 |= 1;
                  }
                  Areg = TRUNC16(temp32);
                  break;
                  
                case OPC_LLS:
                  temp32 = (Qreg << 16) | Areg;
                  for (i = 0; i < count; i++) {
                    uint32 sign = temp32 & 0x80000000;
                    
                    temp32 <<= 1;
                    if (sign)
                      temp32 |= 1;
                  }
                  Areg = TRUNC16(temp32);
                  Qreg = TRUNC16(temp32 >> 16);
                  break;
              }
            }
          }
          break;
      }
      break;
  }
  return SCPE_OK;
}

t_stat sim_instr(void)
{
  t_stat reason = 0;

  ExecutionStarted = TRUE;

  while (reason == 0) {
    if (sim_interval <= 0) {
      if ((reason = sim_process_event()) != 0)
        break;
    }

    if (sim_brk_summ && sim_brk_test(Preg, SWMASK('E')))
      return SCPE_IBKPT;

    reason = executeAnInstruction();
    sim_interval--;

    if (reason == SCPE_OK)
      if (sim_step && (--sim_step <= 0))
        reason = SCPE_STOP;
  }
  return reason;
}

t_stat cpu_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  const char helpString[] =
    /****************************************************************************/
    " The %D device is a 1714 central processing unit.\n"
    "1 Hardware Description\n"
    " The 1714 can access up to 64KW of memory (4KW, 8KW, 16KW, 32KW and 64KW\n"
    " are supported). A 1705 multi-level interrupt system with a direct\n"
    " storage access bus and 3 1706-A buffered data channels are included.\n\n"
    " The amount of memory available to the system can be changed with:\n\n"
    "+sim> SET CPU nK\n\n"
    " The original 1700 series CPU (the 1704) only allowed up to 32KW of\n"
    "to be attached to the CPU and indirect memory references would continue\n"
    "to loop through memory if bit 15 of the target address was set. When 64KW\n"
    " support was added, indirect addressing was limited to a single level\n"
    " so that the entire 16-bits of address could be used. Systems which\n"
    " supported 64KW of memory had a front-panel switch to allow software\n"
    " to run in either mode. The indirect addressing mode may be changed by:\n\n"
    "+sim> SET CPU MODE32K\n"
    "+sim> SET CPU MODE65K\n\n"
    " In 32KW addressing mode, the number of indirect address chaining\n"
    " operations is limited to 10000 to avoid infinite loops.\n"
    "2 Equipment Address\n"
    " The CPU is not directly accessible via an equipment address but it does\n"
    " reserve interrupt 0  (and therefore equipment address 0) for parity\n"
    " errors (never detected by the simulator), protect faults and power fail\n"
    " (not supported by the simulator).\n"
    "2 Instruction Set\n"
    " The instruction set implemented by the CDC 1700 series, and later\n"
    " Cyber-18 models changed as new features were added. When originally\n"
    " released, the 1704 had a number of instruction bits which were ignored\n"
    " by the CPU (e.g. the IIN and EIN instructions each had 8 unused bits).\n"
    " Later the instruction set was refined into Basic and Enhanced. The\n"
    " Basic instruction set reserved these unsed bits (e.g. IIN and EIN\n"
    " instructions were only recognised if the previously unused bits were\n"
    " all set to zero). The MP17 microprocessor implementation of the\n"
    " architecture made use of these newly available bits to implement\n"
    " the Enhanced instruction set. The supported instruction set may be\n"
    " changed by:\n\n"
    "+sim> SET CPU INSTR=ORIGINAL\n"
    "+sim> SET CPU INSTR=BASIC\n"
    "+sim> SET CPU INSTR=ENHANCED\n\n"
    " The Enhanced instruction set is not currently implemented by the\n"
    " simulator. Note that disassembly will always be done with respect to\n"
    " the currently selected instruction set. If the instruction set is set\n"
    " to BASIC, enhanced instructions will be displayed as:\n\n"
    "+ NOP  [ Possible enhanced instruction\n"
    "2 Character Addressing Mode\n"
    " The ORIGINAL instruction set could be enhanced with character (8-bit)\n"
    " addressing mode which added 2 new instructions; enable/disable\n"
    " character addressing mode (ECA/DCA). These new instructions and the\n"
    " ability to perform character addressing may be controlled by:\n\n"
    "+sim> SET CPU CHAR\n"
    "+sim> SET CPU NOCHAR\n"
    "2 $Registers\n"
    "2 Front Panel Switches\n"
    " The 1714 front panel includes a number of switches which control the\n"
    " operation of the CPU. Note that selective stop and selective skip are\n"
    " used extensively to control execution of the System Maintenance\n"
    " Monitor.\n"
    "3 Selective Stop\n"
    " The selective stop switch controls how the 'Selective Stop' (SLS)\n"
    " instruction executes. If the switch is off, SLS executes as a\n"
    " no-operation. If the switch is on, SLS executes as a halt instruction.\n"
    " Continuing after the halt causes the CPU to resume execution at the\n"
    " instruction following the SLS.\n\n"
    "+sim> SET CPU SSTOP\n"
    "+sim> SET CPU NOSSTOP\n\n"
    "3 Selective Skip\n"
    " The selective skip switch controls how the SWS and SWN skip\n"
    " instructions execute. SWS will skip if the switch is set and SWN will\n"
    " skip if the switch is not set.\n\n"
    "+sim> SET CPU SSKIP\n"
    "+sim> SET CPU NOSSKIP\n\n"
    "3 Protect\n"
    " Each word of memory on the CDC 1700 series consists of 18-bits; 16-bits\n"
    " of data/instruction, a parity bit (which is not implemented in the\n"
    " simulator) and a program bit. If the protect switch is off, any program\n"
    " may reference any word of memory. If the protect switch is on, there are\n"
    " a set of rules which control how memory accesses work and when to\n"
    " generate a program protect violation - see one of the 1700 reference\n"
    " manuals on bitsavers.org for exact details. This means that the\n"
    " operating system can be protected from modification by application\n"
    " programs but there is no isolation between application programs.\n\n"
    "+sim> SET CPU PROTECT\n"
    "+sim> SET CPU NOPROTECT\n\n"
    " The Simulator fully implements CPU protect mode allowing protected\n"
    " operating systems such as MSOS 5 to execute. It does not implement\n"
    " peripheral protect operation which allows unprotected applications to\n"
    " directly access some unprotected peripherals.\n\n"
    " Operating systems and other programs which run with the protect switch\n"
    " on usually start up with the protect switch off, manipulate the\n"
    " protect bits in memory (using the CPB/SPB instructions) and then ask\n"
    " the operator to set the protect switch on.\n"
    "1 Configuration\n"
    " The CPU is configured with various simh SET commands.\n"
    "2 $Set commands\n";

  return scp_help(st, dptr, uptr, flag, helpString, cptr);
}
