/* tt2500_sys.c: TT2500 simulator interface

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

   24-Sep-20    LB      New simulator.
*/

#include "tt2500_defs.h"

static uint16 null_read (uint16 reg);
static void null_write (uint16 reg, uint16 data);

int32 sim_emax = 1;
char sim_name[] = "TT2500";

uint16 CRM[4096];
uint16 MEM[65536];
REG *sim_PC = &cpu_reg[0];
TTDEV *dev_tab[0100];

static t_addr sym_addr = -1;
static int sym_immediate = 0;

static TTDEV null_dev = {
  { 0, 0, 0, 0 },
  null_read,
  null_write,
};

DEVICE *sim_devices[] = {
  &cpu_dev,
  &dpy_dev,
  &crt_dev,
  &tv_dev,
  &key_dev,
  &uart_dev,
  NULL
};
  
const char *sim_stop_messages[SCPE_BASE] = {
  "Unknown error",
  "HALT instruction",
  "Breakpoint",
  "Invalid access",
};

static t_stat
get4 (FILE *fileref, uint16 *x)
{
  int c = Fgetc (fileref);
  if (c == EOF)
    return SCPE_FMT;
  *x = c & 017;
  return SCPE_OK;
}

static t_stat
get6 (FILE *fileref, uint16 *x)
{
  int c = Fgetc (fileref);
  if (c == EOF)
    return SCPE_FMT;
  *x = c & 077;
  return SCPE_OK;
}

static t_stat
get8 (FILE *fileref, uint16 *x)
{
  uint16 y;
  t_stat r;
  r = get4 (fileref, x);
  if (r != SCPE_OK)
    return r;
  r = get4 (fileref, &y);
  if (r != SCPE_OK)
    return r;
  *x = (*x << 4) | y;
  return SCPE_OK;
}

static t_stat
get16 (FILE *fileref, uint16 *x)
{
  uint16 y;
  t_stat r;
  r = get8 (fileref, x);
  if (r != SCPE_OK)
    return r;
  r = get8 (fileref, &y);
  if (r != SCPE_OK)
    return r;
  *x = (*x << 8) | y;
  return SCPE_OK;
}

static uint16 checksum;

static t_stat
get18 (FILE *fileref, uint16 *x)
{
  uint16 y;
  t_stat r;
  r = get6 (fileref, x);
  if (r != SCPE_OK)
    return r;
  r = get6 (fileref, &y);
  if (r != SCPE_OK)
    return r;
  *x = (*x << 6) | y;
  r = get6 (fileref, &y);
  if (r != SCPE_OK)
    return r;
  *x = (*x << 6) | y;
  checksum = (checksum + *x) & 0177777;
  return SCPE_OK;
}

static t_stat load_loader (FILE *f, int verbose)
{
  uint16 i, x, y, count, addr;
  t_stat r;

  x = 0;
  do {
    r = get4 (f, &y);
    if (r != SCPE_OK)
      return r;
    x = ((x << 4) + y) & 0177777;
  } while (x != 0147577);

  /* Read count and address. */
  r = get16 (f, &addr);
  if (r != SCPE_OK)
    return r;
  r = get16 (f, &count);
  if (r != SCPE_OK)
    return r;

  if (verbose)
    fprintf (stderr, "Loader: address %06o, %o words\n", addr, count);

  for (i = 1; i <= count; i++) {
    r = get16 (f, &x);
    if (r != SCPE_OK)
      return r;
    CRM[addr - i] = x;
  }

  return SCPE_OK;
}

static t_stat load_block (FILE *f, int verbose)
{
  uint16 i, x, y, type, count, addr;
  t_stat r;

  x = 0;
  do {
    r = get6 (f, &y);
    if (r != SCPE_OK)
      return r;
    x = ((x << 6) + y) & 0177777;
  } while (x != 0120116);

  checksum = 0;

  /* Read type. */
  r = get18 (f, &type);
  if (r != SCPE_OK)
    return r;

  switch (type) {
  case 0:
    r = get18 (f, &x);
    if (r != SCPE_OK)
      return r;
    if (verbose) {
      t_value val = x;
      fprintf (stderr, "Execute: instruction %06o\n", x);
      fprint_sym (stderr, 0, &val, 0, SWMASK ('M'));
      fputc ('\n', stderr);
    }
    return SCPE_EOF;
  case 1:
  case 2:
    break;
  default:
    return SCPE_FMT;
  }

  r = get18 (f, &addr);
  if (r != SCPE_OK)
    return r;
  r = get18 (f, &count);
  if (r != SCPE_OK)
    return r;

  if (type == 1) {
    if (verbose)
      fprintf (stderr, "Load control store: address %06o, %o words\n",
               addr, count);
    for (i = 0; i < count; i++) {
      r = get18 (f, &x);
      if (r != SCPE_OK)
        return r;
      CRM[(addr++) & 07777] = x;
    }
  } else if (type == 2) {
    if (verbose)
      fprintf (stderr, "Load RAM: address %06o, %o words\n",
               addr, count);
    for (i = 0; i < count; i++) {
      r = get18 (f, &x);
      if (r != SCPE_OK)
        return r;
      if ((addr & 0170000) == 0170000) {
        FONT[(addr++) & 07777] = x & 0377;
        FONT[(addr++) & 07777] = x >> 8;
        i++;
      } else
        MEM[(addr++) & 0177777] = x;
    }
  }

  r = get18 (f, &x);
  if (r != SCPE_OK)
    return r;
  /* Should be 0, but some blocks checksum to 1. */
  if (checksum != 0 && checksum != 1)
    return SCPE_CSUM;
  return SCPE_OK;
}

t_stat
sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
  int verbose = sim_switches & SWMASK ('V');
  t_stat r;

  r = load_loader (fileref, verbose);
  if (r != SCPE_OK)
    return r;

  for (;;) {
    r = load_block (fileref, verbose);
    if (r == SCPE_EOF)
      return SCPE_OK;
    if (r != SCPE_OK)
      return r;
  }
}

static uint16 null_read (uint16 reg)
{
  return 0;
}

static void null_write (uint16 reg, uint16 data)
{
}

t_bool build_dev_tab (void)
{
  TTDEV *ttdev;
  DEVICE *dev;
  int i, j;

  for (i = 0; i < 0100; i++)
    dev_tab[i] = &null_dev;

  for (i = 0; (dev = sim_devices[i]) != NULL; i++) {
    ttdev = (TTDEV *)dev->ctxt;
    if (ttdev != NULL) {
      for (j = 0; j < 4; j++)
        if (ttdev->reg[j] != 0)
          dev_tab[ttdev->reg[j]] = ttdev;
    }
  }

  return SCPE_OK;
}

static const char *register_names[] =
  {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "A-latch", "s11", "s12", "s13", "MAGIC", "CHARTV", "s16", "s17",
    "YCOR", "XCOR", "SCROLL", "XR", "UART", "DSR", "KEY", "d27",
    "d30", "d31", "d32", "d33", "d34", "d35", "d36", "d37",
    "scratch40", "scratch41", "scratch42", "scratch43", "scratch44", "scratch45", "scratch46", "scratch47",
    "scratch50", "scratch51", "scratch52", "scratch53", "scratch54", "scratch55", "scratch56", "scratch57",
    "scratch60", "scratch61", "scratch62", "scratch63", "scratch64", "scratch65", "scratch66", "scratch67",
    "scratch70", "scratch71", "scratch72", "scratch73", "scratch74", "scratch75", "scratch76", "scratch77"
  };

static t_stat
fprint_sto (FILE *of, uint16 insn)
{
  uint16 a = (insn >> 6) & 7;
  uint16 b = insn & 017;

  const char *op;
  switch (insn & 077060) {
  case 020000: op = "READ"; break;
  case 020020: op = "READD"; break;
  case 021000: op = "CREAD"; break;
  case 021020: op = "CREADD"; break;
  case 022040: op = "READI"; break;
  case 023040: op = "CREADI"; break;
  case 024000: op = "WRITE"; break;
  case 024020: op = "WRITED"; break;
  case 025000: op = "CWRITE"; break;
  case 025020: op = "CWRITED"; break;
  case 026040: op = "WRITEI"; break;
  case 027040: op = "CWRITEI"; break;
  case 074000: op = "GET"; break;
  default: fprintf (of, "???"); return SCPE_OK;
  }
  fprintf (of, "%s %s %s", op, register_names[a], register_names[b]);
  return SCPE_OK;
}

static t_stat
fprint_reg (FILE *of, uint16 insn)
{
  static const char *name[] =
    { "A", "ANDN", "AND", "NOR", "IOR", "XOR", "MROT", "??",
      "ROT", "DEC", "XADD", "ADD", "SUB", "XSUB", "INC", "ARS" };
  uint16 op = (insn >> 4) & 3;
  uint16 a = (insn >> 6) & 7;
  uint16 b = insn & 017;

  if (insn == 010000) {
    fprintf (of, "NOP");
    return SCPE_OK;
  } else if ((insn & 037060) == 01000) {
    sym_immediate = 1;
    fprintf (of, "LOD %s", register_names[a]);
    return SCPE_OK;
  }

  switch (insn & 030000) {
  case 000000: break;
  case 010000: fprintf (of, "T "); break;
  case 020000: return fprint_sto (of, insn);
  case 030000: fprintf (of, "IFC "); break;
  }

  sym_immediate = insn & 01000;

  op += (insn >> 8) & 014;
  fprintf (of, "%s%s %s %s", name[op], sym_immediate ? "I" : "",
           register_names[a], register_names[b]);
  return SCPE_OK;
}

static t_stat
fprint_dis (FILE *of, uint16 insn)
{
  fprintf (of, "DIS ");
  switch (insn & 01400) {
  case 00000: fprintf (of, "BUS"); break;
  case 00400: fprintf (of, "FLAGS"); break;
  case 01000: fprintf (of, "INTS"); break;
  case 01400: fprintf (of, "STARS"); break;
  }
  fprintf (of, " %o", (~insn >> 4) & 017);
  return SCPE_OK;
}

static t_stat
fprint_bus (FILE *of, uint16 insn)
{
  uint16 a = (insn >> 6) & 7;
  uint16 b = insn & 077;

  if ((insn & 076000) == 072000)
    return fprint_dis (of, insn);

  switch (insn) {
  case 0075400: fprintf (of, "MAGIC"); return SCPE_OK;
  case 0076014: fprintf (of, "MAGIC"); return SCPE_OK;
  case 0076015: fprintf (of, "CHARTV"); return SCPE_OK;
  case 0075500: fprintf (of, "CHARTV"); return SCPE_OK;
  case 0075600: fprintf (of, "POPJ"); return SCPE_OK;
  case 0076016: fprintf (of, "POPJ"); return SCPE_OK;
  case 0076716: fprintf (of, "POPJI"); return SCPE_OK;
  }

  fprintf (of, "%s ", insn & 02000 ? "PUT" : "GET");
  fprintf (of, "%s %s", register_names[a], register_names[b]);
  return SCPE_OK;
}

static t_stat
fprint_branch (FILE *of, uint16 insn, uint16 addr)
{
  static const char *condition[] =
    { "CC", "CS", "VS", "VC", "MI", "PL", "NE", "EQ",
      "GE", "LT", "IS", "IC", "XCI", "XSI", "FS", "FC" };
  uint16 target = insn & 03777;
  if (insn & 02000)
    target = target - 04000;
  target += addr + 1;
  fprintf (of, "B%s %06o", condition[(insn >> 11) & 017], target);
  return SCPE_OK;
}

static t_stat
fprint_cpu (FILE *of, uint16 insn, uint16 addr)
{
  switch ((insn >> 12) & 017) {
  case 000: case 001: case 002: case 003:
    return fprint_reg (of, insn);
  case 004:
    fprintf (of, "PUSHJ %04o", insn & 07777);
    break;
  case 005:
    fprintf (of, "JUMP %04o", insn & 07777);
    break;
  case 006:
    fprintf (of, "(undef)");
    break;
  case 007:
    fprint_bus (of, insn);
    break;
  case 010: case 011: case 012: case 013:
  case 014: case 015: case 016: case 017:
    fprint_branch (of, insn, addr);
    break;
  }
  return SCPE_OK;
}

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
                   UNIT *uptr, int32 sw)
{
  t_stat reason;

  if ((reason = build_dev_tab ()) != SCPE_OK)
    return reason;

  if (sym_addr == addr - 1 && sym_immediate) {
    fprintf (of, "%06o", (unsigned)*val);
    return SCPE_OK;
  }

  sym_addr = addr;
  sym_immediate = 0;

  if (sw & SWMASK ('M'))
    return fprint_cpu (of, *val, addr);
  return SCPE_ARG;
}

t_stat parse_sym (CONST char *cptr, t_addr addr, UNIT *uptr,
                  t_value *val, int32 sw)
{
  t_stat reason;
  *val = get_uint (cptr, 8, ~0, &reason);
  if (reason != SCPE_OK)
    return reason;
  return 0;
}
