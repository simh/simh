/* imlac_cpu.c: Imlac CPU simulator

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

#include "imlac_defs.h"
#ifdef HAVE_LIBSDL
#include "display/display.h"
#endif


/* Debug */
#define DBG_CPU         0001
#define DBG_IRQ         0002
#define DBG_ROM         0004

/* Bootstrap ROM type. */
#define ROM_NONE        0
#define ROM_TTY         1
#define ROM_STTY        2
#define ROM_PTR         3

/* CPU state. */
static uint16 PC;
static uint16 AC;
static uint16 L;
static uint16 DS;
static uint16 IR;
static uint16 MA;
static uint16 MB;
static int ion_delay = 0;

/* IRQ state. */
static uint16 ARM = 0177777;
static uint16 FLAGS = FLAG_SYNC | FLAG_TTY_T;
static uint16 ION;

/* ROM state. */
static int rom_type = ROM_NONE;

static int halt;
uint16 memmask = 017777;

static struct {
  uint16 PC;
  uint16 IR;
  uint16 MA;
  uint16 MB;
  uint16 AC;
  uint16 L;
} *history = NULL;
static uint32 history_i, history_j, history_m, history_n;

/* Function declaration. */
static t_stat cpu_set_hist (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat cpu_show_hist (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat cpu_ex (t_value *vptr, t_addr ea, UNIT *uptr, int32 sw);
static t_stat cpu_dep (t_value val, t_addr ea, UNIT *uptr, int32 sw);
static t_stat cpu_reset (DEVICE *dptr);
static uint16 irq_iot (uint16, uint16);
static t_stat rom_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat rom_show_type (FILE *st, UNIT *up, int32 v, CONST void *dp);

static UNIT cpu_unit = { UDATA (NULL, UNIT_FIX + UNIT_BINK, 020000) };

REG cpu_reg[] = {
  { ORDATAD (PC, PC, 13, "Program Counter") },
  { ORDATAD (AC, AC, 16, "Accumulator") },
  { ORDATAD (L, L, 1, "Link") },
  { ORDATAD (DS, DS, 16, "Data Switches") },
  { ORDATAD (IR, IR, 16, "Instruction") },
  { ORDATAD (MA, MA, 13, "Memory Address") },
  { ORDATAD (MB, MB, 16, "Memory Buffer") },
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
  { NULL, 0 }
};

DEVICE cpu_dev = {
  "CPU", &cpu_unit, cpu_reg, cpu_mod,
  0, 8, 16, 1, 8, 16,
  &cpu_ex, &cpu_dep, &cpu_reset,
  NULL, NULL, NULL, NULL, DEV_DEBUG, 0, cpu_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static REG irq_reg[] = {
  { ORDATAD (ION, ION, 1, "Interrupts on") },
  { ORDATAD (FLAGS, FLAGS, 16, "Flagged interrupts") },
  { ORDATAD (ARM, ARM, 16, "Armed interrupts") },
  { NULL }
};

static IMDEV irq_imdev = {
  3,
  { { 0010, irq_iot, { NULL, "RDI", NULL, NULL } },
    { 0014, irq_iot, { NULL, "ARM", NULL, NULL } },
    { 0016, irq_iot, { NULL, "IOF", "ION", NULL } } }
};

static DEBTAB irq_deb[] = {
  { "IRQ", DBG_IRQ },
  { NULL, 0 }
};

DEVICE irq_dev = {
  "IRQ", NULL, irq_reg, NULL,
  0, 8, 16, 1, 8, 16,
  NULL, NULL, NULL,
  NULL, NULL, NULL,
  &irq_imdev, DEV_DEBUG, 0, irq_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static MTAB rom_mod[] = {
  { MTAB_VDV|MTAB_VALR, 0, "TYPE", "TYPE", &rom_set_type, &rom_show_type },
  { 0 }
};

static DEBTAB rom_deb[] = {
  { "DBG", DBG_ROM },
  { NULL, 0 }
};

DEVICE rom_dev = {
  "ROM", NULL, NULL, rom_mod,
  0, 8, 16, 1, 8, 16,
  NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, DEV_DEBUG, 0, rom_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static void pcinc (int flag)
{
  if (flag)
    PC = (PC + 1) & memmask;
}

static void memaddr (uint16 addr)
{
  MA = addr & memmask;
}

static void memrd (void)
{
  MB = M[MA];
}

static void memwr (void)
{
  if (rom_type == ROM_NONE || (MA & 0177740) != 040)
    M[MA] = MB;
}

static void cpu_class1 (uint16 insn)
{
  if (insn & 0000001) /* T1: CLA */
    AC = 0;
  if (insn & 0000010) /* T1: CLL */
    L = 0;
  if (insn & 0000002) /* T2: CMA */
    AC = ~AC;
  if (insn & 0000020) /* T2: CML */
    L = !L;
  if (insn & 0000004) /* T3: IAC */
    AC++;
  if (insn & 0000040) { /* T3: ODA */
    sim_debug (DBG_CPU, &cpu_dev, "Read data switches: %06o\n", DS);
    AC |= DS;
  }

  halt = !(insn & 0100000);
}

static void cpu_ral (int n)
{
  int i, x;
  for (i = 0; i < n; i++) {
    x = L;
    L = AC >> 15;
    AC = (AC << 1) | x;
  }
}

static void cpu_rar (int n)
{
  int i, x;
  for (i = 0; i < n; i++) {
    x = L;
    L = AC & 1;
    AC = (x << 15) | (AC >> 1);
  }
}

static void cpu_class2 (uint16 insn)
{
  int n = insn & 3;
  uint32 x;

  if (insn & 0000100) /* DON */
    dp_on (1);

  switch (insn & 0000060) {
  case 0000000: /* RAL */
    cpu_ral (n);
    break;
  case 0000020: /* RAR */
    cpu_rar (n);
    break;
  case 0000040: /* SAL */
    AC = (AC & 0100000) | ((AC & 037777) << n);
    break;
  case 0000060: /* SAR */
    if (AC & 0100000)
      x = 01600000 >> n;
    else
      x = 0;
    AC = x | ((AC & 077777) >> n);
    break;
  }
}

static void cpu_class3 (uint16 insn)
{
  int skip = 0;

  if (insn & 0001) /* ASZ */
    skip |= (AC == 0);
  if (insn & 0002) /* ASP */
    skip |= !(AC & 0100000);
  if (insn & 0004) /* LSZ */
    skip |= (L == 0);
  if (insn & 0010) /* DSF */
    skip |= dp_is_on ();
  if (insn & 0020) /* KSF */
    skip |= FLAGS & FLAG_KBD;
  if (insn & 0040) /* RSF */
    skip |= FLAGS & FLAG_TTY_R;
  if (insn & 0100) /* TSF */
    skip |= FLAGS & FLAG_TTY_T;
  if (insn & 0200) /* SSF */
    skip |= FLAGS & FLAG_SYNC;
  if (insn & 0400) /* HSF */
    skip |= FLAGS & FLAG_PTR;

  if (insn & 0100000)
    skip = !skip;

  pcinc (skip);
}

static void cpu_iot (uint16 insn)
{
  SUBDEV *dev = dev_tab[(insn >> 3) & 077];
  if (dev == NULL) {
    sim_debug (DBG_CPU, &cpu_dev, "Unknown device IOT @ %06o: %06o\n", PC, IR);
    return;
  }
  AC = dev->iot (insn, AC);
}

static void cpu_opr (uint16 insn)
{
  switch (insn & 0177000) {
  case 0000000:
  case 0100000:
    cpu_class1 (insn);
    break;
  case 0003000:
    cpu_class2 (insn);
    break;
  case 0002000:
  case 0102000:
    cpu_class3 (insn);
    break;
  case 0001000:
    cpu_iot (insn);
    break;
  default:
    sim_debug (DBG_CPU, &cpu_dev, "Unknown instruction: %06o\n", IR);
    break;
  }
}

static void
cpu_insn (void)
{
  uint32 t32;
  uint16 tmp;

  /* Fetch cycle. */
  memaddr (PC);
  memrd ();
  IR = MB;
  sim_interval--;

  if (((IR >> 12) & 7) != 0) {
    /* Memory referecing. */
    memaddr ((IR & 03777) | (PC & 014000));
    if (IR & 0100000) {
      /* Defer cycle. */
      if ((MA & 03770) == 010) {
        /* Auto incrementing. */
        memrd ();
        MB++;
        memwr ();
      }
      memaddr (M[MA]);
    }
  }

  if (history) {
    history[history_i].PC = PC;
    history[history_i].IR = IR;
    history[history_i].MA = MA;
  }

  pcinc (1);

  /* Execute cycle. */
  switch ((IR >> 9) & 074) {
  case 000: /* OPR */
    cpu_opr (IR);
    break;
  case 004: /* LAW, LCW */
    if (IR & 0100000)
      AC = -(IR & 03777);
    else
      AC = IR & 03777;
    break;
  case 010: /* JMP */
    PC = MA;
    break;
  case 020: /* DAC */
    MB = AC;
    memwr ();
    break;
  case 024: /* XAM */
    memrd ();
    tmp = MB;
    MB = AC;
    memwr ();
    AC = tmp;
    break;
  case 030: /* ISZ */
    memrd ();
    MB++;
    memwr ();
    pcinc (MB == 0);
    break;
  case 034: /* JMS */
    MB = PC;
    memwr ();
    PC = MA;
    pcinc (1);
    break;
  case 044: /* AND */
    memrd ();
    AC &= MB;
    break;
  case 050: /* IOR */
    memrd ();
    AC |= MB;
    break;
  case 054: /* XOR */
    memrd ();
    AC ^= MB;
    break;
  case 060: /* LAC */
    memrd ();
    AC = MB;
    break;
  case 064: /* ADD */
    memrd ();
    t32 = AC;
    t32 += MB;
    AC = t32 & 0177777;
    if (t32 & 0200000)
      L ^= 1;
    break;
  case 070: /* SUB */
    memrd ();
    t32 = AC;
    t32 -= MB;
    AC = t32 & 0177777;
    if (t32 & 0200000)
      L ^= 1;
    break;
  case 074: /* SAM */
    memrd ();
    pcinc (AC == MB);
    break;
  default:
    sim_debug (DBG_CPU, &cpu_dev, "Unknown instruction: %06o\n", IR);
    break;
  }

  if (history) {
    history[history_i].MB = MB;
    history[history_i].AC = AC;
    history[history_i].L = L;
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

    /* Check for interrupts. */
    if (ION && (FLAGS & ARM)) {
      sim_debug (DBG_IRQ, &irq_dev, "Interrupt: %06o\n", FLAGS & ARM);
      M[0] = PC;
      PC = 1;
      ION = 0;
    }

    cpu_insn ();

    if (sim_step != 0) {
      if (--sim_step == 0)
        return SCPE_STEP;
    }

    if (halt)
      return STOP_HALT;

    if (ion_delay && --ion_delay == 0) {
      sim_debug (DBG_IRQ, &irq_dev, "Interrupts on\n");
      ION = 1;
    }
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

  history = calloc (x, sizeof (*history));
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

  fprintf (st, "PC____ IR____ MA____ MB____ AC____ L\n");

  if (history_i >= history_n)
    j = history_i - history_n;
  else
    j = history_m + history_i - history_n;

  for (i = 0; i < history_n; i++) {
    fprintf (st, "%06o %06o %06o %06o %06o %d  ",
             history[j].PC,
             history[j].IR,
             history[j].MA,
             history[j].MB,
             history[j].AC,
             history[j].L);
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
  if (ea >= 040000)
    return SCPE_NXM;
  *vptr = M[ea];
  return SCPE_OK;
}

static t_stat cpu_dep (t_value val, t_addr ea, UNIT *uptr, int32 sw)
{
  if (ea >= 040000)
    return SCPE_NXM;
  M[ea] = val & 0177777;
  return SCPE_OK;
}

static t_bool cpu_is_pc_a_subroutine_call (t_addr **ret_addrs)
{
  static t_addr returns[2] = { 0, 0 };

  if ((M[PC] & 074000) == 034000) {
    returns[0] = PC + 1;
    *ret_addrs = returns;
    return TRUE;
  } else {
    return FALSE;
  }
}

static t_stat
cpu_reset (DEVICE *dptr)
{
  sim_brk_types = SWMASK('D') | SWMASK('E');
  sim_brk_dflt = SWMASK ('E');
  sim_vm_is_subroutine_call = &cpu_is_pc_a_subroutine_call;
  return SCPE_OK;
}

void
flag_on (uint16 flag)
{
  FLAGS |= flag;
  sim_debug (DBG_IRQ, &irq_dev, "Flag on %06o -> %06o\n", flag, FLAGS);
}

void
flag_off (uint16 flag)
{
  FLAGS &= ~flag;
  sim_debug (DBG_IRQ, &irq_dev, "Flag off %06o -> %06o\n", flag, FLAGS);
}

uint16
flag_check (uint16 flag)
{
  return FLAGS & flag;
}

static uint16
irq_iot (uint16 insn, uint16 AC)
{
  if ((insn & 0771) == 0101) { /* RDI */
    AC |= FLAGS;
  }
  if ((insn & 0771) == 0141) { /* ARM */
    ARM = AC;
  }
  if ((insn & 0771) == 0161) { /* IOF */
    sim_debug (DBG_IRQ, &irq_dev, "Interrupts off\n");
    ION = 0;
  }
  if ((insn & 0772) == 0162) { /* ION */
    /* Delay the action until next instruction has executed. */
    ion_delay = 2;
  }
  return AC;
}

void
rom_data (uint16 *data)
{
  int i;
  for (i = 0; i < 040; i++)
    M[040 + i] = data[i];
}

static t_stat
rom_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  t_stat r = SCPE_OK;
  if (strcmp (cptr, "NONE") == 0) {
    rom_type = ROM_NONE;
  } else if (strcmp (cptr, "TTY") == 0) {
    rom_type = ROM_TTY;
    rom_tty ();
  } else if (strcmp (cptr, "STTY") == 0) {
    rom_type = ROM_STTY;
    rom_stty ();
  } else if (strcmp (cptr, "PTR") == 0) {
    rom_type = ROM_PTR;
    rom_ptr ();
  } else
    r = SCPE_ARG;
  return r;
}

static t_stat
rom_show_type (FILE *st, UNIT *up, int32 v, CONST void *dp)
{
  switch (rom_type) {
  case ROM_NONE:
    fprintf (st, "TYPE=NONE");
    break;
  case ROM_TTY:
    fprintf (st, "TYPE=TTY");
    break;
  case ROM_STTY:
    fprintf (st, "TYPE=STTY");
    break;
  case ROM_PTR:
    fprintf (st, "TYPE=PTR");
    break;
  default:
    fprintf (st, "TYPE=(invalid)");
    break;
  }
  return SCPE_OK;
}

#ifdef HAVE_LIBSDL
/* Called from display library to get data switches. */
void
cpu_get_switches (unsigned long *p1, unsigned long *p2)
{
  *p1 = DS;
  *p2 = 0;
}

/* Called from display library to set data switches. */
void
cpu_set_switches (unsigned long p1, unsigned long p2)
{
  DS = p1 & 0177777;
}
#endif /* HAVE_LIBSDL */
