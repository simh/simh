/* pdp11_xu.c: DEUNA/DELUA ethernet controller simulator
  ------------------------------------------------------------------------------

   Copyright (c) 2003-2011, David T. Hittner

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
    http://www.bitsavers.org/pdf/dec/unibus

  Testing performed:
   1) Receives/Transmits single packet under custom RSX driver
   2) Passes RSTS 10.1 controller probe diagnostics during boot
   3) VMS 7.2 on VAX780 summary:
       (May/2007: WinXP x64 host; MS VC++ 2005; SIMH v3.7-0 base; WinPcap 4.0)
        LAT    - SET HOST/LAT in/out
        DECNET - SET HOST in/out, COPY in/out
        TCP/IP - PING in/out; SET HOST/TELNET in/out, COPY/FTP in/out
        Clustering - Successfully clustered with AlphaVMS 8.2
   4) VMS 4.7 on VAX780 summary:
       (Jan/2011: Win7 x64 host; MS VC++ 2008; SIMH v3.8-2 rc1 base; WinPcap 4.1)
        LAT    - SET HOST/LAT in (no outbound exists)
        DECNET - SET HOST in/out, DIR in/out, COPY in/out
        TCP/IP - no kit available to test
        Clustering - not tested
   5) Runs VAX EVDWA diagnostic tests 1-10; tests 11-19 (M68000/ROM/RAM) fail

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

  25-Jan-13  RJ   SELFTEST needs to report the READY state otherwise VMS 3.7 gets fatal controller error
  12-Jan-11  DTH  Added SHOW XU FILTERS modifier
  11-Jan-11  DTH  Corrected SELFTEST command, enabling use by VMS 3.7, VMS 4.7, and Ultrix 1.1
  09-Dec-10  MP   Added address conflict check during attach.
  06-Dec-10  MP   Added loopback processing support
  30-Nov-10  MP   Fixed the fact that no broadcast packets were received by the DEUNA
  15-Aug-08  MP   Fixed transmitted packets to have the correct source MAC address.
                  Fixed incorrect address filter setting calling eth_filter().
  23-Jan-08  MP   Added debugging support to display packet headers and packet data
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

extern int32 tmxr_poll;

t_stat xu_rd(int32* data, int32 PA, int32 access);
t_stat xu_wr(int32  data, int32 PA, int32 access);
t_stat xu_svc(UNIT * uptr);
t_stat xu_tmrsvc(UNIT * uptr);
t_stat xu_reset (DEVICE * dptr);
t_stat xu_attach (UNIT * uptr, CONST char * cptr);
t_stat xu_detach (UNIT * uptr);
t_stat xu_showmac (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat xu_setmac  (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat xu_show_stats (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat xu_set_stats  (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat xu_show_type (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat xu_set_type (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat xu_show_throttle (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat xu_set_throttle (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
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
t_stat xu_show_filters (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat xu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *xu_description (DEVICE *dptr);

#define IOLN_XU        010

DIB xua_dib = { IOBA_AUTO, IOLN_XU, &xu_rd, &xu_wr,
                1, IVCL (XU), VEC_AUTO, {&xu_int}, IOLN_XU };

UNIT xua_unit[] = {
 { UDATA (&xu_svc, UNIT_IDLE|UNIT_ATTABLE, 0) },     /* receive timer */
 { UDATA (&xu_tmrsvc, UNIT_IDLE|UNIT_DIS, 0) }
};

struct xu_device    xua = {
  xua_read_callback,                        /* read callback routine */
  xua_write_callback,                       /* write callback routine */
  {0x08, 0x00, 0x2B, 0xCC, 0xDD, 0xEE},     /* mac */
  XU_T_DELUA,                               /* type */
  ETH_THROT_DEFAULT_TIME,                   /* ms throttle window */
  ETH_THROT_DEFAULT_BURST,                  /* packet packet burst in throttle window */
  ETH_THROT_DISABLED_DELAY                  /* throttle disabled */
  };

MTAB xu_mod[] = {
#if defined (VM_PDP11)
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 010, "ADDRESS", "ADDRESS",
    &set_addr, &show_addr, NULL },
  { MTAB_XTD|MTAB_VDV, 0, NULL, "AUTOCONFIGURE",
    &set_addr_flt, NULL, NULL },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
    &set_vec, &show_vec, NULL },
#else
  { MTAB_XTD|MTAB_VDV, 0, "ADDRESS", NULL,
    NULL, &show_addr, NULL, "Unibus address" },
  { MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
    NULL, &show_vec, NULL, "Interrupt vector" },
#endif
  { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_NC, 0, "MAC", "MAC=xx:xx:xx:xx:xx:xx",
    &xu_setmac, &xu_showmac, NULL, "MAC address" },
  { MTAB_XTD |MTAB_VDV|MTAB_NMO, 0, "ETH", NULL,
    NULL, &eth_show, NULL, "Display attachable devices" },
  { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATS", "STATS",
    &xu_set_stats, &xu_show_stats, NULL, "Display or reset statistics" },
  { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "FILTERS", NULL,
    NULL, &xu_show_filters, NULL, "Display MAC addresses which will be received" },
  { MTAB_XTD|MTAB_VDV, 0, "TYPE", "TYPE={DEUNA|DELUA}",
    &xu_set_type, &xu_show_type, NULL, "Display the controller type" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "THROTTLE", "THROTTLE=DISABLED|TIME=n{;BURST=n{;DELAY=n}}",
    &xu_set_throttle, &xu_show_throttle, NULL, "Display transmit throttle configuration" },
  { 0 },
};

REG xua_reg[] = {
  { GRDATA ( SA0,     xua.mac[0],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( SA1,     xua.mac[1],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( SA2,     xua.mac[2],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( SA3,     xua.mac[3],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( SA4,     xua.mac[4],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( SA5,     xua.mac[5],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( TYPE,    xua.type,     XU_RDX, 32, 0), REG_FIT },
  { FLDATA ( INT,     xua.irq, 0) },
  { GRDATA ( IDTMR,   xua.idtmr,    XU_RDX, 32, 0), REG_HRO},
  { SAVEDATA ( SETUP,   xua.setup) },
  { SAVEDATA ( STATS,   xua.stats) },
  { GRDATA ( CSR0,    xua.pcsr0,    XU_RDX, 16, 0), REG_FIT },
  { GRDATA ( CSR1,    xua.pcsr1,    XU_RDX, 16, 0), REG_FIT },
  { GRDATA ( CSR2,    xua.pcsr2,    XU_RDX, 16, 0), REG_FIT },
  { GRDATA ( CSR3,    xua.pcsr3,    XU_RDX, 16, 0), REG_FIT },
  { GRDATA ( MODE,    xua.mode,     XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( PCBB,    xua.pcbb,     XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( STAT,    xua.stat,     XU_RDX, 16, 0), REG_FIT },
  { GRDATA ( TDRB,    xua.tdrb,     XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( TELEN,   xua.telen,    XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( TRLEN,   xua.trlen,    XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( TXNEXT,  xua.txnext,   XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( RDRB,    xua.rdrb,     XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( RELEN,   xua.relen,    XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( RRLEN,   xua.rrlen,    XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( RXNEXT,  xua.rxnext,   XU_RDX, 32, 0), REG_FIT },
  { BRDATA ( PCB,     xua.pcb,      XU_RDX, 16, 4), REG_HRO},
  { BRDATA ( UDB,     xua.udb,      XU_RDX, 16, UDBSIZE), REG_HRO},
  { BRDATA ( RXHDR,   xua.rxhdr,    XU_RDX, 16, 4), REG_HRO},
  { BRDATA ( TXHDR,   xua.txhdr,    XU_RDX, 16, 4), REG_HRO},
  { GRDATA ( BA,      xua_dib.ba,   XU_RDX, 32, 0), REG_HRO},
  { GRDATA ( VECTOR,  xua_dib.vec,  XU_RDX, 32, 0), REG_HRO},
  { GRDATA ( THR_TIME, xua.throttle_time, XU_RDX, 32, 0), REG_HRO},
  { GRDATA ( THR_BURST, xua.throttle_burst, XU_RDX, 32, 0), REG_HRO},
  { GRDATA ( THR_DELAY, xua.throttle_delay, XU_RDX, 32, 0), REG_HRO},
  { NULL }  };

DEBTAB xu_debug[] = {
  {"TRACE",  DBG_TRC, "trace routine calls"},
  {"WARN",   DBG_WRN, "warnings"},
  {"REG",    DBG_REG, "read/write registers"},
  {"PACKET", DBG_PCK, "packet headers"},
  {"DATA",   DBG_DAT, "packet data"},
  {"ETH",    DBG_ETH, "ethernet device"},
  {0}
};


DEVICE xu_dev = {
    "XU", xua_unit, xua_reg, xu_mod,
    2, XU_RDX, 8, 1, XU_RDX, 8,
    &xu_ex, &xu_dep, &xu_reset,
    NULL, &xu_attach, &xu_detach,
    &xua_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG | DEV_ETHER,
    0, xu_debug, NULL, NULL, &xu_help, NULL, NULL,
    &xu_description
  };

DIB xub_dib = { IOBA_AUTO, IOLN_XU, &xu_rd, &xu_wr,
                1, IVCL (XU), 0, { &xu_int }, IOLN_XU };

UNIT xub_unit[] = {
 { UDATA (&xu_svc, UNIT_IDLE|UNIT_ATTABLE, 0) },     /* receive timer */
 { UDATA (&xu_tmrsvc, UNIT_IDLE|UNIT_DIS, 0) }
};

struct xu_device    xub = {
  xub_read_callback,                        /* read callback routine */
  xub_write_callback,                       /* write callback routine */
  {0x08, 0x00, 0x2B, 0xDD, 0xEE, 0xFF},     /* mac */
  XU_T_DELUA,                               /* type */
  ETH_THROT_DEFAULT_TIME,                   /* ms throttle window */
  ETH_THROT_DEFAULT_BURST,                  /* packet packet burst in throttle window */
  ETH_THROT_DISABLED_DELAY                  /* throttle disabled */
  };

REG xub_reg[] = {
  { GRDATA ( SA0,     xub.mac[0],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( SA1,     xub.mac[1],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( SA2,     xub.mac[2],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( SA3,     xub.mac[3],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( SA4,     xub.mac[4],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( SA5,     xub.mac[5],       16,  8, 0), REG_RO|REG_FIT},
  { GRDATA ( TYPE,    xub.type,     XU_RDX, 32, 0), REG_FIT },
  { FLDATA ( INT,     xub.irq, 0) },
  { GRDATA ( IDTMR,   xub.idtmr,    XU_RDX, 32, 0), REG_HRO},
  { SAVEDATA ( SETUP, xub.setup) },
  { SAVEDATA ( STATS, xub.stats) },
  { GRDATA ( CSR0,    xub.pcsr0,    XU_RDX, 16, 0), REG_FIT },
  { GRDATA ( CSR1,    xub.pcsr1,    XU_RDX, 16, 0), REG_FIT },
  { GRDATA ( CSR2,    xub.pcsr2,    XU_RDX, 16, 0), REG_FIT },
  { GRDATA ( CSR3,    xub.pcsr3,    XU_RDX, 16, 0), REG_FIT },
  { GRDATA ( MODE,    xub.mode,     XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( PCBB,    xub.pcbb,     XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( STAT,    xub.stat,     XU_RDX, 16, 0), REG_FIT },
  { GRDATA ( TDRB,    xub.tdrb,     XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( TELEN,   xub.telen,    XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( TRLEN,   xub.trlen,    XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( TXNEXT,  xub.txnext,   XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( RDRB,    xub.rdrb,     XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( RELEN,   xub.relen,    XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( RRLEN,   xub.rrlen,    XU_RDX, 32, 0), REG_FIT },
  { GRDATA ( RXNEXT,  xub.rxnext,   XU_RDX, 32, 0), REG_FIT },
  { BRDATA ( PCB,     xub.pcb,      XU_RDX, 16, 4), REG_HRO},
  { BRDATA ( UDB,     xub.udb,      XU_RDX, 16, UDBSIZE), REG_HRO},
  { BRDATA ( RXHDR,   xub.rxhdr,    XU_RDX, 16, 4), REG_HRO},
  { BRDATA ( TXHDR,   xub.txhdr,    XU_RDX, 16, 4), REG_HRO},
  { GRDATA ( BA,      xub_dib.ba,   XU_RDX, 32, 0), REG_HRO},
  { GRDATA ( VECTOR,  xub_dib.vec,  XU_RDX, 32, 0), REG_HRO},
  { GRDATA ( THR_TIME, xub.throttle_time, XU_RDX, 32, 0), REG_HRO},
  { GRDATA ( THR_BURST, xub.throttle_burst, XU_RDX, 32, 0), REG_HRO},
  { GRDATA ( THR_DELAY, xub.throttle_delay, XU_RDX, 32, 0), REG_HRO},
  { NULL }  };

DEVICE xub_dev = {
  "XUB", xub_unit, xub_reg, xu_mod,
  2, XU_RDX, 8, 1, XU_RDX, 8,
  &xu_ex, &xu_dep, &xu_reset,
  NULL, &xu_attach, &xu_detach,
  &xub_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG | DEV_ETHER,
  0, xu_debug, NULL, NULL, NULL, NULL, NULL,
  &xu_description
};

#define XU_MAX_CONTROLLERS 2
CTLR xu_ctrl[] = {
   {&xu_dev,  xua_unit, &xua_dib, &xua}       /* XUA controller */
  ,{&xub_dev, xub_unit, &xub_dib, &xub}       /* XUB controller */
};

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

t_stat xu_showmac (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  CTLR* xu = xu_unit2ctlr(uptr);
  char  buffer[20];

  eth_mac_fmt((ETH_MAC*)xu->var->mac, buffer);
  fprintf(st, "MAC=%s", buffer);
  return SCPE_OK;
}

t_stat xu_setmac (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
  t_stat status;
  CTLR* xu = xu_unit2ctlr(uptr);

  if (!cptr) return SCPE_IERR;
  if (uptr->flags & UNIT_ATT) return SCPE_ALATT;
  status = eth_mac_scan_ex(&xu->var->mac, cptr, uptr);
  return status;
}

t_stat xu_set_stats (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
  CTLR* xu = xu_unit2ctlr(uptr);

  /* set stats to zero, regardless of passed parameter */
  memset(&xu->var->stats, 0, sizeof(struct xu_stats));
  return SCPE_OK;
}

t_stat xu_show_stats (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  const char* fmt = "  %-26s%d\n";
  CTLR* xu = xu_unit2ctlr(uptr);
  struct xu_stats* stats = &xu->var->stats;

  fprintf(st, "Ethernet statistics:\n");
  fprintf(st, fmt, "Seconds since cleared:",   stats->secs); 
  fprintf(st, fmt, "Recv frames:",             stats->frecv);
  fprintf(st, fmt, "Recv dbytes:",             stats->rbytes);
  fprintf(st, fmt, "Xmit frames:",             stats->ftrans);
  fprintf(st, fmt, "Xmit dbytes:",             stats->tbytes);
  fprintf(st, fmt, "Recv frames(multicast):",  stats->mfrecv);
  fprintf(st, fmt, "Recv dbytes(multicast):",  stats->mrbytes);
  fprintf(st, fmt, "Xmit frames(multicast):",  stats->mftrans);
  fprintf(st, fmt, "Xmit dbytes(multicast):",  stats->mtbytes);
  fprintf(st, fmt, "Loopback forward Frames:", stats->loopf);
  return SCPE_OK;
}

t_stat xu_show_filters (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  CTLR* xu = xu_unit2ctlr(uptr);
  char  buffer[20];
  int i;

  fprintf(st, "Filters:\n");
  for (i=0; i<XU_FILTER_MAX; i++) {
    eth_mac_fmt((ETH_MAC*)xu->var->setup.macs[i], buffer);
    fprintf(st, "  [%2d]: %s\n", i, buffer);
  }
  if (xu->var->setup.multicast)
    fprintf(st, "All Multicast Receive Mode\n");
  if (xu->var->setup.promiscuous)
    fprintf(st, "Promiscuous Receive Mode\n");
  return SCPE_OK;
}

t_stat xu_show_type (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  CTLR* xu = xu_unit2ctlr(uptr);
  fprintf(st, "type=");
  switch (xu->var->type) {
    case  XU_T_DEUNA:       fprintf(st, "DEUNA");      break;
    case  XU_T_DELUA:       fprintf(st, "DELUA");      break;
  }
  return SCPE_OK;
}

t_stat xu_set_type (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
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

t_stat xu_show_throttle (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  CTLR* xu = xu_unit2ctlr(uptr);

  if (xu->var->throttle_delay == ETH_THROT_DISABLED_DELAY)
    fprintf(st, "throttle=disabled");
  else
    fprintf(st, "throttle=time=%d;burst=%d;delay=%d", xu->var->throttle_time, xu->var->throttle_burst, xu->var->throttle_delay);
  return SCPE_OK;
}

t_stat xu_set_throttle (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
  CTLR* xu = xu_unit2ctlr(uptr);
  char tbuf[CBUFSIZE], gbuf[CBUFSIZE];
  const char *tptr = cptr;
  uint32 newval;
  uint32 set_time = xu->var->throttle_time;
  uint32 set_burst = xu->var->throttle_burst;
  uint32 set_delay = xu->var->throttle_delay;
  t_stat r = SCPE_OK;

  if (!cptr) {
    xu->var->throttle_delay = ETH_THROT_DEFAULT_DELAY;
    eth_set_throttle (xu->var->etherface, xu->var->throttle_time, xu->var->throttle_burst, xu->var->throttle_delay);
    return SCPE_OK;
    }

  /* this assumes that the parameter has already been upcased */
  if ((!strcmp (cptr, "ON")) ||
      (!strcmp (cptr, "ENABLED")))
    xu->var->throttle_delay = ETH_THROT_DEFAULT_DELAY;
  else
    if ((!strcmp (cptr, "OFF")) ||
        (!strcmp (cptr, "DISABLED")))
      xu->var->throttle_delay = ETH_THROT_DISABLED_DELAY;
    else {
      if (set_delay == ETH_THROT_DISABLED_DELAY)
        set_delay = ETH_THROT_DEFAULT_DELAY;
      while (*tptr) {
        tptr = get_glyph_nc (tptr, tbuf, ';');
        cptr = tbuf;
        cptr = get_glyph (cptr, gbuf, '=');
        if ((NULL == cptr) || ('\0' == *cptr))
          return SCPE_ARG;
        newval = (uint32)get_uint (cptr, 10, 100, &r);
        if (r != SCPE_OK)
          return SCPE_ARG;
        if (!MATCH_CMD(gbuf, "TIME")) {
          set_time = newval;
          }
        else
          if (!MATCH_CMD(gbuf, "BURST")) {
            if (newval > 30)
               return SCPE_ARG;
            set_burst = newval;
            }
          else
            if (!MATCH_CMD(gbuf, "DELAY")) {
              set_delay = newval;
              }
            else
              return SCPE_ARG;
        }
      xu->var->throttle_time = set_time;
      xu->var->throttle_burst = set_burst;
      xu->var->throttle_delay = set_delay;
      }
  eth_set_throttle (xu->var->etherface, xu->var->throttle_time, xu->var->throttle_burst, xu->var->throttle_delay);
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

t_stat xu_process_loopback(CTLR* xu, ETH_PACK* pack)
{
  ETH_PACK  response;
  ETH_MAC   physical_address;
  t_stat    status;
  int offset   = 16 + (pack->msg[14] | (pack->msg[15] << 8));
  int function;

  if (offset > ETH_MAX_PACKET - 8)
      return SCPE_NOFNC;
  function = pack->msg[offset] | (pack->msg[offset+1] << 8);

  sim_debug(DBG_TRC, xu->dev, "xu_process_loopback(function=%d)\n", function);

  if (function != 2 /*forward*/)
    return SCPE_NOFNC;

  /* create forward response packet */
  memcpy (&response, pack, sizeof(ETH_PACK));
  memcpy (physical_address, xu->var->setup.macs[0], sizeof(ETH_MAC));

  /* The only packets we should be responding to are ones which 
     we received due to them being directed to our physical MAC address, 
     OR the Broadcast address OR to a Multicast address we're listening to 
     (we may receive others if we're in promiscuous mode, but shouldn't 
     respond to them) */
  if ((0 == (pack->msg[0]&1)) &&           /* Multicast or Broadcast */
      (0 != memcmp(physical_address, pack->msg, sizeof(ETH_MAC))))
      return SCPE_NOFNC;


  memcpy (&response.msg[0], &response.msg[offset+2], sizeof(ETH_MAC));
  memcpy (&response.msg[6], physical_address, sizeof(ETH_MAC));
  offset += 8 - 16; /* Account for the Ethernet Header and Offset value in this number  */
  response.msg[14] = offset & 0xFF;
  response.msg[15] = (offset >> 8) & 0xFF;

  /* send response packet */
  status = eth_write(xu->var->etherface, &response, NULL);
  ++xu->var->stats.loopf;

  if (DBG_PCK & xu->dev->dctrl)
      eth_packet_trace_ex(xu->var->etherface, response.msg, response.len, ((function == 1) ? "xu-loopbackreply" : "xu-loopbackforward"), DBG_DAT & xu->dev->dctrl, DBG_PCK);

  return status;
}

t_stat xu_process_local (CTLR* xu, ETH_PACK* pack)
{
  /* returns SCPE_OK if local processing occurred,
     otherwise returns SCPE_NOFNC or some other code */
  int protocol;

  sim_debug(DBG_TRC, xu->dev, "xu_process_local()\n");

  protocol = pack->msg[12] | (pack->msg[13] << 8);
  switch (protocol) {
    case 0x0090:  /* ethernet loopback */
      return xu_process_loopback(xu, pack);
      break;
    case 0x0260:  /* MOP remote console */
      return SCPE_NOFNC; /* not implemented yet */
      break;
  }
  return SCPE_NOFNC;
}

void xu_read_callback(CTLR* xu, int status)
{
  if (DBG_PCK & xu->dev->dctrl)
      eth_packet_trace_ex(xu->var->etherface, xu->var->read_buffer.msg, xu->var->read_buffer.len, "xu-recvd", DBG_DAT & xu->dev->dctrl, DBG_PCK);

  xu->var->read_buffer.used = 0;  /* none processed yet */

  /* process any packets locally that can be */
  status = xu_process_local (xu, &xu->var->read_buffer);

  /* add packet to read queue */
  if (status != SCPE_OK)
    ethq_insert(&xu->var->ReadQ, ETH_ITM_NORMAL, &xu->var->read_buffer, 0);
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
  if (DBG_PCK & xu->dev->dctrl)
    eth_packet_trace_ex(xu->var->etherface, system_id.msg, system_id.len, "xu-systemid", DBG_DAT & xu->dev->dctrl, DBG_PCK);

  return status;
}

t_stat xu_svc(UNIT* uptr)
{
  int queue_size;
  CTLR* xu = xu_unit2ctlr(uptr);

  /* First pump any queued packets into the system */
  if ((xu->var->ReadQ.count > 0) && ((xu->var->pcsr1 & PCSR1_STATE) == STATE_RUNNING))
    xu_process_receive(xu);

  /* Now read and queue packets that have arrived */
  /* This is repeated as long as they are available and we have room */
  do
    {
    queue_size = xu->var->ReadQ.count;
    /* read a packet from the ethernet - processing is via the callback */
    eth_read (xu->var->etherface, &xu->var->read_buffer, xu->var->rcallback);
  } while (queue_size != xu->var->ReadQ.count);

  /* Now pump any still queued packets into the system */
  if ((xu->var->ReadQ.count > 0) && ((xu->var->pcsr1 & PCSR1_STATE) == STATE_RUNNING))
    xu_process_receive(xu);

  /* resubmit service timer if controller not halted */
  switch (xu->var->pcsr1 & PCSR1_STATE) {
    case STATE_READY:
    case STATE_RUNNING:
      sim_clock_coschedule (&xu->unit[0], tmxr_poll);
      break;
  };

  return SCPE_OK;
}

t_stat xu_tmrsvc(UNIT* uptr)
{
  CTLR* xu = xu_unit2ctlr(uptr);
  const ETH_MAC mop_multicast = {0xAB, 0x00, 0x00, 0x02, 0x00, 0x00};

  /* send identity packet when timer expires */
  if (--xu->var->idtmr <= 0) {
    if ((xu->var->mode & MODE_DMNT) == 0)           /* if maint msg is not disabled */
      xu_system_id(xu, mop_multicast, 0);           /*   then send ID packet */
    xu->var->idtmr = XU_ID_TIMER_VAL;               /* reset timer */
  }

  /* update stats */
  upd_stat16 (&xu->var->stats.secs, 1);

  /* resubmit service timer */
  sim_activate_after(uptr, 1000000);

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
  int i;

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
  for (i=0; i<6; i++)
    xu->var->setup.macs[1][i] = 0xff; /* Broadcast Address */
  xu->var->setup.mac_count = 2;
  if (xu->var->etherface) {
    eth_filter (xu->var->etherface, xu->var->setup.mac_count,
                xu->var->setup.macs, xu->var->setup.multicast,
                xu->var->setup.promiscuous);

    /* activate device */
    sim_clock_coschedule (&xu->unit[0], tmxr_poll);

    /* start service timer */
    sim_activate_after (&xu->unit[1], 1000000);
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
  /* One time only initializations */
  if (!xu->var->initialized) {
    char uname[16];

    xu->var->initialized = TRUE;
    sprintf (uname, "%s-SVC", dptr->name);
    sim_set_uname (&dptr->units[0], uname);
    sprintf (uname, "%s-TMRSVC", dptr->name);
    sim_set_uname (&dptr->units[1], uname);
    /* Set an initial MAC address in the DEC range */
    xu_setmac (dptr->units, 0, "08:00:2B:00:00:00/24", NULL);
    }
  /* init read queue (first time only) */
  status = ethq_init (&xu->var->ReadQ, XU_QUE_MAX);
  if (status != SCPE_OK)
    return status;

  /* software reset controller */
  xu_sw_reset(xu);

  return auto_config (0, 0);                              /* run autoconfig */
}


/* Perform one of the defined ancillary functions. */
int32 xu_command(CTLR* xu)
{
  uint32 udbb;
  int fnc, mtlen, i, j;
  uint16 value, pltlen;
  t_stat rstatus, wstatus, wstatus2, wstatus3;
  struct xu_stats* stats = &xu->var->stats;
  uint16* udb = xu->var->udb;
  uint16* mac_w = (uint16*) xu->var->mac;
  static const ETH_MAC zeros = {0,0,0,0,0,0};
  static const ETH_MAC mcast_load_server = {0xAB, 0x00, 0x00, 0x01, 0x00, 0x00};
  static const char* command[] = {
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

    case FC_RDPA:           /* read default physical address */
      wstatus = Map_WriteB(xu->var->pcbb + 2, 6, xu->var->mac);
      if (wstatus)
        return PCSR0_PCEI + 1;
      break;

    case FC_RPA:            /* read current physical address */
      wstatus = Map_WriteB(xu->var->pcbb + 2, 6, (uint8*)&xu->var->setup.macs[0]);
      if (wstatus)
        return PCSR0_PCEI + 1;
      break;

    case FC_WPA:            /* write current physical address */
      rstatus = Map_ReadB(xu->var->pcbb + 2, 6, (uint8*)&xu->var->setup.macs[0]);
      if (xu->var->pcb[1] & 1)
        return PCSR0_PCEI;
      break;

    case FC_RMAL:   /* read multicast address list */
      mtlen = (xu->var->pcb[2] & 0xFF00) >> 8;
      udbb = xu->var->pcb[1] | ((xu->var->pcb[2] & 03) << 16);
      wstatus = Map_WriteB(udbb, mtlen * 3, (uint8*) &xu->var->setup.macs[2]);
      break;

    case FC_WMAL:   /* write multicast address list */
      mtlen = (xu->var->pcb[2] & 0xFF00) >> 8;
      sim_debug(DBG_TRC, xu->dev, "FC_WAL: mtlen=%d\n", mtlen);
      if (mtlen > 10)
        return PCSR0_PCEI;
      udbb = xu->var->pcb[1] | ((xu->var->pcb[2] & 03) << 16);
      /* clear existing multicast list */
      for (i=2; i<XU_FILTER_MAX; i++) {
        for (j=0; j<6; j++)
          xu->var->setup.macs[i][j] = 0;
      }
      /* get multicast list from host */
      rstatus = Map_ReadB(udbb, mtlen * 6, (uint8*) &xu->var->setup.macs[2]);
      if (rstatus == 0) {
        xu->var->setup.valid = 1;
        xu->var->setup.mac_count = mtlen + 2;
        eth_filter (xu->var->etherface, xu->var->setup.mac_count,
                    xu->var->setup.macs, xu->var->setup.multicast,
                    xu->var->setup.promiscuous);
      } else {
        xu->var->pcsr0 |= PCSR0_PCEI;
      }
      break;

    case FC_RRF:            /* read ring format */
      if ((xu->var->pcb[1] & 1) || (xu->var->pcb[2] & 0374)) 
        return PCSR0_PCEI;
      xu->var->udb[0] = xu->var->tdrb & 0177776;
      xu->var->udb[1] = (uint16)((xu->var->telen << 8) + ((xu->var->tdrb >> 16) & 3));
      xu->var->udb[2] = (uint16)xu->var->trlen;
      xu->var->udb[3] = xu->var->rdrb & 0177776;
      xu->var->udb[4] = (uint16)((xu->var->relen << 8) + ((xu->var->rdrb >> 16) & 3));
      xu->var->udb[5] = (uint16)xu->var->rrlen;

      /* Write UDB to host memory. */
      udbb = xu->var->pcb[1] + ((xu->var->pcb[2] & 3) << 16);
      wstatus = Map_WriteW(udbb, 12, xu->var->pcb);
      if (wstatus != 0)
        return PCSR0_PCEI+1;
      break;

    case FC_WRF:            /* write ring format */
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
//    xu_dump_rxring(xu);
//    xu_dump_txring(xu);

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
      udb[7]  = (uint16)stats->frecve;    /* frames received with error */
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

    case FC_RMODE:          /* read mode register */
      value = (uint16)xu->var->mode;
      wstatus = Map_WriteW(xu->var->pcbb+2, 2, &value);
      if (wstatus)
        return PCSR0_PCEI + 1;
      break;

    case FC_WMODE:          /* write mode register */
      value = (uint16)xu->var->mode;
      xu->var->mode = xu->var->pcb[1];
      sim_debug(DBG_TRC, xu->dev, "FC_WMODE: mode=%04x\n", xu->var->mode);

      /* set promiscuous and multicast flags */
      xu->var->setup.promiscuous = (xu->var->mode & MODE_PROM) ? 1 : 0;
      xu->var->setup.multicast   = (xu->var->mode & MODE_ENAL) ? 1 : 0;

      /* if promiscuous or multicast flags changed, change filter */
      if ((value ^ xu->var->mode) & (MODE_PROM | MODE_ENAL))
        eth_filter (xu->var->etherface, xu->var->setup.mac_count,
                    xu->var->setup.macs, xu->var->setup.multicast,
                    xu->var->setup.promiscuous);
      break;

    case FC_RSTAT:          /* read extended status */
    case FC_RCSTAT:         /* read and clear extended status */
      value = xu->var->stat;
      wstatus = Map_WriteW(xu->var->pcbb+2, 2, &value);
      value = 10;
      wstatus2 = Map_WriteW(xu->var->pcbb+4, 2, &value);
      value = 32;
      wstatus3 = Map_WriteW(xu->var->pcbb+6, 2, &value);
      if (wstatus + wstatus2 + wstatus3)
        return PCSR0_PCEI + 1;

      if (fnc == FC_RCSTAT)
        xu->var->stat &= 0377;  /* clear high byte */
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
      udb[22] = mac_w[0];                 /* HA<15:00> */
      udb[23] = mac_w[1];                 /* HA<31:16> */
      udb[24] = mac_w[2];                 /* HA<47:32> */
      udb[25] = 0x64;                     /* dtype */
      udb[26] = (11 << 8) + 1;            /* dvalue + dlen */
      
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
        wstatus = Map_WriteB(xu->var->pcbb + 2, 6, (const uint8*) mcast_load_server);
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

    default:            /* Unknown (unimplemented) command. */
      sim_printf("%s: unknown ancilliary command 0%o requested !\n", xu->dev->name, fnc);
      return PCSR0_PCEI;
      break;

  } /* switch */

  return PCSR0_DNI;
}

/* Transfer received packets into receive ring. */
void xu_process_receive(CTLR* xu)
{
  uint32 segb, ba;
  int slen, wlen;
  t_stat rstatus, wstatus;
  ETH_ITEM* item = 0;
  int state = xu->var->pcsr1 & PCSR1_STATE;
  int no_buffers = xu->var->pcsr0 & PCSR0_RCBI;

  sim_debug(DBG_TRC, xu->dev, "xu_process_receive(), buffers: %d\n", xu->var->rrlen);

// xu_dump_rxring(xu);  /* debug receive ring */

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
      sim_debug(DBG_TRC, xu->dev, "Stopping input processing - Not Owned receive descriptor=0x%X, ", ba);
      sim_debug_bits(DBG_TRC, xu->dev, xu_rdes_w2, xu->var->rxhdr[2], xu->var->rxhdr[2], 0);
      sim_debug_bits(DBG_TRC, xu->dev, xu_rdes_w3, xu->var->rxhdr[3], xu->var->rxhdr[3], 1);
      break;
    }

    /* set buffer length and address */
    slen = xu->var->rxhdr[0];
    segb = xu->var->rxhdr[1] + ((xu->var->rxhdr[2] & 3) << 16);

    /* Initially clear status bits which are conditionally set below */
    xu->var->rxhdr[2] &= ~(RXR_FRAM|RXR_OFLO|RXR_CRC|RXR_STF|RXR_ENF);

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
    if (item->packet.used == 0)
      xu->var->rxhdr[2] |= RXR_STF;

    /* figure out chained packet size */
    wlen = item->packet.crc_len - item->packet.used;
    if (wlen > slen)
      wlen = slen;

    sim_debug(DBG_TRC, xu->dev, "Using receive descriptor=0x%X, slen=0x%04X(%d), segb=0x%04X, ", ba, slen, slen, segb);
    sim_debug_bits(DBG_TRC, xu->dev, xu_rdes_w2, xu->var->rxhdr[2], xu->var->rxhdr[2], 0);
    sim_debug_bits(DBG_TRC, xu->dev, xu_rdes_w3, xu->var->rxhdr[3], xu->var->rxhdr[3], 0);
    sim_debug(DBG_TRC, xu->dev, ", pktlen=0x%X(%d), used=0x%X, wlen=0x%X\n", item->packet.len, item->packet.len, item->packet.used, wlen);

    /* transfer chained packet to host buffer */
    wstatus = Map_WriteB (segb, wlen, &item->packet.msg[item->packet.used]);
    if (wstatus) {
      /* error during write */
      xu->var->stat |= STAT_ERRS | STAT_MERR | STAT_TMOT | STAT_RRNG;
      xu->var->pcsr0 |= PCSR0_SERI;
      break;
    }

    /* update chained counts */
    item->packet.used += wlen;

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
    xu->var->rxhdr[3] |= (uint16)(item->packet.crc_len);

    /* Is this the end-of-frame? OR is buffer chaining disabled? */
    if ((item->packet.used == item->packet.crc_len) ||
        (xu->var->mode & MODE_DRDC)) {
      /* mark end-of-frame */
      xu->var->rxhdr[2] |= RXR_ENF;

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

    sim_debug(DBG_TRC, xu->dev, "Updating receive descriptor=0x%X, slen=0x%04X, segb=0x%04X, ", ba, slen, segb);
    sim_debug_bits(DBG_TRC, xu->dev, xu_rdes_w2, xu->var->rxhdr[2], xu->var->rxhdr[2], 0);
    sim_debug_bits(DBG_TRC, xu->dev, xu_rdes_w3, xu->var->rxhdr[3], xu->var->rxhdr[3], 1);

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
// xu_dump_rxring(xu);  /* debug receive ring */

}

void xu_process_transmit(CTLR* xu)
{
  uint32 segb, ba;
  int slen, wlen, i, off, giant, runt;
  t_stat rstatus, wstatus;

  sim_debug(DBG_TRC, xu->dev, "xu_process_transmit()\n");
/* xu_dump_txring(xu); *//* debug receive ring */

  off = giant = runt = 0;
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

      /* As described in the DEUNA User Guide (Section 4.7), the DEUNA is responsible 
         for inserting the appropriate source MAC address in the outgoing packet header, 
         so we do that now. */
      memcpy(xu->var->write_buffer.msg+6, (uint8*)&xu->var->setup.macs[0], sizeof(ETH_MAC));

      /* are we in internal loopback mode ? */
      if ((xu->var->mode & MODE_LOOP) && (xu->var->mode & MODE_INTL)) {
        /* just put packet in  receive buffer */
        ethq_insert (&xu->var->ReadQ, ETH_ITM_LOOPBACK, &xu->var->write_buffer, 0);
      } else {
        /* transmit packet synchronously - write callback sets status */
        wstatus = eth_write(xu->var->etherface, &xu->var->write_buffer, xu->var->wcallback);
        if (wstatus)
          xu->var->pcsr0 |= PCSR0_PCEI;
        else
          if (DBG_PCK & xu->dev->dctrl)
            eth_packet_trace_ex(xu->var->etherface, xu->var->write_buffer.msg, xu->var->write_buffer.len, "xu-write", DBG_DAT & xu->dev->dctrl, DBG_PCK);
      }

      /* update transmit status in transmit buffer */
      if (xu->var->write_buffer.status != 0) {
        /* failure */
        const uint16 tdr = (uint16)(100 + wlen * 8); /* arbitrary value */
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
  int command = xu->var->pcsr0 & PCSR0_PCMD;
  int state = xu->var->pcsr1 & PCSR1_STATE;
  static const char* commands[] = {
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
    case CMD_PDMD:          /* POLLING DEMAND */
      /* process transmit buffers, receive buffers are done in the service timer */
      xu_process_transmit(xu);
      xu->var->pcsr0 |= PCSR0_DNI;
      break;

    case CMD_GETCMD:        /* GET COMMAND */
      xu_command(xu);
      xu->var->pcsr0 |= PCSR0_DNI;
      break;

    case CMD_GETPCBB:       /* GET PCB-BASE */
      xu->var->pcbb = (xu->var->pcsr3 << 16) | xu->var->pcsr2;
      xu->var->pcsr0 |= PCSR0_DNI;
      break;

    case CMD_SELFTEST:      /* SELFTEST */
      /*
        SELFTEST is a <=15-second self diagnostic test, setting various
        error flags and the DONE (DNI) flag when complete. For simulation
        purposes, signal completion immediately with no errors. This
        inexact behavior could be incompatible with any guest machine
        diagnostics that are expecting to be able to monitor the
        controller's progress through the diagnostic testing.
      */
      xu->var->pcsr0 |= PCSR0_DNI;
      xu->var->pcsr0 &= ~PCSR0_USCI;
      xu->var->pcsr0 &= ~PCSR0_FATL;
      xu->var->pcsr1 = STATE_READY;
      break;

    case CMD_START:         /* START */
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

    case CMD_STOP:          /* STOP */
      if (state == STATE_RUNNING) {
        xu->var->pcsr1 &= ~PCSR1_STATE;
        xu->var->pcsr1 |= STATE_READY;
        xu->var->pcsr0 |= PCSR0_DNI;
      } else
        xu->var->pcsr0 |= PCSR0_PCEI;
      break;

    case CMD_BOOT:      /* BOOT */
      /* not implemented */
      sim_printf ("%s: BOOT command not implemented!\n", xu->dev->name);

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
  sim_debug(DBG_REG, xu->dev, "xu_rd(), PCSR%d, data=%04x\n", reg, *data);
  if (PA & 1) {
    sim_debug(DBG_WRN, xu->dev, "xu_rd(), Unexpected Odd address access of PCSR%d\n", reg);
  }
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
  sim_debug(DBG_REG, xu->dev, "xu_wr(), PCSR%d, data=%08x, PA=%08x, access=%d[%s]\n", reg, data, PA, access, desc);
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
      xu->var->pcsr2 = data & 0177776;  /* store word, but not MBZ LSB */
      break;

    case 03:
      xu->var->pcsr3 = data & 0000003;  /* store significant bits */
      break;
  }
  return SCPE_OK;
}


/* attach device: */
t_stat xu_attach(UNIT* uptr, CONST char* cptr)
{
  t_stat status;
  char* tptr;
  CTLR* xu = xu_unit2ctlr(uptr);

  sim_debug(DBG_TRC, xu->dev, "xu_attach(cptr=%s)\n", cptr);
  tptr = (char *) malloc(strlen(cptr) + 1);
  if (tptr == NULL) return SCPE_MEM;
  strcpy(tptr, cptr);

  xu->var->etherface = (ETH_DEV *) malloc(sizeof(ETH_DEV));
  if (!xu->var->etherface) {
    free(tptr);
    return SCPE_MEM;
    }

  status = eth_open(xu->var->etherface, cptr, xu->dev, DBG_ETH);
  if (status != SCPE_OK) {
    free(tptr);
    free(xu->var->etherface);
    xu->var->etherface = 0;
    return status;
  }
  eth_set_throttle (xu->var->etherface, xu->var->throttle_time, xu->var->throttle_burst, xu->var->throttle_delay);
  if (SCPE_OK != eth_check_address_conflict (xu->var->etherface, &xu->var->mac)) {
    eth_close(xu->var->etherface);
    free(tptr);
    free(xu->var->etherface);
    xu->var->etherface = NULL;
    return SCPE_NOATT;
  }
  uptr->filename = tptr;
  uptr->flags |= UNIT_ATT;
  eth_setcrc(xu->var->etherface, 1); /* enable CRC */

  /* init read queue (first time only) */
  status = ethq_init(&xu->var->ReadQ, XU_QUE_MAX);
  if (status != SCPE_OK) {
    eth_close(xu->var->etherface);
    free(tptr);
    free(xu->var->etherface);
    xu->var->etherface = NULL;
    return status;
    }

  if (xu->var->setup.valid) {
    int i, count = 0;
    ETH_MAC zeros = {0, 0, 0, 0, 0, 0};
    ETH_MAC filters[XU_FILTER_MAX + 1];

    for (i = 0; i < XU_FILTER_MAX; i++)
      if (memcmp(zeros, &xu->var->setup.macs[i], sizeof(ETH_MAC)))
        memcpy (filters[count++], xu->var->setup.macs[i], sizeof(ETH_MAC));
    eth_filter (xu->var->etherface, count, filters, xu->var->setup.multicast, xu->var->setup.promiscuous);
    }

  return SCPE_OK;
}

/* detach device: */

t_stat xu_detach(UNIT* uptr)
{
  CTLR* xu = xu_unit2ctlr(uptr);
  sim_debug(DBG_TRC, xu->dev, "xu_detach()\n");

  if (uptr->flags & UNIT_ATT) {
    eth_close (xu->var->etherface);
    free(xu->var->etherface);
    xu->var->etherface = NULL;
    free(uptr->filename);
    uptr->filename = NULL;
    uptr->flags &= ~UNIT_ATT;
    /* cancel service timers */
    sim_cancel (uptr);                  /* stop the receiver */
    sim_cancel (uptr+1);                /* stop the timer services */
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
  sim_printf ("receive ring[%s]: base address: %08x  headers: %d, header size: %d, current: %d\n", xu->dev->name, xu->var->rdrb, xu->var->rrlen, xu->var->relen, xu->var->rxnext);
  for (i=0; i<rrlen; i++) {
    uint16 rxhdr[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    uint32 ba = xu->var->rdrb + (xu->var->relen * 2) * i;
    t_stat rstatus = Map_ReadW (ba, 8, rxhdr);  /* get rxring entry[i] */
    int own = (rxhdr[2] & RXR_OWN) >> 15;
    int len = rxhdr[0];
    uint32 addr = rxhdr[1] + ((rxhdr[2] & 3) << 16);
    if (rstatus == 0)
      sim_printf ("  header[%d]: own:%d, len:%d, address:%08x data:{%04x,%04x,%04x,%04x}\n", i, own, len, addr, rxhdr[0], rxhdr[1], rxhdr[2], rxhdr[3]);
  }
}

void xu_dump_txring (CTLR* xu)
{
  int i;
  int trlen = xu->var->trlen;
  sim_printf ("transmit ring[%s]: base address: %08x  headers: %d, header size: %d, current: %d\n", xu->dev->name, xu->var->tdrb, xu->var->trlen, xu->var->telen, xu->var->txnext);
  for (i=0; i<trlen; i++) {
    uint16 txhdr[4];
    uint32 ba = xu->var->tdrb + (xu->var->telen * 2) * i;
    t_stat tstatus = Map_ReadW (ba, 8, txhdr);  /* get rxring entry[i] */
    int own = (txhdr[2] & RXR_OWN) >> 15;
    int len = txhdr[0];
    uint32 addr = txhdr[1] + ((txhdr[2] & 3) << 16);
    if (tstatus == 0)
      sim_printf ("  header[%d]: own:%d, len:%d, address:%08x data:{%04x,%04x,%04x,%04x}\n", i, own, len, addr, txhdr[0], txhdr[1], txhdr[2], txhdr[3]);
  }
}

t_stat xu_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "DELUA/DEUNA Unibus Ethernet Controllers (XU, XUB)\n\n");
fprintf (st, "The simulator implements two DELUA/DEUNA Unibus Ethernet controllers (XU, XUB).\n");
fprintf (st, "Initially, both XU and XQB are disabled.  Options allow control of the MAC\n");
fprintf (st, "address and the controller type.\n\n");
fprint_set_help (st, dptr);
fprintf (st, "\nConfigured options and controller state can be displayed with:\n\n");
fprint_show_help (st, dptr);
fprintf (st, "\nMAC address octets must be delimited by dashes, colons or periods.\n");
fprintf (st, "The controller defaults to a relatively unique MAC address in the range\n");
fprintf (st, "08-00-2B-00-00-00 thru 08-00-2B-FF-FF-FF, which should be sufficient\n");
fprintf (st, "for most network environments.  If desired, the simulated MAC address\n");
fprintf (st, "can be directly set.\n");
fprintf (st, "To access the network, the simulated Ethernet controller must be attached to a\n");
fprintf (st, "real Ethernet interface.\n\n");
eth_attach_help(st, dptr, uptr, flag, cptr);
fprintf (st, "One final note: because of its asynchronous nature, the XU controller is not\n");
fprintf (st, "limited to the ~1.5Mbit/sec of the real DEUNA/DELUA controllers, nor the\n");
fprintf (st, "10Mbit/sec of a standard Ethernet.  Attach it to a Fast or Gigabit Ethernet\n");
fprintf (st, "card, and \"Feel the Power!\" :-)\n");
return SCPE_OK;
}

const char *xu_description (DEVICE *dptr)
{
return "DEUNA/DELUA Ethernet controller";
}


