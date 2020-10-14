/* pdp6_slave.c: Slaved processor.

   Copyright (c) 2018, Lars Brinkhoff

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

   This is a device which interfaces with a master processor through
   shared memory and inter-processor interrupts.
*/

#include "kx10_defs.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_SLAVE
#define NUM_DEVS_SLAVE 0
#endif

#if NUM_DEVS_SLAVE > 0

/* External bus interface. */
#define DATO            1
#define DATI            2
#define ACK             3
#define ERR             4
#define TIMEOUT         5
#define IRQ             6

/* Simulator time units for a Unibus memory cycle. */
#define SLAVE_MEM_CYCLE 100

/* Interprocessor interrupt device. */
#define SLAVE_DEVNUM      020

#define SLAVE_POLL        1000

#define PIA     u3
#define STATUS  u4

static t_stat slave_devio(uint32 dev, uint64 *data);
static t_stat slave_svc (UNIT *uptr);
static t_stat slave_reset (DEVICE *dptr);
static t_stat slave_attach (UNIT *uptr, CONST char *ptr);
static t_stat slave_detach (UNIT *uptr);
static t_stat slave_attach_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char *slave_description (DEVICE *dptr);
static uint8  slave_valid[040000];

UNIT slave_unit[1] = {
  { UDATA (&slave_svc, UNIT_IDLE|UNIT_ATTABLE, 0), 1000 },
};

static REG slave_reg[] = {
  { DRDATAD  (POLL, slave_unit[0].wait,   24,    "poll interval"), PV_LEFT },
  {BRDATA(BUFF, slave_valid, 8, 8, sizeof(slave_valid)), REG_HRO},
  { NULL }
};

static MTAB slave_mod[] = {
    { 0 }
};

#define DEBUG_TRC       0x0000400

static DEBTAB slave_debug[] = {
  {"TRACE",   DEBUG_TRC,    "Routine trace"},
  {"CMD",     DEBUG_CMD,    "Command Processing"},
  {"CONO",    DEBUG_CONO,   "CONO instructions"},
  {"CONI",    DEBUG_CONI,   "CONI instructions"},
  {"DATAIO",  DEBUG_DATAIO, "DATAI/O instructions"},
  {0},
};

DEVICE slave_dev = {
  "SLAVE", slave_unit, slave_reg, slave_mod,
  1, 8, 16, 2, 8, 16,
  NULL,                                               /* examine */
  NULL,                                               /* deposit */
  &slave_reset,                                       /* reset */
  NULL,                                               /* boot */
  slave_attach,                                       /* attach */
  slave_detach,                                       /* detach */
  NULL,                                               /* context */
  DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX,
  DEBUG_CMD,                                          /* debug control */
  slave_debug,                                        /* debug flags */
  NULL,                                               /* memory size chage */
  NULL,                                               /* logical name */
  NULL,                                               /* help */
  &slave_attach_help,                                 /* attach help */
  NULL,                                               /* help context */
  &slave_description,                                 /* description */
};

static TMLN slave_ldsc;                                 /* line descriptor */
static TMXR slave_desc = { 1, 0, 0, &slave_ldsc };      /* mux descriptor */

static t_stat slave_reset (DEVICE *dptr)
{
  sim_debug(DEBUG_TRC, dptr, "slave_reset()\n");

  slave_unit[0].flags |= UNIT_ATTABLE | UNIT_IDLE;
  slave_desc.packet = TRUE;
  slave_desc.notelnet = TRUE;
  slave_desc.buffered = 2048;

  if (slave_unit[0].flags & UNIT_ATT)
    sim_activate (&slave_unit[0], 1000);
  else
    sim_cancel (&slave_unit[0]);

  return SCPE_OK;
}

static t_stat slave_attach (UNIT *uptr, CONST char *cptr)
{
  t_stat r;

  if (!cptr || !*cptr)
    return SCPE_ARG;
  if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOATT;
  r = tmxr_attach_ex (&slave_desc, uptr, cptr, FALSE);
  if (r != SCPE_OK)                                       /* error? */
    return r;
  sim_debug(DEBUG_TRC, &slave_dev, "activate connection\n");
  sim_activate (uptr, 10);    /* start poll */
  return SCPE_OK;
}

static t_stat slave_detach (UNIT *uptr)
{
  t_stat r;

  if (!(uptr->flags & UNIT_ATT))
    return SCPE_OK;
  sim_cancel (uptr);
  r = tmxr_detach (&slave_desc, uptr);
  uptr->filename = NULL;
  return r;
}

static int error (const char *message)
{
  sim_debug (DEBUG_TRC, &slave_dev, "%s\r\n", message);
  sim_debug (DEBUG_TRC, &slave_dev, "CLOSE\r\n");
  slave_ldsc.rcve = 0;
  tmxr_reset_ln (&slave_ldsc);
  return -1;
}

static void build (uint8 *request, uint8 octet)
{
  request[0]++;
  request[request[0]] = octet & 0377;
}

static t_stat process_request (UNIT *uptr, const uint8 *request, size_t size)
{
  uint8 response[12];
  t_addr address;
  uint64 data;
  t_stat stat;

  if (size == 0)
    return SCPE_OK;
  if (size > 9)
    return error ("Malformed transaction");

  sim_debug(DEBUG_CMD, &slave_dev, "got packet\n");

  memset (response, 0, sizeof response);

  switch (request[0]) {
  case DATI:
    address = request[1] + (request[2] << 8) + (request[3] << 16);
    if (address < MEMSIZE) {
      data = M[address];
      build (response, ACK);
      build (response, (uint8)(data & 0xff));
      build (response, (uint8)((data >> 8)) & 0xff);
      build (response, (uint8)((data >> 16) & 0xff));
      build (response, (uint8)((data >> 24) & 0xff));
      build (response, (uint8)((data >> 32) & 0xff));
      sim_debug(DEBUG_DATAIO, &slave_dev, "DATI %06o -> %012llo\n",
                address, data);
    } else {
      build (response, ERR);
      sim_debug(DEBUG_DATAIO, &slave_dev, "DATI %06o -> NXM\n", address);
    }
    break;
  case DATO:
    address = request[1] + (request[2] << 8) + (request[3] << 16);
    if (address < MEMSIZE) {
      data = request[4];
      data |= ((uint64)request[5]) << 8;
      data |= ((uint64)request[6]) << 16;
      data |= ((uint64)request[7]) << 24;
      data |= ((uint64)request[8]) << 32;
      M[address] = data;
      build (response, ACK);
      sim_debug(DEBUG_DATAIO, &slave_dev, "DATO %06o <- %012llo\n",
                address, data);
    } else {
      build (response, ERR);
      sim_debug(DEBUG_DATAIO, &slave_dev, "DATO %06o -> NXM\n", address);
    }
    break;
  case ACK:
    break;
  case IRQ:
    uptr->STATUS |= 010;
    set_interrupt(SLAVE_DEVNUM, uptr->PIA);
    build (response, ACK);
    sim_debug(DEBUG_DATAIO, &slave_dev, "IRQ\n");
    break;
  default:
    return error ("Malformed transaction");
  }

  stat = tmxr_put_packet_ln (&slave_ldsc, response + 1, (size_t)response[0]);
  if (stat != SCPE_OK)
    return error ("Write error in transaction");
  return stat;
}

static t_stat slave_svc (UNIT *uptr)
{
  const uint8 *slave_request;
  size_t size;

  if (tmxr_poll_conn(&slave_desc) >= 0) {
    sim_debug(DEBUG_CMD, &slave_dev, "got connection\n");
    slave_ldsc.rcve = 1;
    memset(&slave_valid[0], 0, sizeof(slave_valid));
    uptr->wait = SLAVE_POLL;
  }

  tmxr_poll_rx (&slave_desc);
  if (slave_ldsc.rcve && !slave_ldsc.conn) {
    slave_ldsc.rcve = 0;
    tmxr_reset_ln (&slave_ldsc);
    sim_debug(DEBUG_CMD, &slave_dev, "reset\n");
  }

  if (tmxr_get_packet_ln (&slave_ldsc, &slave_request, &size) == SCPE_OK)
    process_request (uptr, slave_request, size);

  sim_clock_coschedule (uptr, uptr->wait);
  return SCPE_OK;
}

static t_stat slave_attach_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
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


static const char *slave_description (DEVICE *dptr)
{
  return "Auxiliary processor";
}

t_stat slave_devio(uint32 dev, uint64 *data)
{
    UNIT   *uptr = &slave_unit[0];

    switch(dev & 03) {
    case CONO:
        sim_debug(DEBUG_CONO, &slave_dev, "CONO %012llo\n", *data);
        uptr->PIA = *data & 7;
        if (*data & 010)
          {
            // Clear interrupt from the PDP-10.
            uptr->STATUS &= ~010;
            clr_interrupt(SLAVE_DEVNUM);
          }
#if 0
       if (*data & 020)
         slave_interrupt ();
#endif
        break;
    case CONI:
        *data = (uptr->STATUS & 010) | uptr->PIA;
        sim_debug(DEBUG_CONI, &slave_dev, "CONI %012llo\n", *data);
        break;
    case DATAI:
        *data = 0;
        sim_debug(DEBUG_CONI, &slave_dev, "DATAI %012llo\n", *data);
        break;
    case DATAO:
        sim_debug(DEBUG_CONI, &slave_dev, "DATAO %012llo\n", *data);
        break;
    }

    return SCPE_OK;
}

#endif

