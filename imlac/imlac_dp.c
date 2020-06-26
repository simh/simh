/* imlac_dp.c: Imlac display processor

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

/* Debug */
#define DBG             0001

static t_addr DPC;
static t_addr DT[8];
static uint16 SP = 0;
static uint16 ON = 0;
static uint16 HALT = 0;
static uint16 MODE = 0;
static uint16 XA, YA;
static uint16 SCALE = 2;
static uint16 BLOCK = 0;
static uint16 MIT8K;
static uint16 SGR;
static uint16 SYNC = 1;

/* Function declaration. */
static uint16 dp_iot (uint16, uint16);
static t_stat dp_svc (UNIT *uptr);
static uint16 sync_iot (uint16, uint16);
static t_stat sync_svc (UNIT *uptr);

static IMDEV dp_imdev = {
  3,
  { { 0000, dp_iot, { NULL, NULL, NULL, "DLA" } },
    { 0001, dp_iot, { NULL, "CTB", "DOF", NULL } },
    { 0030, dp_iot, { NULL, NULL, NULL, "DCF" } } }
};

static UNIT dp_unit = {
  UDATA (&dp_svc, UNIT_IDLE, 0)
};

static REG dp_reg[] = {
  { ORDATAD (DPC, DPC, 16, "Display program counter") },
  { ORDATAD (ON, ON, 1, "Display on") },
  { ORDATAD (HALT, HALT, 1, "Display halted") },
  { ORDATAD (MODE, MODE, 1, "Display mode") },
  { BRDATAD (DT, DT, 8, 16, 8, "Return address stack") },
  { ORDATAD (SP, SP, 3, "Stack pointer") },
  { ORDATAD (XA, XA, 11, "X accumulator") },
  { ORDATAD (YA, YA, 11, "Y accumulator") },
  { ORDATAD (SCALE, SCALE, 3, "Scale") },
  { ORDATAD (BLOCK, BLOCK, 3, "Block") },
  { ORDATAD (MIT8K, MIT8K, 1, "MIT 8K addressing") },
  { ORDATAD (SGR, SGR, 1, "Suppressed grid mode") },
  { NULL }
};

static DEBTAB dp_deb[] = {
  { "DBG", DBG },
  { NULL, 0 }
};

DEVICE dp_dev = {
  "DP", &dp_unit, dp_reg, NULL,
  1, 8, 16, 1, 8, 16,
  NULL, NULL, NULL,
  NULL, NULL, NULL, &dp_imdev, DEV_DEBUG, 0, dp_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static UNIT sync_unit = {
  UDATA (&sync_svc, UNIT_IDLE, 0)
};

static REG sync_reg[] = {
  { ORDATAD (SYNC, SYNC, 1, "Flag") },
  { NULL }
};

static IMDEV sync_imdev = {
  1,
  { { 0007, sync_iot, { NULL, "SCF", "IOS", NULL } } }
};

static DEBTAB sync_deb[] = {
  { "DBG", DBG },
  { NULL, 0 }
};

DEVICE sync_dev = {
  "SYNC", &sync_unit, sync_reg, NULL,
  1, 8, 16, 1, 8, 16,
  NULL, NULL, NULL,
  NULL, NULL, NULL,
  &sync_imdev, DEV_DEBUG, 0, sync_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

void
dp_on (int flag)
{
  if (!ON && flag) {
    SP = 0;
    MIT8K = 0;
    sim_activate_abs (&dp_unit, 0);
    sim_debug (DBG, &dp_dev, "Display on\n");
  } else if (ON && !flag) {
    sim_cancel (&dp_unit);
    sim_debug (DBG, &dp_dev, "Display off\n");
    crt_idle ();
    if (SYNC && HALT)
      flag_on (FLAG_SYNC);
  }
  ON = flag;
}

uint16
dp_is_on (void)
{
  return ON;
}

static uint16
dp_iot (uint16 insn, uint16 AC)
{
  if ((insn & 0771) == 0001) { /* DLZ */
    sim_debug (DBG, &dp_dev, "DPC cleared\n");
    DPC = 0;
  }
  if ((insn & 0772) == 0002) { /* DLA */
    sim_debug (DBG, &dp_dev, "DPC set to %06o\n", AC & memmask);
    DPC = AC & memmask;
    BLOCK = (AC >> 12) & 3;
  }
  if ((insn & 0771) == 0011) { /* CTB */
    ;
  }
  if ((insn & 0772) == 0012) { /* DOF */
    dp_on (0);
  }
  if ((insn & 0774) == 0304) { /* DCF */
    HALT = 0;
  }
  return AC;
}

static t_stat
dp_opr(uint16 insn)
{
  if ((insn & 04000) == 0) {
    sim_debug (DBG, &dp_dev, "DHLT ");
    HALT = 1;
  }
  else if (insn == 04000)
    sim_debug (DBG, &dp_dev, "DNOP");
  dp_on ((insn & 04000) != 0); /* DHLT */

  switch (insn & 00014) {
  case 000:
    if (insn & 1) {
      sim_debug (DBG, &dp_dev, "DADR ");
      MIT8K = !MIT8K; 
    }
    break;
  case 004:
    sim_debug (DBG, &dp_dev, "DSTS%o ", insn & 3);
    SCALE = insn & 3;
    if (SCALE == 0)
      SCALE = 1;
    else
      SCALE *= 2;
    break;
  case 010:
    sim_debug (DBG, &dp_dev, "DSTB%o ", insn & 3);
    BLOCK = insn & 3;
    break;
  case 014:
    sim_debug (DBG, &dp_dev, "DSTL%o ", insn & 3);
    /* TODO: Light pen. */
    break;
  }
  if (insn & 00020) { /* DDSP */
    sim_debug (DBG, &dp_dev, "DDSP ");
    crt_point (XA, YA);
  }
  if (insn & 00040) { /* DRJM */
    sim_debug (DBG, &dp_dev, "DRJM ");
    if (SP > 0)
      DPC = DT[--SP];
    else
      sim_debug (DBG, &dp_dev, "stack underflow");
  }
  if (insn & 00100) { /* DDYM */
    sim_debug (DBG, &dp_dev, "DDYM ");
    YA -= 040;
  }
  if (insn & 00200) { /* DDXM */
    sim_debug (DBG, &dp_dev, "DDXM ");
    XA -= 040;
  }
  if (insn & 00400) { /* DIYM */
    sim_debug (DBG, &dp_dev, "DIYM ");
    YA += 040;
  }
  if (insn & 01000) { /* DIXM */
    sim_debug (DBG, &dp_dev, "DIXM ");
    XA += 040;
  }
  if (insn & 02000) { /* DHVC */
    sim_debug (DBG, &dp_dev, "DHVC ");
    crt_hvc ();
  }

  sim_debug (DBG, &dp_dev, "\n");
  return SCPE_OK;
}

static void
jump (uint16 insn)
{
  DPC = insn & 07777;
  if (MIT8K)
    DPC |= (insn & 0100000) >> 3;
  else
    DPC |= BLOCK << 12;
}

static void
dp_sgr (uint16 insn)
{
  sim_debug (DBG, &dp_dev, "DSGR %o\n", insn & 7);

  SGR = insn & 1;
  if (insn & 1) {
    sim_debug (DBG, &dp_dev, "Enter SGR mode\n");
  } else {
    sim_debug (DBG, &dp_dev, "Exit SGR mode\n");
  }
  if (insn & 2) {
    sim_debug (DBG, &dp_dev, "SGR: Return\n");
  }
  if (insn & 4) {
    sim_debug (DBG, &dp_dev, "SGR: Beam on\n");
  } else {
    sim_debug (DBG, &dp_dev, "SGR: Beam off\n");
  }
}

static void
dp_opt (uint16 insn)
{
  switch (insn & 07770) {
  case 07660: /* ASG-1 */
  case 07667:
    break;
  case 07720: /* VIC-1 */
  case 07730:
    break;
  case 07740: /* MCI-1 */
  case 07750:
    break;
  case 07760: /* STI-1 or LPA-1 */
    break;
  case 07770: /* SGR-1 */
    dp_sgr (insn);
    break;
  default:
    sim_debug (DBG, &dp_dev, "Unknown instruction: %06o ", insn);
    break;
  }
}

static void
dp_inc_vector (uint16 byte)
{
  uint16 x1 = XA, y1 = YA;
  uint16 dx, dy;

  if (byte == 0200) {
    sim_debug (DBG, &dp_dev, "P");
  } else {
    sim_debug (DBG, &dp_dev, "%s", byte & 0100 ? "B" : "D");
    if (byte & 00040)
      sim_debug (DBG, &dp_dev, "M");
    sim_debug (DBG, &dp_dev, "%o", (byte >> 3) & 3);
    if (byte & 00004)
      sim_debug (DBG, &dp_dev, "M");
    sim_debug (DBG, &dp_dev, "%o", byte & 3);
  }

  dx = SCALE * ((byte >> 3) & 3);
  dy = SCALE * (byte & 3);
  if (byte & 040)
    XA -= dx;
  else
    XA += dx;
  if (byte & 4)
    YA -= dy;
  else
    YA += dy;
  if (byte & 0100)
    crt_line (x1, y1, XA, YA);

}

static void
dp_inc_escape (uint16 byte)
{
  if (byte == 0100)
    sim_debug (DBG, &dp_dev, "T");
  else if (byte == 0140)
    sim_debug (DBG, &dp_dev, "X");
  else if (byte == 0151)
    sim_debug (DBG, &dp_dev, "R");
  else
    sim_debug (DBG, &dp_dev, "%03o", byte);

  if (byte & 0100)
    MODE = 0;
  if (byte & 040) {
    if (SP > 0)
      DPC = DT[--SP];
    else
      sim_debug (DBG, &dp_dev, "stack underflow");
  }
  if (byte & 020)
    XA += 040;
  if (byte & 010)
    XA &= 03740;
  if (byte & 4) /* Enter PPM mode. */
    ;
  if (byte & 2)
    YA += 040;
  if (byte & 1)
    YA &= 03740;
}

static void
dp_inc (uint16 byte)
{
  if (byte & 0200) {
    /* Increment byte. */
    dp_inc_vector (byte);
  } else {
    /* Escape byte. */
    dp_inc_escape (byte);
  }
}

static void
dp_deim (uint16 insn)
{
  MODE = 1;
  sim_debug (DBG, &dp_dev, "E,");
  dp_inc (insn & 0377);
  sim_debug (DBG, &dp_dev, "\n");
}

static void
dp_dlvh (uint16 insn1, uint16 insn2, uint16 insn3)
{
  uint16 x1 = XA, y1 = YA;
  uint16 m, n, dx, dy;
  m = insn2 & 07777;
  n = insn3 & 07777;
  if (insn3 & 010000) {
    dx = n;
    dy = m;
  } else {
    dx = m;
    dy = n;
  }
  if (insn3 & 040000)
    XA -= SCALE * dx;
  else
    XA += SCALE * dx;
  if (insn3 & 020000)
    YA -= SCALE * dy;
  else
    YA += SCALE * dy;
  if (insn2 & 020000)
    crt_line (x1, y1, XA, YA);
}

static void
dp_insn (uint16 insn)
{
  switch ((insn >> 12) & 7) {
  case 0: /* DOPR */
    dp_opr (insn);
    break;
  case 1: /* DLXA */
    sim_debug (DBG, &dp_dev, "DLXA\n");
    XA = (insn & 01777) << 1;
    break;
  case 2: /* DLYA */
    sim_debug (DBG, &dp_dev, "DLYA\n");
    YA = (insn & 01777) << 1;
    break;
  case 3: /* DEIM */
    sim_debug (DBG, &dp_dev, "DEIM ");
    dp_deim (insn);
    break;
  case 4: /* DLVH */
    sim_debug (DBG, &dp_dev, "DLVH\n");
    dp_dlvh (insn, M[DPC], M[DPC+1]);
    DPC += 2;
    break;
  case 5: /* DJMS */
    sim_debug (DBG, &dp_dev, "DJMS\n");
    if (SP < 7)
      DT[SP++] = DPC;
    else
      sim_debug (DBG, &dp_dev, "stack overflow");
    /* Fall through. */
  case 6: /* DJMP */
    sim_debug (DBG, &dp_dev, "DJMP\n");
    jump (insn);
    break;
  case 7: /* optional */
    dp_opt (insn);
    break;
  }
}

static t_stat
dp_svc(UNIT * uptr)
{
  uint16 insn;

  if (sim_brk_summ && sim_brk_test(DPC, SWMASK('D'))) {
    sim_activate_abs (&dp_unit, 0);
    return sim_messagef (SCPE_STOP, "Display processor breakpoint.\n");
  }

  sim_debug (DBG, &dp_dev, "%06o ", DPC);
  insn = M[DPC];
  DPC++;
  if (MODE) {
    sim_debug (DBG, &dp_dev, "INC ");
    dp_inc (insn >> 8);
    if (MODE) {
      sim_debug (DBG, &dp_dev, ",");
      dp_inc (insn & 0377);
    }
    sim_debug (DBG, &dp_dev, "\n");
  } else
    dp_insn (insn);

  if (ON)
    sim_activate_after (&dp_unit, 2);

  return SCPE_OK;
}

static t_stat
sync_svc (UNIT *uptr)
{
  sim_debug (DBG, &sync_dev, "40 Hz sync\n");
  SYNC = 1;
  if (SYNC && HALT)
    flag_on (FLAG_SYNC);
  sim_cancel (&sync_unit);
  return SCPE_OK;
}

static uint16
sync_iot (uint16 insn, uint16 AC)
{
  if ((insn & 0771) == 0071) { /* SCF */
    sim_debug (DBG, &sync_dev, "Clear flag\n");
    SYNC = 0;
    flag_off (FLAG_SYNC);
    sim_activate_after (&sync_unit, 25000);
  }
  if ((insn & 0772) == 0072) { /* IOS */
  }
  return AC;
}
