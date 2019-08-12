/* ka10_auxcpu.c: Auxiliary processor.

   Copyright (c) 2018, Lars Brinkhoff

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
   RICHARD CORNWELL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   This is a device which interfaces with an auxiliary processor
   through shared memory and inter-processor interrupts.
*/

#include "kx10_defs.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_AUXCPU
#define NUM_DEVS_AUXCPU 0
#endif

#if NUM_DEVS_AUXCPU > 0
#include <fcntl.h>
//#include <unistd.h>
#include <sys/types.h>

/* External bus interface. */
#define DATO            1
#define DATI            2
#define ACK             3
#define ERR             4
#define TIMEOUT         5
#define IRQ             6

/* Simulator time units for a Unibus memory cycle. */
#define AUXCPU_MEM_CYCLE 100

/* Interprocessor interrupt device. */
#define AUXCPU_DEVNUM      020

#define AUXCPU_POLL        1000


static int pia = 0;
static int status = 0;
t_value auxcpu_base = 03000000;

static t_stat auxcpu_devio(uint32 dev, t_uint64 *data);
static t_stat auxcpu_svc (UNIT *uptr);
static t_stat auxcpu_reset (DEVICE *dptr);
static t_stat auxcpu_attach (UNIT *uptr, CONST char *ptr);
static t_stat auxcpu_detach (UNIT *uptr);
static t_stat auxcpu_set_base (UNIT *uptr, int32 val, CONST char *cptr, void *desc);
static t_stat auxcpu_show_base (FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat auxcpu_attach_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char *auxcpu_description (DEVICE *dptr);

UNIT auxcpu_unit[1] = {
  { UDATA (&auxcpu_svc,        UNIT_IDLE|UNIT_ATTABLE, 0), 1000 },
};

static REG auxcpu_reg[] = {
  { DRDATAD  (POLL, auxcpu_unit[0].wait,   24,    "poll interval"), PV_LEFT },
  { NULL }
};

static MTAB auxcpu_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "base address", "BASE",
          &auxcpu_set_base, &auxcpu_show_base },
    { 0 }
};

#define DBG_TRC         1
#define DBG_CMD         2

static DEBTAB auxcpu_debug[] = {
  {"TRACE",   DBG_TRC,    "Routine trace"},
  {"CMD",     DBG_CMD,    "Command Processing"},
  {0},
};

DEVICE auxcpu_dev = {
  "AUXCPU", auxcpu_unit, auxcpu_reg, auxcpu_mod,
  1, 8, 16, 2, 8, 16,
  NULL,                                               /* examine */
  NULL,                                               /* deposit */
  &auxcpu_reset,                                       /* reset */
  NULL,                                               /* boot */
  auxcpu_attach,                                       /* attach */
  auxcpu_detach,                                       /* detach */
  NULL,                                               /* context */
  DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX,
  DBG_CMD,                                            /* debug control */
  auxcpu_debug,                                        /* debug flags */
  NULL,                                               /* memory size chage */
  NULL,                                               /* logical name */
  NULL,                                               /* help */
  &auxcpu_attach_help,                                 /* attach help */
  NULL,                                               /* help context */
  &auxcpu_description,                                 /* description */
};

static TMLN auxcpu_ldsc;                                 /* line descriptor */
static TMXR auxcpu_desc = { 1, 0, 0, &auxcpu_ldsc };      /* mux descriptor */

static t_stat auxcpu_reset (DEVICE *dptr)
{
  sim_debug(DBG_TRC, dptr, "auxcpu_reset()\n");

  auxcpu_unit[0].flags |= UNIT_ATTABLE | UNIT_IDLE;
  auxcpu_desc.packet = TRUE;
  auxcpu_desc.notelnet = TRUE;
  auxcpu_desc.buffered = 2048;

  if (auxcpu_unit[0].flags & UNIT_ATT)
    sim_activate (&auxcpu_unit[0], 1000);
  else
    sim_cancel (&auxcpu_unit[0]);

  return SCPE_OK;
}

static t_stat auxcpu_attach (UNIT *uptr, CONST char *cptr)
{
  t_stat r;

  if (!cptr || !*cptr)
    return SCPE_ARG;
  if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOATT;
  r = tmxr_attach_ex (&auxcpu_desc, uptr, cptr, FALSE);
  if (r != SCPE_OK)                                       /* error? */
    return r;
  sim_debug(DBG_TRC, &auxcpu_dev, "activate connection\n");
  sim_activate (uptr, 10);    /* start poll */
  uptr->flags |= UNIT_ATT;
  return SCPE_OK;
}

static t_stat auxcpu_detach (UNIT *uptr)
{
  t_stat r;

  if (!(uptr->flags & UNIT_ATT))
    return SCPE_OK;
  sim_cancel (uptr);
  r = tmxr_detach (&auxcpu_desc, uptr);
  uptr->flags &= ~UNIT_ATT;
  free (uptr->filename);
  uptr->filename = NULL;
  return r;
}

static void build (unsigned char *request, unsigned char octet)
{
  request[0]++;
  request[request[0]] = octet & 0377;
}

static t_stat auxcpu_svc (UNIT *uptr)
{
  tmxr_poll_rx (&auxcpu_desc);
  if (auxcpu_ldsc.rcve && !auxcpu_ldsc.conn) {
    auxcpu_ldsc.rcve = 0;
    tmxr_reset_ln (&auxcpu_ldsc);
  }

  /* If incoming interrput => status |= 010 */
  if (status & 010)
    set_interrupt(AUXCPU_DEVNUM, pia);
  else
    clr_interrupt(AUXCPU_DEVNUM);

  if (tmxr_poll_conn(&auxcpu_desc) >= 0) {
    sim_debug(DBG_CMD, &auxcpu_dev, "got connection\n");
    auxcpu_ldsc.rcve = 1;
    uptr->wait = AUXCPU_POLL;
  }
  sim_activate (uptr, uptr->wait);
  return SCPE_OK;
}

static t_stat auxcpu_attach_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
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

 return scp_help (st, dptr, uptr, flag, helpString, cptr);
 return SCPE_OK;
}


static const char *auxcpu_description (DEVICE *dptr)
{
  return "Auxiliary processor";
}

static int error (const char *message)
{
  sim_debug (DBG_TRC, &auxcpu_dev, "%s\r\n", message);
  sim_debug (DBG_TRC, &auxcpu_dev, "CLOSE\r\n");
  auxcpu_ldsc.rcve = 0;
  tmxr_reset_ln (&auxcpu_ldsc);
  return -1;
}

static int transaction (unsigned char *request, unsigned char *response)
{
  const uint8 *auxcpu_request;
  size_t size;
  t_stat stat;

  stat = tmxr_put_packet_ln (&auxcpu_ldsc, request + 1, (size_t)request[0]);
  if (stat != SCPE_OK)
    return error ("Write error in transaction");

  do {
    tmxr_poll_rx (&auxcpu_desc);
    stat = tmxr_get_packet_ln (&auxcpu_ldsc, &auxcpu_request, &size);
  } while (stat != SCPE_OK || size == 0);

  if (size > 7)
    return error ("Malformed transaction");

  memcpy (response, auxcpu_request, size);
  return 0;
}

int auxcpu_read (int addr, t_uint64 *data)
{
  unsigned char request[12];
  unsigned char response[12];

  sim_interval -= AUXCPU_MEM_CYCLE;

  if ((auxcpu_unit[0].flags & UNIT_ATT) == 0) {
      *data = 0;
      return 0;
  }

  addr &= 037777;

  memset (request, 0, sizeof request);
  build (request, DATI);
  build (request, addr & 0377);
  build (request, (addr >> 8) & 0377);
  build (request, (addr >> 16) & 0377);

  transaction (request, response);

  switch (response[0])
    {
    case ACK:
      *data = (t_uint64)response[1];
      *data |= (t_uint64)response[2] << 8;
      *data |= (t_uint64)response[3] << 16;
      *data |= (t_uint64)response[4] << 24;
      *data |= (t_uint64)response[5] << 32;
      break;
    case ERR:
      fprintf (stderr, "AUXCPU: Read error %06o\r\n", addr);
      *data = 0;
      break;
    case TIMEOUT:
      fprintf (stderr, "AUXCPU: Read timeout %06o\r\n", addr);
      *data = 0;
      break;
    default:
      return error ("Protocol error");
    }

  return 0;
}

int auxcpu_write (int addr, t_uint64 data)
{
  unsigned char request[12];
  unsigned char response[12];

  sim_interval -= AUXCPU_MEM_CYCLE;

  if ((ten11_unit[0].flags & UNIT_ATT) == 0) {
      return 0;
  }

  addr &= 037777;

  memset (request, 0, sizeof request);
  build (request, DATO);
  build (request, (addr) & 0377);
  build (request, (addr >> 8) & 0377);
  build (request, (addr >> 16) & 0377);
  build (request, (data) & 0377);
  build (request, (data >> 8) & 0377);
  build (request, (data >> 16) & 0377);
  build (request, (data >> 24) & 0377);
  build (request, (data >> 32) & 0377);

  transaction (request, response);

  switch (response[0])
    {
    case ACK:
      break;
    case ERR:
      fprintf (stderr, "AUXCPU: Write error %06o\r\n", addr);
      break;
    case TIMEOUT:
      fprintf (stderr, "AUXCPU: Write timeout %06o\r\n", addr);
      break;
    default:
      return error ("Protocol error");
    }

  return 0;
}

static int auxcpu_interrupt (void)
{
  unsigned char request[12];
  unsigned char response[12];
  memset (request, 0, sizeof request);

  sim_debug(DEBUG_IRQ, &auxcpu_dev, "PDP-10 interrupting the PDP-6\n");

  build (request, IRQ);

  transaction (request, response);

  switch (response[1])
    {
    case ACK:
      break;
    case ERR:
    case TIMEOUT:
      fprintf (stderr, "AUXCPU: Interrupt error or timeout\r\n");
      break;
    default:
      return error ("Protocol error");
    }

  return 0;
}

t_stat auxcpu_devio(uint32 dev, t_uint64 *data)
{
    DEVICE *dptr = &auxcpu_dev;

    switch(dev & 07) {
    case CONO:
        sim_debug(DEBUG_CONO, &auxcpu_dev, "CONO %012llo\n", *data);
        pia = *data & 7;
        if (*data & 010)
          {
            // Clear interrupt from the PDP-6.
            status &= ~010;
            clr_interrupt(AUXCPU_DEVNUM);
          }
        if (*data & 020)
          auxcpu_interrupt ();
        break;
    case CONI:
        *data = (status & 010) | pia;
        sim_debug(DEBUG_CONI, &auxcpu_dev, "CONI %012llo\n", *data);
        break;
    case DATAI:
        *data = 0;
        sim_debug(DEBUG_CONI, &auxcpu_dev, "DATAI %012llo\n", *data);
        break;
    case DATAO:
        sim_debug(DEBUG_CONI, &auxcpu_dev, "DATAO %012llo\n", *data);
        break;
    }

    return SCPE_OK;
}

static t_stat auxcpu_set_base (UNIT *uptr, int32 val, CONST char *cptr, void *desc)
{
    t_stat r;
    t_value x;

    if (cptr == NULL || *cptr == 0)
        return SCPE_ARG;

    x = get_uint (cptr, 8, 03777777, &r);
    if (r != SCPE_OK)
        return SCPE_ARG;

    auxcpu_base = x;
    return SCPE_OK;
}

static t_stat auxcpu_show_base (FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    fprintf (st, "Base: %llo", auxcpu_base);
    return SCPE_OK;
}
#endif
