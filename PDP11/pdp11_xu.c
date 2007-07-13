/* pdp11_xu.c: DEUNA/DELUA ethernet controller simulator
  ------------------------------------------------------------------------------

   Copyright (c) 2003-2007, David T. Hittner

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
   THE AUTHOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of the author shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author.

  ------------------------------------------------------------------------------

  This DEUNA/DELUA simulation is based on:
    Digital DELUA Users Guide, Part# EK-DELUA-UG-002
    Digital DEUNA Users Guide, Part# EK-DEUNA-UG-001
  These manuals can be found online at:
    http://www.spies.com/~aek/pdf/dec/unibus

  Testing performed:
   1) Receives/Transmits single packet under custom RSX driver
   2) Passes RSTS 10.1 controller probe diagnostics during boot
   3) VMS 7.2 on VAX780 summary:
       (May/2007: WinXP x64 host; MS VC++ 2005; SIMH v3.7-0 base; WinPcap 4.0)
        LAT    - SET HOST/LAT in/out
        DECNET - SET HOST in/out, COPY in/out
        TCP/IP - PING in/out; SET HOST/TELNET in/out, COPY/FTP in/out
        Clustering - Successfully clustered with AlphaVMS 8.2
   4) Runs VAX EVDWA diagnostic tests 1-10; tests 11-19 (M68000/ROM/RAM) fail

  Known issues:
   1) Most auxiliary commands are not implemented yet.
   2) System_ID broadcast is not implemented.
   3) There are residual Map_ReadB and Map_WriteB from the FvK version that
      probably need to be converted to Map_ReadW and Map_WriteW calls.
   4) Some jerkiness seen during interactive I/O with remote systems;
      this is probably attributable to changed polling times from when
	  the poll duration was standardized for idling support.

  ------------------------------------------------------------------------------

  Modification history:

  18-Jun-07  RMS  Added UNIT_IDLE flag
  03-May-07  DTH  Added missing FC_RMAL command; cleared multicast on write
  29-Oct-06  RMS  Synced poll and clock
  08-Dec-05  DTH  Implemented ancilliary functions 022/023/024/025
  18-Nov-05  DTH  Corrected time between system ID packets
  07-Sep-05  DTH  Corrected runt packet processing (found by Tim Chapman),
                  Removed unused variable
  16-Aug-05  RMS  Fixed C++ declaration and cast problems
  10-Mar-05  RMS  Fixed equality test in RCSTAT (from Mark Hittinger)
  16-Jan-04  DTH  Added more info to SHOW MOD commands
  09-Jan-04  DTH  Made XU floating address so that XUB will float correctly
  08-Jan-04  DTH  Added system_id message
  06-Jan-04  DTH  Added protection against changing mac and type if attached
  05-Jan-04  DTH  Moved most of xu_setmac to sim_ether
                  Implemented auxiliary function 12/13
                  Added SET/SHOW XU STATS
  31-Dec-03  DTH  RSTS 10.1 accepts controller during boot tests
                  Implemented chained buffers in transmit/receive processing
  29-Dec-03  DTH  Primitive RSX packet sending succeeds
  23-Dec-03  DTH  Implemented write function
  17-Dec-03  DTH  Implemented read function
  05-May-03  DTH  Started XU simulation -
                  core logic pirated from unreleased FvK PDP10 variant

  ------------------------------------------------------------------------------
*/

#include "pdp11_xu.h"

extern int32 tmxr_poll, tmr_poll, clk_tps, cpu_astop;
extern FILE *sim_log;

t_stat xu_rd(int32* data, int32 PA, int32 access);
t_stat xu_wr(int32  data, int32 PA, int32 access);
t_stat xu_svc(UNIT * uptr);
t_stat xu_reset (DEVICE * dptr);
t_stat xu_attach (UNIT * uptr, char * cptr);
t_stat xu_detach (UNIT * uptr);
t_stat xu_showmac (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xu_setmac  (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat xu_show_stats (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xu_set_stats  (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat xu_show_type (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xu_set_type (UNIT* uptr, int32 val, char* cptr, void* desc);
int32 xu_int (void);
t_stat xu_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat xu_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
void xua_read_callback(int status);
void xub_read_callback(int status);
void xua_write_callback(int status);
void xub_write_callback(int status);
void xu_setint (CTLR* xu);
void xu_clrint (CTLR* xu);
void xu_process_receive(CTLR* xu);
void xu_dump_rxring(CTLR* xu);
void xu_dump_txring(CTLR* xu);

DIB xua_dib = { IOBA_XU, IOLN_XU, &xu_rd, &xu_wr,
1, IVCL (XU), VEC_XU, {&xu_int} };

UNIT xua_unit[] = {
 { UDATA (&xu_svc, UNIT_IDLE|UNIT_ATTABLE|UNIT_DISABLE, 0) }      /* receive timer */
};

struct xu_device    xua = {
  xua_read_callback,                        /* read callback routine */
  xua_write_callback,                       /* write callback routine */
  {0x08, 0x00, 0x2B, 0xCC, 0xDD, 0xEE},     /* mac */
  XU_T_DELUA                                /* type */
  };

MTAB xu_mod[] = {
#if defined (VM_PDP11)
	{ MTAB_XTD|MTAB_VDV, 004, "ADDRESS", "ADDRESS",
		&set_addr, &show_addr, NULL },
#else
	{ MTAB_XTD|MTAB_VDV, 004, "ADDRESS", NULL,
		NULL, &show_addr, NULL },
#endif
	{ MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
		NULL, &show_vec, NULL },
  { MTAB_XTD | MTAB_VDV, 0, "MAC", "MAC=xx:xx:xx:xx:xx:xx",
    &xu_setmac, &xu_showmac, NULL },
  { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "ETH", "ETH",
    NULL, &eth_show, NULL },
  { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATS", "STATS",
    &xu_set_stats, &xu_show_stats, NULL },
  { MTAB_XTD | MTAB_VDV, 0, "TYPE", "TYPE={DEUNA|DELUA}",
    &xu_set_type, &xu_show_type, NULL },
  { 0 },
};

REG xua_reg[] = {
	{ NULL }  };

DEBTAB xu_debug[] = {
  {"TRACE",  DBG_TRC},
  {"WARN",   DBG_WRN},
  {"REG",    DBG_REG},
  {"PACKET", DBG_PCK},
  {"ETH",    DBG_ETH},
  {0}
};


DEVICE xu_dev = {
	"XU", xua_unit, xua_reg, xu_mod,
	1, XU_RDX, 8, 1, XU_RDX, 8,
	&xu_ex, &xu_dep, &xu_reset,
	NULL, &xu_attach, &xu_detach,
	&xua_dib, DEV_FLTA | DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG,
  0, xu_debug
  };


/* XUB does not exist in the PDP10 simulation */
#if defined(IOBA_XUB)

DIB xub_dib = { IOBA_XUB, IOLN_XUB, &xu_rd, &xu_wr,
		1, IVCL (XU), 0, { &xu_int } };

UNIT xub_unit[] = {
 { UDATA (&xu_svc, UNIT_IDLE|UNIT_ATTABLE|UNIT_DISABLE, 0) }      /* receive timer */
};

struct xu_device    xub = {
  xub_read_callback,                        /* read callback routine */
  xub_write_callback,                       /* write callback routine */
  {0x08, 0x00, 0x2B, 0xDD, 0xEE, 0xFF},     /* mac */
  XU_T_DELUA                                /* type */
  };

REG xub_reg[] = {
	{ NULL }  };

DEVICE xub_dev = {
  "XUB", xub_unit, xub_reg, xu_mod,
  1, XU_RDX, 8, 1, XU_RDX, 8,
  &xu_ex, &xu_dep, &xu_reset,
  NULL, &xu_attach, &xu_detach,
  &xub_dib, DEV_FLTA | DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG,
  0, xu_debug
};

#define XU_MAX_CONTROLLERS 2
CTLR xu_ctrl[] = {
   {&xu_dev,  xua_unit, &xua_dib, &xua}       /* XUA controller */
  ,{&xub_dev, xub_unit, &xub_dib, &xub}       /* XUB controller */
};
#else /* IOBA_XUB */
#define XU_MAX_CONTROLLERS 1
CTLR xu_ctrl[] = {
   {&xu_dev,  xua_unit, &xua_dib, &xua}       /* XUA controller */
}; 
#endif /* IOBA_XUB */

/*============================================================================*/

/* Multicontroller support */

CTLR* xu_unit2ctlr(UNIT* uptr)
{
  int i;
  unsigned int j;
  for (i=0; i<XU_MAX_CONTROLLERS; i++)
    for (j=0; j<xu_ctrl[i].dev->numunits; j++)
      if (&xu_ctrl[i].unit[j] == uptr)
        return &xu_ctrl[i];
  /* not found */
  return 0;
}

CTLR* xu_dev2ctlr(DEVICE* dptr)
{
  int i;
  for (i=0; i<XU_MAX_CONTROLLERS; i++)
    if (xu_ctrl[i].dev == dptr)
      return &xu_ctrl[i];
  /* not found */
  return 0;
}

CTLR* xu_pa2ctlr(uint32 PA)
{
  int i;
  for (i=0; i<XU_MAX_CONTROLLERS; i++)
    if ((PA >= xu_ctrl[i].dib->ba) && (PA < (xu_ctrl[i].dib->ba + xu_ctrl[i].dib->lnt)))
      return &xu_ctrl[i];
  /* not found */
  return 0;
}

/*============================================================================*/

/* stop simh from reading non-existant unit data stream */
t_stat xu_ex (t_value* vptr, t_addr addr, UNIT* uptr, int32 sw)
{
  return SCPE_NOFNC;
}

/* stop simh from writing non-existant unit data stream */
t_stat xu_dep (t_value val, t_addr addr, UNIT* uptr, int32 sw)
{
  return SCPE_NOFNC;
}

t_stat xu_showmac (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  CTLR* xu = xu_unit2ctlr(uptr);
  char  buffer[20];

  eth_mac_fmt((ETH_MAC*)xu->var->mac, buffer);
  fprintf(st, "MAC=%s", buffer);
  return SCPE_OK;
}

t_stat xu_setmac (UNIT* uptr, int32 val, char* cptr, void* desc)
{
  t_stat status;
  CTLR* xu = xu_unit2ctlr(uptr);

  if (!cptr) return SCPE_IERR;
  if (uptr->flags & UNIT_ATT) return SCPE_ALATT;
  status = eth_mac_scan(&xu->var->mac, cptr);
  return status;
}

t_stat xu_set_stats (UNIT* uptr, int32 val, char* cptr, void* desc)
{
  CTLR* xu = xu_unit2ctlr(uptr);

  /* set stats to zero, regardless of passed parameter */
  memset(&xu->var->stats, 0, sizeof(struct xu_stats));
  return SCPE_OK;
}

t_stat xu_show_stats (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  char* fmt = "  %-24s%d\n";
  CTLR* xu = xu_unit2ctlr(uptr);
  struct xu_stats* stats = &xu->var->stats;

  fprintf(st, "Ethernet statistics:\n");
  fprintf(st, fmt, "Seconds since cleared:",  stats->secs); 
  fprintf(st, fmt, "Recv frames:",            stats->frecv);
  fprintf(st, fmt, "Recv dbytes:",            stats->rbytes);
  fprintf(st, fmt, "Xmit frames:",            stats->ftrans);
  fprintf(st, fmt, "Xmit dbytes:",            stats->tbytes);
  fprintf(st, fmt, "Recv frames(multicast):", stats->mfrecv);
  fprintf(st, fmt, "Recv dbytes(multicast):", stats->mrbytes);
  fprintf(st, fmt, "Xmit frames(multicast):", stats->mftrans);
  fprintf(st, fmt, "Xmit dbytes(multicast):", stats->mtbytes);
  return SCPE_OK;
}

t_stat xu_show_type (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  CTLR* xu = xu_unit2ctlr(uptr);
  fprintf(st, "type=");
  switch (xu->var->type) {
    case  XU_T_DEUNA:       fprintf(st, "DEUNA");      break;
    case  XU_T_DELUA:       fprintf(st, "DELUA");      break;
  }
  return SCPE_OK;
}

t_stat xu_set_type (UNIT* uptr, int32 val, char* cptr, void* desc)
{
  CTLR* xu = xu_unit2ctlr(uptr);
  if (!cptr) return SCPE_IERR;
  if (uptr->flags & UNIT_ATT) return SCPE_ALATT;

  /* this assumes that the parameter has already been upcased */
  if      (!strcmp(cptr, "DEUNA"))      xu->var->type = XU_T_DEUNA;
  else if (!strcmp(cptr, "DELUA"))      xu->var->type = XU_T_DELUA;
  else return SCPE_ARG;

  return SCPE_OK;
}

/*============================================================================*/

void upd_stat16(uint16* stat, uint16 add)
{
  *stat += add;
  /* did stat roll over? latches at maximum */
  if (*stat < add)
    *stat = 0xFFFF;
}

void upd_stat32(uint32* stat, uint32 add)
{
  *stat += add;
  /* did stat roll over? latches at maximum */
  if (*stat < add)
    *stat = 0xFFFFFFFF;
}

void bit_stat16(uint16* stat, uint16 bits)
{
  *stat |= bits;
}

t_stat xu_process_local (CTLR* xu, ETH_PACK* pack)
{
  return SCPE_NOFNC; /* not implemented yet */
}

void xu_read_callback(CTLR* xu, int status)
{
  /* process any packets locally that can be */
  status = xu_process_local (xu, &xu->var->read_buffer);

  /* add packet to read queue */
  if (status != SCPE_OK)
    ethq_insert(&xu->var->ReadQ, 2, &xu->var->read_buffer, 0);
}

void xua_read_callback(int status)
{
  xu_read_callback(&xu_ctrl[0], status);
}

void xub_read_callback(int status)
{
  xu_read_callback(&xu_ctrl[1], status);
}

t_stat xu_system_id (CTLR* xu, const ETH_MAC dest, uint16 receipt_id)
{
  static uint16 receipt = 0;
  ETH_PACK system_id;
  uint8* const msg = &system_id.msg[0];
  t_stat status;

  sim_debug(DBG_TRC, xu->dev, "xu_system_id()\n");
  memset (&system_id, 0, sizeof(system_id));
  memcpy (&msg[0], dest, sizeof(ETH_MAC));
  memcpy (&msg[6], xu->var->setup.macs[0], sizeof(ETH_MAC));
  msg[12] = 0x60;                         /* type */
  msg[13] = 0x02;                         /* type */
  msg[14] = 0x1C;                         /* character count */
  msg[15] = 0x00;                         /* character count */
  msg[16] = 0x07;                         /* code */
  msg[17] = 0x00;                         /* zero pad */
  if (receipt_id) {
    msg[18] = receipt_id & 0xFF;          /* receipt number */
    msg[19] = (receipt_id >> 8) & 0xFF;   /* receipt number */
  } else {
    msg[18] = receipt & 0xFF;             /* receipt number */
    msg[19] = (receipt++ >> 8) & 0xFF;    /* receipt number */
  }

                                          /* MOP VERSION */
  msg[20] = 0x01;                         /* type */
  msg[21] = 0x00;                         /* type */
  msg[22] = 0x03;                         /* length */
  msg[23] = 0x03;                         /* version */
  msg[24] = 0x00;                         /* eco */
  msg[25] = 0x00;                         /* user eco */

                                          /* FUNCTION */
  msg[26] = 0x02;                         /* type */
  msg[27] = 0x00;                         /* type */
  msg[28] = 0x02;                         /* length */
  msg[29] = 0x05;                         /* value 1 */
  msg[30] = 0x00;                         /* value 2 */

                                          /* HARDWARE ADDRESS */
  msg[31] = 0x07;                         /* type */
  msg[32] = 0x00;                         /* type */
  msg[33] = 0x06;                         /* length */
  memcpy (&msg[34], xu->var->mac, sizeof(ETH_MAC)); /* ROM address */

                                          /* DEVICE TYPE */
  msg[40] = 0x64;                         /* type */
  msg[41] = 0x00;                         /* type */
  msg[42] = 0x01;                         /* length */
  if (xu->var->type == XU_T_DEUNA)
    msg[43] = 1;                          /* value (1=DEUNA) */
  else
    msg[43] = 11;                         /* value (11=DELUA) */

  /* write system id */
  system_id.len = 60;
  status = eth_write(xu->var->etherface, &system_id, NULL);

  return status;
}

t_stat xu_svc(UNIT* uptr)
{
  int queue_size;
  t_stat status;
  CTLR* xu = xu_unit2ctlr(uptr);
  const ETH_MAC mop_multicast = {0xAB, 0x00, 0x00, 0x02, 0x00, 0x00};
  const int one_second = clk_tps * tmr_poll;

  /* First pump any queued packets into the system */
  if ((xu->var->ReadQ.count > 0) && ((xu->var->pcsr1 & PCSR1_STATE) == STATE_RUNNING))
    xu_process_receive(xu);

  /* Now read and queue packets that have arrived */
  /* This is repeated as long as they are available and we have room */
  do
    {
    queue_size = xu->var->ReadQ.count;
    /* read a packet from the ethernet - processing is via the callback */
    status = eth_read (xu->var->etherface, &xu->var->read_buffer, xu->var->rcallback);
  } while (queue_size != xu->var->ReadQ.count);

  /* Now pump any still queued packets into the system */
  if ((xu->var->ReadQ.count > 0) && ((xu->var->pcsr1 & PCSR1_STATE) == STATE_RUNNING))
    xu_process_receive(xu);

  /* send identity packet when timer expires */
  if (--xu->var->idtmr <= 0) {
    if ((xu->var->mode & MODE_DMNT) == 0)           /* if maint msg is not disabled */
      status = xu_system_id(xu, mop_multicast, 0);  /*   then send ID packet */
    xu->var->idtmr = XU_ID_TIMER_VAL * one_second;  /* reset timer */
  }

  /* has one second timer expired? if so, update stats and reset timer */
  if (++xu->var->sectmr >= XU_SERVICE_INTERVAL) {
    upd_stat16 (&xu->var->stats.secs, 1);
    xu->var->sectmr = 0;
  }

  /* resubmit service timer if controller not halted */
  switch (xu->var->pcsr1 & PCSR1_STATE) {
    case STATE_READY:
    case STATE_RUNNING:
      sim_activate(&xu->unit[0], tmxr_poll);
      break;
  };

  return SCPE_OK;
}

void xu_write_callback (CTLR* xu, int status)
{
  xu->var->write_buffer.status = status;
}

void xua_write_callback (int status)
{
  xu_write_callback(&xu_ctrl[0], status);
}

void xub_write_callback (int status)
{
  xu_write_callback(&xu_ctrl[1], status);
}

void xu_setclrint(CTLR* xu, int32 bits)
{
  if (xu->var->pcsr0 & 0xFF00) {    /* if any interrupt bits on, */
    xu->var->pcsr0 |= PCSR0_INTR;   /*   turn master bit on */
    xu_setint(xu);                  /*   and trigger interrupt */
  } else {
    xu->var->pcsr0 &= ~PCSR0_INTR;  /*   ... or off */
    xu_clrint(xu);                  /*   and clear interrupt if needed*/
  }
}

t_stat xu_sw_reset (CTLR* xu)
{
  t_stat status;

  sim_debug(DBG_TRC, xu->dev, "xu_sw_reset()\n");

  /* Clear the registers. */
  xu->var->pcsr0 = PCSR0_DNI | PCSR0_INTR;
  xu->var->pcsr1 = STATE_READY;
  switch (xu->var->type) {
    case XU_T_DELUA:
      xu->var->pcsr1 |= TYPE_DELUA;
      break;
    case XU_T_DEUNA:
      xu->var->pcsr1 |= TYPE_DEUNA;
      if (!xu->var->etherface)  /* if not attached, set transceiver powerfail */
        xu->var->pcsr1 |= PCSR1_XPWR;
      break;
  }
  xu->var->pcsr2 = 0;
  xu->var->pcsr3 = 0;

  /* Clear the parameters. */
  xu->var->mode = 0;
  xu->var->pcbb = 0;
  xu->var->stat = 0;

  /* clear read queue */
  ethq_clear(&xu->var->ReadQ);

  /* clear setup info */
  memset(&xu->var->setup, 0, sizeof(struct xu_setup));

  /* clear network statistics */
  memset(&xu->var->stats, 0, sizeof(struct xu_stats));

  /* reset ethernet interface */
  memcpy (xu->var->setup.macs[0], xu->var->mac, sizeof(ETH_MAC));
  xu->var->setup.mac_count = 1;
  if (xu->var->etherface)
    status = eth_filter (xu->var->etherface, xu->var->setup.mac_count,
                         &xu->var->mac, xu->var->setup.multicast,
                         xu->var->setup.promiscuous);

  /* activate device if not disabled */
  if ((xu->dev->flags & DEV_DIS) == 0) {
    sim_activate_abs(&xu->unit[0], clk_cosched (tmxr_poll));
  }

  /* clear load_server address */
  memset(xu->var->load_server, 0, sizeof(ETH_MAC));

  return SCPE_OK;
}

/* Reset device. */
t_stat xu_reset(DEVICE* dptr)
{
  t_stat status;
  CTLR* xu = xu_dev2ctlr(dptr);

  sim_debug(DBG_TRC, xu->dev, "xu_reset()\n");
  /* init read queue (first time only) */
  status = ethq_init (&xu->var->ReadQ, XU_QUE_MAX);
  if (status != SCPE_OK)
    return status;

  /* software reset controller */
  xu_sw_reset(xu);

  return SCPE_OK;
}


/* Perform one of the defined ancillary functions. */
int32 xu_command(CTLR* xu)
{
  uint32 udbb;
  int fnc, mtlen, i, j;
  uint16 value, pltlen;
  t_stat status, rstatus, wstatus, wstatus2, wstatus3;
  struct xu_stats* stats = &xu->var->stats;
  uint16* udb = xu->var->udb;
  uint16* mac_w = (uint16*) xu->var->mac;
  static const ETH_MAC zeros = {0,0,0,0,0,0};
  static const ETH_MAC mcast_load_server = {0xAB, 0x00, 0x00, 0x01, 0x00, 0x00};
  static char* command[] = {
      "NO-OP",
      "Start Microaddress",
      "Read Default Physical Address",
      "NO-OP",
      "Read Physical Address",
      "Write Physical Address",
      "Read Multicast Address List",
      "Write Multicast Address List",
      "Read Descriptor Ring Format",
      "Write Descriptor Ring Format",
      "Read Counters",
      "Read/Clear Counters",
      "Read Mode Register",
      "Write Mode Register",
      "Read Status",
      "Read/Clear Status",
      "Dump Internal Memory",
      "Load Internal Memory",
      "Read System ID",
      "Write System ID",
      "Read Load Server Address",
      "Write Load Server Address"
    };

  /* Grab the PCB from the host. */
  rstatus = Map_ReadW(xu->var->pcbb, 8, xu->var->pcb);
  if (rstatus != 0)
    return PCSR0_PCEI + 1;

  /* High 8 bits are defined as MBZ. */
  if (xu->var->pcb[0] & 0177400)
    return PCSR0_PCEI;

  /* Decode the function to be performed. */
  fnc = xu->var->pcb[0] & 0377;
  sim_debug(DBG_TRC, xu->dev, "xu_command(), Command: %s [0%o]\n", command[fnc], fnc);

  switch (fnc) {
    case FC_NOOP:
      break;

    case FC_RDPA:			/* read default physical address */
      wstatus = Map_WriteB(xu->var->pcbb + 2, 6, xu->var->mac);
      if (wstatus)
        return PCSR0_PCEI + 1;
      break;

    case FC_RPA:			/* read current physical address */
      wstatus = Map_WriteB(xu->var->pcbb + 2, 6, (uint8*)&xu->var->setup.macs[0]);
      if (wstatus)
        return PCSR0_PCEI + 1;
      break;

    case FC_WPA:			/* write current physical address */
      rstatus = Map_ReadB(xu->var->pcbb + 2, 6, (uint8*)&xu->var->setup.macs[0]);
      if (xu->var->pcb[1] & 1)
        return PCSR0_PCEI;
      break;

    case FC_RMAL:   /* read multicast address list */
      mtlen = (xu->var->pcb[2] & 0xFF00) >> 8;
      udbb = xu->var->pcb[1] | ((xu->var->pcb[2] & 03) << 16);
      wstatus = Map_WriteB(udbb, mtlen * 3, (uint8*) &xu->var->setup.macs[1]);
	  break;

    case FC_WMAL:   /* write multicast address list */
      mtlen = (xu->var->pcb[2] & 0xFF00) >> 8;
sim_debug(DBG_TRC, xu->dev, "FC_WAL: mtlen=%d\n", mtlen);
      if (mtlen > 10)
        return PCSR0_PCEI;
      udbb = xu->var->pcb[1] | ((xu->var->pcb[2] & 03) << 16);
	  /* clear existing multicast list */
	  for (i=1; i<XU_FILTER_MAX; i++) {
		  for (j=0; j<6; j++)
			xu->var->setup.macs[i][j] = 0;
	  }
	  /* get multicast list from host */
      rstatus = Map_ReadB(udbb, mtlen * 6, (uint8*) &xu->var->setup.macs[1]);
      if (rstatus == 0) {
        xu->var->setup.mac_count = mtlen + 1;
        status = eth_filter (xu->var->etherface, xu->var->setup.mac_count,
                             xu->var->setup.macs, xu->var->setup.multicast,
                             xu->var->setup.promiscuous);
      } else {
        xu->var->pcsr0 |= PCSR0_PCEI;
      }
      break;

    case FC_RRF:			/* read ring format */
      if ((xu->var->pcb[1] & 1) || (xu->var->pcb[2] & 0374)) 
        return PCSR0_PCEI;
      xu->var->udb[0] = xu->var->tdrb & 0177776;
      xu->var->udb[1] = (xu->var->telen << 8) + ((xu->var->tdrb >> 16) & 3);
      xu->var->udb[2] = xu->var->trlen;
      xu->var->udb[3] = xu->var->rdrb & 0177776;
      xu->var->udb[4] = (xu->var->relen << 8) + ((xu->var->rdrb >> 16) & 3);
      xu->var->udb[5] = xu->var->rrlen;

      /* Write UDB to host memory. */
      udbb = xu->var->pcb[1] + ((xu->var->pcb[2] & 3) << 16);
      wstatus = Map_WriteW(udbb, 12, xu->var->pcb);
      if (wstatus != 0)
        return PCSR0_PCEI+1;
      break;

    case FC_WRF:			/* write ring format */
      if ((xu->var->pcb[1] & 1) || (xu->var->pcb[2] & 0374)) 
        return PCSR0_PCEI;
      if ((xu->var->pcsr1 & PCSR1_STATE) == STATE_RUNNING)
        return PCSR0_PCEI;

      /* Read UDB into local memory. */
      udbb = xu->var->pcb[1] + ((xu->var->pcb[2] & 3) << 16);
      rstatus = Map_ReadW(udbb, 12, xu->var->udb);
      if (rstatus)
        return PCSR0_PCEI+1;

      if ((xu->var->udb[0] & 1) || (xu->var->udb[1] & 0374) ||
          (xu->var->udb[3] & 1) || (xu->var->udb[4] & 0374) ||
          (xu->var->udb[5] < 2)) {
        return PCSR0_PCEI;
      }

      xu->var->tdrb = ((xu->var->udb[1] & 3) << 16) + (xu->var->udb[0] & 0177776);
      xu->var->telen = (xu->var->udb[1] >> 8) & 0377;
      xu->var->trlen = xu->var->udb[2];
      xu->var->rdrb = ((xu->var->udb[4] & 3) << 16) + (xu->var->udb[3] & 0177776);
      xu->var->relen = (xu->var->udb[4] >> 8) & 0377;
      xu->var->rrlen = xu->var->udb[5];
      xu->var->rxnext = 0;
      xu->var->txnext = 0;
// xu_dump_rxring(xu);
// xu_dump_txring(xu);

      break;

    case FC_RDCTR:      /* read counters */
    case FC_RDCLCTR:    /* read and clear counters */
      /* prepare udb for stats transfer */
      memset(xu->var->udb, 0, sizeof(xu->var->udb));

      /* place stats in udb */
      udb[0]  = 68;                       /* udb length */
      udb[1]  = stats->secs;              /* seconds since zeroed */
      udb[2]  = stats->frecv & 0xFFFF;    /* frames received <15:00> */
      udb[3]  = stats->frecv >> 16;       /* frames received <31:16> */
      udb[4]  = stats->mfrecv & 0xFFFF;   /* multicast frames received <15:00> */
      udb[5]  = stats->mfrecv >> 16;      /* multicast frames received <31:16> */
      udb[6]  = stats->rxerf;             /* receive error status bits */
      udb[7]  = stats->frecve;            /* frames received with error */
      udb[8]  = stats->rbytes & 0xFFFF;   /* data bytes received <15:00> */
      udb[9]  = stats->rbytes >> 16;      /* data bytes received <31:16> */
      udb[10] = stats->mrbytes & 0xFFFF;  /* multicast data bytes received <15:00> */
      udb[11] = stats->mrbytes >> 16;     /* multicast data bytes received <31:16> */
      udb[12] = stats->rlossi;            /* received frames lost - internal buffer */
      udb[13] = stats->rlossl;            /* received frames lost - local buffer */
      udb[14] = stats->ftrans & 0xFFFF;   /* frames transmitted <15:00> */
      udb[15] = stats->ftrans >> 16;      /* frames transmitted <31:16> */
      udb[16] = stats->mftrans & 0xFFFF;  /* multicast frames transmitted <15:00> */
      udb[17] = stats->mftrans >> 16;     /* multicast frames transmitted <31:16> */
      udb[18] = stats->ftrans3 & 0xFFFF;  /* frames transmitted 3+ tries <15:00> */
      udb[19] = stats->ftrans3 >> 16;     /* frames transmitted 3+ tries <31:16> */
      udb[20] = stats->ftrans2 & 0xFFFF;  /* frames transmitted 2 tries <15:00> */
      udb[21] = stats->ftrans2 >> 16;     /* frames transmitted 2 tries <31:16> */
      udb[22] = stats->ftransd & 0xFFFF;  /* frames transmitted deferred <15:00> */
      udb[23] = stats->ftransd >> 16;     /* frames transmitted deferred <31:16> */
      udb[24] = stats->tbytes & 0xFFFF;   /* data bytes transmitted <15:00> */
      udb[25] = stats->tbytes >> 16;      /* data bytes transmitted <31:16> */
      udb[26] = stats->mtbytes & 0xFFFF;  /* multicast data bytes transmitted <15:00> */
      udb[27] = stats->mtbytes >> 16;     /* multicast data bytes transmitted <31:16> */
      udb[28] = stats->txerf;             /* transmit frame error status bits */
      udb[29] = stats->ftransa;           /* transmit frames aborted */
      udb[30] = stats->txccf;             /* transmit collision check failure */
      udb[31] = 0;                        /* MBZ */
      udb[32] = stats->porterr;           /* port driver error */
      udb[33] = stats->bablcnt;           /* babble counter */

      /* transfer udb to host */
      udbb = xu->var->pcb[1] + ((xu->var->pcb[2] & 3) << 16);
      wstatus = Map_WriteW(udbb, 68, xu->var->udb);
      if (wstatus) {
        xu->var->pcsr0 |= PCSR0_PCEI;
      }

      /* if clear function, clear network stats */
      if (fnc == FC_RDCLCTR)
        memset(stats, 0, sizeof(struct xu_stats));
      break;

    case FC_RMODE:			/* read mode register */
      value = xu->var->mode;
      wstatus = Map_WriteW(xu->var->pcbb+2, 2, &value);
      if (wstatus)
        return PCSR0_PCEI + 1;
      break;

    case FC_WMODE:			/* write mode register */
      value = xu->var->mode;
      xu->var->mode = xu->var->pcb[1];
sim_debug(DBG_TRC, xu->dev, "FC_WMODE: mode=%04x\n", xu->var->mode);

      /* set promiscuous and multicast flags */
      xu->var->setup.promiscuous = (xu->var->mode & MODE_PROM) ? 1 : 0;
      xu->var->setup.multicast   = (xu->var->mode & MODE_ENAL) ? 1 : 0;

      /* if promiscuous or multicast flags changed, change filter */
      if ((value ^ xu->var->mode) & (MODE_PROM | MODE_ENAL))
        status = eth_filter (xu->var->etherface, xu->var->setup.mac_count,
                            &xu->var->mac, xu->var->setup.multicast,
                             xu->var->setup.promiscuous);
      break;

    case FC_RSTAT:			/* read extended status */
    case FC_RCSTAT:			/* read and clear extended status */
      value = xu->var->stat;
      wstatus = Map_WriteW(xu->var->pcbb+2, 2, &value);
      value = 10;
      wstatus2 = Map_WriteW(xu->var->pcbb+4, 2, &value);
      value = 32;
      wstatus3 = Map_WriteW(xu->var->pcbb+6, 2, &value);
      if (wstatus + wstatus2 + wstatus3)
        return PCSR0_PCEI + 1;

      if (fnc == FC_RCSTAT)
        xu->var->stat &= 0377;	/* clear high byte */
      break;

    case FC_RSID: /* read system id parameters */
      /* prepare udb for transfer */
      memset(xu->var->udb, 0, sizeof(xu->var->udb));

      udb[11] = 0x260;                    /* type */
      udb[12] = 28/* + parameter size */; /* ccount */
      udb[13] = 7;                        /* code */
      udb[14] = 0;                        /* recnum */
                                          /* mop information */
      udb[15] = 1;                        /* mvtype */
      udb[16] = 0x0303;                   /* mvver + mvlen */
      udb[17] = 0;                        /* mvueco + mveco */
                                          /* function information */
      udb[18] = 2;                        /* ftype */
      udb[19] = 0x0502;                   /* fval1 + flen */
      udb[20] = 0x0700;                   /* hatype<07:00> + fval2 */
      udb[21] = 0x0600;                   /* halen + hatype<15:08> */
                                          /* built-in MAC address */
      udb[21] = mac_w[0];                 /* HA<15:00> */
      udb[22] = mac_w[1];                 /* HA<31:16> */
      udb[23] = mac_w[2];                 /* HA<47:32> */
      udb[24] = 0x64;                     /* dtype */
      udb[25] = (11 << 8) + 1;            /* dvalue + dlen */
      
      /* transfer udb to host */
      udbb = xu->var->pcb[1] + ((xu->var->pcb[2] & 3) << 16);
      wstatus = Map_WriteW(udbb, 52, xu->var->udb);
      if (wstatus)
        xu->var->pcsr0 |= PCSR0_PCEI;
      break;

    case FC_WSID: /* write system id parameters */
      /* get udb base */
      udbb = xu->var->pcb[1] + ((xu->var->pcb[2] & 3) << 16);
      /* get udb length */
      pltlen = xu->var->pcb[3];

      /* transfer udb from host */
      rstatus = Map_ReadW(udbb, pltlen * 2, xu->var->udb);
      if (rstatus)
        return PCSR0_PCEI + 1;

      /* decode and store system ID fields , if we ever need to.
         for right now, just return "success" */

      break;

    case FC_RLSA: /* read load server address */
      if (memcmp(xu->var->load_server, zeros, sizeof(ETH_MAC))) {
        /* not set, use default multicast load address */
        wstatus = Map_WriteB(xu->var->pcbb + 2, 6, (uint8*) mcast_load_server);
      } else {
        /* is set, use load_server */
        wstatus = Map_WriteB(xu->var->pcbb + 2, 6, xu->var->load_server);
      }
      if (wstatus)
        return PCSR0_PCEI + 1;
      break;


    case FC_WLSA: /* write load server address */
      rstatus = Map_ReadB(xu->var->pcbb + 2, 6, xu->var->load_server);
      if (rstatus)
        return PCSR0_PCEI + 1;
      break;

    default:			/* Unknown (unimplemented) command. */
      printf("%s: unknown ancilliary command 0%o requested !\n", xu->dev->name, fnc);
      return PCSR0_PCEI;
      break;

  } /* switch */

  return PCSR0_DNI;
}

/* Transfer received packets into receive ring. */
void xu_process_receive(CTLR* xu)
{
  uint32 segb, ba;
  int slen, wlen, off;
  t_stat rstatus, wstatus;
  ETH_ITEM* item = 0;
  int state = xu->var->pcsr1 & PCSR1_STATE;
  int no_buffers = xu->var->pcsr0 & PCSR0_RCBI;

  sim_debug(DBG_TRC, xu->dev, "xu_process_receive(), buffers: %d\n", xu->var->rrlen);

/* xu_dump_rxring(xu); /* debug receive ring */

  /* process only when in the running state, and host buffers are available */
  if ((state != STATE_RUNNING) || no_buffers)
    return;

  /* check read queue for buffer loss */
  if (xu->var->ReadQ.loss) {
    upd_stat16(&xu->var->stats.rlossl, (uint16) xu->var->ReadQ.loss);
    xu->var->ReadQ.loss = 0;
  }

  /* while there are still packets left to process in the queue */
  while (xu->var->ReadQ.count > 0) {

    /* get next receive buffer */
    ba = xu->var->rdrb + (xu->var->relen * 2) * xu->var->rxnext;
    rstatus = Map_ReadW (ba, 8, xu->var->rxhdr);
    if (rstatus) {
      /* tell host bus read failed */
      xu->var->stat |= STAT_ERRS | STAT_MERR | STAT_TMOT | STAT_RRNG;
      xu->var->pcsr0 |= PCSR0_SERI;
      break;
    }

    /* if buffer not owned by controller, exit [at end of ring] */
    if (!(xu->var->rxhdr[2] & RXR_OWN)) {
      /* tell the host there are no more buffers */
      /* xu->var->pcsr0 |= PCSR0_RCBI; */ /* I don't think this is correct 08-dec-2005 dth */
      break;
    }

    /* set buffer length and address */
    slen = xu->var->rxhdr[0];
    segb = xu->var->rxhdr[1] + ((xu->var->rxhdr[2] & 3) << 16);

    /* get first packet from receive queue */
    if (!item) {
      item = &xu->var->ReadQ.item[xu->var->ReadQ.head];
      /*
       * 2.11BSD does not seem to like small packets.
       * For example.. an incoming ARP packet is:
       * ETH dstaddr [6]
       * ETH srcaddr [6]
       * ETH type    [2]
       * ARP arphdr  [8]
       * ARP dstha   [6]
       * ARP dstpa   [4]
       * ARP srcha   [6]
       * ARP srcpa   [4]
       *
       * for a total of 42 bytes.  According to the 2.11BSD
       * driver for DEUNA (if_de.c), this is not a legal size,
       * and the packet is dropped.  Therefore, we pad the
       * thing to minimum size here. Stupid runts...
       */
      if (item->packet.len < ETH_MIN_PACKET) {
        int len = item->packet.len;
        memset (&item->packet.msg[len], 0, ETH_MIN_PACKET - len);
	      item->packet.len = ETH_MIN_PACKET;
      }
    }

    /* is this the start of frame? */
    if (item->packet.used == 0) {
      xu->var->rxhdr[2] |= RXR_STF;
      off = 0;
    }

    /* figure out chained packet size */
    wlen = item->packet.crc_len - item->packet.used;
    if (wlen > slen)
      wlen = slen;

    /* transfer chained packet to host buffer */
    wstatus = Map_WriteB (segb, wlen, &item->packet.msg[off]);
    if (wstatus) {
      /* error during write */
      xu->var->stat |= STAT_ERRS | STAT_MERR | STAT_TMOT | STAT_RRNG;
      xu->var->pcsr0 |= PCSR0_SERI;
      break;
    }

    /* update chained counts */
    item->packet.used += wlen;
    off += wlen;

    /* Is this the end-of-frame? */
    if (item->packet.used == item->packet.crc_len) {
      /* mark end-of-frame */
      xu->var->rxhdr[2] |= RXR_ENF;

      /*
       * Fill in the Received Message Length field.
       * The documenation notes that the DEUNA actually performs
       * a full CRC check on the data buffer, and adds this CRC
       * value to the data, in the last 4 bytes.  The question
       * is: does MLEN include these 4 bytes, or not???  --FvK
       *
       * A quick look at the RSX Process Software driver shows
       * that the CRC byte count(4) is added to MLEN, but does
       * not show if the DEUNA/DELUA actually transfers the
       * CRC bytes to the host buffers, since the driver never
       * tries to use them. However, since the host max buffer
       * size is only 1514, not 1518, I doubt the CRC is actually
       * transferred in normal mode. Maybe CRC is transferred
       * and used in Loopback mode.. -- DTH
       *
       * The VMS XEDRIVER indicates that CRC is transferred as
       * part of the packet, and is included in the MLEN count. -- DTH
       */
      xu->var->rxhdr[3] &= ~RXR_MLEN;
      xu->var->rxhdr[3] |= (item->packet.crc_len);
      if (xu->var->mode & MODE_DRDC) /* data chaining disabled */
        xu->var->rxhdr[3] |= RXR_NCHN;

      /* update stats */
      upd_stat32(&xu->var->stats.frecv, 1);
      upd_stat32(&xu->var->stats.rbytes, item->packet.len - 14);
      if (item->packet.msg[0] & 1) {        /* multicast? */
        upd_stat32(&xu->var->stats.mfrecv, 1);
        upd_stat32(&xu->var->stats.mrbytes, item->packet.len - 14);
      }

      /* remove processed packet from the receive queue */
      ethq_remove (&xu->var->ReadQ);
      item = 0;

      /* tell host we received a packet */
      xu->var->pcsr0 |= PCSR0_RXI;
    } /* if end-of-frame */

    /* give buffer back to host */
    xu->var->rxhdr[2] &= ~RXR_OWN;              /* clear ownership flag */

    /* update the ring entry in host memory. */
    wstatus = Map_WriteW (ba, 8, xu->var->rxhdr);
    if (wstatus) {
      /* tell host bus write failed */
      xu->var->stat |= STAT_ERRS | STAT_MERR | STAT_TMOT | STAT_RRNG;
      xu->var->pcsr0 |= PCSR0_SERI;
      /* if this was end-of-frame, log frame loss */
      if (xu->var->rxhdr[2] & RXR_ENF)
        upd_stat16(&xu->var->stats.rlossi, 1);
    }

    /* set to next receive ring buffer */
    xu->var->rxnext += 1;
    if (xu->var->rxnext == xu->var->rrlen)
      xu->var->rxnext = 0;

  } /* while */

  /* if we failed to finish receiving the frame, flush the packet */
  if (item) {
    ethq_remove(&xu->var->ReadQ);
    upd_stat16(&xu->var->stats.rlossl, 1);
  }

  /* set or clear interrupt, depending on what happened */
  xu_setclrint(xu, 0);
// xu_dump_rxring(xu); /* debug receive ring */

}

void xu_process_transmit(CTLR* xu)
{
  uint32 segb, ba;
  int slen, wlen, i, off, giant, runt;
  t_stat rstatus, wstatus;

  sim_debug(DBG_TRC, xu->dev, "xu_process_transmit()\n");
/* xu_dump_txring(xu); /* debug receive ring */

  for (;;) {

    /* get next transmit buffer */
    ba = xu->var->tdrb + (xu->var->telen * 2) * xu->var->txnext;
    rstatus = Map_ReadW (ba, 8, xu->var->txhdr);
    if (rstatus) {
      /* tell host bus read failed */
      xu->var->stat |= STAT_ERRS | STAT_MERR | STAT_TMOT | STAT_TRNG;
      xu->var->pcsr0 |= PCSR0_SERI;
      break;
    }

    /* if buffer not owned by controller, exit [at end of ring] */
    if (!(xu->var->txhdr[2] & TXR_OWN))
      break;

    /* set buffer length and address */
    slen = xu->var->txhdr[0];
    segb = xu->var->txhdr[1] + ((xu->var->txhdr[2] & 3) << 16);
    wlen = slen;

    /* prepare to accumulate transmit information if start of frame */
    if (xu->var->txhdr[2] & TXR_STF) {
      memset(&xu->var->write_buffer, 0, sizeof(ETH_PACK));
      off = giant = runt = 0;
    }

    /* get packet data from host */
    if (xu->var->write_buffer.len + slen > ETH_MAX_PACKET) {
      wlen = ETH_MAX_PACKET - xu->var->write_buffer.len;
      giant = 1;
    }
    if (wlen > 0) {
      rstatus = Map_ReadB(segb, wlen, &xu->var->write_buffer.msg[off]);
      if (rstatus) {
        /* tell host bus read failed */
        xu->var->stat |= STAT_ERRS | STAT_MERR | STAT_TMOT | STAT_TRNG;
        xu->var->pcsr0 |= PCSR0_SERI;
        break;
      }
    }
    off += wlen;
    xu->var->write_buffer.len += wlen;

    /* transmit packet when end-of-frame is reached */
    if (xu->var->txhdr[2] & TXR_ENF) {

      /* make sure packet is minimum length */
      if (xu->var->write_buffer.len < ETH_MIN_PACKET) {
        xu->var->write_buffer.len = ETH_MIN_PACKET;  /* pad packet to minimum length */
        if ((xu->var->mode & MODE_TPAD) == 0)  /* if pad mode is NOT on, set runt error flag */
         runt = 1;
      }

      /* are we in internal loopback mode ? */
      if ((xu->var->mode & MODE_LOOP) && (xu->var->mode & MODE_INTL)) {
        /* just put packet in  receive buffer */
        ethq_insert (&xu->var->ReadQ, 1, &xu->var->write_buffer, 0);
      } else {
        /* transmit packet synchronously - write callback sets status */
        wstatus = eth_write(xu->var->etherface, &xu->var->write_buffer, xu->var->wcallback);
        if (wstatus)
          xu->var->pcsr0 |= PCSR0_PCEI;
      }

      /* update transmit status in transmit buffer */
      if (xu->var->write_buffer.status != 0) {
        /* failure */
        const uint16 tdr = 100 + wlen * 8; /* arbitrary value */
        xu->var->txhdr[3] |= TXR_RTRY;
        xu->var->txhdr[3] |= tdr & TXR_TDR;
        xu->var->txhdr[2] |= TXR_ERRS;
      }

      /* was packet too big or too small? */
      if (giant || runt) {
        xu->var->txhdr[3] |= TXR_BUFL;
        xu->var->txhdr[2] |= TXR_ERRS;
      }

      /* was packet self-addressed? */
      for (i=0; i<XU_FILTER_MAX; i++)
        if (memcmp(xu->var->write_buffer.msg, xu->var->setup.macs[i], sizeof(ETH_MAC)) == 0)
          xu->var->txhdr[2] |= TXR_MTCH;

      /* tell host we transmitted a packet */
      xu->var->pcsr0 |= PCSR0_TXI;

      /* update stats */
      upd_stat32(&xu->var->stats.ftrans, 1);
      upd_stat32(&xu->var->stats.tbytes, xu->var->write_buffer.len - 14);
      if (xu->var->write_buffer.msg[0] & 1) {        /* multicast? */
        upd_stat32(&xu->var->stats.mftrans, 1);
        upd_stat32(&xu->var->stats.mtbytes, xu->var->write_buffer.len - 14);
      }
      if (giant)
        bit_stat16(&xu->var->stats.txerf, 0x10);

    } /* if end-of-frame */

    
    /* give buffer ownership back to host */
    xu->var->txhdr[2] &= ~TXR_OWN;

    /* update transmit buffer */
    wstatus = Map_WriteW (ba, 8, xu->var->txhdr);
    if (wstatus) {
      /* tell host bus write failed */
      xu->var->pcsr0 |= PCSR0_PCEI;
      /* update stats */
      upd_stat16(&xu->var->stats.ftransa, 1);
      break;
    }

    /* set to next transmit ring buffer */
    xu->var->txnext += 1;
    if (xu->var->txnext == xu->var->trlen)
      xu->var->txnext = 0;

  } /* while */
}

void xu_port_command (CTLR* xu)
{
  char* msg;
  int command = xu->var->pcsr0 & PCSR0_PCMD;
  int state = xu->var->pcsr1 & PCSR1_STATE;
  int bits;
  static char* commands[] = {
      "NO-OP",
      "GET PCBB",
      "GET CMD",
      "SELFTEST",
      "START",
      "BOOT",
      "Reserved NO-OP",
      "Reserved NO-OP",
      "PDMD",
      "Reserved NO-OP",
      "Reserved NO-OP",
      "Reserved NO-OP",
      "Reserved NO-OP",
      "Reserved NO-OP",
      "HALT",
      "STOP"
    };

  sim_debug(DBG_TRC, xu->dev, "xu_port_command(), Command = %s [0%o]\n", commands[command], command);
  switch (command) {  /* cases in order of most used to least used */
    case CMD_PDMD:			/* POLLING DEMAND */
      /* process transmit buffers, receive buffers are done in the service timer */
      xu_process_transmit(xu);
      xu->var->pcsr0 |= PCSR0_DNI;
      break;

    case CMD_GETCMD:		/* GET COMMAND */
      bits = xu_command(xu);
      xu->var->pcsr0 |= PCSR0_DNI;
      break;

    case CMD_GETPCBB:		/* GET PCB-BASE */
      xu->var->pcbb = (xu->var->pcsr3 << 16) | xu->var->pcsr2;
      xu->var->pcsr0 |= PCSR0_DNI;
      break;

    case CMD_SELFTEST:		/* SELFTEST */
      xu_sw_reset(xu);
      xu->var->pcsr0 |= PCSR0_DNI;
      break;

    case CMD_START:			/* START */
      if (state == STATE_READY) {
        xu->var->pcsr1 &= ~PCSR1_STATE;
        xu->var->pcsr1 |= STATE_RUNNING; 
        xu->var->pcsr0 |= PCSR0_DNI;

        /* reset ring pointers */
        xu->var->rxnext = 0;
        xu->var->txnext = 0;

      } else
        xu->var->pcsr0 |= PCSR0_PCEI;
      break;

    case CMD_HALT:      /* HALT */
      if ((state == STATE_READY) || (state == STATE_RUNNING)) {
        sim_cancel (&xu->unit[0]);        /* cancel service timer */
        xu->var->pcsr1 &= ~PCSR1_STATE;
        xu->var->pcsr1 |= STATE_HALT;
        xu->var->pcsr0 |= PCSR0_DNI;
      } else
        xu->var->pcsr0 |= PCSR0_PCEI;
      break;

    case CMD_STOP:			/* STOP */
      if (state == STATE_RUNNING) {
        xu->var->pcsr1 &= ~PCSR1_STATE;
        xu->var->pcsr1 |= STATE_READY;
        xu->var->pcsr0 |= PCSR0_DNI;
      } else
        xu->var->pcsr0 |= PCSR0_PCEI;
      break;

    case CMD_BOOT:      /* BOOT */
      /* not implemented */
      msg = "%s: BOOT command not implemented!\n";
      printf (msg, xu->dev->name);
      if (sim_log) fprintf(sim_log, msg, xu->dev->name);

      xu->var->pcsr0 |= PCSR0_PCEI;
      break;

    case CMD_NOOP:      /* NO-OP */
      /* NOOP does NOT set DNI */
      break;

    case CMD_RSV06:     /* RESERVED */
    case CMD_RSV07:     /* RESERVED */
    case CMD_RSV11:     /* RESERVED */
    case CMD_RSV12:     /* RESERVED */
    case CMD_RSV13:     /* RESERVED */
    case CMD_RSV14:     /* RESERVED */
    case CMD_RSV15:     /* RESERVED */
      /* all reserved commands act as a no-op but set DNI */
      xu->var->pcsr0 |= PCSR0_DNI;
      break;
  } /* switch */

  /* set interrupt if needed */
  xu_setclrint(xu, 0);
}

t_stat xu_rd(int32 *data, int32 PA, int32 access)
{
  CTLR* xu = xu_pa2ctlr(PA);
  int reg = (PA >> 1) & 03;

  switch (reg) {
    case 00:
      *data = xu->var->pcsr0;
      break;
    case 01:
      *data = xu->var->pcsr1;
      break;
    case 02:
      *data = xu->var->pcsr2;
      break;
    case 03:
      *data = xu->var->pcsr3;
      break;
  }
  sim_debug(DBG_TRC, xu->dev, "xu_rd(), PCSR%d, data=%04x\n", reg, *data);
  if (PA & 1)
    sim_debug(DBG_WRN, xu->dev, "xu_rd(), Unexpected Odd address access of PCSR%d\n", reg);
  return SCPE_OK;
}

t_stat xu_wr(int32 data, int32 PA, int32 access)
{
  CTLR* xu = xu_pa2ctlr(PA);
  int reg = (PA >> 1) & 03;
  char desc[10];

  switch (access) {
    case WRITE :
      strcpy(desc, "Word");
      break;
    case WRITEB:
      if (PA & 1) {
        strcpy(desc, "ByteHi");
      } else {
        strcpy(desc, "ByteLo");
      }
      break;
    default :
      strcpy(desc, "Unknown");
      break;
  }
  sim_debug(DBG_TRC, xu->dev, "xu_wr(), PCSR%d, data=%08x, PA=%08x, access=%d[%s]\n", reg, data, PA, access, desc);
  switch (reg) {
    case 00:
	/* Clear write-one-to-clear interrupt bits */
      if (access == WRITEB) {
        data &= 0377;
        if (PA & 1) {
          /* Handle WriteOneToClear trick. */
          xu->var->pcsr0 &= ~((data << 8) & 0177400);

          /* set/reset interrupt */
          xu_setclrint(xu, 0);

          /* Bail out early to avoid PCMD crap. */
          return SCPE_OK;
        }
      } else {                       /* access == WRITE [Word] */
        uint16 mask = data & 0xFF00; /* only interested in high byte */
        xu->var->pcsr0 &= ~mask;     /* clear write-one-to-clear bits */
      }
      /* RESET function requested? */
      if (data & PCSR0_RSET) {
        xu_sw_reset(xu);
        xu_setclrint(xu, 0);
        return SCPE_OK;              /* nothing else to do on reset */
      }
      /* Handle the INTE interlock; if INTE changes state, no commands can occur */
      if ((xu->var->pcsr0 ^ data) & PCSR0_INTE) {
        xu->var->pcsr0 ^= PCSR0_INTE;
        xu->var->pcsr0 |= PCSR0_DNI;
        if (xu->var->pcsr0 & PCSR0_INTE) {
          sim_debug(DBG_TRC, xu->dev, "xu_wr(), Interrupts Enabled\n");
        } else {
          sim_debug(DBG_TRC, xu->dev, "xu_wr(), Interrupts Disabled\n");
        }
      } else {
        /* Normal write, no interlock. */
        xu->var->pcsr0 &= ~PCSR0_PCMD;
        xu->var->pcsr0 |= (data & PCSR0_PCMD);
        xu_port_command(xu);
      }
      /* We might have changed the interrupt sys. */
      xu_setclrint(xu, 0);
      break;

    case 01:
      sim_debug(DBG_WRN, xu->dev, "xu_wr(), invalid write access on PCSR1!\n");
      break;

    case 02:
      xu->var->pcsr2 = data & 0177776;	/* store word, but not MBZ LSB */
      break;

    case 03:
      xu->var->pcsr3 = data & 0000003;	/* store significant bits */
      break;
  }
  return SCPE_OK;
}


/* attach device: */
t_stat xu_attach(UNIT* uptr, char* cptr)
{
  t_stat status;
  char* tptr;
  CTLR* xu = xu_unit2ctlr(uptr);

  sim_debug(DBG_TRC, xu->dev, "xu_attach(cptr=%s)\n", cptr);
  tptr = (char *) malloc(strlen(cptr) + 1);
  if (tptr == NULL) return SCPE_MEM;
  strcpy(tptr, cptr);

  xu->var->etherface = (ETH_DEV *) malloc(sizeof(ETH_DEV));
  if (!xu->var->etherface) return SCPE_MEM;

  status = eth_open(xu->var->etherface, cptr, xu->dev, DBG_ETH);
  if (status != SCPE_OK) {
    free(tptr);
    free(xu->var->etherface);
    xu->var->etherface = 0;
    return status;
  }
  uptr->filename = tptr;
  uptr->flags |= UNIT_ATT;
  eth_setcrc(xu->var->etherface, 1); /* enable CRC */

  /* reset the device with the new attach info */
  xu_reset(xu->dev);

  return SCPE_OK;
}

/* detach device: */

t_stat xu_detach(UNIT* uptr)
{
  t_stat status;
  CTLR* xu = xu_unit2ctlr(uptr);
  sim_debug(DBG_TRC, xu->dev, "xu_detach()\n");

  if (uptr->flags & UNIT_ATT) {
    status = eth_close (xu->var->etherface);
    free(xu->var->etherface);
    xu->var->etherface = 0;
    free(uptr->filename);
    uptr->filename = NULL;
    uptr->flags &= ~UNIT_ATT;
  }
  return SCPE_OK;
}

void xu_setint(CTLR* xu)
{
  if (xu->var->pcsr0 & PCSR0_INTE) {
    xu->var->irq = 1;
    SET_INT(XU);
  }
  return;
}

void xu_clrint(CTLR* xu)
{
  int i;
  xu->var->irq = 0;                               /* set controller irq off */
  /* clear master interrupt? */
  for (i=0; i<XU_MAX_CONTROLLERS; i++)            /* check all controllers.. */
    if (xu_ctrl[i].var->irq) {                    /* if any irqs enabled */
      SET_INT(XU);                                /* set master interrupt on */
      return;
    }
  CLR_INT(XU);                                    /* clear master interrupt */
  return;
}

int32 xu_int (void)
{
  int i;
  for (i=0; i<XU_MAX_CONTROLLERS; i++) {
    CTLR* xu = &xu_ctrl[i];
    if (xu->var->irq) {                           /* if interrupt pending */
      xu_clrint(xu);                              /* clear interrupt */
      return xu->dib->vec;                        /* return vector */
    }
  }
  return 0;                                       /* no interrupt request active */
}

/*==============================================================================
/                               debugging routines
/=============================================================================*/

void xu_dump_rxring (CTLR* xu)
{
  int i;
  int rrlen = xu->var->rrlen;
  printf ("receive ring[%s]: base address: %08x  headers: %d, header size: %d, current: %d\n", xu->dev->name, xu->var->rdrb, xu->var->rrlen, xu->var->relen, xu->var->rxnext);
  for (i=0; i<rrlen; i++) {
    uint16 rxhdr[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    uint32 ba = xu->var->rdrb + (xu->var->relen * 2) * i;
    t_stat rstatus = Map_ReadW (ba, 8, rxhdr);	/* get rxring entry[i] */
    int own = (rxhdr[2] & RXR_OWN) >> 15;
    int len = rxhdr[0];
    uint32 addr = rxhdr[1] + ((rxhdr[2] & 3) << 16);
    printf ("  header[%d]: own:%d, len:%d, address:%08x data:{%04x,%04x,%04x,%04x}\n", i, own, len, addr, rxhdr[0], rxhdr[1], rxhdr[2], rxhdr[3]);
  }
}

void xu_dump_txring (CTLR* xu)
{
  int i;
  int trlen = xu->var->trlen;
  printf ("transmit ring[%s]: base address: %08x  headers: %d, header size: %d, current: %d\n", xu->dev->name, xu->var->tdrb, xu->var->trlen, xu->var->telen, xu->var->txnext);
  for (i=0; i<trlen; i++) {
    uint16 txhdr[4];
    uint32 ba = xu->var->tdrb + (xu->var->telen * 2) * i;
    t_stat tstatus = Map_ReadW (ba, 8, txhdr);	/* get rxring entry[i] */
    int own = (txhdr[2] & RXR_OWN) >> 15;
    int len = txhdr[0];
    uint32 addr = txhdr[1] + ((txhdr[2] & 3) << 16);
    printf ("  header[%d]: own:%d, len:%d, address:%08x data:{%04x,%04x,%04x,%04x}\n", i, own, len, addr, txhdr[0], txhdr[1], txhdr[2], txhdr[3]);
  }
}
