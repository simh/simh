/* pdp11_xq.c: DEQNA/DELQA ethernet controller simulator
  ------------------------------------------------------------------------------

   Copyright (c) 2002-2003, David T. Hittner

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

  This DEQNA/DELQA simulation is based on:
    Digital DELQA Users Guide, Part# EK-DELQA-UG-002
    Digital DEQNA Users Guide, Part# EK-DEQNA-UG-001
  These manuals can be found online at:
    http://www.spies.com/~aek/pdf/dec/qbus

  Certain adaptations have been made because this is an emulation:
    Ethernet transceiver power flag CSR<12> is ON when attached.
    External Loopback does not go out to the physical adapter, it is
      implemented more like an extended Internal Loopback
    Time Domain Reflectometry (TDR) numbers are faked
    The 10-second approx. hardware/software reset delay does not exist
    Some physical ethernet receive events like Runts, Overruns, etc. are
      never reported back, since the packet-level driver never sees them

  Certain advantages are derived from this emulation:
    If the real ethernet controller is faster than 10Mbit/sec, the speed is
      seen by the simulated cpu since there are no minimum response times.

  Known Bugs or Unsupported features, in priority order:
    1) PDP11 (modified) bootrom loader                    [done! 10-Apr-03]
    2) Second controller                                  [done! 05-May-03]
    3) Cannot split inbound packet into multiple buffers  [done! 05-Jun-03]
    4) PDP11 bootstrap
    5) MOP functionality not implemented
    6) Local packet processing not implemented

  Regression Tests used by the Author:
    VAX:
      1. Console SHOW DEVICE
      2. VMS v7.2 boots/initializes/shows device
      3. VMS DECNET - SET HOST and COPY tests
      4. VMS MultiNet - SET HOST/TELNET and FTP tests
      5. VMS LAT - SET HOST/LAT tests
      6. VMS Cluster - SHOW CLUSTER, SHOW DEVICE, and cluster disk COPY tests
      7. Console boot into VMSCluster (>>>B XQAO)
    PDP11:
      1. RT-11 v5.3 - FTPSB copy test

  ------------------------------------------------------------------------------

  Modification history:

  05-Jun-03  DTH  Added receive packet splitting
  03-Jun-03  DTH  Added SHOW XQ FILTERS
  02-Jun-03  DTH  Added SET/SHOW XQ STATS (packet statistics), runt & giant processing
  28-May-03  DTH  Modified message queue for dynamic size to shrink executable
  28-May-03  MP   Fixed bug in xq_setmac
  06-May-03  DTH  Changed 32-bit t_addr to uint32 for v3.0
                  Removed SET ADDRESS functionality
  05-May-03  DTH  Added second controller
  26-Mar-03  DTH  Added PDP11 bootrom loader
                  Adjusted xq_ex and xq_dev to allow pdp11 to look at bootrom
                  Patched bootrom to allow "pass" of diagnostics on RSTS/E
  06-Mar-03  DTH  Corrected interrupts on IE state transition (code by Tom Evans)
                  Added interrupt clear on soft reset (first noted by Bob Supnik)
                  Removed interrupt when setting XL or RL (multiple people)
  16-Jan-03  DTH  Merged Mark Pizzolato's enhancements with main source
                  Corrected PDP11 XQ_DEBUG compilation
  15-Jan-03  MP   Fixed the number of units in the xq device structure.
  13-Jan-03  MP   Reworked the timer management logic which initiated
                  the system id broadcast messages.  The original
                  implementation triggered this on the CSR transition
                  of Receiver Enabled.  This was an issue since the
                  it seems that at least VMS's XQ driver makes this
                  transition often and the resulting overhead reduces
                  the simulated CPU instruction execution thruput by
                  about 40%.  I start the system id timer on device
                  reset and it fires once a second so that it can
                  leverage the reasonably recalibrated tmr_poll value.
  13-Jan-03  MP   Changed the scheduling of xq_svc to leverage the
                  dynamically computed clock values to achieve an
                  approximate interval of 100 per second.  This is
                  more than sufficient for normal system behaviour
                  expecially since we service recieves with every
                  transmit.  The previous fixed value of 2500
                  attempted to get 200/sec but it was a guess that
                  didn't adapt.  On faster host systems (possibly
                  most of them) the 2500 number spends too much time
                  polling.
  10-Jan-03  DTH  Removed XQ_DEBUG dependency from Borland #pragmas
                  Added SET XQ BOOTROM command for PDP11s
  07-Jan-03  DTH  Added pointer to online manuals
  02-Jan-03  DTH  Added local packet processing
  30-Dec-02  DTH  Added automatic system id broadcast
  27-Dec-02  DTH  Merged Mark Pizzolato's enhancements with main source
  20-Dec-02  MP   Fix bug that caused VMS system crashes when attempting cluster
                  operations.  Added additional conditionally compiled debug
                  info needed to track down the issue.
  17-Dec-02  MP   Added SIMH "registers" describing the Ethernet state
                  so this information can be recorded in a "saved" snapshot.
  05-Dec-02  MP   Adjusted the rtime value from 100 to 2500 which increased the
                  available CPU cycles for Instruction execution by almost 100%.
                  This made sense after the below enhancements which, in general
                  caused the draining of the received data stream much more
                  agressively with less overhead.
  05-Dec-02  MP   Added a call to xq_svc after all successful calls to eth_write
                  to allow receive processing to happen before the next event
                  service time.
  05-Dec-02  MP   Restructured the flow of processing in xq_svc so that eth_read
                  is called repeatedly until either a packet isn't found or
                  there is no room for another one in the queue.  Once that has
                  been done, xq_processrdbl is called to pass the queued packets
                  into the simulated system as space is available there.
                  xq_process_rdbl is also called at the beginning of xq_svc to
                  drain the queue into the simulated system, making more room
                  available in the queue.  No processing is done at all in
                  xq_svc if the receiver is disabled.
  04-Dec-02  MP   Changed interface and usage to xq_insert_queue to pass
                  the packet to be inserted by reference.  This avoids 3K bytes
                  of buffer copy operations for each packet received.  Now only
                  copy actual received packet data.
  31-Oct-02  DTH  Cleaned up pointer warnings (found by Federico Schwindt)
                  Corrected unattached and no network behavior
                  Added message when SHOW XQ ETH finds no devices
  23-Oct-02  DTH  Beta 5 released
  22-Oct-02  DTH  Added all_multicast and promiscuous support
  21-Oct-02  DTH  Added write buffer max size check (code by Jason Thorpe)
                  Corrected copyright again
                  Implemented NXM testing and recovery
  16-Oct-02  DTH  Beta 4 released
                  Added and debugged Sanity Timer code
                  Corrected copyright
  15-Oct-02  DTH  Rollback to known good Beta3 and roll forward; TCP broken
  12-Oct-02  DTH  Fixed VAX network bootstrap; setup packets must return TDR > 0
  11-Oct-02  DTH  Added SET/SHOW XQ TYPE and SET/SHOW XQ SANITY commands
  10-Oct-02  DTH  Beta 3 released; Integrated with 2.10-0b1
                  Fixed off-by-1 bug on xq->setup.macs[7..13]
                  Added xq_make_checksum
                  Added rejection of multicast addresses in SET XQ MAC
  08-Oct-02  DTH  Beta 2 released; Integrated with 2.10-0p4
                  Added variable vector (fixes PDP11) and copyrights
  03-Oct-02  DTH  Beta version of xq/sim_ether released for SIMH 2.09-11
  24-Sep-02  DTH  Moved more code to Sim_Ether module, added SHOW ETH command
  23-Sep-02  DTH  Added SET/SHOW MAC command
  22-Sep-02  DTH  Multinet TCP/IP loaded, tests OK via SET HOST/TELNET
  20-Sep-02  DTH  Cleaned up code fragments, fixed non-DECNET MAC use
  19-Sep-02  DTH  DECNET finally stays up; successful SET HOST to another node
  15-Sep-02  DTH  Added ethernet packet read/write
  13-Sep-02  DTH  DECNET starts, but circuit keeps going up & down
  26-Aug-02  DTH  DECNET loaded, returns device timeout
  22-Aug-02  DTH  VMS 7.2 recognizes device as XQA0
  18-Aug-02  DTH  VAX sees device as XQA0; shows hardcoded MAC correctly
  15-Aug-02  DTH  Started XQ simulation

  ------------------------------------------------------------------------------
*/

/* compiler directives to help the Author keep the code clean :-) */
#if defined (__BORLANDC__)
#pragma warn +8070      /* function should return value */
/* #pragma warn +8071 *//* conversion may lose significant digits */
#pragma warn +8075      /* suspicious pointer conversion */
#pragma warn +8079      /* mixing different char pointers */
#pragma warn +8080      /* variable declared but not used */
#endif /* __BORLANDC__ */

#include <assert.h>
#include "pdp11_xq.h"
#include "pdp11_xq_bootrom.h"

#define XQ_MAX_CONTROLLERS 2    /* maximum controllers allowed */

extern int32 int_req[IPL_HLVL];
extern int32 tmr_poll, clk_tps;
extern FILE *sim_log;

/* forward declarations */
t_stat xq_rd(int32* data, int32 PA, int32 access);
t_stat xq_wr(int32  data, int32 PA, int32 access);
t_stat xq_svc(UNIT * uptr);
t_stat xq_sansvc(UNIT * uptr);
t_stat xq_idsvc(UNIT * uptr);
t_stat xq_reset (DEVICE * dptr);
t_stat xq_attach (UNIT * uptr, char * cptr);
t_stat xq_detach (UNIT * uptr);
t_stat xq_showmac (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xq_setmac  (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat xq_show_filters (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xq_show_stats (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xq_set_stats  (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat xq_show_type (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xq_set_type (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat xq_show_sanity (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xq_set_sanity (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat xq_showeth (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xq_process_xbdl(CTLR* xq);
t_stat xq_dispatch_xbdl(CTLR* xq);
void xq_start_receiver(void);
void xq_sw_reset(CTLR* xq);
int32 xq_inta (void);
int32 xq_intb (void);
t_stat xq_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat xq_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
void xq_start_santmr(CTLR* xq);
void xq_cancel_santmr(CTLR* xq);
void xq_reset_santmr(CTLR* xq);
t_stat xq_boot_host(void);
void xq_start_idtmr(CTLR* xq);
t_stat xq_system_id(CTLR* xq, const ETH_MAC dst, uint16 receipt_id);
void xqa_read_callback(int status);
void xqb_read_callback(int status);
void xqa_write_callback(int status);
void xqb_write_callback(int status);

struct xq_device    xqa = {
  xqa_read_callback,                        /* read callback routine */
  xqa_write_callback,                       /* write callback routine */
  {0x08, 0x00, 0x2B, 0xAA, 0xBB, 0xCC},     /* mac */
  XQ_T_DELQA,                               /* type */
  {0}                                       /* sanity */
  };

struct xq_device    xqb = {
  xqb_read_callback,                        /* read callback routine */
  xqb_write_callback,                       /* write callback routine */
  {0x08, 0x00, 0x2B, 0xBB, 0xCC, 0xDD},     /* mac */
  XQ_T_DELQA,                               /* type */
  {0}                                       /* sanity */
  };

/* SIMH device structures */
DIB xqa_dib = { IOBA_XQ, IOLN_XQ, &xq_rd, &xq_wr,
		1, IVCL (XQ), 0, { &xq_inta } };

UNIT xqa_unit[] = {
 { UDATA (&xq_svc, UNIT_ATTABLE + UNIT_DISABLE, 2047) },  /* receive timer */
 { UDATA (&xq_sansvc, UNIT_DIS, 0) },                     /* sanity timer */
 { UDATA (&xq_idsvc, UNIT_DIS, 0) }                       /* system id timer */
};

REG xqa_reg[] = {
  { GRDATA ( SA0,  xqa.addr[0], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA1,  xqa.addr[1], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA2,  xqa.addr[2], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA3,  xqa.addr[3], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA4,  xqa.addr[4], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA5,  xqa.addr[5], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( RBDL, xqa.rbdl, XQ_RDX, 32, 0) },
  { GRDATA ( XBDL, xqa.xbdl, XQ_RDX, 32, 0) },
  { GRDATA ( VAR,  xqa.var,  XQ_RDX, 16, 0) },
  { GRDATA ( CSR,  xqa.csr,  XQ_RDX, 16, 0) },
  { GRDATA ( SETUP_PRM, xqa.setup.promiscuous, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_MLT, xqa.setup.multicast, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_L1, xqa.setup.l1, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_L2, xqa.setup.l2, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_L3, xqa.setup.l3, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_SAN, xqa.setup.sanity_timer, XQ_RDX, 32, 0), REG_HRO},
  { BRDATA ( SETUP_MACS, &xqa.setup.macs, XQ_RDX, 8, sizeof(xqa.setup.macs)), REG_HRO},
  { NULL },
};

DIB xqb_dib = { IOBA_XQB, IOLN_XQB, &xq_rd, &xq_wr,
		1, IVCL (XQ), 0, { &xq_intb } };

UNIT xqb_unit[] = {
 { UDATA (&xq_svc, UNIT_ATTABLE + UNIT_DISABLE, 2047) },  /* receive timer */
 { UDATA (&xq_sansvc, UNIT_DIS, 0) },                     /* sanity timer */
 { UDATA (&xq_idsvc, UNIT_DIS, 0) }                       /* system id timer */
};

REG xqb_reg[] = {
  { GRDATA ( SA0,  xqb.addr[0], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA1,  xqb.addr[1], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA2,  xqb.addr[2], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA3,  xqb.addr[3], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA4,  xqb.addr[4], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA5,  xqb.addr[5], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( RBDL, xqb.rbdl, XQ_RDX, 32, 0) },
  { GRDATA ( XBDL, xqb.xbdl, XQ_RDX, 32, 0) },
  { GRDATA ( VAR,  xqb.var,  XQ_RDX, 16, 0) },
  { GRDATA ( CSR,  xqb.csr,  XQ_RDX, 16, 0) },
  { GRDATA ( SETUP_PRM, xqb.setup.promiscuous, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_MLT, xqb.setup.multicast, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_L1, xqb.setup.l1, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_L2, xqb.setup.l2, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_L3, xqb.setup.l3, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_SAN, xqb.setup.sanity_timer, XQ_RDX, 32, 0), REG_HRO},
  { BRDATA ( SETUP_MACS, &xqb.setup.macs, XQ_RDX, 8, sizeof(xqb.setup.macs)), REG_HRO},
  { NULL },
};

MTAB xq_mod[] = {
	{ MTAB_XTD|MTAB_VDV, 004, "ADDRESS", NULL,
		NULL, &show_addr, NULL },
	{ MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
		NULL, &show_vec, NULL },
  { MTAB_XTD | MTAB_VDV, 0, "MAC", "MAC",
    &xq_setmac, &xq_showmac, NULL },
  { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "ETH", NULL,
    NULL, &xq_showeth, NULL },
  { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "FILTERS", NULL,
    NULL, &xq_show_filters, NULL },
  { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "STATS", "STATS",
    &xq_set_stats, &xq_show_stats, NULL },
  { MTAB_XTD | MTAB_VDV, 0, "TYPE", "TYPE",
    &xq_set_type, &xq_show_type, NULL },
  { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "SANITY", "SANITY",
    &xq_set_sanity, &xq_show_sanity, NULL },
  { 0 },
};

DEVICE xq_dev = {
  "XQ", xqa_unit, xqa_reg, xq_mod,
  3, XQ_RDX, 11, 1, XQ_RDX, 16,
  &xq_ex, &xq_dep, &xq_reset,
  NULL, &xq_attach, &xq_detach,
  &xqa_dib, DEV_DISABLE | DEV_QBUS
};

DEVICE xqb_dev = {
  "XQB", xqb_unit, xqb_reg, xq_mod,
  3, XQ_RDX, 11, 1, XQ_RDX, 16,
  &xq_ex, &xq_dep, &xq_reset,
  NULL, &xq_attach, &xq_detach,
  &xqb_dib, DEV_DISABLE | DEV_DIS | DEV_QBUS
};

CTLR xq_ctrl[] = {
  {&xq_dev,  xqa_unit, &xqa_dib, &xqa},       /* XQA controller */
  {&xqb_dev, xqb_unit, &xqb_dib, &xqb}        /* XQB controller */
};

#ifdef XQ_DEBUG

const char* const xq_recv_regnames[] = {
  "MAC0", "MAC1", "MAC2", "MAC3", "MAC4", "MAC5", "VAR", "CSR"
};

const char* const xq_xmit_regnames[] = {
  "", "", "RBDL-Lo", "RBDL-Hi", "XBDL-Lo", "XBDL-Hi", "VAR", "CSR"
};

const char* const xq_csr_bits[] = {
  "RE ", "SR ", "NI ", "BD ", "XL ", "RL ", "IE ", "XI ",
  "IL ", "EL ", "SE ", "RR ", "OK ", "CA ", "PE ", "RI"
};

/* internal debugging routines */
void xq_debug_setup(CTLR* xq);
void xq_dump_csr(CTLR* xq);
void xq_dump_var(CTLR* xq);
void xq_csr_changes(CTLR* xq, uint16 data);
void xq_var_changes(CTLR* xq, uint16 data);

/* sanity timer debugging */
#include <sys\timeb.h>
struct timeb start, finish;

#endif /* XQ_DEBUG */

/*
================================================================================
                              Queue Management
================================================================================
*/

t_stat xq_init_queue(CTLR* xq, struct xq_msg_que* que)
{
  /* create dynamic queue if it does not exist */
  if (!que->item) {
    size_t size = sizeof(struct xq_msg_itm) * XQ_QUE_MAX;
    que->item = malloc(size);
    if (que->item) {
      /* init dynamic memory */
      memset(que->item, 0, size);
    } else {
      /* failed to allocate memory */
      printf("%s: failed to allocate dynamic queue\n", xq->dev->name);
      if (sim_log) fprintf(sim_log, "%s: failed to allocate dynamic queue\n", xq->dev->name);
      return SCPE_MEM;
    };
  };
  return SCPE_OK;
}

void xq_clear_queue(struct xq_msg_que* que)
{
  int i;
  struct xq_msg_itm* item;

  for (i = 0; i < XQ_QUE_MAX; i++) {
    item = &que->item[i];
    item->type = 0;
    item->packet.len = 0;
    item->packet.used = 0;
    item->status = 0;
  };
  que->count = que->head = que->tail = que->loss = 0;
}

void xq_remove_queue(struct xq_msg_que* que)
{
  struct xq_msg_itm* item = &que->item[que->head];

  if (que->count) {
    item->type = 0;
    item->packet.len = 0;
    item->packet.used = 0;
    item->status = 0;
    if (++que->head == XQ_QUE_MAX)
      que->head = 0;
    que->count--;
  }
}

void xq_insert_queue(struct xq_msg_que* que, int32 type, ETH_PACK* packet, int32 status)
{
  struct xq_msg_itm* item;

  /* if queue empty, set pointers to beginning */
  if (!que->count) {
    que->head = 0;
    que->tail = -1;
  }

  /* find new tail of the circular queue */
  if (++que->tail == XQ_QUE_MAX)
    que->tail = 0;
  if (++que->count > XQ_QUE_MAX) {
    que->count = XQ_QUE_MAX;
    /* lose oldest packet */
    if (++que->head == XQ_QUE_MAX)
      que->head = 0;
    que->loss++;
#ifdef XQ_DEBUG
    fprintf(stderr, "Packet Lost\n");
#endif
    }
  if (que->count > que->high)
    que->high = que->count;

  /* set information in (new) tail item */
  item = &que->item[que->tail];
  item->type = type;
  item->packet.len = packet->len;
  item->packet.used = 0;
  memcpy(item->packet.msg, packet->msg, packet->len);
  item->status = status;
}

/*
================================================================================
*/

/*============================================================================*/

/* Multicontroller support */

CTLR* xq_unit2ctlr(UNIT* uptr)
{
  unsigned int i,j;
  for (i=0; i<XQ_MAX_CONTROLLERS; i++)
    for (j=0; j<xq_ctrl[i].dev->numunits; j++)
      if (&xq_ctrl[i].unit[j] == uptr) return &xq_ctrl[i];
  /* not found */
  return 0;
}

CTLR* xq_dev2ctlr(DEVICE* dptr)
{
  int i;
  for (i=0; i<XQ_MAX_CONTROLLERS; i++)
    if (xq_ctrl[i].dev == dptr) return &xq_ctrl[i];
  /* not found */
  return 0;
}

CTLR* xq_pa2ctlr(uint32 PA)
{
  int i;
  for (i=0; i<XQ_MAX_CONTROLLERS; i++)
    if ((PA >= xq_ctrl[i].dib->ba) && (PA < (xq_ctrl[i].dib->ba + xq_ctrl[i].dib->lnt)))
      return &xq_ctrl[i];
  /* not found */
  return 0;
}

/*============================================================================*/

/* stop simh from reading non-existant unit data stream */
t_stat xq_ex (t_value* vptr, t_addr addr, UNIT* uptr, int32 sw)
{
  /* on PDP-11, allow EX command to look at bootrom */
#ifdef VM_PDP11
  if (addr <= sizeof(xq_bootrom)/2)
    *vptr = xq_bootrom[addr];
  else
    *vptr = 0;
  return SCPE_OK;
#else
  return SCPE_NOFNC;
#endif
}

/* stop simh from writing non-existant unit data stream */
t_stat xq_dep (t_value val, t_addr addr, UNIT* uptr, int32 sw)
{
  return SCPE_NOFNC;
}

t_stat xq_showmac (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  CTLR* xq = xq_unit2ctlr(uptr);
  char  buffer[20];

  eth_mac_fmt((ETH_MAC*)xq->var->mac, buffer);
  fprintf(st, "MAC=%s", buffer);
  return SCPE_OK;
}

void xq_make_checksum(CTLR* xq)
{
  /* checksum calculation routine detailed in vaxboot.zip/xqbtdrivr.mar */
  uint32  checksum = 0;
  const uint32 wmask = 0xFFFF;
  int i;

  for (i = 0; i < sizeof(ETH_MAC); i += 2) {
    checksum <<= 1;
    if (checksum > wmask)
      checksum -= wmask;
    checksum += (xq->var->mac[i] << 8) | xq->var->mac[i+1];
    if (checksum > wmask)
      checksum -= wmask;
  }
  if (checksum == wmask)
    checksum = 0;

  /* set checksum bytes */
  xq->var->mac_checksum[0] = checksum & 0xFF;
  xq->var->mac_checksum[1] = checksum >> 8;
}

t_stat xq_setmac (UNIT* uptr, int32 val, char* cptr, void* desc)
{
  int i, j, len;
  short int num;
  ETH_MAC newmac = {0,0,0,0,0,0};
  const ETH_MAC zeros = {0,0,0,0,0,0};
  const ETH_MAC ones = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  CTLR* xq = xq_unit2ctlr(uptr);

  if (!cptr) return SCPE_IERR;
  /* parse new mac and validate */
  len = strlen(cptr);
  if (len != 17) return SCPE_ARG;
  /* make sure byte separators are OK */
  for (i=2; i<len; i=i+3) {
    if ((cptr[i] != '-') && (cptr[i] != '.')) return SCPE_ARG;
    cptr[i] = '\0';
  }
  /* get and set address bytes */
  for (i=0, j=0; i<len; i=i+3, j++) {
    int valid = strspn(&cptr[i], "0123456789abcdefABCDEF");
    if (valid < 2) return SCPE_ARG;
    sscanf(&cptr[i], "%hx", &num);
    newmac[j] = (unsigned char) num;
  }
  /* final check - cannot be broadcast or multicast address */
  if (!memcmp(newmac, zeros, sizeof(ETH_MAC)) ||  /* broadcast */
      !memcmp(newmac, ones,  sizeof(ETH_MAC)) ||  /* broadcast */
      (newmac[0] & 0x01)                          /* multicast */
     )
    return SCPE_ARG;
  /* set mac, it's OK */
  memcpy(xq->var->mac, newmac, sizeof(ETH_MAC));
  /* calculate MAC checksum */
  xq_make_checksum(xq);
  return SCPE_OK;
}

t_stat xq_showeth (FILE* st, UNIT* uptr, int32 val, void* desc)
{
#define XQ_MAX_LIST 10
  int i;
  ETH_LIST  list[XQ_MAX_LIST];
  int number = eth_devices(XQ_MAX_LIST, list);

  fprintf(st, "ETH devices:\n");
  if (number)
    for (i=0; i<number; i++)
      fprintf(st,"  %d  %s (%s)\n", i, list[i].name, list[i].desc);
  else
    fprintf(st, "  no network devices are available\n");
  return SCPE_OK;
}

t_stat xq_set_stats (UNIT* uptr, int32 val, char* cptr, void* desc)
{
  /* this sets all ints in the stats structure to the integer passed */
  CTLR* xq = xq_unit2ctlr(uptr);
#ifdef XQ_DEBUG
  /* set individual stats to passed parameter value */
  int init = cptr ? atoi(cptr) : 0;
  int* stat_array = (int*) &xq->var->stats;
  int elements = sizeof(struct xq_stats)/sizeof(int);
  int i;
  for (i=0; i<elements; i++)
    stat_array[i] = init;
#else
  /* set stats to zero, regardless of passed parameter */
  memset(&xq->var->stats, 0, sizeof(struct xq_stats));
#endif
  return SCPE_OK;
}

t_stat xq_show_stats (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  char* fmt = "  %-15s%d\n";
  CTLR* xq = xq_unit2ctlr(uptr);

  fprintf(st, "Ethernet statistics:\n");
  fprintf(st, fmt, "Recv:",       xq->var->stats.recv);
  fprintf(st, fmt, "Filtered:",   xq->var->stats.filter);
  fprintf(st, fmt, "Xmit:",       xq->var->stats.xmit);
  fprintf(st, fmt, "Xmit Fail:",  xq->var->stats.fail);
  fprintf(st, fmt, "Runts:",      xq->var->stats.runt);
  fprintf(st, fmt, "Oversize:",   xq->var->stats.giant);
  fprintf(st, fmt, "Setup:",      xq->var->stats.setup);
  fprintf(st, fmt, "Loopback:",   xq->var->stats.loop);
  fprintf(st, fmt, "ReadQ high:", xq->var->ReadQ.high);
  return SCPE_OK;
}

t_stat xq_show_filters (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  CTLR* xq = xq_unit2ctlr(uptr);
  char  buffer[20];
  int i;

  fprintf(st, "Filters:\n");
  for (i=0; i<XQ_FILTER_MAX; i++) {
    eth_mac_fmt((ETH_MAC*)xq->var->setup.macs[i], buffer);
    fprintf(st, "  [%2d]: %s\n", i, buffer);
  };
  return SCPE_OK;
}

t_stat xq_show_type (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  CTLR* xq = xq_unit2ctlr(uptr);
  fprintf(st, "type=");
  switch (xq->var->type) {
    case  XQ_T_DEQNA:       fprintf(st, "DEQNA");      break;
    case  XQ_T_DELQA:       fprintf(st, "DELQA");      break;
  }
  return SCPE_OK;
}

t_stat xq_set_type (UNIT* uptr, int32 val, char* cptr, void* desc)
{
  CTLR* xq = xq_unit2ctlr(uptr);
  if (!cptr) return SCPE_IERR;

  /* this assumes that the parameter has already been upcased */
  if      (!strcmp(cptr, "DEQNA"))      xq->var->type = XQ_T_DEQNA;
  else if (!strcmp(cptr, "DELQA"))      xq->var->type = XQ_T_DELQA;
  else return SCPE_ARG;

  return SCPE_OK;
}

t_stat xq_show_sanity (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  CTLR* xq = xq_unit2ctlr(uptr);

  fprintf(st, "sanity=");
  switch (xq->var->sanity.enabled) {
    case 0:  fprintf(st, "OFF"); break;
    case 1:  fprintf(st, "ON");  break;
  }
  return SCPE_OK;
}

t_stat xq_set_sanity (UNIT* uptr, int32 val, char* cptr, void* desc)
{
  CTLR* xq = xq_unit2ctlr(uptr);
  if (!cptr) return SCPE_IERR;

  /* this assumes that the parameter has already been upcased */
  if      (!strcmp(cptr, "ON"))  xq->var->sanity.enabled = 1;
  else if (!strcmp(cptr, "OFF")) xq->var->sanity.enabled = 0;
  else return SCPE_ARG;

  return SCPE_OK;
}

t_stat xq_nxm_error(CTLR* xq)
{
#ifdef XQ_DEBUG
  fprintf(stderr,"%s: Non Existent Memory Error\n", xq->dev->name);
#endif
  /* set NXM and associated bits in CSR */
  xq->var->csr |= (XQ_CSR_NI | XQ_CSR_XI | XQ_CSR_XL | XQ_CSR_RL);

  /* interrupt if required */
  if (xq->var->csr & XQ_CSR_IE)
    SET_INT(XQ);

  return SCPE_OK;
}

/*
** write callback
*/
void xq_write_callback (CTLR* xq, int status)
{
  t_stat rstatus;
  int32 wstatus;
  const uint16 TDR = 100 + xq->var->write_buffer.len * 8; /* arbitrary value */
  uint16 write_success[2] = {0};
  uint16 write_failure[2] = {XQ_DSC_C};
  write_success[1] = TDR & 0x03FF; /* Does TDR get set on successful packets ?? */
  write_failure[1] = TDR & 0x03FF; /* TSW2<09:00> */

  xq->var->stats.xmit += 1;
  /* update write status words */
  if (status == 0) { /* success */
    wstatus = Map_WriteW(xq->var->xbdl_ba + 8, 4, write_success, NOMAP);
  } else { /* failure */
#ifdef XQ_DEBUG
      fprintf(stderr, "%s: Packet Write Error\n", xq->dev->name);
#endif
    xq->var->stats.fail += 1;
    wstatus = Map_WriteW(xq->var->xbdl_ba + 8, 4, write_failure, NOMAP);
  }
  if (wstatus) {
    xq_nxm_error(xq);
    return;
  }

  /* update csr */
  xq->var->csr |= XQ_CSR_XI;
  if (xq->var->csr & XQ_CSR_IE)
    SET_INT(XQ);

  /* reset sanity timer */
  xq_reset_santmr(xq);

  /* clear write buffer */
  xq->var->write_buffer.len = 0;

  /* next descriptor (implicit) */
  xq->var->xbdl_ba += 12;

  /* finish processing xbdl */
  rstatus = xq_process_xbdl(xq);
}

void xqa_write_callback (int status)
{
  xq_write_callback(&xq_ctrl[0], status);
}

void xqb_write_callback (int status)
{
  xq_write_callback(&xq_ctrl[1], status);
}

/* read registers: */
t_stat xq_rd(int32* data, int32 PA, int32 access)
{
  CTLR* xq = xq_pa2ctlr(PA);
  int index = (PA >> 1) & 07;   /* word index */

#ifdef XQ_DEBUG
  if (index != 7)
#if defined(VM_VAX)
    fprintf (stderr,"%s: %s %08X %08X read: %X\n",
      xq->dev->name, xq_recv_regnames[index], fault_PC, PSL, *data);
#else
    fprintf (stderr,"%s: %s read: %X\n",
      xq->dev->name, xq_recv_regnames[index], *data);
#endif /* VM_VAX */
#endif

  switch (index) {
    case 0:
    case 1:
      /* return checksum in external loopback mode */
      if (xq->var->csr & XQ_CSR_EL)
        *data = 0xFF00 | xq->var->mac_checksum[index];
      else
        *data = 0xFF00 | xq->var->mac[index];
      break;
    case 2:
    case 3:
    case 4:
    case 5:
      *data = 0xFF00 | xq->var->mac[index];
      break;
    case 6:
#if 0
#ifdef XQ_DEBUG
      xq_dump_var(xq);
#endif
#endif
      *data = xq->var->var;
      break;
    case 7:
#ifdef XQ_DEBUG
      xq_dump_csr(xq);
#endif
      *data = xq->var->csr;
      break;
  }
  return SCPE_OK;
}


/* dispatch ethernet read request
   procedure documented in sec. 3.2.2 */

t_stat xq_process_rbdl(CTLR* xq)
{
  int32 rstatus, wstatus;
  uint16 b_length, w_length, rbl;
  uint32 address;
  struct xq_msg_itm* item;
  char* rbuf;

#ifdef XQ_DEBUG
  fprintf(stderr,"%s: CSR - Processing read\n", xq->dev->name);
#endif
  /* process buffer descriptors */
  while(1) {

    /* get receive bdl from memory */
    xq->var->rbdl_buf[0] = 0xFFFF;
    wstatus = Map_WriteW(xq->var->rbdl_ba,     2, &xq->var->rbdl_buf[0], NOMAP);
    rstatus = Map_ReadW (xq->var->rbdl_ba + 2, 6, &xq->var->rbdl_buf[1], NOMAP);
    if (rstatus || wstatus) return xq_nxm_error(xq);

    /* invalid buffer? */
    if (~xq->var->rbdl_buf[1] & XQ_DSC_V) {
      xq->var->csr |= XQ_CSR_RL;
      return SCPE_OK;
    }

    /* explicit chain buffer? */
    if (xq->var->rbdl_buf[1] & XQ_DSC_C) {
      xq->var->rbdl_ba = ((xq->var->rbdl_buf[1] & 0x3F) << 16) | xq->var->rbdl_buf[2];
      continue;
    }

    /* stop processing if nothing in read queue */
    if (!xq->var->ReadQ.count) break;

    /* get status words */
    rstatus = Map_ReadW(xq->var->rbdl_ba + 8, 4, &xq->var->rbdl_buf[4], NOMAP);
    if (rstatus) return xq_nxm_error(xq);

    /* get host memory address */
    address = ((xq->var->rbdl_buf[1] & 0x3F) << 16) | xq->var->rbdl_buf[2];

    /* decode buffer length - two's complement (in words) */
    w_length = ~xq->var->rbdl_buf[3] + 1;
    b_length = w_length * 2;
    if (xq->var->rbdl_buf[1] & XQ_DSC_H) b_length -= 1;
    if (xq->var->rbdl_buf[1] & XQ_DSC_L) b_length -= 1;

    item = &xq->var->ReadQ.item[xq->var->ReadQ.head];
    rbl = item->packet.len;
    rbuf = item->packet.msg;

    /* see if packet must be size-adjusted or is splitting */
    if (item->packet.used) {
      int used = item->packet.used;
      rbl -= used;
      rbuf = &item->packet.msg[used];
    } else {
      /* adjust runt packets */
      if (rbl < ETH_MIN_PACKET) {
        xq->var->stats.runt += 1;
#ifdef XQ_DEBUG
        printf("%s: Runt detected, size = %d\n", xq->dev->name, rbl);
#endif
        /* pad runts with zeros up to minimum size - this allows "legal" (size - 60)
           processing of those weird short ARP packets that seem to occur occasionally */
        memset(&item->packet.msg[rbl], 0, ETH_MIN_PACKET);
        rbl = ETH_MIN_PACKET;
      };

      /* adjust oversized packets */
      if (rbl > ETH_MAX_PACKET) {
        xq->var->stats.giant += 1;
#ifdef XQ_DEBUG
        printf("%s: Giant detected, size = %d\n", xq->dev->name, rbl);
#endif
        /* trim giants down to maximum size - no documentation on how to handle the data loss */
        item->packet.len = ETH_MAX_PACKET;
        rbl = ETH_MAX_PACKET;
      };
    };

    /* make sure entire packet fits in buffer - if not, will need to split into multiple buffers */
    /* assert(rbl <= b_length); */ /* abort if packet won't fit into single buffer */
    if (rbl > b_length)
      rbl = b_length;
    item->packet.used += rbl;
    
    /* send data to host */
    wstatus = Map_WriteB(address, rbl, rbuf, NOMAP);
    if (wstatus) return xq_nxm_error(xq);

    /* set receive size into RBL - RBL<10:8> maps into Status1<10:8>,
       RBL<7:0> maps into Status2<7:0>, and Status2<15:8> (copy) */

    xq->var->rbdl_buf[4] = 0;
    switch (item->type) {
      case 0: /* setup packet */
        xq->var->stats.setup += 1;
        xq->var->rbdl_buf[4] = 0x2700; /* set esetup and RBL 10:8 */
        break;
      case 1: /* loopback packet */
        xq->var->stats.loop += 1;
        xq->var->rbdl_buf[4] = 0x2000;         /* loopback flag */
        xq->var->rbdl_buf[4] |= (rbl & 0x0700); /* high bits of rbl */
        break;
      case 2: /* normal packet */
        rbl -= 60;    /* keeps max packet size in 11 bits */
        xq->var->rbdl_buf[4] = (rbl & 0x0700); /* high bits of rbl */
        break;
    }
    if (item->packet.used < item->packet.len)
      xq->var->rbdl_buf[4] |= 0xC000;            /* not last segment */
    xq->var->rbdl_buf[5] = ((rbl & 0x00FF) << 8) | (rbl & 0x00FF);
    if (xq->var->ReadQ.loss) {
#ifdef XQ_DEBUG
        fprintf(stderr, "%s: ReadQ overflow\n", xq->dev->name);
#endif
      xq->var->rbdl_buf[4] |= 0x0001;   /* set overflow bit */
      xq->var->ReadQ.loss = 0;          /* reset loss counter */
    }

    /* update read status words*/
    wstatus = Map_WriteW(xq->var->rbdl_ba + 8, 4, &xq->var->rbdl_buf[4], NOMAP);
    if (wstatus) return xq_nxm_error(xq);

    /* remove packet from queue */
    if (item->packet.used >= item->packet.len)
      xq_remove_queue(&xq->var->ReadQ);

    /* reset sanity timer */
    xq_reset_santmr(xq);

    /* mark transmission complete */
    xq->var->csr |= XQ_CSR_RI;
    if (xq->var->csr & XQ_CSR_IE)
      SET_INT(XQ);

    /* set to next bdl (implicit chain) */
    xq->var->rbdl_ba += 12;

 } /* while */

  return SCPE_OK;
}

t_stat xq_process_mop(CTLR* xq)
{
  uint32 address;
  uint16 size;
  int32 wstatus;
  struct xq_meb* meb = (struct xq_meb*) &xq->var->write_buffer.msg[0200];
  const struct xq_meb* limit = (struct xq_meb*) &xq->var->write_buffer.msg[0400];

#ifdef XQ_DEBUG
  fprintf(stderr, "%s: Processing MOP data\n", xq->dev->name);
#endif
  if (xq->var->type == XQ_T_DEQNA)  /* DEQNA's don't MOP */
    return SCPE_NOFNC;

  while ((meb->type != 0) && (meb < limit)) {
    address = (meb->add_hi << 16) || (meb->add_mi << 8) || meb->add_lo;
    size    = (meb->siz_hi << 8) || meb->siz_lo;

    /* MOP stuff here - NOT YET FULLY IMPLEMENTED */

#ifdef XQ_DEBUG
    printf("%s: Processing MEB type: %d\n", xq->dev->name, meb->type);
#endif
    switch (meb->type) {
      case 0:   /* MOP Termination */
        break;
      case 1:   /* MOP Read Ethernet Address */
        wstatus = Map_WriteB(address, sizeof(ETH_MAC), (uint8*) &xq->var->setup.macs[0], NOMAP);
        if (wstatus) return xq_nxm_error(xq);
        break;
      case 2:   /* MOP Reset System ID */
        break;
      case 3:   /* MOP Read Last MOP Boot */
        break;
      case 4:   /* MOP Read Boot Password */
        break;
      case 5:   /* MOP Write Boot Password */
        break;
      case 6:   /* MOP Read System ID */
        break;
      case 7:   /* MOP Write System ID */
        break;
      case 8:   /* MOP Read Counters */
        break;
      case 9:   /* Mop Read/Clear Counters */
        break;
    } /* switch */

    /* process next meb */
    meb += sizeof(struct xq_meb);

  } /* while */
  return SCPE_OK;
}

t_stat xq_process_setup(CTLR* xq)
{
  int i,j;
  int count = 0;
  float secs;
  t_stat status;
  ETH_MAC zeros = {0, 0, 0, 0, 0, 0};
  ETH_MAC filters[XQ_FILTER_MAX + 1];

  /* extract filter addresses from setup packet */
  memset(xq->var->setup.macs, '\0', sizeof(xq->var->setup.macs));
  for (i = 0; i < 7; i++)
    for (j = 0; j < 6; j++) {
      xq->var->setup.macs[i]  [j] = xq->var->write_buffer.msg[(i +   01) + (j * 8)];
      if (xq->var->write_buffer.len > 112)
        xq->var->setup.macs[i+7][j] = xq->var->write_buffer.msg[(i + 0101) + (j * 8)];
    }

  /* process high byte count */
  if (xq->var->write_buffer.len > 128) {
    uint16 len = xq->var->write_buffer.len;
    uint16 led, san;

    if (len & XQ_SETUP_MC)
      xq->var->setup.multicast = 1;
    if (len & XQ_SETUP_PM)
      xq->var->setup.promiscuous = 1;
    if (led = (len & XQ_SETUP_LD) >> 2) {
      switch (led) {
        case 1: xq->var->setup.l1 = 0; break;
        case 2: xq->var->setup.l2 = 0; break;
        case 3: xq->var->setup.l3 = 0; break;
      } /* switch */
    } /* if led */
    /* set sanity timer timeout */
    san = (len & XQ_SETUP_ST) >> 4;
    switch(san) {
      case 0: secs = 0.25;    break;  /* 1/4 second  */
      case 1: secs = 1;       break;  /*   1 second  */
      case 2: secs = 4;       break;  /*   4 seconds */
      case 3: secs = 16;      break;  /*  16 seconds */
      case 4: secs =  1 * 60; break;  /*   1 minute  */
      case 5: secs =  4 * 60; break;  /*   4 minutes */
      case 6: secs = 16 * 60; break;  /*  16 minutes */
      case 7: secs = 64 * 60; break;  /*  64 minutes */
    }
    xq->var->sanity.quarter_secs = (int) (secs * 4);

    /* if sanity timer enabled, start sanity timer */
    if (xq->var->csr & XQ_CSR_SE || xq->var->sanity.enabled)
      xq_start_santmr(xq);
    else
      xq_cancel_santmr(xq);
  }

  /* set ethernet filter */
  /* memcpy (filters[count++], xq->mac, sizeof(ETH_MAC)); */
  for (i = 0; i < XQ_FILTER_MAX; i++)
    if (memcmp(zeros, &xq->var->setup.macs[i], sizeof(ETH_MAC)))
      memcpy (filters[count++], xq->var->setup.macs[i], sizeof(ETH_MAC));
  status = eth_filter (xq->var->etherface, count, filters, xq->var->setup.multicast, xq->var->setup.promiscuous);

  /* process MOP information */
  if (xq->var->write_buffer.msg[0])
    status = xq_process_mop(xq);

  /* mark setup block valid */
  xq->var->setup.valid = 1;

#ifdef XQ_DEBUG
  xq_debug_setup(xq);
#endif
  return SCPE_OK;
}

/*
  Dispatch Write Operation

  The DELQA manual does not explicitly state whether or not multiple packets
  can be written in one transmit operation, so a maximum of 1 packet is assumed.

*/
t_stat xq_process_xbdl(CTLR* xq)
{
  const uint16  implicit_chain_status[2] = {XQ_DSC_V | XQ_DSC_C, 1};
  const uint16  write_success[2] = {0, 1 /*Non-Zero TDR*/};

  uint16 b_length, w_length;
  int32 rstatus, wstatus;
  uint32 address;
  t_stat status;

#ifdef XQ_DEBUG
  fprintf(stderr,"%s: xq_process_xbdl - Processing write\n", xq->dev->name);
#endif
  /* clear write buffer */
  xq->var->write_buffer.len = 0;

  /* process buffer descriptors until not valid */
  while (1) {

    /* Get transmit bdl from memory */
    rstatus = Map_ReadW (xq->var->xbdl_ba,    12, &xq->var->xbdl_buf[0], NOMAP);
    xq->var->xbdl_buf[0] = 0xFFFF;
    wstatus = Map_WriteW(xq->var->xbdl_ba,     2, &xq->var->xbdl_buf[0], NOMAP);
    if (rstatus || wstatus) return xq_nxm_error(xq);

    /* invalid buffer? */
    if (~xq->var->xbdl_buf[1] & XQ_DSC_V) {
      xq->var->csr |= XQ_CSR_XL;
#ifdef XQ_DEBUG
        fprintf(stderr,"%s: xq_process_xbdl - List Empty - Done Processing write\n", xq->dev->name);
#endif
      return SCPE_OK;
    }

#ifdef XQ_DEBUG
  fprintf(stderr,"%s: xq_process_xbdl: Buffer Descriptor Information: %04X %04X %04X %04X %04X \n",
          xq->dev->name, xq->var->xbdl_buf[1], xq->var->xbdl_buf[2],
          xq->var->xbdl_buf[3], xq->var->xbdl_buf[4], xq->var->xbdl_buf[5]);
#endif
    /* compute host memory address */
    address = ((xq->var->xbdl_buf[1] & 0x3F) << 16) | xq->var->xbdl_buf[2];

    /* decode buffer length - two's complement (in words) */
    w_length = ~xq->var->xbdl_buf[3] + 1;
    b_length = w_length * 2;
    if (xq->var->xbdl_buf[1] & XQ_DSC_H) b_length -= 1;
    if (xq->var->xbdl_buf[1] & XQ_DSC_L) b_length -= 1;

    /* explicit chain buffer? */
    if (xq->var->xbdl_buf[1] & XQ_DSC_C) {
      xq->var->xbdl_ba = address;
#ifdef XQ_DEBUG
      fprintf(stderr,"%s: xq_process_xbdl: Chained Buffer Encountered: %d\n", xq->dev->name, b_length);
#endif
      continue;
    }

    /* add to transmit buffer, making sure it's not too big */
    if ((xq->var->write_buffer.len + b_length) > sizeof(xq->var->write_buffer.msg))
      b_length = sizeof(xq->var->write_buffer.msg) - xq->var->write_buffer.len;
    rstatus = Map_ReadB(address, b_length, &xq->var->write_buffer.msg[xq->var->write_buffer.len], NOMAP);
    if (rstatus) return xq_nxm_error(xq);
    xq->var->write_buffer.len += b_length;

    /* end of message? */
    if (xq->var->xbdl_buf[1] & XQ_DSC_E) {
      if (((~xq->var->csr & XQ_CSR_RE) && ((~xq->var->csr & XQ_CSR_IL) || (xq->var->csr & XQ_CSR_EL))) ||  /* loopback */
           (xq->var->xbdl_buf[1] & XQ_DSC_S)) { /* or setup packet (forces loopback regardless of state) */
        if (xq->var->xbdl_buf[1] & XQ_DSC_S) { /* setup packet */
          status = xq_process_setup(xq);

          /* put packet in read buffer */
          xq_insert_queue (&xq->var->ReadQ, 0, &xq->var->write_buffer, status);
        } else { /* loopback */
          /* put packet in read buffer */
          xq_insert_queue (&xq->var->ReadQ, 1, &xq->var->write_buffer, 0);
        }

        /* update write status */
        wstatus = Map_WriteW(xq->var->xbdl_ba + 8, 4, (uint16*) write_success, NOMAP);
        if (wstatus) return xq_nxm_error(xq);

        /* clear write buffer */
        xq->var->write_buffer.len = 0;

        /* reset sanity timer */
        xq_reset_santmr(xq);

        /* mark transmission complete */
        xq->var->csr |= XQ_CSR_XI;
        if (xq->var->csr & XQ_CSR_IE)
          SET_INT(XQ);

        /* now trigger "read" of setup or loopback packet */
        if (~xq->var->csr & XQ_CSR_RL)
          status = xq_process_rbdl(xq);

      } else { /* not loopback */

        status = eth_write(xq->var->etherface, &xq->var->write_buffer, xq->var->wcallback);
        if (status != SCPE_OK)           /* not implemented or unattached */
          xq_write_callback(xq, 1);      /* fake failure */
        else
          xq_svc(&xq->unit[0]);           /* service any received data */
#ifdef XQ_DEBUG
        fprintf(stderr,"%s: xq_process_xbdl: Completed Processing write\n", xq->dev->name);
#endif
        return SCPE_OK;

      } /* loopback/non-loopback */
    } else { /* not at end-of-message */

#ifdef XQ_DEBUG
      fprintf(stderr,"%s: xq_process_xbdl: Processing Implicit Chained Buffer Segment\n", xq->dev->name);
#endif
      /* update bdl status words */
      wstatus = Map_WriteW(xq->var->xbdl_ba + 8, 4, (uint16*) implicit_chain_status, NOMAP);
      if(wstatus) return xq_nxm_error(xq);
    }

    /* set to next bdl (implicit chain) */
    xq->var->xbdl_ba += 12;

  } /* while */
}

t_stat xq_dispatch_rbdl(CTLR* xq)
{
  int i;
  int32 rstatus, wstatus;
  t_stat status;

#ifdef XQ_DEBUG
  fprintf(stderr,"%s: CSR - Dispatching read\n", xq->dev->name);
#endif

  /* mark receive bdl valid */
  xq->var->csr &= ~XQ_CSR_RL;

  /* init receive bdl buffer */
  for (i=0; i<6; i++)
    xq->var->rbdl_buf[i] = 0;

  /* get address of first receive buffer */
  xq->var->rbdl_ba = ((xq->var->rbdl[1] & 0x3F) << 16) | (xq->var->rbdl[0] & ~01);

  /* get first receive buffer */
  xq->var->rbdl_buf[0] = 0xFFFF;
  wstatus = Map_WriteW(xq->var->rbdl_ba,     2, &xq->var->rbdl_buf[0], NOMAP);
  rstatus = Map_ReadW (xq->var->rbdl_ba + 2, 6, &xq->var->rbdl_buf[1], NOMAP);
  if (rstatus || wstatus) return xq_nxm_error(xq);

  /* is buffer valid? */
  if (~xq->var->rbdl_buf[1] & XQ_DSC_V) {
    xq->var->csr |= XQ_CSR_RL;
    return SCPE_OK;
  }

  /* process any waiting packets in receive queue */
  if (xq->var->ReadQ.count)
    status = xq_process_rbdl(xq);

  return SCPE_OK;
}

t_stat xq_dispatch_xbdl(CTLR* xq)
{
  int i;
  t_stat status;
#ifdef XQ_DEBUG
  fprintf(stderr,"%s: CSR - Dispatching write\n", xq->dev->name);
#endif
  /* mark transmit bdl valid */
  xq->var->csr &= ~XQ_CSR_XL;

  /* initialize transmit bdl buffers */
  for (i=0; i<6; i++)
    xq->var->xbdl_buf[i] = 0;

  /* clear transmit buffer */
  xq->var->write_buffer.len = 0;

  /* get base address of first transmit descriptor */
  xq->var->xbdl_ba = ((xq->var->xbdl[1] & 0x3F) << 16) | (xq->var->xbdl[0] & ~01);

  /* process xbdl */
  status = xq_process_xbdl(xq);

  return status;
}

t_stat xq_process_loopback(CTLR* xq, ETH_PACK* pack)
{
  ETH_PACK  reply;
  ETH_MAC   physical_address;
  t_stat    status;
  int offset   = pack->msg[14] | (pack->msg[15] << 8);
  int function = pack->msg[offset] | (pack->msg[offset+1] << 8);

  if (function != 2 /*forward*/)
    return SCPE_NOFNC;

  /* create reply packet */
  memcpy (&reply, pack, sizeof(ETH_PACK));
  memcpy (physical_address, xq->var->setup.valid ? xq->var->setup.macs[0] : xq->var->mac, sizeof(ETH_MAC));
  memcpy (&reply.msg[0], &reply.msg[offset+2], sizeof(ETH_MAC));
  memcpy (&reply.msg[6], physical_address, sizeof(ETH_MAC));
  memcpy (&reply.msg[offset+2], physical_address, sizeof(ETH_MAC));
  reply.msg[offset] = 0x01;
  offset += 8;
  reply.msg[14] = offset & 0xFF;
  reply.msg[15] = (offset >> 8) & 0xFF;

  /* send reply packet */
  status = eth_write(xq->var->etherface, &reply, NULL);

  return status;
}

t_stat xq_process_remote_console (CTLR* xq, ETH_PACK* pack)
{
  t_stat status;
  ETH_MAC source;
  uint16 receipt;
  int code = pack->msg[16];

  switch (code) {
    case 0x05: /* request id */
      receipt = pack->msg[18] | (pack->msg[19] << 8);
      memcpy(source, &pack->msg[6], sizeof(ETH_MAC));

      /* send system id to requestor */
      status = xq_system_id (xq, source, receipt);
      return status;
      break;
    case 0x06:  /* boot */
      /*
      NOTE: the verification field should be checked here against the
      verification value established in the setup packet. If they match the
      reboot should occur, otherwise nothing happens, and the packet
      is passed on to the host.

      Verification is not implemented, since the setup packet processing code
      isn't complete yet.

      Various values are also passed: processor, control, and software id.
      These control the various boot parameters, however SIMH does not
      have a mechanism to pass these to the host, so just reboot.
      */

      status = xq_boot_host();
      return status;
      break;
  } /* switch */

  return SCPE_NOFNC;
}

t_stat xq_process_local (CTLR* xq, ETH_PACK* pack)
{
  /* returns SCPE_OK if local processing occurred,
     otherwise returns SCPE_NOFNC or some other code */
  int protocol;

  /* DEQNA's have no local processing capability */
  if (xq->var->type == XQ_T_DEQNA)
    return SCPE_NOFNC;

  protocol = pack->msg[12] | (pack->msg[13] << 8);
  switch (protocol) {
    case 0x0090:  /* ethernet loopback */
      return xq_process_loopback(xq, pack);
      break;
    case 0x0260:  /* MOP remote console */
      return xq_process_remote_console(xq, pack);
      break;
  }
  return SCPE_NOFNC;
}

void xq_read_callback(CTLR* xq, int status)
{
  xq->var->stats.recv += 1;
  if (xq->var->csr & XQ_CSR_RE) { /* receiver enabled */

    /* process any packets locally that can be */
    t_stat status = xq_process_local (xq, &xq->var->read_buffer);

    /* add packet to read queue */
    if (status != SCPE_OK)
      xq_insert_queue(&xq->var->ReadQ, 2, &xq->var->read_buffer, status);
  }
#ifdef XQ_DEBUG
  else
    fprintf(stderr, "%s: packet received with receiver disabled\n", xq->dev->name);
#endif
}

void xqa_read_callback(int status)
{
  xq_read_callback(&xq_ctrl[0], status);
}

void xqb_read_callback(int status)
{
  xq_read_callback(&xq_ctrl[1], status);
}

void xq_sw_reset(CTLR* xq)
{
  int i;

  /* cancel all timers (ethernet, sanity, system_id) */
  for (i=0; i<3; i++)
    sim_cancel(&xq->unit[i]);

  /* reset csr bits */
  xq->var->csr = XQ_CSR_XL | XQ_CSR_RL;

  if (xq->var->etherface)
    xq->var->csr |= XQ_CSR_OK;

  /* clear CPU interrupts */
  CLR_INT(XQ);

  /* flush read queue */
  xq_clear_queue(&xq->var->ReadQ);

  /* clear setup info */
  memset (&xq->var->setup, 0, sizeof(xq->var->setup));
}

/* write registers: */

t_stat xq_wr_var(CTLR* xq, int32 data)
{

#ifdef XQ_DEBUG
  xq_var_changes(xq, data);
#endif

  switch (xq->var->type) {
    case XQ_T_DEQNA:
      xq->var->var = (data & XQ_VEC_IV);
      break;
    case XQ_T_DELQA:
      xq->var->var = (xq->var->var & XQ_VEC_RO) | (data & XQ_VEC_RW);

      /* if switching to DEQNA-LOCK mode clear VAR<14:10> */
      if (~xq->var->var & XQ_VEC_MS)
        xq->var->var &= ~(XQ_VEC_OS | XQ_VEC_RS | XQ_VEC_ST);
      break;
  }

  /* set vector of SIMH device */
  if (data & XQ_VEC_IV)
    xq->dib->vec = (data & XQ_VEC_IV) + VEC_Q;
  else
    xq->dib->vec = 0;

  return SCPE_OK;
}

#ifdef VM_PDP11
t_stat xq_process_bootrom (CTLR* xq)
{
  /*
  NOTE: BOOT ROMs are a PDP-11ism, since they contain PDP-11 binary code.
        the host is responsible for creating two *2KB* receive buffers.

  RSTS/E v10.1 source (INIONE.MAR/XHLOOK:) indicates that both the DEQNA and
  DELQA will set receive status word 1 bits 15 & 14 on both packets. It also
  states that a hardware bug in the DEQNA will set receive status word 1 bit 15
  (only) in the *third* receive buffer (oops!).

  RSTS/E v10.1 will run the Citizenship test from the bootrom after loading it.
  Documentation on the Boot ROM can be found in INIQNA.MAR.
  */

  int32 rstatus, wstatus;
  uint16 b_length, w_length;
  uint32 address;
  uint8*  bootrom = (uint8*) xq_bootrom;
  int     i, checksum;

#ifdef XQ_DEBUG
  fprintf(stderr,"%s: CSR - Processing boot rom load\n", xq->dev->name);
#endif

  /*
  RSTS/E v10.1 invokes the Citizenship tests in the Bootrom. For some
  reason, the current state of the XQ emulator cannot pass these. So,
  to get moving on RSTE/E support, we will replace the following line in
  INIQNA.MAR/CITQNA::
    70$: MOV (R2),R0 ;get the status word
  with
    70$: CLR R0      ;force success
  to cause the Citizenship test to return success to RSTS/E.

  At some point, the real problem (failure to pass citizenship diagnostics)
  does need to be corrected to find incompatibilities in the emulation, and to
  ultimately allow it to pass Digital hardware diagnostic tests.
  */
  for (i=0; i<sizeof(xq_bootrom)/2; i++)
    if (xq_bootrom[i] == 011200) {  /* MOV (R2),R0 */
      xq_bootrom[i] = 005000;       /* CLR R0 */
      break;
    }

  /* recalculate checksum, which is a simple byte sum */
  for (i=0, checksum=0; i<sizeof(xq_bootrom)-2; i++)
    checksum += bootrom[i];

  /* set new checksum */
  xq_bootrom[sizeof(xq_bootrom)/2-1] = checksum;

  /* --------------------------- bootrom part 1 -----------------------------*/

  /* get receive bdl from memory */
  xq->var->rbdl_buf[0] = 0xFFFF;
  wstatus = Map_WriteW(xq->var->rbdl_ba,     2, &xq->var->rbdl_buf[0], NOMAP);
  rstatus = Map_ReadW (xq->var->rbdl_ba + 2, 6, &xq->var->rbdl_buf[1], NOMAP);
  if (rstatus || wstatus) return xq_nxm_error(xq);

  /* invalid buffer? */
  if (~xq->var->rbdl_buf[1] & XQ_DSC_V) {
    xq->var->csr |= XQ_CSR_RL;
    return SCPE_OK;
  }

  /* get status words */
  rstatus = Map_ReadW(xq->var->rbdl_ba + 8, 4, &xq->var->rbdl_buf[4], NOMAP);
  if (rstatus) return xq_nxm_error(xq);

  /* get host memory address */
  address = ((xq->var->rbdl_buf[1] & 0x3F) << 16) | xq->var->rbdl_buf[2];

#ifdef XQ_DEBUG
  fprintf(stderr,"%s: BootRom1 load address: 0%o\n", xq->dev->name, address);
#endif

  /* decode buffer length - two's complement (in words) */
  w_length = ~xq->var->rbdl_buf[3] + 1;
  b_length = w_length * 2;
  if (xq->var->rbdl_buf[1] & XQ_DSC_H) b_length -= 1;
  if (xq->var->rbdl_buf[1] & XQ_DSC_L) b_length -= 1;

  /* make sure entire packet fits in buffer */
  assert(b_length >= sizeof(xq_bootrom)/2);

  /* send data to host */
  wstatus = Map_WriteB(address, sizeof(xq_bootrom)/2, bootrom, NOMAP);
  if (wstatus) return xq_nxm_error(xq);

  /* update read status words */
  xq->var->rbdl_buf[4] = XQ_DSC_V | XQ_DSC_C;   /* valid, chain */
  xq->var->rbdl_buf[5] = 0;

  /* update read status words*/
  wstatus = Map_WriteW(xq->var->rbdl_ba + 8, 4, &xq->var->rbdl_buf[4], NOMAP);
  if (wstatus) return xq_nxm_error(xq);

  /* set to next bdl (implicit chain) */
  xq->var->rbdl_ba += 12;

  /* --------------------------- bootrom part 2 -----------------------------*/

  /* get receive bdl from memory */
  xq->var->rbdl_buf[0] = 0xFFFF;
  wstatus = Map_WriteW(xq->var->rbdl_ba,     2, &xq->var->rbdl_buf[0], NOMAP);
  rstatus = Map_ReadW (xq->var->rbdl_ba + 2, 6, &xq->var->rbdl_buf[1], NOMAP);
  if (rstatus || wstatus) return xq_nxm_error(xq);

  /* invalid buffer? */
  if (~xq->var->rbdl_buf[1] & XQ_DSC_V) {
    xq->var->csr |= XQ_CSR_RL;
    return SCPE_OK;
  }

  /* get status words */
  rstatus = Map_ReadW(xq->var->rbdl_ba + 8, 4, &xq->var->rbdl_buf[4], NOMAP);
  if (rstatus) return xq_nxm_error(xq);

  /* get host memory address */
  address = ((xq->var->rbdl_buf[1] & 0x3F) << 16) | xq->var->rbdl_buf[2];

#ifdef XQ_DEBUG
  fprintf(stderr,"%s: BootRom2 load address: 0%o\n", xq->dev->name, address);
#endif

  /* decode buffer length - two's complement (in words) */
  w_length = ~xq->var->rbdl_buf[3] + 1;
  b_length = w_length * 2;
  if (xq->var->rbdl_buf[1] & XQ_DSC_H) b_length -= 1;
  if (xq->var->rbdl_buf[1] & XQ_DSC_L) b_length -= 1;

  /* make sure entire packet fits in buffer */
  assert(b_length >= sizeof(xq_bootrom)/2);

  /* send data to host */
  wstatus = Map_WriteB(address, sizeof(xq_bootrom)/2, &bootrom[2048], NOMAP);
  if (wstatus) return xq_nxm_error(xq);

  /* update read status words */
  xq->var->rbdl_buf[4] = XQ_DSC_V | XQ_DSC_C;   /* valid, chain */
  xq->var->rbdl_buf[5] = 0;

  /* update read status words*/
  wstatus = Map_WriteW(xq->var->rbdl_ba + 8, 4, &xq->var->rbdl_buf[4], NOMAP);
  if (wstatus) return xq_nxm_error(xq);

  /* set to next bdl (implicit chain) */
  xq->var->rbdl_ba += 12;

  /* --------------------------- bootrom part 3 -----------------------------*/

  switch (xq->var->type) {
    case XQ_T_DEQNA:

      /* get receive bdl from memory */
      xq->var->rbdl_buf[0] = 0xFFFF;
      wstatus = Map_WriteW(xq->var->rbdl_ba,     2, &xq->var->rbdl_buf[0], NOMAP);
      rstatus = Map_ReadW (xq->var->rbdl_ba + 2, 6, &xq->var->rbdl_buf[1], NOMAP);
      if (rstatus || wstatus) return xq_nxm_error(xq);

      /* invalid buffer? */
      if (~xq->var->rbdl_buf[1] & XQ_DSC_V) {
        xq->var->csr |= XQ_CSR_RL;
        return SCPE_OK;
      }

      /* get status words */
      rstatus = Map_ReadW(xq->var->rbdl_ba + 8, 4, &xq->var->rbdl_buf[4], NOMAP);
      if (rstatus) return xq_nxm_error(xq);

      /* update read status words */
      xq->var->rbdl_buf[4] = XQ_DSC_V;   /* valid */
      xq->var->rbdl_buf[5] = 0;

      /* update read status words*/
      wstatus = Map_WriteW(xq->var->rbdl_ba + 8, 4, &xq->var->rbdl_buf[4], NOMAP);
      if (wstatus) return xq_nxm_error(xq);

      /* set to next bdl (implicit chain) */
      xq->var->rbdl_ba += 12;
      break;
  } /* switch */

  /* --------------------------- Done, finish up -----------------------------*/

  /* mark transmission complete */
  xq->var->csr |= XQ_CSR_RI;
  if (xq->var->csr & XQ_CSR_IE)
    SET_INT(XQ);

  /* reset sanity timer */
  xq_reset_santmr(xq);

  return SCPE_OK;
}
#endif /* ifdef VM_PDP11 */

t_stat xq_wr_csr(CTLR* xq, int32 data)
{
#ifdef VM_PDP11
  static const uint16 bd_bits_on = XQ_CSR_BD | XQ_CSR_EL;
#endif
  int old_int_state, new_int_state;
  const uint16 saved_csr = xq->var->csr;

#ifdef XQ_DEBUG
  xq_csr_changes(xq, data);
#endif

  /* reset controller when SR transitions to cleared */
  if (xq->var->csr & XQ_CSR_SR & ~data) {
    xq_sw_reset(xq);
    return SCPE_OK;
  }

  /* write the writeable bits */
  xq->var->csr = (xq->var->csr & XQ_CSR_RO) | (data & XQ_CSR_RW);

  /* clear write-one-to-clear bits */
  xq->var->csr &= ~(data & XQ_CSR_W1);
  if (data & XQ_CSR_XI)         /* clearing XI clears NI too */
    xq->var->csr &= ~XQ_CSR_NI;

  /* start receiver timer when RE transitions to set */
  if (~saved_csr & XQ_CSR_RE & data) {
    sim_activate(&xq->unit[0], (clk_tps * tmr_poll)/100);
  }

  /* stop receiver timer when RE transitions to clear */
  if (saved_csr & XQ_CSR_RE & ~data) {
    sim_cancel(&xq->unit[0]);
  }

  /* check and correct CPU interrupt state */
  old_int_state = (saved_csr      & XQ_CSR_IE) && (saved_csr    & (XQ_CSR_XI | XQ_CSR_RI));
  new_int_state = (xq->var->csr   & XQ_CSR_IE) && (xq->var->csr & (XQ_CSR_XI | XQ_CSR_RI));
  if ( old_int_state && !new_int_state) CLR_INT(XQ);
  if (!old_int_state &&  new_int_state) SET_INT(XQ);

#ifdef VM_PDP11
  /* request boot/diagnostic rom? [PDP-11 only] */
  if ((bd_bits_on & data) == bd_bits_on)  /* all bits must be on */
    xq_process_bootrom(xq);
#endif

  return SCPE_OK;
}

t_stat xq_wr(int32 data, int32 PA, int32 access)
{
  t_stat status;
  CTLR* xq = xq_pa2ctlr(PA);
  int index = (PA >> 1) & 07;   /* word index */

#ifdef XQ_DEBUG
  if (index != 7)
    fprintf (stderr,"%s: %s", xq->dev->name, xq_xmit_regnames[index]);
#if defined(VM_VAX)
    fprintf (stderr," %08X %08X", fault_PC, PSL);
#endif /* VM_VAX */
    fprintf (stderr," write: %X\n", data);
#endif

  switch (index) {
    case 0:   /* these should not be written */
    case 1:
      break;
    case 2:   /* receive bdl low bits */
      xq->var->rbdl[0] = data;
      break;
    case 3:   /* receive bdl high bits */
      xq->var->rbdl[1] = data;
      status = xq_dispatch_rbdl(xq); /* start receive operation */
      break;
    case 4:   /* transmit bdl low bits */
      xq->var->xbdl[0] = data;
      break;
    case 5:   /* transmit bdl high bits */
      xq->var->xbdl[1] = data;
      status = xq_dispatch_xbdl(xq); /* start transmit operation */
      break;
    case 6:   /* vector address register */
      status = xq_wr_var(xq, data);
      break;
    case 7:   /* control and status register */
      status = xq_wr_csr(xq, data);
      break;
  }
  return SCPE_OK;
}


/* reset device */
t_stat xq_reset(DEVICE* dptr)
{
  t_stat status;
  CTLR* xq = xq_dev2ctlr(dptr);

  /* calculate MAC checksum */
  xq_make_checksum(xq);

  /* init vector address register */
  switch (xq->var->type) {
    case XQ_T_DEQNA:
      xq->var->var = 0;
      break;
    case XQ_T_DELQA:
      xq->var->var = XQ_VEC_MS | XQ_VEC_OS;
      break;
  }
  xq->dib->vec = 0;

  /* init control status register */
  xq->var->csr = XQ_CSR_RL | XQ_CSR_XL;

  /* reset ethernet interface */
  if (xq->var->etherface) {
    status = eth_filter (xq->var->etherface, 1, &xq->var->mac, 0, 0);
    xq->var->csr |= XQ_CSR_OK;
  }

  /* init read queue (first time only) */
  status = xq_init_queue (xq, &xq->var->ReadQ);
  if (status != SCPE_OK)
    return status;

  /* clear read queue */
  xq_clear_queue(&xq->var->ReadQ);

  /* start sanity timer if power-on SANITY is set */
  switch (xq->var->type) {
    case XQ_T_DEQNA:
      if (xq->var->sanity.enabled) {
        xq->var->sanity.quarter_secs = 4 * (4 * 60);   /* default is 4 minutes */;
        xq_start_santmr(xq);
      }
      break;
    case XQ_T_DELQA:
      /* note that the DELQA in NORMAL mode has no power-on SANITY state! */
      xq_start_idtmr(xq);
      break;
  };

  return SCPE_OK;
}

void xq_start_santmr(CTLR* xq)
{
  UNIT* xq_santmr = &xq->unit[1];     /* sanity timer uses unit 1 */

  /* must be recalculated each time since tmr_poll is a dynamic number */
  const int32 quarter_sec = (clk_tps * tmr_poll) / 4;

#if 0
#ifdef XQ_DEBUG
  fprintf(stderr,"%s: SANITY TIMER ENABLED, qsecs: %d, poll:%d\n",
    xq->dev->name, xq->var->sanity.quarter_secs, tmr_poll);
#endif
#endif
  if (sim_is_active(xq_santmr))   /* cancel timer, just in case */
    sim_cancel(xq_santmr);
  xq_reset_santmr(xq);
  sim_activate(xq_santmr, quarter_sec);
}

void xq_cancel_santmr(CTLR* xq)
{
  UNIT* xq_santmr = &xq->unit[1];     /* sanity timer uses unit 1 */

  /* can't cancel hardware switch sanity timer */
  if (sim_is_active(xq_santmr) && !xq->var->sanity.enabled) {
#if 0
#ifdef XQ_DEBUG
    fprintf(stderr,"%s: SANITY TIMER CANCELLED, qsecs: %d\n",
      xq->dev->name, xq->var->sanity.quarter_secs);
#endif
#endif
    sim_cancel(xq_santmr);
  }
}

void xq_reset_santmr(CTLR* xq)
{
#if 0
#ifdef XQ_DEBUG
  ftime(&start);
  fprintf(stderr,"%s: SANITY TIMER RESETTING, qsecs: %d\n",
    xq->dev->name, xq->var->sanity.quarter_secs);
#endif
#endif
  xq->var->sanity.countdown = xq->var->sanity.quarter_secs;
}

t_stat xq_sansvc(UNIT* uptr)
{
  CTLR* xq = xq_unit2ctlr(uptr);
  UNIT* xq_santmr = &xq->unit[1];     /* sanity timer uses unit 1 */

  if (--xq->var->sanity.countdown) {
    /* must be recalculated each time since tmr_poll is a dynamic number */
    const int32 quarter_sec = (clk_tps * tmr_poll) / 4;

    /* haven't hit the end of the countdown timer yet, resubmit */
    sim_activate(xq_santmr, quarter_sec);
  } else {
    /*
    If this section is entered, it means that the sanity timer has expired
    without being reset, and the controller must reboot the processor.
    */
#if 0
#ifdef XQ_DEBUG
    ftime(&finish);
    fprintf(stderr,"%s: SANITY TIMER EXPIRED, qsecs: %d, poll: %d, elapsed: %d\n",
           xq->dev->name, xq->var->sanity.quarter_secs, tmr_poll, finish.time - start.time);
#endif
#endif
    xq_boot_host();
  }
  return SCPE_OK;
}

t_stat xq_boot_host(void)
{
  /*
  The manual says the hardware should force the Qbus BDCOK low for
  3.6 microseconds, which will cause the host to reboot.

  Since the SIMH Qbus emulator does not have this functionality, we call
  a special STOP_ code, and let the CPU stop dispatch routine decide
  what the appropriate cpu-specific behavior should be.
  */
  return STOP_SANITY;
}

void xq_start_idtmr(CTLR* xq)
{
  UNIT* xq_idtmr = &xq->unit[2];     /* system id timer uses unit 2 */

  /* must be recalculated each time since tmr_poll is a dynamic number */
  const int32 one_sec = clk_tps * tmr_poll;

  if (sim_is_active(xq_idtmr))   /* cancel timer, just in case */
    sim_cancel(xq_idtmr);
  xq->var->id.enabled = 1;
  /* every 8-10 minutes (9 in this case) the DELQA broadcasts a system id message */
  xq->var->id.countdown = 9 * 60;
  sim_activate(xq_idtmr, one_sec);
}

t_stat xq_system_id (CTLR* xq, const ETH_MAC dest, uint16 receipt_id)
{
  static uint16 receipt = 0;
  ETH_PACK system_id;
  uint8* const msg = &system_id.msg[0];
  t_stat status;

  memset (&system_id, 0, sizeof(system_id));
  memcpy (&msg[0], dest, sizeof(ETH_MAC));
  memcpy (&msg[6], xq->var->setup.valid ? xq->var->setup.macs[0] : xq->var->mac, sizeof(ETH_MAC));
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
  msg[24] = 0x01;                         /* eco */
  msg[25] = 0x00;                         /* user eco */

                                          /* FUNCTION */
  msg[26] = 0x02;                         /* type */
  msg[27] = 0x00;                         /* type */
  msg[28] = 0x02;                         /* length */
  msg[29] = 0x00;                         /* value 1 ??? */
  msg[30] = 0x00;                         /* value 2 */

                                          /* HARDWARE ADDRESS */
  msg[31] = 0x07;                         /* type */
  msg[32] = 0x00;                         /* type */
  msg[33] = 0x06;                         /* length */
  memcpy (&msg[34], xq->var->mac, sizeof(ETH_MAC)); /* ROM address */

                                          /* DEVICE TYPE */
  msg[40] = 37;                           /* type */
  msg[41] = 0x00;                         /* type */
  msg[42] = 0x01;                         /* length */
  msg[43] = 0x11;                         /* value (0x11=DELQA) */

  /* write system id */
  system_id.len = 60;
  status = eth_write(xq->var->etherface, &system_id, NULL);

  return status;
}

t_stat xq_idsvc(UNIT* uptr)
{
  CTLR* xq = xq_unit2ctlr(uptr);
  UNIT* xq_idtmr = &xq->unit[2];     /* system id timer uses unit 2 */

  /* must be recalculated each time since tmr_poll is a dynamic number */
  const int32 one_sec = clk_tps * tmr_poll;
  const ETH_MAC mop_multicast = {0xAB, 0x00, 0x00, 0x02, 0x00, 0x00};

  /* DEQNAs don't issue system id messages */
  if (xq->var->type == XQ_T_DEQNA)
    return SCPE_NOFNC;

  if (--xq->var->id.countdown <= 0) {
    /*
    If this section is entered, it means that the 9 minute interval has elapsed
    so broadcast system id to MOP multicast address
    */
    xq_system_id(xq, mop_multicast, 0);
    /* every 8-10 minutes (9 in this case) the DELQA broadcasts a system id message */
    xq->var->id.countdown = 9 * 60;
  }

  /* resubmit - for one second to get a well calibrated value of tmr_poll */
  sim_activate(xq_idtmr, one_sec);
  return SCPE_OK;
}

/*
** service routine - used for ethernet reading loop
*/
t_stat xq_svc(UNIT* uptr)
{
  t_stat status;
  int queue_size;
  CTLR* xq = xq_unit2ctlr(uptr);
  UNIT* xq_svctmr = &xq->unit[0];

  /* Don't try a read if the receiver is disabled */
  if (!(xq->var->csr & XQ_CSR_RE)) return SCPE_OK;

  /* First pump any queued packets into the system */
  if ((xq->var->ReadQ.count > 0) && (~xq->var->csr & XQ_CSR_RL))
    status = xq_process_rbdl(xq);

  /* Now read and queue packets that have arrived */
  /* This is repeated as long as they are available and we have room */
  do
    {
    queue_size = xq->var->ReadQ.count;
    /* read a packet from the ethernet - processing is via the callback */
    status = eth_read (xq->var->etherface, &xq->var->read_buffer, xq->var->rcallback);
  } while (queue_size != xq->var->ReadQ.count);

  /* Now pump any still queued packets into the system */
  if ((xq->var->ReadQ.count > 0) && (~xq->var->csr & XQ_CSR_RL))
    status = xq_process_rbdl(xq);

  /* resubmit if still receive enabled */
  if (xq->var->csr & XQ_CSR_RE)
    sim_activate(xq_svctmr, (clk_tps * tmr_poll)/100);

  return SCPE_OK;
}



/* attach device: */
t_stat xq_attach(UNIT* uptr, char* cptr)
{
  t_stat status;
  char* tptr;
  CTLR* xq = xq_unit2ctlr(uptr);

  tptr = malloc(strlen(cptr) + 1);
  if (tptr == NULL) return SCPE_MEM;
  strcpy(tptr, cptr);

  xq->var->etherface = malloc(sizeof(ETH_DEV));
  if (!xq->var->etherface) return SCPE_MEM;

  status = eth_open(xq->var->etherface, cptr);
  if (status != SCPE_OK) {
    free(tptr);
    free(xq->var->etherface);
    xq->var->etherface = 0;
    return status;
  }
  uptr->filename = tptr;
  uptr->flags |= UNIT_ATT;

  /* turn on transceiver power indicator */
  xq->var->csr |= XQ_CSR_OK;

  return SCPE_OK;
}

/* detach device: */

t_stat xq_detach(UNIT* uptr)
{
  t_stat status;
  CTLR* xq = xq_unit2ctlr(uptr);

  if (uptr->flags & UNIT_ATT) {
    status = eth_close (xq->var->etherface);
    free(xq->var->etherface);
    xq->var->etherface = 0;
    free(uptr->filename);
    uptr->filename = NULL;
    uptr->flags &= ~UNIT_ATT;
  }

  /* turn off transceiver power indicator */
  xq->var->csr &= ~XQ_CSR_OK;

  return SCPE_OK;
}

int32 xq_inta (void)
{
  return xqa_dib.vec;
}

int32 xq_intb (void)
{
  return xqb_dib.vec;
}

/*==============================================================================
/                               debugging routines
/=============================================================================*/

#ifdef XQ_DEBUG

void xq_dump_csr (CTLR* xq)
{
 static int cnt  = 0;
  /* tell user what is changing in register */
  int i;
  int mask = 1;
  uint16 csr = xq->var->csr;
  char hi[256] = "Set: ";
  char lo[256] = "Reset: ";
  for (i=0; i<16; i++, mask <<= 1) {
    if ((csr & mask))  strcat (hi, xq_csr_bits[i]);
    if ((~csr & mask)) strcat (lo, xq_csr_bits[i]);
  }
#if defined (VM_VAX)
  printf ("%s: CSR %08X %08X read: %s %s\n", xq->dev->name, fault_PC, PSL, hi, lo);
#else
if (cnt < 20)
  printf ("%s: CSR read[%d]: %s %s\n", xq->dev->name, cnt++, hi, lo);
#endif /* VM_VAX */
}

void xq_dump_var (CTLR* xq)
{
  /* tell user what is changing in register */
  uint16 var = xq->var->var;
  char hi[256] = "Set: ";
  char lo[256] = "Reset: ";
  int vec = (var & XQ_VEC_IV) >> 2;
  strcat((var & XQ_VEC_MS) ? hi : lo, "MS ");
  strcat((var & XQ_VEC_OS) ? hi : lo, "OS ");
  strcat((var & XQ_VEC_RS) ? hi : lo, "RS ");
  strcat((var & XQ_VEC_S3) ? hi : lo, "S3 ");
  strcat((var & XQ_VEC_S2) ? hi : lo, "S2 ");
  strcat((var & XQ_VEC_S1) ? hi : lo, "S1 ");
  strcat((var & XQ_VEC_RR) ? hi : lo, "RR ");
  strcat((var & XQ_VEC_ID) ? hi : lo, "ID ");
  printf ("%s: VAR read: %s %s - Vec: %d \n", xq->dev->name, hi, lo, vec);
}

void xq_csr_changes (CTLR* xq, uint16 data)
{
  /* tell user what is changing in register */
  int i;
  int mask = 1;
  uint16 csr = xq->var->csr;
  char hi[256] = "Setting: ";
  char lo[256] = "Resetting: ";
  for (i=0; i<16; i++, mask <<= 1) {
    if ((csr & mask) && (~data & mask)) strcat (lo, xq_csr_bits[i]);
    if ((~csr & mask) && (data & mask)) strcat (hi, xq_csr_bits[i]);
  }
  /* write-one-to-clear bits*/
  if (data & XQ_CSR_RI) strcat(lo, "RI ");
  if (data & XQ_CSR_XI) strcat(lo, "XI ");
#if defined(VM_VAX)
  printf ("%s: CSR %08X %08X write: %s %s\n", xq->dev->name, fault_PC, PSL, hi, lo);
#else
  printf ("%s: CSR write: %s %s\n", xq->dev->name, hi, lo);
#endif /* VM_VAX */
}

void xq_var_changes (CTLR* xq, uint16 data)
{
  /* tell user what is changing in register */
  uint16 vec;
  uint16 var = xq->var->var;
  char hi[256] = "Setting: ";
  char lo[256] = "Resetting: ";
  if (~var & XQ_VEC_MS & data) strcat (hi, "MS ");
  if (var & XQ_VEC_MS & ~data) strcat (lo, "MS ");
  if (~var & XQ_VEC_OS & data) strcat (hi, "OS ");
  if (var & XQ_VEC_OS & ~data) strcat (lo, "OS ");
  if (~var & XQ_VEC_RS & data) strcat (hi, "RS ");
  if (var & XQ_VEC_RS & ~data) strcat (lo, "RS ");
  if (~var & XQ_VEC_ID & data) strcat (hi, "ID ");
  if (var & XQ_VEC_ID & ~data) strcat (lo, "ID ");

  if ((var & XQ_VEC_IV) != (data & XQ_VEC_IV)) {
    vec = (data & XQ_VEC_IV) >> 2;
    printf ("%s: VAR write: %s %s - Vec: %d\n", xq->dev->name, hi, lo, vec);
  } else
    printf ("%s: VAR write: %s %s\n", xq->dev->name, hi, lo);
}

void xq_debug_setup(CTLR* xq)
{
  int   i;
  char  buffer[20];
  if (xq->var->write_buffer.msg[0])
    printf ("%s: Setup: MOP info present!\n", xq->dev->name);

  for (i = 0; i < XQ_FILTER_MAX; i++) {
    eth_mac_fmt(&xq->var->setup.macs[i], buffer);
    printf ("%s: Setup: set addr[%d]: %s\n", xq->dev->name, i, buffer);
  }

  if (xq->var->write_buffer.len > 128) {
    char buffer[20] = {0};
    uint16 len = xq->var->write_buffer.len;
    if (len & XQ_SETUP_MC) strcat(buffer, "MC ");
    if (len & XQ_SETUP_PM) strcat(buffer, "PM ");
    if (len & XQ_SETUP_LD) strcat(buffer, "LD ");
    if (len & XQ_SETUP_ST) strcat(buffer, "ST ");
    printf ("%s: Setup: Length [%d =0x%X, LD:%d, ST:%d] info: %s\n",
      xq->dev->name, len, len, (len & XQ_SETUP_LD) >> 2, (len & XQ_SETUP_ST) >> 4, buffer);
  }
}
#endif
