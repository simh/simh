/* imlac_sys.c: Imlac simulator interface

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

   21-Apr-20    LB      New simulator.
*/

#include "imlac_defs.h"

int32 sim_emax = 1;
char sim_name[] = "Imlac";

uint16 M[040000];
SUBDEV *dev_tab[0100];
REG *sim_PC = &cpu_reg[0];

DEVICE *sim_devices[] = {
  &cpu_dev,
  &rom_dev,
  &dp_dev,    /* 0-1 */
  &crt_dev,
  &kbd_dev,   /* 2 */
  &tty_dev,   /* 3-4 */
  &ptr_dev,   /* 5-6 */
  &sync_dev,  /* 7, 30 */
  &irq_dev,   /* 10, 14, 16 */
  /* &prot_dev, / * 11 */
  /* &pen_dev, / * 13 */
  &ptp_dev,   /* 27 */
  /* &mse_dev, / * 70, 73 */
  &bel_dev,   /* 71 */
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
  int c;
  for (;;) {
    c = Fgetc (fileref);
    if (c == EOF)
      return SCPE_IOERR;
    if ((c & 0160) == 0100) {
      *x = c & 017;
      return SCPE_OK;
    }
  }
}

static t_stat
get8 (FILE *fileref, uint16 *x)
{
  uint16 y;
  if (get4 (fileref, x) != SCPE_OK)
    return SCPE_IOERR;
  if (get4 (fileref, &y) != SCPE_OK)
    return SCPE_IOERR;
  *x = (*x << 4) | y;
  return SCPE_OK;
}

static t_stat
get16 (FILE *fileref, uint16 *x)
{
  uint16 y;
  if (get8 (fileref, x) != SCPE_OK)
    return SCPE_IOERR;
  if (get8 (fileref, &y) != SCPE_OK)
    return SCPE_IOERR;
  *x = (*x << 8) | y;
  return SCPE_OK;
}

static t_stat
load_stty (FILE *fileref)
{
  int verbose = sim_switches & SWMASK ('V');
  uint16 *PC = (uint16 *)sim_PC->loc;
  uint16 x, count, addr;
  uint32 csum;
  int i;

  /* Discard block loader. */
  for (i = 0; i < 65; i++) {
    if (get16 (fileref, &x) != SCPE_OK)
      return SCPE_IOERR;
  }

  for (;;) {
    /* Read count and address. */
    if (get8 (fileref, &count) != SCPE_OK)
      return SCPE_IOERR;
    if (get16 (fileref, &addr) != SCPE_OK)
      return SCPE_IOERR;

    /* Address all ones means done. */
    if (addr == 0177777) {
      *PC = 077713 & memmask;
      return SCPE_OK;
    }

    if (verbose)
      sim_messagef (SCPE_OK, "Address %06o: %d words.\n", addr, count);

    csum = 0;
    for (i = 0; i < count; i++) {
      if (get16 (fileref, &x) != SCPE_OK)
        return SCPE_IOERR;
      M[(addr++) & memmask] = x;
      csum += x;
      if (csum & 0200000) {
        csum++;
        csum &= 0177777;
      }
    }

    if (get16 (fileref, &x) != SCPE_OK)
      return SCPE_IOERR;
    if (x != csum)
      return SCPE_CSUM;
  }
}

t_stat
sim_load (FILE *fileref, CONST char *cptr, CONST char *fnam, int flag)
{
  if (sim_switches & SWMASK ('T'))
    ;
  if (sim_switches & SWMASK ('S'))
    ;
  if (sim_switches & SWMASK ('M'))
    ;
  if (sim_switches & SWMASK ('P'))
    ;

  return load_stty (fileref);
}

t_bool build_dev_tab (void)
{
  DEVICE *dev;
  IMDEV *imdev;
  int i, j;

  memset (dev_tab, 0, sizeof dev_tab);

  for (i = 0; (dev = sim_devices[i]) != NULL; i++) {
    imdev = (IMDEV *)dev->ctxt;
    if (imdev != NULL) {
      for (j = 0; j < imdev->codes; j++)
        dev_tab[imdev->subdev[j].num] = &imdev->subdev[j];
    }
  }

  return SCPE_OK;
}

static t_stat fprint_class1 (FILE *of, uint16 insn)
{ 
  switch (insn & 0777) {
  case 0000: fprintf (of, "NOP"); break;
  case 0001: fprintf (of, "CLA"); break;
  case 0002: fprintf (of, "CMA"); break;
  case 0003: fprintf (of, "STA"); break;
  case 0004: fprintf (of, "IAC"); break;
  case 0005: fprintf (of, "COA"); break;
  case 0006: fprintf (of, "CIA"); break;
  case 0010: fprintf (of, "CLL"); break;
  case 0011: fprintf (of, "CAL"); break;
  case 0020: fprintf (of, "CML"); break;
  case 0030: fprintf (of, "STL"); break;
  case 0040: fprintf (of, "ODA"); break;
  case 0041: fprintf (of, "LDA"); break;
  default:   fprintf (of, "%06o", insn); break;
  }
  return SCPE_OK;
}

static t_stat fprint_class2 (FILE *of, uint16 insn)
{ 
  switch (insn & 0770) {
  case 0000: fprintf (of, "RAL %o", insn & 7); break;
  case 0020: fprintf (of, "RAR %o", insn & 7); break;
  case 0040: fprintf (of, "SAL %o", insn & 7); break;
  case 0060: fprintf (of, "SAR %o", insn & 7); break;
  case 0100: fprintf (of, "DON"); break;
  default:   fprintf (of, "%06o", insn); break;
  }
  return SCPE_OK;
}

static t_stat fprint_class3 (FILE *of, uint16 insn)
{ 
  switch (insn & 0177777) {
  case 0002001: fprintf (of, "ASZ"); break;
  case 0102001: fprintf (of, "ASN"); break;
  case 0002002: fprintf (of, "ASP"); break;
  case 0102002: fprintf (of, "ASM"); break;
  case 0002004: fprintf (of, "LSZ"); break;
  case 0102004: fprintf (of, "LSN"); break;
  case 0002010: fprintf (of, "DSF"); break;
  case 0102010: fprintf (of, "DSN"); break;
  case 0002020: fprintf (of, "KSF"); break;
  case 0102020: fprintf (of, "KSN"); break;
  case 0002040: fprintf (of, "RSF"); break;
  case 0102040: fprintf (of, "RSN"); break;
  case 0002100: fprintf (of, "TSF"); break;
  case 0102100: fprintf (of, "TSN"); break;
  case 0002200: fprintf (of, "SSF"); break;
  case 0102200: fprintf (of, "SSN"); break;
  case 0002400: fprintf (of, "HSF"); break;
  case 0102400: fprintf (of, "HSN"); break;
  default:      fprintf (of, "%06o", insn); break;
  }
  return SCPE_OK;
}

static t_stat fprint_iot (FILE *of, uint16 insn)
{ 
  SUBDEV *imdev;

  imdev = dev_tab[(insn >> 3) & 077];
  if (imdev != NULL && imdev->mnemonics[insn & 7] != NULL) {
    fprintf (of, "%s", imdev->mnemonics[insn & 7]);
    return SCPE_OK;
  }

  fprintf (of, "IOT %03o", insn & 0777);
  return SCPE_OK;
}

static t_stat fprint_opr (FILE *of, uint16 insn)
{
  switch ((insn >> 9) & 0177) {
  case 0000:
    fprintf (of, "HLT ");
    if (insn == 0)
      break;
    /* Fall through. */
  case 0100:
    return fprint_class1 (of, insn);
  case 0003:
    return fprint_class2 (of, insn);
  case 0002:
  case 0102:
    return fprint_class3 (of, insn);
  case 0001:
    return fprint_iot (of, insn);
  default:
    fprintf (of, "%06o", insn);
    break;
  }

  return SCPE_OK;
}

static t_stat
fprint_cpu (FILE *of, uint16 insn, uint16 addr)
{
  switch ((insn >> 9) & 074) {
  case 000:
    return fprint_opr (of, insn);
  case 004:
    if (insn & 0100000)
      fprintf (of, "LWC %o", insn & 03777);
    else
      fprintf (of, "LAW %o", insn & 03777);
    return SCPE_OK;
  case 010:
    fprintf (of, "JMP");
    break;
  case 020:
    fprintf (of, "DAC");
    break;
  case 024:
    fprintf (of, "XAM");
    break;
  case 030:
    fprintf (of, "ISZ");
    break;
  case 034:
    fprintf (of, "JMS");
    break;
  case 044:
    fprintf (of, "AND");
    break;
  case 050:
    fprintf (of, "IOR");
    break;
  case 054:
    fprintf (of, "XOR");
    break;
  case 060:
    fprintf (of, "LAC");
    break;
  case 064:
    fprintf (of, "ADD");
    break;
  case 070:
    fprintf (of, "SUB");
    break;
  case 074:
    fprintf (of, "SAM");
    break;
  default:
    fprintf (of, "%06o", insn);
    break;
  }
  
  fprintf (of, " ");
  if (insn & 0100000)
    fprintf (of, "@");
  fprintf (of, "%o", (insn & 03777) | (addr & 014000));

  return SCPE_OK;
}

static t_stat
fprint_dopr (FILE *of, uint16 insn)
{
  if (insn == 04000) {
    fprintf (of, "DNOP");
    return SCPE_OK;
  }

  switch (insn & 00014) {
  case 000:
    if (insn & 1)
      fprintf (of, "DADR ");
    break;
  case 004:
    fprintf (of, "DSTS %o ", insn & 3);
    break;
  case 010:
    fprintf (of, "DSTB %o ", insn & 3);
    break;
  case 014:
    fprintf (of, "Unknown DP instruction %06o", insn);
    break;
  }
  if (insn & 00020)
    fprintf (of, "DDSP ");
  if (insn & 00040)
    fprintf (of, "DRJM ");
  if (insn & 00100)
    fprintf (of, "DDYM ");
  if (insn & 00200)
    fprintf (of, "DDXM ");
  if (insn & 00400)
    fprintf (of, "DIYM ");
  if (insn & 01000)
    fprintf (of, "DIXM ");
  if (insn & 02000)
    fprintf (of, "DHVC ");
  if ((insn & 04000) == 0)
    fprintf (of, "DHLT ");
    
  return SCPE_OK;
}

static t_stat
fprint_inc_byte (FILE *of, uint16 byte)
{
  if (byte & 0200) {
    if (byte == 0200) {
      fprintf (of, "P");
      return SCPE_OK;
    }

    fprintf (of, "%s", byte & 0100 ? "B" : "D");
    if (byte & 00040)
      fprintf (of, "M");
    fprintf (of, "%o", (byte >> 3) & 3);
    if (byte & 00004)
      fprintf (of, "M");
    fprintf (of, "%o", byte & 3);
  } else {
    if (byte == 0140)
      fprintf (of, "X");
    else if (byte == 0060)
      fprintf (of, "E");
    else if (byte == 0100)
      fprintf (of, "T");
    else if (byte == 0111)
      fprintf (of, "N");
    else if (byte == 0151)
      fprintf (of, "R");
    else if (byte == 0171)
      fprintf (of, "F");
    else {
      if (byte & 0100)
        fprintf (of, "ESC ");
      if (byte & 0040)
        fprintf (of, "RJM ");
      if (byte & 0020)
        fprintf (of, "+X ");
      if (byte & 0010)
        fprintf (of, "0X ");
      if (byte & 0004)
        fprintf (of, "PPM ");
      if (byte & 0002)
        fprintf (of, "+Y ");
      if (byte & 0001)
        fprintf (of, "0Y ");
    }
  }

  return SCPE_OK;
}

static t_stat
fprint_deim (FILE *of, uint16 insn)
{
  fprintf (of, "DEIM ");
  fprint_inc_byte (of, (insn >> 8) & 0377);
  fprintf (of, ",");
  fprint_inc_byte (of, insn & 0377);
  return SCPE_OK;
}

static t_stat
fprint_dp_opt (FILE *of, uint16 insn)
{
  switch (insn) {
  case 077771:
    fprintf (of, "DGD");
    break;
  case 077775:
    fprintf (of, "DGB");
    break;
  default:
    fprintf (of, "Unknown DP instruction: %06o", insn);
    break;
  }

  return SCPE_OK;
}

static t_stat
fprint_dp (FILE *of, uint16 insn, uint16 addr)
{
  switch ((insn >> 12) & 7) {
  case 0:
    return fprint_dopr (of, insn);
  case 1:
    fprintf (of, "DLXA %o", insn & 07777);
    break;
  case 2:
    fprintf (of, "DLYA %o", insn & 07777);
    break;
  case 3:
    return fprint_deim (of, insn);
  case 4:
    fprintf (of, "DLVH %04o, %06o, %06o",
             insn & 07777, M[addr + 1], M[addr + 2]);
    return -2;
  case 5:
    fprintf (of, "DJMS %o", insn & 07777);
    break;
  case 6:
    fprintf (of, "DJMP %o", insn & 07777);
    break;
  case 7:
    return fprint_dp_opt (of, insn);
  }

  return SCPE_OK;
}

static t_stat fprint_inc (FILE *of, uint16 insn)
{
  fprintf (of, "INC ");
  fprint_inc_byte (of, (insn >> 8) & 0377);
  fprintf (of, ",");
  fprint_inc_byte (of, insn & 0377);
  return SCPE_OK;
}

t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
                   UNIT *uptr, int32 sw)
{
  t_stat reason;

  if ((reason = build_dev_tab ()) != SCPE_OK)
    return reason;

  if (sw & SWMASK ('M'))
    return fprint_cpu (of, *val, addr);
  else if (sw & SWMASK ('D'))
    return fprint_dp (of, *val, addr);
  else if (sw & SWMASK ('I'))
    return fprint_inc (of, *val);
  else
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
