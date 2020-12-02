/* tt2500_cpu.c: TT2500 CPU simulator

   Copyright (c) 2020, Lars Brinkhoff

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
   LARS BRINKHOFF BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Lars Brinkhoff shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Lars Brinkhoff.
*/

#include "tt2500_defs.h"
#ifdef USE_DISPLAY
#include "display/display.h"
#endif


/* Debug */
#define DBG_CPU         0001
#define DBG_FET         0002
#define DBG_EXE         0004
#define DBG_STATE       0010
#define DBG_INT         0020

/* CPU state. */
static uint16 PC;
static uint16 IR;
static int ROM = 1;
int C, V, N, Z;
static uint16 IM = 0;
static uint16 STACK[16];
static uint16 SP = 0;
static uint16 R[64];
static uint16 RES, FLAGS, INTS, STARS;
static uint16 new_XR;

static int halt;

typedef struct {
  uint16 PC;
  uint16 IR;
  uint16 MA;
  uint16 MB;
  uint16 AC;
  uint16 L;
} HISTORY;
static HISTORY *history = NULL;
static uint32 history_i, history_m, history_n;

/* Function declaration. */
static t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat cpu_ex (t_value *vptr, t_addr ea, UNIT *uptr, int32 sw);
static t_stat cpu_dep (t_value val, t_addr ea, UNIT *uptr, int32 sw);
static t_stat cpu_reset (DEVICE *dptr);
static void cpu_update (void);

static UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, 020000) };

static BITFIELD flags_bits[] = {
  BIT(KB),
  BIT(RSD),
  BITNCF(2),
  ENDBITS
};

static BITFIELD ints_bits[] = {
  BIT(2KHZ),
  BIT(RRD),
  BIT(60HZ),
  BITNC,
  ENDBITS
};

static BITFIELD stars_bits[] = {
  BIT(WRAP),
  BIT(MINUS1),
  ENDBITS
};

REG cpu_reg[] = {
  { ORDATAD (PC, PC, 13, "Program Counter") },
  { ORDATAD (ROM, ROM, 1, "Read from ROM") },
  { ORDATAD (IR, IR, 16, "Instruction") },
  { ORDATAD (XR, R[REG_XR], 12, "Execute register") },
  { ORDATAD (A, R[REG_ALATCH], 16, "A latch") },
  { ORDATAD (IM, IM, 16, "Immediate") },
  { ORDATAD (RES, RES, 16, "Result") },
  { HRDATADF (FLAGS, FLAGS, 4, "Flags", flags_bits ) },
  { HRDATADF (INTS, INTS, 4, "Interrupts", ints_bits ) },
  { HRDATADF (STARS, STARS, 4, "Stars", stars_bits ) },
  { BRDATAD (REG, R, 8, 16, 64, "Registers") },
  { NULL }
};

static MTAB cpu_mod[] = {
  { MTAB_XTD|MTAB_VDV, 0, "IDLE", "IDLE", &sim_set_idle, &sim_show_idle },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "NOIDLE", &sim_clr_idle, NULL },
  { MTAB_XTD|MTAB_VDV|MTAB_NMO|MTAB_SHP, 0, "HISTORY", "HISTORY",
    &cpu_set_hist, &cpu_show_hist },
  { 0 }
};

static DEBTAB cpu_deb[] = {
  { "CPU", DBG_CPU },
  { "FETCH", DBG_FET },
  { "EXECUTE", DBG_EXE },
  { "STATE", DBG_STATE },
  { "INT", DBG_INT },
  { NULL, 0 }
};

DEVICE cpu_dev = {
  "CPU", &cpu_unit, cpu_reg, cpu_mod,
  0, 8, 16, 1, 8, 16,
  &cpu_ex, &cpu_dep, &cpu_reset,
  NULL, NULL, NULL, NULL, DEV_DEBUG, 0, cpu_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static uint16 crm_read (uint16 addr)
{
  if (ROM && addr < 32)
    return tt2500_rom[addr];

  ROM = 0;
  return CRM[addr]; 
}

static uint16 bus_read (uint16 reg)
{
  if ((reg & 060) == 020) {
    RES = dev_tab[reg]->read (reg);
    sim_debug (DBG_STATE, &cpu_dev, "%06o <= BUS[%02o]\n", RES, reg);
  } else {
    RES = R[reg];
  }
  return RES;
}

static uint16 cpu_rot (uint16 data, uint16 n)
{
  return (data >> n) + (data << (16 - n));
}

static uint16 cpu_ars (uint16 data, uint16 n)
{
  uint32 sign = 0;
  if (data & 0100000)
    sign = 0177777 << 16;
  return (data >> n) + (sign >> n);
}

uint16 cpu_alu (uint16 insn, uint16 op, uint16 adata, uint16 bdata)
{
  uint32 result;

  V = 0;

  switch (op) {
  case ALU_A:    result = adata; break;
  case ALU_ANDN: result = adata & ~bdata; break;
  case ALU_AND:  result = adata & bdata; break;
  case ALU_NOR:  result = (~(adata | bdata)) & 0177777; break;
  case ALU_IOR:  result = adata | bdata; break;
  case ALU_XOR:  result = adata ^ bdata; break;
  case ALU_MROT: result = adata & cpu_rot (R[R[REG_ALATCH]], insn & 017); break;
  case ALU_ROT:  result = cpu_rot (adata, insn & 017); break;
  case ALU_DEC:  result = adata - 1; V = (result == 077777); break;
  case ALU_XADD: bdata += C; /* Fall through. */
  case ALU_ADD:
    result = adata + bdata;
    V = (((~adata ^ bdata) & (bdata ^ result)) >> 15) & 1;
    break;
  case ALU_XSUB: bdata += C; /* Fall through. */
  case ALU_SUB:
    result = adata - bdata;
    V = (((adata ^ bdata) & (~bdata ^ result)) >> 15) & 1;
    break;
  case ALU_INC:  result = adata + 1; V = (result == 0100000); break;
  case ALU_ARS:  result = cpu_ars (adata, insn & 017); break;
  default:       result = 0; break;
  }

  C = !!(result & 0200000);
  result &= 0177777;
  N = !!(result & 0100000);
  Z = (result == 0);

  sim_debug (DBG_STATE, &cpu_dev, "ALU: %06o %06o => %06o (%c%c%c%c)\n",
             adata, bdata, result,
             C ? 'C' : '-',
             V ? 'V' : '-',
             N ? 'N' : '-',
             Z ? 'Z' : '-');

  return result;
}

static uint16 mem_read (uint16 address)
{
  if ((address & 0170000) == 0170000 && (DSR & DSR_TVON) == 0)
    return FONT[address - 0170000];
  else
    return MEM[address];
}

static void mem_write (uint16 address, uint16 data)
{
  if ((address & 0170000) == 0170000 && (DSR & DSR_TVON) == 0)
    FONT[address - 0170000] = (uint8)data;
  else
    MEM[address] = data;
}

static void cpu_reg_op (uint16 insn)
{
  uint16 a = (insn >> 6) & 7;
  uint16 b = insn & 7;
  uint16 alu_op;
  uint16 adata;
  uint16 bdata;
  uint16 result;

  if (IM != 0) {
    adata = IR;
    IM = 0;
  } else if ((insn & 01000) != 0 && (insn & 030000) != 020000) {
    IM = IR;
    return;
  } else
    adata = R[a];
  if (insn & 010)
    bdata = 0;
  else
    bdata = R[b];

  alu_op = insn & 06060;

  if ((insn & 030000) == 020000) {
    if ((insn & 01000) == 0) {
      if (insn & 04000) {
        sim_debug (DBG_STATE, &cpu_dev, "MEM[%06o] <= %06o <= REG[%02o]\n",
                   adata, bdata, b);
        mem_write (adata, bdata);
      } else {
        bdata = R[b] = mem_read(adata);
        sim_debug (DBG_STATE, &cpu_dev, "REG[%02o] <= %06o <= MEM[%06o]\n",
                   b, bdata, adata);
      }
    }
    if (alu_op != 0)
      alu_op |= 04000;
  }

  result = cpu_alu (insn, alu_op, adata, bdata);

  switch (insn & 030000) {
  case 030000:
    if (C) {
  case 000000:
      sim_debug (DBG_STATE, &cpu_dev, "REG[%02o] <= %06o\n", a, result);
      R[a] = result;
    }
    break;
  case 010000:
    break;
  case 020000:
    sim_debug (DBG_STATE, &cpu_dev, "REG[%02o] <= %06o\n", a, result);
    R[a] = result;
    if (insn & 01000) {
      IM = 0;
      if (insn & 04000) {
        sim_debug (DBG_STATE, &cpu_dev, "CWRITE[%04o]\n", RES);
        CRM[RES] = result;
      } else {
        sim_debug (DBG_STATE, &cpu_dev, "CREAD[%04o]\n", RES);
        R[a] = crm_read (RES);
        V = 0;
        C = 0;
        N = !!(R[a] & 0100000);
        Z = (R[a] == 0);
        sim_debug (DBG_STATE, &cpu_dev, "REG[%02o] <= %06o (%c%c%c%c)\n",
                   a, R[a],
                   C ? 'C' : '-',
                   V ? 'V' : '-',
                   N ? 'N' : '-',
                   Z ? 'Z' : '-');
      }
    }
    break;
  }

  R[REG_ALATCH] = a;
  sim_debug (DBG_STATE, &cpu_dev, "A <= %o\n", R[REG_ALATCH]);
  RES = result;
}

static void cpu_jump (uint16 insn, int push)
{
  if (push) {
    STACK[SP] = PC;
    sim_debug (DBG_STATE, &cpu_dev, "STACK[%02o] <= %04o\n", SP, PC);
    SP = (SP + 1) & 017;
  }
  PC = insn & 07777;
}

static void cpu_dis (uint16 insn)
{
  uint16 data;
  uint16 mask;

  switch (insn & 01400) {
  case 00000:
    data = ((RES >> 15) & 1) | ((RES >> 13) & 2) |
      ((RES >> 11) & 4) | ((RES >> 9) & 8);
    break;
  case 00400:
    data = FLAGS;
    break;
  case 01000:
    data = INTS;
    break;
  case 01400:
    data = STARS;
    break;
  default:
    return;
  }

  mask = (insn >> 4) & 017;
  PC = PC + (data & ~mask);
}

static void cpu_popj (void)
{
  SP = (SP - 1) & 017;
  PC = STACK[SP];
  sim_debug (DBG_STATE, &cpu_dev, "PC <= %04o <= STACK[%02o]\n", PC, SP);
}

static void bus_write (uint16 reg, uint16 data)
{
  switch (reg) {
  case 012: PC = data & 07777; break;
  case 014: dpy_magic (data, &R[2], &R[3], R[4], R[5]); break;
  case 015: dpy_chartv (data); break;
  case 016: cpu_popj (); break;
  case 023: new_XR = data; break;
  case 020: case 021: case 022: case 024: case 025: case 026: case 027:
  case 030: case 031: case 032: case 033: case 034: case 035: case 036: case 037:
    sim_debug (DBG_STATE, &cpu_dev, "BUS[%02o] <= %06o\n", reg, data);
    dev_tab[reg]->write (reg, data);
    break;
  default: /* 40-77 is scratchpad. */
    R[reg] = data;
    sim_debug (DBG_STATE, &cpu_dev, "REG[%02o] <= %06o\n", reg, data);
    break;
  }
}

static void cpu_bus (uint16 insn)
{
  uint16 a = (insn >> 6) & 7;
  uint16 b = insn & 077;
  uint16 bb = insn & 01000;

  if ((insn & 0176000) == 0072000) {
    cpu_dis (insn);
    return;
  }

  if (bb) {
    switch (a) {
    case 2: PC = RES;
    case 4: dpy_magic (RES, &R[2], &R[3], R[4], R[5]); return;
    case 5: dpy_chartv (R[b]); return;
    case 6: cpu_popj (); return;
    default:
      sim_debug (DBG_CPU, &cpu_dev, "Unknown instruction: %06o\n", IR);
      break;
    }
  }

  if (insn & 02000) {
    bus_write (b, R[a]);
  } else {
    R[a] = bus_read (b);
    sim_debug (DBG_STATE, &cpu_dev, "REG[%02o] <= %06o\n", a, R[a]);
  }
}

static void cpu_branch (uint16 insn)
{
  uint16 target = insn & 03777;
  int jump = 0;

  switch (insn & 070000) {
  case 000000: jump = !C; break;
  case 010000: jump = !V; break;
  case 020000: jump = N; break;
  case 030000: jump = !Z; break;
  case 040000: jump = N ^ V; break;
  case 050000: jump = INTS; break;
  case 060000: jump = !(R[REG_XR] & 04000); new_XR = R[REG_XR] + 1; break;
  case 070000: jump = FLAGS; break;
  }

  if (insn & 04000)
    jump = !jump;

  if (jump) {
    if (insn & 02000)
      target -= 04000;
    PC = (PC + target) & 07777;
  }
}

static void
cpu_fetch (void)
{
  /* Fetch cycle. */
  IR = crm_read (PC);
  sim_debug (DBG_FET, &cpu_dev, "%04o: %06o\n", PC, IR);
  sim_interval--;

  if (history) {
    history[history_i].PC = PC;
    history[history_i].IR = IR;
  }

  PC = (PC + 1) & 07777;
}

static void cpu_update (void)
{
  new_XR &= 07777;
  if (R[REG_XR] != new_XR)
    sim_debug (DBG_STATE, &cpu_dev, "XR <= %04o\n", new_XR);
  R[REG_XR] = new_XR;
  R[011] = (new_XR >> 6) & 077;
  R[012] = new_XR & 077;
  R[015] = (new_XR >> 6) & 7;
  R[016] = new_XR & 7;
}

static void
cpu_execute (void)
{
  if (IM != 0) {
    sim_debug (DBG_EXE, &cpu_dev, "%06o\n", IM);
    cpu_reg_op (IM);
    return;
  }

  if (cpu_dev.dctrl & DBG_EXE) {
    t_value val = IR;
    sim_debug (DBG_EXE, &cpu_dev, "%06o (", IR);
    fprint_sym (sim_deb, PC-1, &val, NULL, SWMASK ('M'));
    sim_debug (DBG_EXE, &cpu_dev, ")\n");
  }

  switch ((IR >> 12) & 017) {
  case 000: case 001: case 002: case 003:
    cpu_reg_op (IR);
    break;
  case 004:
    cpu_jump (IR, 1);
    break;
  case 005:
    cpu_jump (IR, 0);
    break;
  case 007:
    cpu_bus (IR);
    break;
  case 010: case 011: case 012: case 013:
  case 014: case 015: case 016: case 017:
    cpu_branch (IR);
    break;
  default:
    sim_debug (DBG_CPU, &cpu_dev, "Unknown instruction: %06o\n", IR);
    break;
  }
}

static void
cpu_insn (void)
{
  cpu_update ();
  cpu_execute ();
  cpu_fetch ();

  if (history) {
    history_i = (history_i + 1) % history_m;
    if (history_n < history_m)
      history_n++;
  }
}

t_stat sim_instr (void)
{
  t_stat reason;

  if ((reason = build_dev_tab ()) != SCPE_OK)
    return reason;

  halt = 0;

  for (;;) {
    AIO_CHECK_EVENT;
    if (sim_interval <= 0) {
      if ((reason = sim_process_event()) != SCPE_OK)
        return reason;
    }

    if (sim_brk_summ && sim_brk_test(PC, SWMASK('E')))
      return STOP_IBKPT;

    cpu_insn ();

    if (sim_step != 0) {
      if (--sim_step == 0)
        return SCPE_STEP;
    }

    if (halt)
      return STOP_HALT;
  }

  return SCPE_OK;
}

static t_stat
cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  t_stat r;
  uint32 x;

  if (cptr == NULL)
    return SCPE_ARG;

  x = get_uint (cptr, 10, 1000000, &r);
  if (r != SCPE_OK)
    return r;

  history = (HISTORY *)calloc (x, sizeof (*history));
  if (history == NULL)
    return SCPE_MEM;

  history_m = x;
  history_n = 0;
  history_i = 0;
  return SCPE_OK;
}

static t_stat
cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
  t_value insn;
  uint32 i, j;

  fprintf (st, "PC____ IR____\n");

  if (history_i >= history_n)
    j = history_i - history_n;
  else
    j = history_m + history_i - history_n;

  for (i = 0; i < history_n; i++) {
    fprintf (st, "%06o %06o  ",
             history[j].PC,
             history[j].IR);
    insn = history[j].IR;
    fprint_sym (st, history[j].PC, &insn, NULL, SWMASK ('M'));
    fputc ('\n', st);
    j = (j + 1) % history_m;
  }

  return SCPE_OK;
}

static t_stat cpu_ex (t_value *vptr, t_addr ea, UNIT *uptr, int32 sw)
{
  if (vptr == NULL)
    return SCPE_ARG;
  if (sw & SIM_SW_STOP)
    sw |= SWMASK ('C');
  if (sw & SWMASK ('C')) {
    if (ea >= 4096)
      return SCPE_NXM;
    *vptr = crm_read (ea);
  } else {
    if (ea >= 65536)
      return SCPE_NXM;
    *vptr = mem_read (ea);
  }
  return SCPE_OK;
}

static t_stat cpu_dep (t_value val, t_addr ea, UNIT *uptr, int32 sw)
{
  if (sw & SWMASK ('C')) {
    if (ea >= 4096)
      return SCPE_NXM;
    CRM[ea] = val & 0177777;
  } else {
    if (ea >= 65536)
      return SCPE_NXM;
    mem_write (ea, val & 0177777);
  }
  return SCPE_OK;
}

static t_bool cpu_is_pc_a_subroutine_call (t_addr **ret_addrs)
{
  static t_addr returns[2] = { 0, 0 };

  if ((CRM[PC] & 0170000) == 040000) {
    returns[0] = PC + 1;
    *ret_addrs = returns;
    return TRUE;
  }

  return FALSE;
}

static t_stat
cpu_reset (DEVICE *dptr)
{
  ROM = 1;
  PC = 0;
  IR = 010000;
  IM = 0;
  SP = 0;
  C = V = N = Z = 0;
  new_XR = 0;
  RES = FLAGS = INTS = STARS = 0;

  sim_brk_types = SWMASK ('E');
  sim_brk_dflt = SWMASK ('E');
  sim_vm_is_subroutine_call = &cpu_is_pc_a_subroutine_call;
  return SCPE_OK;
}

static const char *flag_name (uint16 flag)
{
  switch (flag) {
  case FLAG_KB: return "KB";
  case FLAG_RSD: return "RSD";
  case INT_2KHZ: return "2KHZ";
  case INT_RRD: return "RRD";
  case INT_60HZ: return "60HZ";
  case STAR_WRAP: return "WRAP";
  case STAR_MINUS1: return "MINUS1";
  default: return "(unknown)";
  }
}

void flag_on (uint16 flag)
{
  sim_debug (DBG_INT, &cpu_dev, "Flag on %03o (%s)\n", flag, flag_name (flag));
  FLAGS |= flag & 017;
  flag >>= 4;
  INTS |= flag & 017;
  flag >>= 4;
  STARS |= flag & 017;
}

void flag_off (uint16 flag)
{
  sim_debug (DBG_INT, &cpu_dev, "Flag off %03o (%s)\n",
             flag, flag_name (flag));
  FLAGS &= ~(flag & 017);
  flag >>= 4;
  INTS &= ~(flag & 017);
  flag >>= 4;
  STARS &= ~(flag & 017);
}

#ifdef USE_DISPLAY
/* Called from display library to get data switches. */
void
cpu_get_switches (unsigned long *p1, unsigned long *p2)
{
}

/* Called from display library to set data switches. */
void
cpu_set_switches (unsigned long p1, unsigned long p2)
{
}
#endif
