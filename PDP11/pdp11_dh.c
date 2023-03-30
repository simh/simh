/* pdp11_dh.c: DH11, asynchronous serial line interface

   Copyright (c) 2022, Lars Brinkhoff

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
   THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the names of the authors shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the authors.
*/

#include "pdp11_defs.h"
#include "sim_tmxr.h"

t_stat dh_rd(int32 *data, int32 PA, int32 access);
t_stat dh_wr(int32 data, int32 PA, int32 access);
t_stat dh_input_svc(UNIT *uptr);
t_stat dh_output_svc(UNIT *uptr);
t_stat dh_reset(DEVICE *dptr);
t_stat dh_attach (UNIT *uptr, CONST char *cptr);
t_stat dh_detach (UNIT *uptr);
const char *dh_description (DEVICE *dptr);

#define DH_LINES 16

uint16 dh_scr;             /* System Control Register */
uint16 dh_nrcr;            /* Next Received Character Register */
uint16 dh_lpr[DH_LINES];   /* Line Parameter Regiser */
uint32 dh_car[DH_LINES];   /* Current Address Register */
uint16 dh_bcr[DH_LINES];   /* Byte Count Register */
uint16 dh_bar;             /* Buffer Active Register */
uint16 dh_brcr;            /* Break Control Register */
uint16 dh_ssr;             /* Silo Status Register */
uint16 dh_silo[64];

#define LN (dh_scr & 017)

/* DHSCR bits */
#define RIE     0000100
#define RI      0000200
#define CNXM    0000400
#define MAINT   0001000
#define NXM     0002000
#define MCLR    0004000
#define SIE     0010000
#define OIE     0020000
#define SI      0040000
#define TI      0100000

/* DHNRCR bits */
#define DPR     0100000

/* DHLPR bits */
#define RSPEED  0001700
#define TSPEED  0036000
#define HFD     0040000
#define ECHO    0100000

TMLN dh_ldsc[DH_LINES] = { { 0 } };
TMXR dh_desc = { DH_LINES, 0, 0, dh_ldsc };

#define IOLN_DH   020
DIB dh_dib = {
  IOBA_AUTO, IOLN_DH, &dh_rd, &dh_wr,
  2, IVCL (DHRX), 0, {NULL}, IOLN_DH
};

UNIT dh_unit[] = {
  { UDATA (&dh_input_svc, UNIT_ATTABLE | UNIT_IDLE, 0) },
  { UDATA (&dh_output_svc, UNIT_DIS | UNIT_IDLE, 0) }
};

REG dh_reg[] = {
  { ORDATAD(DHSCR,  dh_scr,  16, "System Control Register") },
  { ORDATAD(DHNRCR, dh_nrcr, 16, "Next Received Character Register") },
  { BRDATAD(DHLPR,  dh_lpr,  8, 16, DH_LINES, "Line Parameter Regiser") },
  { BRDATAD(DHCAR,  dh_car,  8, 18, DH_LINES, "Current Address Register") },
  { BRDATAD(DHBCR,  dh_bcr,  8, 16, DH_LINES, "Byte Count Register") },
  { ORDATAD(DHBAR,  dh_bar,  16, "Buffer Active Register") },
  { ORDATAD(DHBRCR, dh_brcr, 16, "Break Control Register") },
  { ORDATAD(DHSSR,  dh_ssr,  16, "Silo Status Register") },
  { BRDATAD(DHSILO, dh_silo, 8, 16, 64, "Silo") },
  { NULL }
};

MTAB dh_mod[] = {
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 020, "ADDRESS", "ADDRESS",
    &set_addr, &show_addr, NULL, "Bus address" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR,   0, "VECTOR",  "VECTOR",
    &set_vec,  &show_vec, NULL, "Interrupt vector" },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
    &set_addr_flt, NULL, NULL, "Enable autoconfiguration of address & vector" },
  { UNIT_ATT,         UNIT_ATT, "summary", NULL,
    NULL, &tmxr_show_summ, (void *) &dh_desc, "Display a summary of line states"  },
  { MTAB_XTD|MTAB_VDV|MTAB_NMO, 1, "CONNECTIONS", NULL,
    NULL, &tmxr_show_cstat, (void *) &dh_desc, "Display current connections" },
  { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATISTICS", NULL,
    NULL, &tmxr_show_cstat, (void *) &dh_desc, "Display multiplexer statistics" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 1, NULL, "DISCONNECT",
    &tmxr_dscln, NULL, &dh_desc, "Disconnect a specific line" },
  { 0 }  };

#define DBG_IO        0001

DEBTAB dh_deb[] = {
  { "IO", DBG_IO, "trace" },
  { NULL, 0 }
};

DEVICE dh_dev = {
    "DH", dh_unit, dh_reg, dh_mod,
    2, 8, 16, 1, 8, 16,
    NULL, NULL, &dh_reset,
    NULL, &dh_attach, &dh_detach,
    &dh_dib, DEV_DIS | DEV_DISABLE | DEV_UBUS | DEV_DEBUG | DEV_MUX,
    0, dh_deb, NULL, NULL, NULL, NULL, NULL, 
    &dh_description
};

t_stat
dh_rd(int32 *data, int32 PA, int32 access)
{
  t_stat stat = SCPE_OK;
  *data = 0;
  switch (PA & 017) {
  case 000:
    *data = dh_scr;
    sim_debug (DBG_IO, &dh_dev, "READ DHSCR %06o\n", *data);
    break;
  case 002:
    *data = dh_nrcr;
    dh_nrcr = 0;
    dh_scr &= ~RI;
    CLR_INT (DHRX);
    sim_activate_abs (&dh_unit[0], 0);
    sim_debug (DBG_IO, &dh_dev, "READ DHNRCR %06o\n", *data);
    break;
  case 004:
    *data = 0;
    sim_debug (DBG_IO, &dh_dev, "READ DHLPR[%o]\n", LN);
    break;
  case 006:
    *data = dh_car[LN];
    sim_debug (DBG_IO, &dh_dev, "READ DHCAR[%o] %06o\n", LN, *data);
    break;
  case 010:
    *data = dh_bcr[LN];
    sim_debug (DBG_IO, &dh_dev, "READ DHBCR[%o] %06o\n", LN, *data);
    break;
  case 012:
    *data = dh_bar;
    sim_debug (DBG_IO, &dh_dev, "READ DHBAR %06o\n", *data);
    break;
  case 014:
    *data = dh_brcr;
    sim_debug (DBG_IO, &dh_dev, "READ DHBRCR %06o\n", *data);
    break;
  case 016:
    dh_ssr &= ~0300;
    dh_ssr |= (dh_car[LN] >> 10) & 0300;
    *data = dh_ssr;
    sim_debug (DBG_IO, &dh_dev, "READ DHSSR %06o\n", *data);
    break;
  default:
    *data = 0;
    break;
  }
  return stat;
}

t_stat
dh_wr(int32 data, int32 PA, int32 access)
{
  t_stat stat = SCPE_OK;

  switch (PA & 017) {
  case 000:
    sim_debug (DBG_IO, &dh_dev, "WRITE DHSCR %06o\n", data);
    if (access == WRITEB) {
        /* Only writing one byte of "SCR" */
        if ((PA & 1) == 0) {
            /* Even byte offset */
            data = (dh_scr & ~0377) | data;
        }
        else {
            /* Odd byte offset */
            data = (dh_scr & 0377) | (data << 8);
        }
    }
    dh_scr = data;
    if (data & MCLR)
      dh_reset (&dh_dev);
    if (data & CNXM)
      data &= ~NXM;
    if (data & TI) {
      if (dh_scr & OIE)
        SET_INT (DHTX);
    } else {
      if (dh_bar != 0)
        sim_activate_abs (&dh_unit[1], 0);
      CLR_INT (DHTX);
    }
    break;
  case 002:
    sim_debug (DBG_IO, &dh_dev, "WRITE DHNRCR %06o\n", data);
    break;
  case 004:
    sim_debug (DBG_IO, &dh_dev, "WRITE DHLPR[%o] %06o\n", LN, data);
    dh_lpr[LN] = data;
    if (data & RSPEED)
      sim_activate_abs (&dh_unit[0], 0);
    break;
  case 006:
    sim_debug (DBG_IO, &dh_dev, "WRITE DHCAR[%o] %06o\n", LN, data);
    dh_car[LN] = data;
    dh_car[LN] |= ((uint32)dh_scr & 060) << 12;
    break;
  case 010:
    sim_debug (DBG_IO, &dh_dev, "WRITE DHBCR[%o] %06o\n", LN, data);
    dh_bcr[LN] = data;
    if (data == 0)
      sim_cancel (&dh_unit[1]);
    break;
  case 012:
    sim_debug (DBG_IO, &dh_dev, "WRITE DHBAR %06o\n", data);
    dh_bar = data;
    if (dh_bar == 0)
      sim_cancel (&dh_unit[1]);
    else
      sim_activate_abs (&dh_unit[1], 0);
    break;
  case 014:
    sim_debug (DBG_IO, &dh_dev, "WRITE DHBRCR %06o\n", data);
    dh_brcr = data;
    break;
  case 016:
    sim_debug (DBG_IO, &dh_dev, "WRITE DHSSR %06o\n", data);
    dh_ssr &= 077700;
    dh_ssr |= data & 0100077;
    break;
  default:
    break;
  }
  return stat;
}

t_stat dh_attach (UNIT *uptr, CONST char *cptr)
{
  return tmxr_attach (&dh_desc, uptr, cptr);
}

t_stat dh_detach (UNIT *uptr)
{
  return tmxr_detach (&dh_desc, uptr);
}

t_stat dh_input_svc(UNIT *uptr)
{
  int32 ch;
  int i;

  sim_clock_coschedule (uptr, 100);

  i = tmxr_poll_conn (&dh_desc);
  if (i >= 0) {
    dh_ldsc[i].rcve = 1;
    dh_ldsc[i].xmte = 1;
    sim_debug(DBG_IO, &dh_dev, "Connect %d\n", i);
  }

  tmxr_poll_rx (&dh_desc);

  for (i = 0; i < DH_LINES; i++) {
    ch = tmxr_getc_ln (&dh_ldsc[i]);
    if (ch & TMXR_VALID) {
      ch &= (1 << ((dh_lpr[i] & 3) + 5)) - 1;
      dh_nrcr = ch | (i << 8) | DPR;
      dh_scr |= RI;
      if (dh_scr & RIE)
        SET_INT (DHRX);
      sim_debug(DBG_IO, &dh_dev, "Input character %03o line %d\n", ch, i);
      sim_cancel (&dh_unit[0]);
      break;
    }
  }

  return SCPE_OK;
}

t_stat dh_output_svc(UNIT *uptr)
{
  int32 ch;
  int i;

  sim_clock_coschedule (uptr, 100);

  for (i = 0; i < DH_LINES; i++) {
    if ((dh_bar & (1 << i)) == 0)
      continue;
    if (dh_bcr[i] == 0)
      continue;
    ch = RdMemB (dh_car[i]);
    ch &= (1 << ((dh_lpr[i] & 3) + 5)) - 1;
    if (tmxr_putc_ln (&dh_ldsc[i], ch) != SCPE_STALL) {
      sim_debug(DBG_IO, &dh_dev, "Output character %03o line %d\n", ch, i);
      dh_car[i]++;
      dh_car[i] &= 0777777;
      dh_bcr[i]++;
      dh_bcr[i] &= 0177777;
      if (dh_bcr[i] == 0) {
        dh_bar &= ~(1 << i);
        dh_scr |= TI;
        if (dh_bar == 0)
          sim_cancel (uptr);
        if (dh_scr & OIE)
          SET_INT (DHTX);
      }
    }
  }

  tmxr_poll_tx (&dh_desc);

  return SCPE_OK;
}

t_stat dh_reset(DEVICE *dptr)
{
  CLR_INT (DHRX);
  CLR_INT (DHTX);
  sim_cancel (&dh_unit[0]);
  sim_cancel (&dh_unit[1]);
  dh_scr = 0;
  dh_nrcr = 0;
  dh_bar = 0;
  dh_brcr = 0;
  dh_ssr = 0;
  memset (dh_lpr, 0, sizeof dh_lpr);
  memset (dh_car, 0, sizeof dh_car);
  memset (dh_bcr, 0, sizeof dh_bcr);
  return SCPE_OK;
}

const char *dh_description (DEVICE *dptr)
{
  return "DH-11, asynchronous serial line interface";
}
