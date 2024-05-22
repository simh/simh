/* pdp6_ge.c: GE DATANET-760 with four consoles.

   Copyright (c) 2023, Lars Brinkhoff

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL LARS BRINKHOFF BE LIABLE FOR ANY CLAIM, DAMAGES
   OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This implements the MIT AI lab interface to a GE DATANET-760 with
   four consoles.  It consists of two somewhat independent IO bus
   devices: 070 GTYI for keyboard input, and 750 GTYO for display
   output.  This file presents the two as a single GE device to SIMH
   users.
*/

#include "kx10_defs.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_GE
#define NUM_DEVS_GE 0
#endif

#if NUM_DEVS_GE > 0

#define GTYI_DEVNUM   0070    /* GE console input. */
#define GTYO_DEVNUM   0750    /* GE console output. */

#define GE_CONSOLES   4

#define GTYI_PIA      00007   /* PI assignment. */
#define GTYI_DONE     00010   /* Input data ready. */
#define GTYI_STATUS   (GTYI_PIA | GTYI_DONE)

#define GTYO_PIA      00007   /* PI assignment. */
#define GTYO_DONE     00100   /* Output data ready. */
#define GTYO_FROB     00200   /* Set done? */
#define GTYO_STATUS   (GTYO_PIA | GTYO_DONE)

#define STATUS  u3
#define DATA    u4
#define PORT    u5
#define LP      u6

#define GE_SOH        001  /* Start of header/message. */
#define GE_STX        002  /* Start of text. */
#define GE_ETX        003  /* End of text. */


static t_stat gtyi_svc(UNIT *uptr);
static t_stat gtyo_svc(UNIT *uptr);
static t_stat gtyi_devio(uint32 dev, uint64 *data);
static t_stat gtyo_devio(uint32 dev, uint64 *data);
static t_stat ge_reset(DEVICE *dptr);
static t_stat ge_attach(UNIT *uptr, CONST char *ptr);
static t_stat ge_detach(UNIT *uptr);
static t_stat ge_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char *ge_description(DEVICE *dptr);

static void gtyo_done(void);
static void gtyo_soh(char data);
static void gtyo_adr(char data);
static void gtyo_status(char data);
static void gtyo_stx(char data);
static void gtyo_text(char data);
static void gtyo_lp(char data);
static void (*gtyo_process)(char data) = gtyo_soh;

DIB gtyi_dib = { GTYI_DEVNUM, 1, &gtyi_devio, NULL };
DIB gtyo_dib = { GTYO_DEVNUM, 1, &gtyo_devio, NULL };

UNIT ge_unit[2] = {
  { UDATA(&gtyi_svc, UNIT_IDLE|UNIT_ATTABLE, 0), 1000 },
  { UDATA(&gtyo_svc, UNIT_IDLE|UNIT_ATTABLE, 0), 1000 },
};
#define gtyi_unit  (&ge_unit[0])
#define gtyo_unit  (&ge_unit[1])

static REG ge_reg[] = {
  { NULL }
};

static MTAB ge_mod[] = {
  { 0 }
};

#define DEBUG_TRC       0x0000400

static DEBTAB ge_debug[] = {
  {"TRACE",   DEBUG_TRC,    "Routine trace"},
  {"CMD",     DEBUG_CMD,    "Command Processing"},
  {"CONO",    DEBUG_CONO,   "CONO instructions"},
  {"CONI",    DEBUG_CONI,   "CONI instructions"},
  {"DATAIO",  DEBUG_DATAIO, "DATAI/O instructions"},
  {"IRQ",     DEBUG_IRQ,    "Debug IRQ requests"},
  {0},
};

DEVICE ge_dev = {
  "GE", ge_unit, ge_reg, ge_mod,
  2, 8, 18, 1, 8, 36,
  NULL, NULL, &ge_reset,
  NULL, &ge_attach, &ge_detach,
  &gtyi_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX,
  0, ge_debug,
  NULL, NULL, NULL,
  &ge_attach_help, NULL, &ge_description
};

DEVICE gtyo_dev = {
  "GTYO", NULL, NULL, NULL,
  0, 8, 18, 1, 8, 36,
  NULL, NULL, NULL,
  NULL, NULL, NULL,
  &gtyo_dib, DEV_DIS | DEV_MUX,
  0, NULL,
  NULL, NULL, NULL,
  NULL, NULL, NULL
};

static TMLN ge_ldsc[GE_CONSOLES];
static TMXR ge_desc = { GE_CONSOLES, 0, 0, ge_ldsc };

static t_stat ge_reset(DEVICE *dptr)
{
  sim_debug(DEBUG_TRC, dptr, "ge_reset()\n");

  if (ge_dev.flags & DEV_DIS)
    gtyo_dev.flags |= DEV_DIS;
  else
    gtyo_dev.flags &= ~DEV_DIS;

  if (gtyi_unit->flags & UNIT_ATT) {
    sim_activate(gtyi_unit, 10);
  } else {
    sim_cancel(gtyi_unit);
    sim_cancel(gtyo_unit);
  }

  return SCPE_OK;
}

static t_stat ge_attach(UNIT *uptr, CONST char *cptr)
{
  t_stat r;

  if (!cptr || !*cptr)
    return SCPE_ARG;
  ge_desc.buffered = 1000;
  r = tmxr_attach(&ge_desc, uptr, cptr);
  if (r != SCPE_OK)
    return r;
  sim_debug(DEBUG_TRC, &ge_dev, "activate connection\n");
  gtyi_unit->STATUS = 0;
  gtyo_unit->STATUS = 0;
  gtyo_process = gtyo_soh;
  sim_activate(gtyi_unit, 10);

  return SCPE_OK;
}

static t_stat ge_detach(UNIT *uptr)
{
  t_stat r;

  if (!(uptr->flags & UNIT_ATT))
    return SCPE_OK;
  sim_cancel(gtyi_unit);
  sim_cancel(gtyo_unit);
  r = tmxr_detach(&ge_desc, uptr);
  uptr->filename = NULL;
  return r;
}

static void gtyi_poll(UNIT *uptr)
{
  int32 ch;
  int i;

  tmxr_poll_rx(&ge_desc);
  for (i = 0; i < GE_CONSOLES; i++) {
    if (!ge_ldsc[i].rcve)
      continue;

    if (!ge_ldsc[i].conn) {
      ge_ldsc[i].rcve = 0;
      tmxr_reset_ln(&ge_ldsc[i]);
      sim_debug(DEBUG_CMD, &ge_dev, "Port %d connection lost\n", i);
      continue;
    }

    ch = tmxr_getc_ln(&ge_ldsc[i]);
    if (ch & TMXR_VALID) {
      ch &= 0177;
      sim_debug(DEBUG_CMD, &ge_dev, "Port %d got %03o\n", i, ch);
      if (ch >= 0141 && ch <= 0172)
        ch -= 0100;
      else if (ch == 0140 || (ch >= 0173 && ch <= 0174)) {
        sim_debug(DEBUG_CMD, &ge_dev, "Discard invalid character\n");
        continue;
      }

      uptr->DATA = ch;
      uptr->PORT = i;
      uptr->STATUS |= GTYI_DONE;
      if (uptr->STATUS & 7)
        sim_debug(DEBUG_CMD, &ge_dev, "GTYI interrupt on channel %d\n", uptr->STATUS & 7);
      set_interrupt(GTYI_DEVNUM, uptr->STATUS);
      ge_ldsc[i].rcve = 0;
      break;
    }
  }
}

static t_stat gtyi_svc(UNIT *uptr)
{
  int32 n;

  n = tmxr_poll_conn(&ge_desc);
  if (n >= 0) {
    sim_debug(DEBUG_CMD, &ge_dev, "got connection\n");
    ge_ldsc[n].rcve = 1;
  }

  if ((uptr->STATUS & GTYI_DONE) == 0)
    gtyi_poll(uptr);

  sim_activate_after(uptr, 10000);
  return SCPE_OK;
}

static t_stat gtyo_svc(UNIT *uptr)
{
  switch (tmxr_putc_ln(&ge_ldsc[uptr->PORT], uptr->DATA)) {
  case SCPE_OK:
    sim_debug(DEBUG_CMD, &ge_dev, "Sent %03o to console %d\n",
              uptr->DATA, uptr->PORT);
    gtyo_done();
    break;
  case SCPE_LOST:
    ge_ldsc[uptr->PORT].rcve = 0;
    tmxr_reset_ln(&ge_ldsc[uptr->PORT]);
    sim_debug(DEBUG_CMD, &ge_dev, "lost\n");
    break;
  case SCPE_STALL:
    sim_debug(DEBUG_CMD, &ge_dev, "stall\n");
    sim_clock_coschedule(uptr, 1000);
    break;
  default:
    break;
  }

  tmxr_poll_tx(&ge_desc);
  return SCPE_OK;
}

static t_stat ge_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
const char helpString[] =
 /* The '*'s in the next line represent the standard text width of a help line */
     /****************************************************************************/
    " The %D device connects a secondary processor that is sharing memory with the.\n"
    "  primary.\n\n"
    " The device must be attached to a receive port, this is done by using the\n"
    " ATTACH command to specify the receive port number.\n"
    "\n"
    "+sim> ATTACH %U port\n"
    "\n"
    ;

 return scp_help(st, dptr, uptr, flag, helpString, cptr);
}

static const char *ge_description(DEVICE *dptr)
{
  return "GE DATANET-760";
}

static void gtyo_done(void)
{
  gtyo_unit->STATUS |= GTYO_DONE;
  set_interrupt(GTYO_DEVNUM, gtyo_unit->STATUS);
  if (gtyo_unit->STATUS & 7)
    sim_debug(DEBUG_CMD, &ge_dev, "GTYO interrupt on channel %d\n", gtyo_unit->STATUS & 7);
}

static void gtyo_soh(char data) {
  if (data == GE_SOH) {
    gtyo_process = gtyo_adr;
    gtyo_unit->LP = 0;
  }
  gtyo_done();
}

static void gtyo_adr(char data) {
  switch(data) {
  case 0140:
  case 0150:
  case 0160:
  case 0170:
    gtyo_unit->PORT = (data >> 3) & 3;
    gtyo_process = gtyo_status;
    break;
  default:
    gtyo_process = gtyo_soh;
    break;
  }
  gtyo_done();
}

static void gtyo_status(char data) {
  if (data == 0)
    gtyo_process = gtyo_stx;
  else
    gtyo_process = gtyo_soh;
  gtyo_done();
}

static void gtyo_stx(char data) {
  if (data == GE_STX)
    gtyo_process = gtyo_text;
  gtyo_done();
}

static void gtyo_text(char data) {
  if (data == GE_ETX) {
    gtyo_process = gtyo_lp;
    gtyo_done();
  } else {
    gtyo_unit->DATA = data;
    sim_activate_after(gtyo_unit, 10000);
  }
}

static void gtyo_lp(char data) {
  if (gtyo_unit->LP != 0)
    sim_debug(DEBUG_CMD, &ge_dev, "Checksum mismatch\n");
  gtyo_process = gtyo_soh;
  gtyo_done();
}

t_stat gtyi_devio(uint32 dev, uint64 *data)
{
  UNIT *uptr = gtyi_unit;

  switch(dev & 03) {
  case CONO:
    sim_debug(DEBUG_CONO, &ge_dev, "GTYI %012llo\n", *data);
    uptr->STATUS &= ~GTYI_PIA;
    uptr->STATUS |= *data & GTYI_PIA;
    break;
  case CONI:
    *data = uptr->STATUS & GTYI_STATUS;
    sim_debug(DEBUG_CONI, &ge_dev, "GTYI %012llo\n", *data);
    break;
  case DATAI:
    *data = uptr->DATA | (uptr->PORT << 18);
    sim_debug(DEBUG_DATAIO, &ge_dev, "GTYI %012llo\n", *data);
    uptr->STATUS &= ~GTYI_DONE;
    sim_debug(DEBUG_IRQ, &ge_dev, "Clear GTYI interrupt\n");
    clr_interrupt(GTYI_DEVNUM);
    ge_ldsc[uptr->PORT].rcve = 1;
    sim_activate(gtyi_unit, 10);
    break;
  }

  return SCPE_OK;
}

t_stat gtyo_devio(uint32 dev, uint64 *data)
{
  UNIT *uptr = gtyo_unit;
  int ch;

  switch(dev & 03) {
  case CONO:
    sim_debug(DEBUG_CONO, &ge_dev, "GTYO %012llo\n", *data);
    sim_debug(DEBUG_IRQ, &ge_dev, "Clear GTYO interrupt\n");
    clr_interrupt(GTYO_DEVNUM);
    uptr->STATUS &= ~GTYO_PIA;
    uptr->STATUS |= *data & GTYO_PIA;
    if (*data & GTYO_FROB)
      gtyo_done();
    break;
  case CONI:
    *data = uptr->STATUS & GTYO_STATUS;
    sim_debug(DEBUG_CONI, &ge_dev, "GTYO %012llo\n", *data);
    break;
  case DATAO:
    sim_debug(DEBUG_DATAIO, &ge_dev, "GTYO %012llo\n", *data);
    if (uptr->STATUS & GTYO_DONE) {
      sim_debug(DEBUG_IRQ, &ge_dev, "Clear GTYO interrupt\n");
      clr_interrupt(GTYO_DEVNUM);
      uptr->STATUS &= ~GTYO_DONE;
      ch = *data & 0177;
      ch ^= 0177;
      ch = ((ch << 1) | (ch >> 6)) & 0177;
      if (ch >= 040 && ch <= 0137)
        sim_debug(DEBUG_DATAIO, &ge_dev, "Character %03o %c\n", ch, ch);
      else
        sim_debug(DEBUG_DATAIO, &ge_dev, "Character %03o\n", ch);
      uptr->LP ^= ch;
      sim_debug(DEBUG_DATAIO, &ge_dev, "LP %03o\n", uptr->LP);
      gtyo_process(ch);
    }
    break;
  }

  return SCPE_OK;
}
#endif
