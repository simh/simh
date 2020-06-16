/* imlac_tty.c: Imlac serial port device

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
#include "sim_tmxr.h"

/* Debug */
#define DBG             0001

#define TTY_FILE    1  /* Attached to a file. */
#define TTY_PORT    2  /* Attached to a network port. */

static uint16 RBUF, TBUF;
static int tty_type = TTY_PORT;

/* Function declaration. */
static uint16 tty_iot (uint16, uint16);
static t_stat tty_r_svc (UNIT *uptr);
static t_stat tty_t_svc (UNIT *uptr);
static t_stat tty_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat tty_show_type (FILE *st, UNIT *up, int32 v, CONST void *dp);
static t_stat tty_boot (int32 u, DEVICE *dptr);
static t_stat tty_attach (UNIT *uptr, CONST char *cptr);
static t_stat tty_detach (UNIT *uptr);

static TMLN tty_ldsc = { 0 };
static TMXR tty_desc = { 1, 0, 0, &tty_ldsc };

static uint16 tty_rom[] = {
  0060077, 0020010, 0104076, 0020020, 0001032, 0100011, 0002040, 0010046,
  0001031, 0074075, 0010044, 0002040, 0010053, 0001033, 0003003, 0003003,
  0003002, 0002040, 0010061, 0001033, 0120010, 0100011, 0030020, 0010053,
  0110076, 0000000, 0000000, 0000000, 0000000, 0000002, 0037700, 0037677,
};

static uint16 stty_rom[] = {
  0001032, 0104101, 0020010, 0020020, 0104004, 0020021, 0100011, 0020022,
  0100011, 0002040, 0010051, 0001033, 0020023, 0044075, 0074076, 0010050,
  0060023, 0044077, 0024022, 0003003, 0003001, 0050022, 0020022, 0030021,
  0010050, 0120010, 0030020, 0010044, 0110000, 0000160, 0000100, 0000017
};

static uint16 mtty_rom[] = {
  0060077, 0020010, 0104076, 0020020, 0001032, 0100011, 0002040, 0010046,
  0001031, 0074075, 0010044, 0002040, 0010053, 0001033, 0003003, 0003003,
  0003002, 0002040, 0010061, 0001033, 0120010, 0100011, 0030020, 0010053,
  0110076, 0004200, 0100040, 0001043, 0010040, 0000002, 0037700, 0037677,
};

static UNIT tty_unit[] = {
  { UDATA (&tty_r_svc, UNIT_IDLE+UNIT_ATTABLE, 0) },
  { UDATA (&tty_t_svc, UNIT_IDLE+UNIT_ATTABLE, 0) }
};

static REG tty_reg[] = {
  { ORDATAD (RB, RBUF, 8, "Receive buffer") },
  { ORDATAD (TB, TBUF, 8, "Transmit buffer") },
  { NULL }
};

MTAB tty_mod[] = {
  { MTAB_VDV|MTAB_VALR, 1, "TYPE", "TYPE", &tty_set_type,
    &tty_show_type, NULL, "Set attach type" },
  { MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
    &tmxr_dscln, NULL, &tty_desc, "Disconnect a specific line" },
  { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL, NULL,
    &tmxr_show_summ, (void *) &tty_desc, "Display a summary of line states" },
  { MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL, NULL,
    &tmxr_show_cstat, (void *) &tty_desc, "Display current connections" },
  { MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL, NULL,
    &tmxr_show_cstat, (void *) &tty_desc, "Display multiplexer statistics" },
  { 0 }
};

static IMDEV tty_imdev = {
  2,
  { { 0003, tty_iot, { NULL, "RRB", "RCF", "RRC" } },
    { 0004, tty_iot, { NULL, "TPR", "TCF", "TPC" } } }
};

static DEBTAB tty_deb[] = {
  { "DBG", DBG },
  { NULL, 0 }
};

DEVICE tty_dev = {
  "TTY", tty_unit, tty_reg, tty_mod,
  2, 8, 16, 1, 8, 16,
  NULL, NULL, NULL,
  tty_boot, tty_attach, tty_detach,
  &tty_imdev, DEV_DEBUG, 0, tty_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static t_stat
tty_r_svc(UNIT *uptr)
{
  int32 ch;

  if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_OK;

  if (uptr->fileref != NULL) {
    unsigned char buf;
    if (sim_fread (&buf, 1, 1, uptr->fileref) == 1) {
      sim_debug (DBG, &tty_dev, "Received character %03o\n", buf);
      RBUF = buf;
      flag_on (FLAG_TTY_R);
    }
  } else {
    if (tty_ldsc.conn) {
      tmxr_poll_rx (&tty_desc);
      ch = tmxr_getc_ln (&tty_ldsc);
      if (ch & TMXR_VALID) {
        RBUF = sim_tt_inpcvt (ch, TT_GET_MODE (tty_unit[0].flags));
        sim_debug (DBG, &tty_dev, "Received character %03o\n", RBUF);
        flag_on (FLAG_TTY_R);
        return SCPE_OK;
      }
      sim_activate_after (uptr, 200);
    } else {
      int32 ln = tmxr_poll_conn (&tty_desc);
      if (ln >= 0) {
        tty_ldsc.rcve = 1;
        sim_debug (DBG, &tty_dev, "Connect\n");
        sim_activate_after (uptr, 200);
      } else {
        sim_activate_after (uptr, 10000);
      }
    }
  }

  return SCPE_OK;
}

static t_stat
tty_t_svc(UNIT *uptr)
{
  int32 ch;

  tmxr_poll_tx (&tty_desc);

  if (!tmxr_txdone_ln (&tty_ldsc))
    return SCPE_OK;

  ch = sim_tt_outcvt (TBUF, TT_GET_MODE (tty_unit[1].flags));
  if (tmxr_putc_ln (&tty_ldsc, ch) == SCPE_STALL) {
    sim_activate_after (&tty_unit[1], 200);
  } else {
    sim_debug (DBG, &tty_dev, "Transmitted character %03o\n", TBUF);
    tmxr_poll_tx (&tty_desc);
    flag_on (FLAG_TTY_T);
  }

  return SCPE_OK;
}

static uint16
tty_iot (uint16 insn, uint16 AC)
{
  if ((insn & 0771) == 0031) { /* RRB */
    sim_debug (DBG, &tty_dev, "Read character %03o\n", RBUF);
    AC |= RBUF;
  }
  if ((insn & 0772) == 0032) { /* RCF */
    sim_debug (DBG, &tty_dev, "Clear read flag\n");
    flag_off (FLAG_TTY_R);
    sim_activate_after (&tty_unit[0], 200);
  }
  if ((insn & 0771) == 0041) { /* TPR */
    sim_debug (DBG, &tty_dev, "Write character %03o\n", AC);
    TBUF = AC;
    sim_activate_after (&tty_unit[1], 200);
  }
  if ((insn & 0772) == 0042) { /* TCF */
    sim_debug (DBG, &tty_dev, "Clear transmit flag\n");
    flag_off (FLAG_TTY_T);
  }
  return AC;
}

static t_stat
tty_set_type (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
  t_stat r = SCPE_OK;
  if (strcmp (cptr, "FILE") == 0)
    tty_type = TTY_FILE;
  else if (strcmp (cptr, "PORT") == 0)
    tty_type = TTY_PORT;
  else
    r = SCPE_ARG;
  return r;
}

static t_stat
tty_show_type (FILE *st, UNIT *up, int32 v, CONST void *dp)
{
  switch (tty_type) {
  case TTY_FILE:
    fprintf (st, "TYPE=FILE");
    break;
  case TTY_PORT:
    fprintf (st, "TYPE=PORT");
    break;
  default:
    fprintf (st, "TYPE=(invalid)");
    break;
  }
  return SCPE_OK;
}

void rom_tty (void)
{
  rom_data (tty_rom);
}

void rom_stty (void)
{
  rom_data (stty_rom);
}

static t_stat tty_boot (int32 u, DEVICE *dptr)
{
  uint16 *PC = (uint16 *)sim_PC->loc;
  if (sim_switches & SWMASK ('T'))
    set_cmd (0, "ROM TYPE=TTY");
  else if (sim_switches & SWMASK ('S'))
    set_cmd (0, "ROM TYPE=STTY");
  else
    return sim_messagef (SCPE_ARG, "Must specify one of -S or -T\n");
  *PC = 040;
  return SCPE_OK;
}

static t_stat
tty_attach (UNIT *uptr, CONST char *cptr)
{
  t_stat r;

  switch (tty_type) {
  case TTY_PORT:
    r = tmxr_attach (&tty_desc, uptr, cptr);
    if (r != SCPE_OK)
      return r;
    sim_activate_abs (uptr, 0);
    break;
  case TTY_FILE:
    r = attach_unit (uptr, cptr);
    if (r != SCPE_OK)
      return r;
    break;
  default:
    return SCPE_ARG;
  }

  return SCPE_OK;
}

static t_stat
tty_detach (UNIT *uptr)
{
  if (!(uptr->flags & UNIT_ATT))
    return SCPE_OK;
  if (sim_is_active (uptr))
    sim_cancel (uptr);
  return detach_unit (uptr);
}
