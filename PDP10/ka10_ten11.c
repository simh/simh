/* ka10_ten11.c: Rubin 10-11 interface.

   Copyright (c) 2018-2019, Lars Brinkhoff

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

   This is a device which interfaces with eight Unibuses.  It's
   specific to the MIT AI lab PDP-10.
*/

#include "kx10_defs.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_TEN11
#define NUM_DEVS_TEN11 0
#endif

#if (NUM_DEVS_TEN11 > 0)
#include <fcntl.h>
#include <sys/types.h>

/* Rubin 10-11 pager. */
static uint64 ten11_pager[256];

/* Physical address of 10-11 control page. */
#define T11CPA          03776000

/* Bits in a 10-11 page table entry. */
#define T11VALID        (0400000000000LL)
#define T11WRITE        (0200000000000LL)
#define T11PDP11        (0003400000000LL)
#define T11ADDR         (0000377776000LL)
#define T11LIMIT        (0000000001777LL)

/* External Unibus interface. */
#define DATO            1
#define DATI            2
#define ACK             3
#define ERR             4
#define TIMEOUT         5

#define TEN11_POLL  100

/* Simulator time units for a Unibus memory cycle. */
#define UNIBUS_MEM_CYCLE 100


static t_stat ten11_svc (UNIT *uptr);
static t_stat ten11_reset (DEVICE *dptr);
static t_stat ten11_attach (UNIT *uptr, CONST char *ptr);
static t_stat ten11_detach (UNIT *uptr);
static t_stat ten11_attach_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
static const char *ten11_description (DEVICE *dptr);

UNIT ten11_unit[1] = {
  { UDATA (&ten11_svc, UNIT_IDLE|UNIT_ATTABLE, 0), 1000 },
};

static REG ten11_reg[] = {
  { DRDATAD  (POLL, ten11_unit[0].wait,   24,    "poll interval"), PV_LEFT },
  { NULL }
};

static MTAB ten11_mod[] = {
    { 0 }
};

#define DBG_TRC         1
#define DBG_CMD         2

static DEBTAB ten11_debug[] = {
  {"TRACE",   DBG_TRC,    "Routine trace"},
  {"CMD",     DBG_CMD,    "Command Processing"},
  {0},
};

DEVICE ten11_dev = {
  "TEN11", ten11_unit, ten11_reg, ten11_mod,
  1, 8, 16, 2, 8, 16,
  NULL,                                               /* examine */
  NULL,                                               /* deposit */
  &ten11_reset,                                       /* reset */
  NULL,                                               /* boot */
  ten11_attach,                                       /* attach */
  ten11_detach,                                       /* detach */
  NULL,                                               /* context */
  DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX,
  DBG_CMD,                                            /* debug control */
  ten11_debug,                                        /* debug flags */
  NULL,                                               /* memory size chage */
  NULL,                                               /* logical name */
  NULL,                                               /* help */
  &ten11_attach_help,                                 /* attach help */
  NULL,                                               /* help context */
  &ten11_description,                                 /* description */
};

static TMLN ten11_ldsc;                                 /* line descriptor */
static TMXR ten11_desc = { 1, 0, 0, &ten11_ldsc };      /* mux descriptor */

static t_stat ten11_reset (DEVICE *dptr)
{
  sim_debug(DBG_TRC, dptr, "ten11_reset()\n");

  ten11_unit[0].flags |= UNIT_ATTABLE | UNIT_IDLE;
  ten11_desc.packet = TRUE;
  ten11_desc.notelnet = TRUE;
  ten11_desc.buffered = 2048;

  if (ten11_unit[0].flags & UNIT_ATT)
    sim_activate_abs (&ten11_unit[0], 0);
  else
    sim_cancel (&ten11_unit[0]);

  return SCPE_OK;
}

static t_stat ten11_attach (UNIT *uptr, CONST char *cptr)
{
  t_stat r;

  if (!cptr || !*cptr)
    return SCPE_ARG;
  if (!(uptr->flags & UNIT_ATTABLE))
    return SCPE_NOATT;
  r = tmxr_attach_ex (&ten11_desc, uptr, cptr, FALSE);
  if (r != SCPE_OK)                                       /* error? */
    return r;
  sim_debug(DBG_TRC, &ten11_dev, "activate connection\n");
  sim_activate_abs (uptr, 0);    /* start poll */
  uptr->flags |= UNIT_ATT;
  return SCPE_OK;
}

static t_stat ten11_detach (UNIT *uptr)
{
  t_stat r;

  if (!(uptr->flags & UNIT_ATT))
    return SCPE_OK;
  sim_cancel (uptr);
  r = tmxr_detach (&ten11_desc, uptr);
  uptr->flags &= ~UNIT_ATT;
  free (uptr->filename);
  uptr->filename = NULL;
  return r;
}

static void build (unsigned char *request, unsigned char octet)
{
  request[0]++;
  request[request[0]] = octet;
}

static t_stat ten11_svc (UNIT *uptr)
{
  tmxr_poll_rx (&ten11_desc);
  if (ten11_ldsc.rcve && !ten11_ldsc.conn) {
    ten11_ldsc.rcve = 0;
    tmxr_reset_ln (&ten11_ldsc);
  }
  if (tmxr_poll_conn(&ten11_desc) >= 0) {
    sim_debug(DBG_CMD, &ten11_dev, "got connection\n");
    ten11_ldsc.rcve = 1;
    uptr->wait = TEN11_POLL;
  }
  sim_clock_coschedule (uptr, uptr->wait);
  return SCPE_OK;
}

static t_stat ten11_attach_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
const char helpString[] =
 /* The '*'s in the next line represent the standard text width of a help line */
     /****************************************************************************/
    " The %D device is an implementation of the Rubin PDP-10 to PDP-11 interface\n"
    " facility.  This allows a PDP 10 system to reach into a PDP-11 simulator\n"
    " and modify or access the contents of the PDP-11 memory.\n\n"
    " The device must be attached to a receive port, this is done by using the\n"
    " ATTACH command to specify the receive port number.\n"
    "\n"
    "+sim> ATTACH %U port\n"
    "\n"
    ;

 return scp_help (st, dptr, uptr, flag, helpString, cptr);
 return SCPE_OK;
}


static const char *ten11_description (DEVICE *dptr)
{
  return "Rubin PDP-10 to PDP-11 interface";
}

static int error (const char *message)
{
  sim_debug (DBG_TRC, &ten11_dev, "%s\r\n", message);
  sim_debug (DBG_TRC, &ten11_dev, "CLOSE\r\n");
  ten11_ldsc.rcve = 0;
  tmxr_reset_ln (&ten11_ldsc);
  return -1;
}

static int transaction (unsigned char *request, unsigned char *response)
{
  const uint8 *ten11_request;
  size_t size;
  t_stat stat;

  stat = tmxr_put_packet_ln (&ten11_ldsc, request + 1, (size_t)request[0]);
  if (stat != SCPE_OK)
    return error ("Write error in transaction");

  do {
    tmxr_poll_rx (&ten11_desc);
    stat = tmxr_get_packet_ln (&ten11_ldsc, &ten11_request, &size);
  } while (stat != SCPE_OK || size == 0);

  if (size > 7)
    return error ("Malformed transaction");

  memcpy (response, ten11_request, size);
  return 0;
}

static int read_word (int addr, int *data)
{
  unsigned char request[8];
  unsigned char response[8];

  sim_interval -= UNIBUS_MEM_CYCLE;

  if ((ten11_unit[0].flags & UNIT_ATT) == 0) {
      *data = 0;
      return 0;
  }

  memset (request, 0, sizeof request);
  build (request, DATI);
  build (request, (addr >> 16) & 0377);
  build (request, (addr >> 8) & 0377);
  build (request, (addr) & 0377);

  if (transaction (request, response) == -1) {
    /* Network error. */
    *data = 0;
    return 0;
  }

  switch (response[0])
    {
    case ACK:
      *data = response[2];
      *data |= response[1] << 8;
      sim_debug (DBG_TRC, &ten11_dev, "Read word %06o\n", *data);
      break;
    case ERR:
      fprintf (stderr, "TEN11: Read error %06o\r\n", addr);
      *data = 0;
      break;
    case TIMEOUT:
      fprintf (stderr, "TEN11: Read timeout %06o\r\n", addr);
      *data = 0;
      break;
    default:
      return error ("Protocol error");
    }

  return 0;
}

int ten11_read (int addr, uint64 *data)
{
  int offset = addr & 01777;
  int word1, word2;

  if (addr >= T11CPA) {
    /* Accessing the control page. */
    if (offset >= 0400) {
      sim_debug (DBG_TRC, &ten11_dev,
                 "Control page read NXM: %o @ %o\n",
                 offset, PC);
      return 1;
    }
    *data = ten11_pager[offset];
  } else {
    /* Accessing a memory page. */
    int page = (addr >> 10) & 0377;
    uint64 mapping = ten11_pager[page];
    int unibus, uaddr, limit;

    limit = mapping & T11LIMIT;
    if ((mapping & T11VALID) == 0 || offset > limit) {
      sim_debug (DBG_TRC, &ten11_dev,
                 "(%o) %07o >= 4,,000000 / %llo / %o > %o\n",
                 page, addr, (mapping & T11VALID), offset, limit);
      return 1;
    }

    unibus = (mapping & T11PDP11) >> 26;
    uaddr = ((mapping & T11ADDR) >> 10) + offset;
    uaddr <<= 2;

    read_word (uaddr, &word1);
    read_word (uaddr + 2, &word2);
    *data = ((uint64)word1 << 20) | (word2 << 4);
    
    sim_debug (DBG_TRC, &ten11_dev,
               "Read: (%o) %06o -> %012llo\n",
               unibus, uaddr, *data);
  }
  return 0;
}

static int write_word (int addr, uint16 data)
{
  unsigned char request[8];
  unsigned char response[8];

  sim_interval -= UNIBUS_MEM_CYCLE;

  if ((ten11_unit[0].flags & UNIT_ATT) == 0) {
      return 0;
  }

  memset (request, 0, sizeof request);
  build (request, DATO);
  build (request, (addr >> 16) & 0377);
  build (request, (addr >> 8) & 0377);
  build (request, (addr) & 0377);
  build (request, (data >> 8) & 0377);
  build (request, (data) & 0377);

  transaction (request, response);

  switch (response[0])
    {
    case ACK:
      break;
    case ERR:
      fprintf (stderr, "TEN11: Write error %06o\r\n", addr);
      break;
    case TIMEOUT:
      fprintf (stderr, "TEN11: Write timeout %06o\r\n", addr);
      break;
    default:
      return error ("Protocol error");
    }

  return 0;
}

int ten11_write (int addr, uint64 data)
{
  int offset = addr & 01777;

  if (addr >= T11CPA) {
    /* Accessing the control page. */
    if (offset >= 0400) {
      sim_debug (DBG_TRC, &ten11_dev,
                 "Control page write NXM: %o @ %o\n",
                 offset, PC);
      return 1;
    }
    ten11_pager[offset] = data;
    sim_debug (DBG_TRC, &ten11_dev,
               "Page %03o: %s %s (%llo) %06llo/%04llo\n",
               offset,
               (data & T11VALID) ? "V" : "I",
               (data & T11WRITE) ? "RW" : "R",
               (data & T11PDP11) >> 26,
               (data & T11ADDR) >> 10,
               (data & T11LIMIT));
  } else {
    /* Accessing a memory page. */
    int page = (addr >> 10) & 0377;
    uint64 mapping = ten11_pager[page];
    int unibus, uaddr, limit;
    limit = mapping & T11LIMIT;
    if ((mapping & T11VALID) == 0 || offset > limit) {
      sim_debug (DBG_TRC, &ten11_dev,
                 "(%o) %07o >= 4,,000000 / %llo / %o > %o\n",
                 page, addr, (mapping & T11VALID), offset, limit);
      return 1;
    }
    unibus = (mapping & T11PDP11) >> 26;
    uaddr = ((mapping & T11ADDR) >> 10) + offset;
    uaddr <<= 2;
    sim_debug (DBG_TRC, &ten11_dev,
               "Write: (%o) %06o <- %012llo\n",
               unibus, uaddr, data);

    if ((data & 010) == 0)
      write_word (uaddr, (data >> 20) & 0177777);
    if ((data & 004) == 0)
      write_word (uaddr + 2, (data >> 4) & 0177777);
  }
  return 0;
}
#endif
