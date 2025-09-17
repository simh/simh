/* linc_sys.c: LINC simulator interface

   Copyright (c) 2025, Lars Brinkhoff

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

   17-Sept-25    LB      New simulator.
*/

#include "linc_defs.h"

int32 sim_emax = 1;
char sim_name[] = "LINC";

uint16 M[MEMSIZE];
REG *sim_PC = &cpu_reg[0];

DEVICE *sim_devices[] = {
  &cpu_dev,
  &crt_dev,
  &dpy_dev,
  &kbd_dev,
  &tape_dev,
  &tty_dev,
  NULL
};
  
const char *sim_stop_messages[SCPE_BASE] = {
  "Unknown error",
  "HALT instruction",
  "Breakpoint",
  "Read Breakpoint",
  "Write Breakpoint"
};

static t_stat
get_binary_word(FILE *fileref, uint16 *x)
{
  uint16 y;
  int c = Fgetc(fileref);
  if (c == EOF)
    return SCPE_EOF;
  y = c & 0xFF;
  c = Fgetc(fileref);
  if (c == EOF)
    return SCPE_IOERR;
  if (c & 0xF0)
    return SCPE_FMT;
  *x = y | (c << 8);
  return SCPE_OK;
}

static t_stat
get_octal_word(FILE *fileref, uint16 *x)
{
  uint16 y, i;
  int c;
  for (i = 0;;) {
    c = Fgetc(fileref);
    if (c == EOF)
      return SCPE_EOF;
    if (c >= '0' && c <= '9') {
      y = c - '0';
      i++;
      break;
    }
  }
  for (; i < 4;) {
    c = Fgetc(fileref);
    if (c == EOF)
      return SCPE_IOERR;
    if (c < '0' || c > '9')
      break;
    y <<= 3;
    y |= c - '0';
    i++;
  }

  *x = y;
  return SCPE_OK;
}

t_stat
sim_load(FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
  t_stat (*get_word)(FILE *fileref, uint16 *x) = get_binary_word;
  t_addr addr, length = MEMSIZE, start = 0, end;
  int16 forward_offset = 0, reverse_offset;
  uint16 block_size;
  long offset = 0;
  t_stat stat;

  if (sim_switches & SWMASK('E')) {
    stat = tape_metadata(fileref, &block_size, &forward_offset, &reverse_offset);
    if (stat != SCPE_OK)
      return stat;
    if (block_size != 256)
      return SCPE_FMT;
  }

  if (sim_switches & SWMASK('O'))
    get_word = get_octal_word;

  while (*cptr !=0) {
    char gbuf[100];
    cptr = get_glyph(cptr, gbuf, 0);
    if (strncmp(gbuf, "START=", 6) == 0)
      start = (t_addr)get_uint(gbuf + 6, 8, ~0, &stat);
    else if (strncmp(gbuf, "OFFSET=", 7) == 0)
      offset = 2 * (long)get_uint(gbuf + 7, 8, ~0, &stat);
    else if (strncmp(gbuf, "BLOCK=", 6) == 0)
      offset = 512 * ((long)get_uint(gbuf + 6, 8, ~0, &stat) - forward_offset);
    else if (strncmp(gbuf, "LENGTH=", 7) == 0)
      length = (t_addr)get_uint(gbuf + 7, 8, ~0, &stat);
    else
      return SCPE_ARG;
  }

  end = start + length;
  if (end > MEMSIZE)
    end = MEMSIZE;

  sim_fseek(fileref, offset, SEEK_SET);

  for (addr = start; addr < end; addr++) {
    uint16 x;
    t_stat stat = get_word(fileref, &x);
    if (stat == SCPE_EOF)
      return SCPE_OK;
    if (stat != SCPE_OK)
      return stat;
    M[addr] = x;
  }

  return SCPE_OK;
}

t_bool build_dev_tab(void)
{
  DEVICE *dev;
  int i;

  for (i = 0; (dev = sim_devices[i]) != NULL; i++) {
    ;
  }

  return SCPE_OK;
}

static t_stat fprint_next(FILE *of, uint16 addr)
{
  fprintf(of, "\n");
  fprint_val(of, ++addr & XMASK, 8, 10, PV_LEFT);
  fprintf(of, ":\t%04o", M[addr]);
  return -1;
}

static void fprint_misc(FILE *of, uint16 insn)
{
  switch (insn) {
  case 00000:
    fprintf(of, "HLT");
    break;
  case 00005:
    fprintf(of, "ZTA");
    break;
  case 00010:
    fprintf(of, "ENI");
    break;
  case 00011:
    fprintf(of, "CLR");
    break;
  case 00012:
    fprintf(of, "DIN");
    break;
  case 00014:
    fprintf(of, "ATR");
    break;
  case 00015:
    fprintf(of, "RTA");
    break;
  case 00016:
    fprintf(of, "NOP");
    break;
  case 00017:
    fprintf(of, "COM");
    break;
  default:
    fprintf(of, "MSC %o", insn);
    break;
  }
}

static t_stat fprint_index(FILE *of, uint16 insn, uint16 addr)
{
  if (insn & IMASK)
    fprintf(of, " i");
  if (insn & BMASK)
    fprintf(of, " %o", insn & BMASK);
  else
    return fprint_next(of, addr);
  return SCPE_OK;
}

static void fprint_set(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "SET");
  fprint_index(of, insn, addr);
  fprint_next(of, addr);
}

static void fprint_sam(FILE *of, uint16 insn)
{
  fprintf(of, "SAM%s %o", insn & IMASK ? " i" : "", insn & BMASK);
}

static t_stat fprint_dis(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "DIS");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_xsk(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "XSK");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_rol(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "ROL");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_ror(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "ROR");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_scr(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "SCR");
  return fprint_index(of, insn, addr);
}

static void fprint_skip(FILE *of, uint16 insn)
{
  char beta[3];
  switch (insn & 057) {
  case 000: case 001: case 002: case 003: case 004: case 005: case 006: case 007:
  case 010: case 011: case 012: case 013:
    fprintf(of, "SXL");
    snprintf(beta, sizeof beta, "%o", insn & BMASK);
    break;
  case 015:
    fprintf(of, "KST");
    break;
  case 040: case 041: case 042: case 043: case 044: case 045:
    fprintf(of, "SNS ");
    snprintf(beta, sizeof beta, "%o", insn & 7);
    break;
  case 046:
    fprintf(of, "PIN");
    break;
  case 050:
    fprintf(of, "AZE");
    break;
  case 051:
    fprintf(of, "APO");
    break;
  case 052:
    fprintf(of, "LZE");
    break;
  case 053:
    fprintf(of, "IBZ");
    break;
  case 054:
    fprintf(of, "OVF");
    break;
  case 055:
    fprintf(of, "ZZZ");
    break;
  default:
    fprintf(of, "%04o", insn);
    return;
  }
  if (insn & IMASK)
    fprintf(of, " i" );
  fprintf(of, " %s", beta);
}

static void fprint_opr(FILE *of, uint16 insn)
{
  switch (insn & 07757) {
  case 0500: case 0501: case 0502: case 0503: case 0504: case 0505: case 0506: case 0507:
  case 0510: case 0511: case 0512: case 0513:
    break;
  case 0535:
  case 0515:
    fprintf(of, "KBD ");
    break;
  case 0516:
    fprintf(of, "RSW ");
    break;
  case 0517:
    fprintf(of, "LSW ");
    break;
  default:
    fprintf(of, "%04o", insn);
    break;
  }
  if (insn & IMASK)
    fprintf(of, "i" );
}

static void fprint_lmb(FILE *of, uint16 insn)
{
  fprintf(of, "LMB ");
}

static void fprint_umb(FILE *of, uint16 insn)
{
  fprintf(of, "UMB ");
}

static void fprint_tape(FILE *of, uint16 insn, uint16 addr)
{
  switch (insn & 0707) {
  case 0700:
    fprintf(of, "RDC");
    break;
  case 0701:
    fprintf(of, "RCG");
    break;
  case 0702:
    fprintf(of, "RDE");
    break;
  case 0703:
    fprintf(of, "MTB");
    break;
  case 0704:
    fprintf(of, "WRC");
    break;
  case 0705:
    fprintf(of, "WCG");
    break;
  case 0706:
    fprintf(of, "WRI");
    break;
  case 0707:
    fprintf(of, "CHK");
    break;
  }
  if (insn & IMASK)
    fprintf(of, " i");
  if (insn & UMASK)
    fprintf(of, " u");
  fprint_next(of, addr);
}

static t_stat fprint_lda(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "LDA");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_sta(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "STA");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_ada(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "ADA");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_adm(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "ADM");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_lam(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "LAM");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_mul(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "MUL");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_ldh(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "LDH");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_sth(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "STH");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_shd(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "SHD");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_sae(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "SAE");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_sro(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "SRO");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_bcl(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "BCL");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_bse(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "BSE");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_bco(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "BCO");
  return fprint_index(of, insn, addr);
}

static t_stat fprint_dsc(FILE *of, uint16 insn, uint16 addr)
{
  fprintf(of, "DSC");
  return fprint_index(of, insn, addr);
}

static void fprint_add(FILE *of, uint16 insn)
{
  fprintf(of, "ADD %04o", insn & XMASK);
}

static void fprint_stc(FILE *of, uint16 insn)
{
  fprintf(of, "STC %04o", insn & XMASK);
}

static void fprint_jmp(FILE *of, uint16 insn)
{
  fprintf(of, "JMP %04o", insn & XMASK);
}

t_stat fprint_sym(FILE *of, t_addr addr, t_value *val, UNIT *uptr, int32 sw)
{
  t_stat stat;

  if ((sw & SWMASK ('M')) == 0)
    return SCPE_ARG;

  if ((stat = build_dev_tab()) != SCPE_OK)
    return stat;

  switch (*val & 07740) {
  case 00000:
    fprint_misc(of, *val);
    break;
  case 00040:
    fprint_set(of, *val, addr);
    return -1;
  case 00100:
    fprint_sam(of, *val);
    break;
  case 00140:
    fprint_dis(of, *val, addr);
    break;
  case 00200:
    fprint_xsk(of, *val, addr);
    break;
  case 00240:
    fprint_rol(of, *val, addr);
    break;
  case 00300:
    fprint_ror(of, *val, addr);
    break;
  case 00340:
    fprint_scr(of, *val, addr);
    break;
  case 00400:
  case 00440:
    fprint_skip(of, *val);
    break;
  case 00500:
  case 00540:
    fprint_opr(of, *val);
    break;
  case 00600:
    fprint_lmb(of, *val);
    break;
  case 00640:
    fprint_umb(of, *val);
    break;
  case 00700:
  case 00740:
    fprint_tape(of, *val, addr);
    break;
  case 01000:
    return fprint_lda(of, *val, addr);
  case 01040:
    return fprint_sta(of, *val, addr);
  case 01100:
    return fprint_ada(of, *val, addr);
  case 01140:
    return fprint_adm(of, *val, addr);
  case 01200:
    return fprint_lam(of, *val, addr);
  case 01240:
    return fprint_mul(of, *val, addr);
  case 01300:
    return fprint_ldh(of, *val, addr);
  case 01340:
    return fprint_sth(of, *val, addr);
  case 01400:
    return fprint_shd(of, *val, addr);
  case 01440:
    return fprint_sae(of, *val, addr);
  case 01500:
    return fprint_sro(of, *val, addr);
  case 01540:
    return fprint_bcl(of, *val, addr);
  case 01600:
    return fprint_bse(of, *val, addr);
  case 01640:
    return fprint_bco(of, *val, addr);
  case 01740:
    return fprint_dsc(of, *val, addr);
  case 02000: case 02040: case 02100: case 02140: case 02200: case 02240: case 02300: case 02340:
  case 02400: case 02440: case 02500: case 02540: case 02600: case 02640: case 02700: case 02740:
  case 03000: case 03040: case 03100: case 03140: case 03200: case 03240: case 03300: case 03340:
  case 03400: case 03440: case 03500: case 03540: case 03600: case 03640: case 03700: case 03740:
    fprint_add(of, *val);
    break;
  case 04000: case 04040: case 04100: case 04140: case 04200: case 04240: case 04300: case 04340:
  case 04400: case 04440: case 04500: case 04540: case 04600: case 04640: case 04700: case 04740:
  case 05000: case 05040: case 05100: case 05140: case 05200: case 05240: case 05300: case 05340:
  case 05400: case 05440: case 05500: case 05540: case 05600: case 05640: case 05700: case 05740:
    fprint_stc(of, *val);
    break;
  case 06000: case 06040: case 06100: case 06140: case 06200: case 06240: case 06300: case 06340:
  case 06400: case 06440: case 06500: case 06540: case 06600: case 06640: case 06700: case 06740:
  case 07000: case 07040: case 07100: case 07140: case 07200: case 07240: case 07300: case 07340:
  case 07400: case 07440: case 07500: case 07540: case 07600: case 07640: case 07700: case 07740:
    fprint_jmp(of, *val);
    break;
  }

  return SCPE_OK;
}

struct symbol {
  const char *name;
  uint16 value;
};

static const struct symbol symbols[] = {
  { "U",   00010 },
  { "I",   00020 },
  { "HLT", 00000 },
  { "ZTA", 00005 },
  { "CLR", 00011 },
  { "DIN", 00012 },
  { "ATR", 00014 },
  { "RTA", 00015 },
  { "NOP", 00016 },
  { "COM", 00017 },
  { "SET", 00040 },
  { "SAM", 00100 },
  { "DIS", 00140 },
  { "XSK", 00200 },
  { "ROL", 00240 },
  { "ROR", 00300 },
  { "SCR", 00340 },
  { "SXL", 00400 },
  { "KST", 00415 },
  { "SNS", 00440 },
  { "AZE", 00450 },
  { "APO", 00451 },
  { "LZE", 00452 },
  { "IBZ", 00453 },
  { "OVF", 00454 },
  { "ZZZ", 00455 },
  { "OPR", 00500 },
  { "KBD", 00515 },
  { "RSW", 00516 },
  { "LSW", 00517 },
  { "LMB", 00600 },
  { "UMB", 00640 },
  { "RDC", 00700 },
  { "RCG", 00701 },
  { "RDE", 00702 },
  { "MTB", 00703 },
  { "WRC", 00704 },
  { "WCG", 00705 },
  { "WRI", 00706 },
  { "CHK", 00707 },
  { "LDA", 01000 },
  { "STA", 01040 },
  { "ADA", 01100 },
  { "ADM", 01140 },
  { "LAM", 01200 },
  { "MUL", 01240 },
  { "LDH", 01300 },
  { "STH", 01340 },
  { "SHD", 01400 },
  { "SAE", 01440 },
  { "SRO", 01500 },
  { "BCL", 01540 },
  { "BSE", 01600 },
  { "BCO", 01640 },
  { "DSC", 01740 },
  { "ADD", 02000 },
  { "STC", 04000 },
  { "JMP", 06000 }
};

t_stat parse_sym(CONST char *cptr, t_addr addr, UNIT *uptr,
                  t_value *val, int32 sw)
{
  char gbuf[CBUFSIZE];
  t_value val2;
  t_stat stat;
  int i;

  *val = get_uint(cptr, 8, ~0, &stat);
  if (*val > 07777)
    return SCPE_ARG;
  if (stat == SCPE_OK)
    return SCPE_OK;

  if (*cptr == '-') {
    stat = parse_sym(cptr + 1, addr, uptr, val, sw);
    if (stat != SCPE_OK)
      return stat;
    *val ^= 07777;
    if (stat == SCPE_OK)
      return SCPE_OK;
  }

  cptr = get_glyph(cptr, gbuf, 0);
  for (i = 0; i < sizeof symbols / sizeof symbols[0]; i++) {
    if (strcmp(gbuf, symbols[i].name) != 0)
      continue;
    *val = symbols[i].value;
    if (*cptr) {
      stat = parse_sym(cptr, addr, uptr, &val2, sw);
      if (stat != SCPE_OK)
        return stat;
      *val |= val2;
    }
    return SCPE_OK;
  }

  return SCPE_ARG;
}
