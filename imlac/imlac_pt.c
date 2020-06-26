/* imlac_pt.c: Imlac paper tape reader and punch

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

/* Function declaration. */
static t_stat ptr_svc (UNIT *uptr);
static uint16 ptr_iot (uint16, uint16);
static uint16 ptp_iot (uint16, uint16);
static t_stat ptr_boot (int32 u, DEVICE *dptr);
static t_stat ptr_detach (UNIT *uptr);

static uint16 PTRB;

static uint16 ptr_rom[] = {
  0060077, 0020010, 0104076, 0020020, 0001061, 0100011, 0002400, 0010046,
  0001051, 0074075, 0010045, 0002400, 0010053, 0001051, 0003003, 0003003,
  0003002, 0102400, 0010061, 0002400, 0010063, 0001051, 0120010, 0102400,
  0010067, 0100011, 0030020, 0010053, 0110076, 0000002, 0037700, 0037677,
};

static UNIT ptr_unit = {
  UDATA (&ptr_svc, UNIT_IDLE+UNIT_ATTABLE, 0)
};

static REG ptr_reg[] = {
  { ORDATAD (PTRB, PTRB, 8, "Receive buffer") },
  { NULL }
};

static IMDEV ptr_imdev = {
  2,
  { { 0005, ptr_iot, { NULL, "HRB", "HOF", NULL } },
    { 0006, ptr_iot, { NULL, "HON", "STB", NULL } } }
};

static DEBTAB ptr_deb[] = {
  { "DBG", DBG },
  { NULL, 0 }
};

DEVICE ptr_dev = {
  "PTR", &ptr_unit, ptr_reg, NULL,
  1, 8, 16, 1, 8, 16,
  NULL, NULL, NULL,
  &ptr_boot, &attach_unit, &ptr_detach,
  &ptr_imdev, DEV_DISABLE | DEV_DEBUG | DEV_DIS, 0, ptr_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static IMDEV ptp_imdev = {
  1,
  { { 0027, ptp_iot, { "PUN", NULL, NULL, "PSF" } } }
};

DEVICE ptp_dev = {
  "PTP", NULL, NULL, NULL,
  0, 8, 16, 1, 8, 16,
  NULL, NULL, NULL,
  NULL, NULL, NULL,
  &ptp_imdev, DEV_DISABLE | DEV_DEBUG | DEV_DIS, 0, NULL,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static t_stat
ptr_svc (UNIT *uptr)
{
  unsigned char ch;

  /* This function is called when the motor is on.  The data ready
     flag toggles on and off when the tape goes past the read head. */

  if (flag_check (FLAG_PTR)) {
    flag_off (FLAG_PTR);
  } else {
    if (sim_fread (&ch, 1, 1, uptr->fileref) == 1) {
      sim_debug (DBG, &ptr_dev, "Received character %03o\n", ch);
      PTRB = ch;
      flag_on (FLAG_PTR);
    } else {
      sim_debug (DBG, &ptr_dev, "No more data\n");
      return SCPE_OK;
    }
  }

  sim_activate_after (uptr, 1000);
  return SCPE_OK;
}

static uint16
ptr_iot (uint16 insn, uint16 AC)
{
  if ((insn & 0771) == 0051) { /* HRB */
    sim_debug (DBG, &ptr_dev, "Read character %03o\n", PTRB);
    AC |= PTRB;
  }
  if ((insn & 0772) == 0052) { /* HOF */
    flag_off (FLAG_PTR);
    if (sim_is_active (&ptr_unit))
      sim_cancel (&ptr_unit);
    sim_debug (DBG, &ptr_dev, "Motor off.\n");
  }
  if ((insn & 0771) == 0061) { /* HON */
    flag_off (FLAG_PTR);
    if (ptr_unit.flags & UNIT_ATT)
      sim_activate_after (&ptr_unit, 1000);
    sim_debug (DBG, &ptr_dev, "Motor on.\n");
  }
  if ((insn & 0772) == 0062) { /* STB */
    ;
  }
  return AC;
}

static uint16
ptp_iot (uint16 insn, uint16 AC)
{
  if ((insn & 0771) == 0271) { /* PUN */
    ;
  }
  if ((insn & 0772) == 0274) { /* PSF */
    ;
  }
  return SCPE_OK;
}

void
rom_ptr (void)
{
  rom_data (ptr_rom);
}

static t_stat
ptr_boot (int32 u, DEVICE *dptr)
{
  uint16 *PC = (uint16 *)sim_PC->loc;
  set_cmd (0, "ROM TYPE=PTR");
  *PC = 040;
  return SCPE_OK;
}

static t_stat
ptr_detach (UNIT *uptr)
{
  if (!(uptr->flags & UNIT_ATT))
    return SCPE_OK;
  if (sim_is_active (uptr))
    sim_cancel (uptr);
  return detach_unit (uptr);
}
