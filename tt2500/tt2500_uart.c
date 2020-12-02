/* tt2500_uart.c: TT2500 serial port device

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
#include "sim_tmxr.h"

/* Debug */
#define DBG_TX          0001
#define DBG_RX          0002

#define UART_FILE     (1 << TTUF_V_UF)  /* Attached to a file. */
#define UART_PORT     (2 << TTUF_V_UF)  /* Attached to a network port. */
#define UART_TYPE     (3 << TTUF_V_UF)  /* File or port. */
#define UART_REVERSE  (4 << TTUF_V_UF)  /* Transmit bits in reverse order. */

static uint16 RBUF, TBUF;

/* Function declaration. */
static t_stat uart_r_svc (UNIT *uptr);
static t_stat uart_t_svc (UNIT *uptr);
static t_stat uart_reset (DEVICE *dptr);
static t_stat uart_attach (UNIT *uptr, CONST char *cptr);
static t_stat uart_detach (UNIT *uptr);
static uint16 uart_read (uint16);
static void uart_write (uint16, uint16);

static TMLN uart_ldsc = { 0 };
static TMXR uart_desc = { 1, 0, 0, &uart_ldsc };

static UNIT uart_unit[] = {
  { UDATA (&uart_r_svc, UNIT_IDLE+UNIT_ATTABLE+UART_PORT, 0) },
  { UDATA (&uart_t_svc, UNIT_IDLE+UNIT_ATTABLE+UART_PORT, 0) }
};

static REG uart_reg[] = {
  { ORDATAD (RB, RBUF, 8, "Receive buffer") },
  { ORDATAD (TB, TBUF, 8, "Transmit buffer") },
  { NULL }
};

MTAB uart_mod[] = {
  { UART_TYPE, UART_PORT, "PORT", "PORT", NULL, NULL, NULL,
              "Attach to port"},
  { UART_TYPE, UART_FILE, "FILE", "FILE", NULL, NULL, NULL,
              "Attach to file"},
  { UART_REVERSE, UART_REVERSE, "REVERSE", "REVERSE", NULL, NULL, NULL,
              "Transmit bits in reverse order"},
  { UART_REVERSE, 0, NULL, "NOREVERSE", NULL, NULL, NULL,
              "Transmit bits in normal order"},
  { MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
    &tmxr_dscln, NULL, &uart_desc, "Disconnect a specific line" },
  { UNIT_ATT, UNIT_ATT, "SUMMARY", NULL, NULL,
    &tmxr_show_summ, (void *) &uart_desc, "Display a summary of line states" },
  { MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL, NULL,
    &tmxr_show_cstat, (void *) &uart_desc, "Display current connections" },
  { MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL, NULL,
    &tmxr_show_cstat, (void *) &uart_desc, "Display multiplexer statistics" },
  { 0 }
};

static DEBTAB uart_deb[] = {
  { "RX", DBG_RX },
  { "TX", DBG_TX },
  { NULL, 0 }
};

static TTDEV uart_ttdev = {
  { REG_UART, 0, 0, 0 },
  uart_read,
  uart_write,
};

DEVICE uart_dev = {
  "UART", uart_unit, uart_reg, uart_mod,
  2, 8, 16, 1, 8, 16,
  NULL, NULL, uart_reset,
  NULL, uart_attach, uart_detach,
  &uart_ttdev, DEV_DEBUG, 0, uart_deb,
  NULL, NULL, NULL, NULL, NULL, NULL
};

static t_stat
uart_r_svc(UNIT *uptr)
{
  int32 ch;

  if ((uptr->flags & UNIT_ATT) == 0)
    return SCPE_OK;

  if (uptr->fileref != NULL) {
    unsigned char buf;
    if (sim_fread (&buf, 1, 1, uptr->fileref) == 1) {
      sim_debug (DBG_RX, &uart_dev, "Received character %03o\n", buf);
      RBUF = buf;
      flag_on (INT_RRD);
    }
  } else if (uart_ldsc.conn) {
    tmxr_poll_rx (&uart_desc);
    ch = tmxr_getc_ln (&uart_ldsc);
    if (ch & TMXR_VALID) {
      RBUF = sim_tt_inpcvt (ch, TT_GET_MODE (uart_unit[0].flags));
      sim_debug (DBG_RX, &uart_dev, "Received character %03o\n", RBUF);
      flag_on (INT_RRD);
      return SCPE_OK;
    }
    sim_activate_after (uptr, 200);
  } else {
    int32 ln = tmxr_poll_conn (&uart_desc);
    if (ln >= 0) {
      uart_ldsc.rcve = 1;
      sim_debug (DBG_RX, &uart_dev, "Connect\n");
      sim_activate_after (uptr, 200);
    } else {
      sim_activate_after (uptr, 10000);
    }
  }

  return SCPE_OK;
}

static t_stat
uart_t_svc(UNIT *uptr)
{
  int32 ch;

  tmxr_poll_tx (&uart_desc);

  if (!tmxr_txdone_ln (&uart_ldsc))
    return SCPE_OK;

  ch = sim_tt_outcvt (TBUF, TT_GET_MODE (uart_unit[1].flags));
  if (tmxr_putc_ln (&uart_ldsc, ch) == SCPE_STALL) {
    sim_activate_after (&uart_unit[1], 200);
  } else {
    sim_debug (DBG_TX, &uart_dev, "Transmitted character %03o\n", TBUF);
    tmxr_poll_tx (&uart_desc);
    flag_on (FLAG_RSD);
  }

  return SCPE_OK;
}

static t_stat
uart_reset (DEVICE *dptr)
{
  flag_off (INT_RRD);
  flag_on (FLAG_RSD);
  return SCPE_OK;
}

static t_stat
uart_attach (UNIT *uptr, CONST char *cptr)
{
  t_stat r;

  if (uptr->flags & UART_PORT) {
    r = tmxr_attach (&uart_desc, uptr, cptr);
    if (r != SCPE_OK)
      return r;
    sim_activate_abs (uptr, 0);
  } else if (uptr->flags & UART_FILE) {
    r = attach_unit (uptr, cptr);
    if (r != SCPE_OK)
      return r;
    sim_activate_abs (uptr, 0);
  } else {
    return SCPE_ARG;
  }

  return SCPE_OK;
}

static t_stat
uart_detach (UNIT *uptr)
{
  if (!(uptr->flags & UNIT_ATT))
    return SCPE_OK;
  if (sim_is_active (uptr))
    sim_cancel (uptr);
  return detach_unit (uptr);
}

static uint16 uart_read (uint16 reg)
{
  sim_debug (DBG_RX, &uart_dev, "Read character %03o\n", RBUF);
  flag_off (INT_RRD);
  sim_activate_after (&uart_unit[0], 200);
  return RBUF;
}

static uint16 reverse (uint16 data)
{
  uint16 i, x = 0;
  for (i = 1; i <= 0200; i <<= 1) {
    x <<= 1;
    if (data & i)
      x++;
  }
  return x;
}

static void uart_write (uint16 reg, uint16 data)
{
  data &= 0377;
  if (uart_unit[0].flags & UART_REVERSE)
    data = reverse (data);
  sim_debug (DBG_TX, &uart_dev, "Write character %03o\n", data);
  TBUF = data;
  sim_activate_after (&uart_unit[1], 200);
  flag_off (FLAG_RSD);
}
