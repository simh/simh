/* pdp11_xq.c: DEQNA/DELQA ethernet controller simulator
  ------------------------------------------------------------------------------

   Copyright (c) 2002, David T. Hittner

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
      The default MAC address is 08-00-2B-AA-BB-CC unless set otherwise.
      Ethernet transceiver power flag CSR<12> is ON when attached.
      External Loopback does not go out to the physical adapter, it is
        implemented more like an extended Internal Loopback
      Time Domain Reflectometry (TDR) numbers are faked
      The 10-second approx. hardware/software reset delay does not exist
      Some physical ethernet receive events like Runts, Overruns, etc. are
        never reported back, since the packet-level driver never sees them

  Certain advantages are derived from this emulation:
      If the real ethernet controller is faster than 10Mbit, the speed is
        passed on since there are no minimum response times.

  Known Bugs or Unsupported features:
    1) DEQNA and DEQNA-LOCK modes not implemented fully [done! 07-Jan-03]
    2) Sanity Timer not implemented                     [done! 16-Oct-02]
    3) MOP functionality not implemented
    4) Multicast support is weak                        [done! 22-Oct-02]
    5) Promiscuous mode not implemented                 [done! 22-Oct-02]
    6) Cannot VMScluster node                           [done! 27-Dec-02]
    7) Cannot bootstrap module on VAX (>>> B XQA0)      [done! 12-Oct-02]
    8) PDP11 bootstrap code missing
    9) Automatic ID broadcast every 8-10 minutes        [done! 30-Dec-02]
   10) External loopback packet processing              [done! 02-Jan-03]
   11) NXM detection/protection                         [done! 21-Oct-02]

  ------------------------------------------------------------------------------

  Modification history:

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
                  Fixed off-by-1 bug on xq.setup.macs[7..13]
                  Added make_checksum
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

extern int32 int_req[IPL_HLVL];
extern int32 tmr_poll, clk_tps;

struct xq_device    xq = {
  2500,                                     /* rtime */
  {0x08, 0x00, 0x2B, 0xAA, 0xBB, 0xCC},     /* mac */
  XQ_T_DELQA,                               /* type */
  {0}                                       /* sanity */
  };


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
t_stat xq_show_type (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xq_set_type (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat xq_show_sanity (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xq_set_sanity (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat xq_show_bootrom (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xq_set_bootrom (UNIT* uptr, int32 val, char* cptr, void* desc);
t_stat xq_showeth (FILE* st, UNIT* uptr, int32 val, void* desc);
t_stat xq_process_xbdl(void);
t_stat xq_dispatch_xbdl(void);
void xq_start_receiver(void);
void xq_sw_reset(void);
int32 xq_inta (void);
t_stat xq_ex (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat xq_dep (t_value val, t_addr addr, UNIT *uptr, int32 sw);
void xq_start_santmr(void);
void xq_cancel_santmr(void);
void xq_reset_santmr(void);
t_stat xq_boot_host(void);
void xq_start_idtmr(void);
t_stat xq_system_id(const ETH_MAC dst, uint16 receipt_id);

/* SIMH device structures */
DIB xq_dib = { IOBA_XQ, IOLN_XQ, &xq_rd, &xq_wr,
		1, IVCL (XQ), 0, { &xq_inta } };

UNIT xq_unit[] = {
 { UDATA (&xq_svc, UNIT_ATTABLE + UNIT_DISABLE, 0) },     /* receive timer */
 { UDATA (&xq_sansvc, UNIT_DIS, 0) },                     /* sanity timer */
 { UDATA (&xq_idsvc, UNIT_DIS, 0) }                       /* system id timer */
};

REG xq_reg[] = {
  { GRDATA ( SA0,  xq.addr[0], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA1,  xq.addr[1], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA2,  xq.addr[2], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA3,  xq.addr[3], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA4,  xq.addr[4], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( SA5,  xq.addr[5], XQ_RDX, 16, 0), REG_RO},
  { GRDATA ( RBDL, xq.rbdl, XQ_RDX, 32, 0) },
  { GRDATA ( XBDL, xq.xbdl, XQ_RDX, 32, 0) },
  { GRDATA ( VAR,  xq.var,  XQ_RDX, 16, 0) },
  { GRDATA ( CSR,  xq.csr,  XQ_RDX, 16, 0) },
  { GRDATA ( SETUP_PRM, xq.setup.promiscuous, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_MLT, xq.setup.multicast, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_L1, xq.setup.l1, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_L2, xq.setup.l2, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_L3, xq.setup.l3, XQ_RDX, 32, 0), REG_HRO},
  { GRDATA ( SETUP_SAN, xq.setup.sanity_timer, XQ_RDX, 32, 0), REG_HRO},
  { BRDATA ( SETUP_MACS, &xq.setup.macs, XQ_RDX, 8, sizeof(xq.setup.macs)), REG_HRO},
  { NULL },
};

MTAB xq_mod[] = {
#if defined (VM_PDP11)
	{ MTAB_XTD|MTAB_VDV, 004, "ADDRESS", "ADDRESS",
		&set_addr, &show_addr, NULL },
  { MTAB_XTD | MTAB_VDV | MTAB_NMO | MTAB_NC, 0, "BOOTROM", "BOOTROM",
    &xq_set_bootrom, &xq_show_bootrom, NULL },
#else
	{ MTAB_XTD|MTAB_VDV, 004, "ADDRESS", NULL,
		NULL, &show_addr, NULL },
#endif
	{ MTAB_XTD|MTAB_VDV, 0, "VECTOR", NULL,
		NULL, &show_vec, NULL },
  { MTAB_XTD | MTAB_VDV, 0, "MAC", "MAC",
    &xq_setmac, &xq_showmac, &xq.mac },
  { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "ETH", NULL,
    NULL, &xq_showeth, NULL },
  { MTAB_XTD | MTAB_VDV, 0, "TYPE", "TYPE",
    &xq_set_type, &xq_show_type, NULL },
  { MTAB_XTD | MTAB_VDV|MTAB_NMO, 0, "SANITY", "SANITY",
    &xq_set_sanity, &xq_show_sanity, NULL },
  { 0 },
};

DEVICE xq_dev = {
  "XQ", xq_unit, xq_reg, xq_mod,
  3, XQ_RDX, 0, 1, XQ_RDX, 8,
  &xq_ex, &xq_dep, &xq_reset,
  NULL, &xq_attach, &xq_detach,
  &xq_dib, DEV_DISABLE | DEV_QBUS
};

#ifdef XQ_DEBUG
#if defined(VM_VAX)
extern int32 PSL;           /* PSL */
extern int32 fault_PC;      /* fault PC */
#endif

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
void xq_debug_setup(void);
void xq_dump_csr(void);
void xq_dump_var(void);
void xq_csr_changes(uint16 data);
void xq_var_changes(uint16 data);

/* sanity timer debugging */
#include <sys\timeb.h>
struct timeb start, finish;

#endif /* XQ_DEBUG */

/*
================================================================================
                              Queue Management
================================================================================
*/

void xq_clear_queue(struct xq_msg_que* que)
{
  int i;
  struct xq_msg_itm* item;

  for (i = 0; i < XQ_QUE_MAX; i++) {
    item = &que->item[i];
    item->type = 0;
    item->packet.len = 0;
    item->status = 0;
  }
  que->count = que->head = que->tail = que->loss = 0;
}

void xq_remove_queue(struct xq_msg_que* que)
{
  struct xq_msg_itm* item = &que->item[que->head];

  if (que->count) {
    item->type = 0;
    item->packet.len = 0;
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

  /* set information in (new) tail item */
  item = &que->item[que->tail];
  item->type = type;
  item->packet.len = packet->len;
  memcpy(item->packet.msg, packet->msg, packet->len);
  item->status = status;
}

/*
================================================================================
*/


/* stop simh from reading non-existant unit data stream */
t_stat xq_ex (t_value* vptr, t_addr addr, UNIT* uptr, int32 sw)
{
  return SCPE_NOFNC;
}

/* stop simh from writing non-existant unit data stream */
t_stat xq_dep (t_value val, t_addr addr, UNIT* uptr, int32 sw)
{
  return SCPE_NOFNC;
}

t_stat xq_showmac (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  ETH_MAC* mac = (ETH_MAC*) desc;
  char  buffer[20];

  if (!desc) return SCPE_IERR;
  eth_mac_fmt(mac, buffer);

  fprintf(st, "MAC=%s", buffer);
  return SCPE_OK;
}

void make_checksum(void)
{
  /* checksum calculation routine detailed in vaxboot.zip/xqbtdrivr.mar */
  uint32  checksum = 0;
  const uint32 wmask = 0xFFFF;
  int i;

  for (i = 0; i < sizeof(ETH_MAC); i += 2) {
    checksum <<= 1;
    if (checksum > wmask)
      checksum -= wmask;
    checksum += (xq.mac[i] << 8) | xq.mac[i+1];
    if (checksum > wmask)
      checksum -= wmask;
  }
  if (checksum == wmask)
    checksum = 0;

  /* set checksum bytes */
  xq.mac_checksum[0] = checksum & 0xFF;
  xq.mac_checksum[1] = checksum >> 8;
}

t_stat xq_setmac (UNIT* uptr, int32 val, char* cptr, void* desc)
{
  int i, j, len;
  short int num;
  ETH_MAC newmac = {0,0,0,0,0,0};
  const ETH_MAC zeros = {0,0,0,0,0,0};
  const ETH_MAC ones = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  if ((!cptr) || (!desc)) return SCPE_IERR;
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
  memcpy(xq.mac, newmac, sizeof(ETH_MAC));
  /* calculate MAC checksum */
  make_checksum();
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

t_stat xq_show_type (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  fprintf(st, "type=");
  switch (xq.type) {
    case  XQ_T_DEQNA:       fprintf(st, "DEQNA");      break;
    case  XQ_T_DELQA:       fprintf(st, "DELQA");      break;
  }
  return SCPE_OK;
}

t_stat xq_set_type (UNIT* uptr, int32 val, char* cptr, void* desc)
{
  if (!cptr) return SCPE_IERR;

  /* this assumes that the parameter has already been upcased */
  if      (!strcmp(cptr, "DEQNA"))      xq.type = XQ_T_DEQNA;
  else if (!strcmp(cptr, "DELQA"))      xq.type = XQ_T_DELQA;
  else return SCPE_ARG;

  return SCPE_OK;
}

t_stat xq_show_sanity (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  fprintf(st, "sanity=");
  switch (xq.sanity.enabled) {
    case 0:  fprintf(st, "OFF"); break;
    case 1:  fprintf(st, "ON");  break;
  }
  return SCPE_OK;
}

t_stat xq_set_sanity (UNIT* uptr, int32 val, char* cptr, void* desc)
{
  if (!cptr) return SCPE_IERR;

  /* this assumes that the parameter has already been upcased */
  if      (!strcmp(cptr, "ON"))  xq.sanity.enabled = 1;
  else if (!strcmp(cptr, "OFF")) xq.sanity.enabled = 0;
  else return SCPE_ARG;

  return SCPE_OK;
}

t_stat xq_show_bootrom (FILE* st, UNIT* uptr, int32 val, void* desc)
{
  /* format includes \n for MTAB_NMO compatibility */
  fprintf(st, "bootrom=%s\n", xq.bootrom);
  return SCPE_OK;
}

t_stat xq_set_bootrom (UNIT* uptr, int32 val, char* cptr, void* desc)
{
  size_t len;
  if (!cptr) return SCPE_IERR;
  len = strlen(cptr) + 1;
  if (len > sizeof(xq.bootrom)) return SCPE_ARG;

  memcpy(xq.bootrom, cptr, strlen(cptr)+1);
  return SCPE_OK;
}

t_stat xq_nxm_error(void)
{
#ifdef XQ_DEBUG
  fprintf(stderr,"XQ: Non Existent Memory Error\n");
#endif
  /* set NXM and associated bits in CSR */
  xq.csr |= (XQ_CSR_NI | XQ_CSR_XI | XQ_CSR_XL | XQ_CSR_RL);

  /* interrupt if required */
  if (xq.csr & XQ_CSR_IE)
    SET_INT(XQ);

  return SCPE_OK;
}

/*
** write callback
*/
void xq_write_callback (int status)
{
  t_stat rstatus;
  int32 wstatus;
  const uint16 TDR = 100 + xq.write_buffer.len * 8; /* arbitrary value */
  uint16 write_success[2] = {0};
  uint16 write_failure[2] = {XQ_DSC_C};
  write_success[1] = TDR & 0x03FF; /* Does TDR get set on successful packets ?? */
  write_failure[1] = TDR & 0x03FF; /* TSW2<09:00> */

  /* update write status words */
  if (status == 0) { /* success */
    wstatus = Map_WriteW(xq.xbdl_ba + 8, 4, write_success, NOMAP);
  } else { /* failure */
#ifdef XQ_DEBUG
      fprintf(stderr, "XQ: Packet Write Error\n");
#endif
    wstatus = Map_WriteW(xq.xbdl_ba + 8, 4, write_failure, NOMAP);
  }
  if (wstatus) {
    xq_nxm_error();
    return;
  }

  /* update csr */
  xq.csr |= XQ_CSR_XI;
  if (xq.csr & XQ_CSR_IE)
    SET_INT(XQ);

  /* reset sanity timer */
  xq_reset_santmr();

  /* clear write buffer */
  xq.write_buffer.len = 0;

  /* next descriptor (implicit) */
  xq.xbdl_ba += 12;

  /* finish processing xbdl */
  rstatus = xq_process_xbdl();
}

/* read registers: */
t_stat xq_rd(int32* data, int32 PA, int32 access)
{
  int index = (PA >> 1) & 07;   /* word index */

#ifdef XQ_DEBUG
  if (index != 7)
#if defined(VM_VAX)
    fprintf (stderr,"XQ: %s %08X %08X read: %X\n", xq_recv_regnames[index], fault_PC, PSL, *data);
#else
    fprintf (stderr,"XQ: %s read: %X\n", xq_recv_regnames[index], *data);
#endif /* VM_VAX */
#endif

  switch (index) {
    case 0:
    case 1:
      /* return checksum in external loopback mode */
      if (xq.csr & XQ_CSR_EL)
        *data = 0xFF00 | xq.mac_checksum[index];
      else
        *data = 0xFF00 | xq.mac[index];
      break;
    case 2:
    case 3:
    case 4:
    case 5:
      *data = 0xFF00 | xq.mac[index];
      break;
    case 6:
#if 0
#ifdef XQ_DEBUG
      xq_dump_var();
#endif
#endif
      *data = xq.var;
      break;
    case 7:
#ifdef XQ_DEBUG
      xq_dump_csr();
#endif
      *data = xq.csr;
      break;
  }
  return SCPE_OK;
}


/* dispatch ethernet read request
   procedure documented in sec. 3.2.2 */

t_stat xq_process_rbdl(void)
{
  int32 rstatus, wstatus;
  uint16 b_length, w_length, rbl;
  t_addr address;
  struct xq_msg_itm* item;

#ifdef XQ_DEBUG
  fprintf(stderr,"XQ: CSR - Processing read\n");
#endif
  /* process buffer descriptors */
  while(1) {

    /* get receive bdl from memory */
    xq.rbdl_buf[0] = 0xFFFF;
    wstatus = Map_WriteW(xq.rbdl_ba,     2, &xq.rbdl_buf[0], NOMAP);
    rstatus = Map_ReadW (xq.rbdl_ba + 2, 6, &xq.rbdl_buf[1], NOMAP);
    if (rstatus || wstatus) return xq_nxm_error();

    /* invalid buffer? */
    if (~xq.rbdl_buf[1] & XQ_DSC_V) {
      xq.csr |= XQ_CSR_RL;
      if (xq.csr & XQ_CSR_IE)
        SET_INT(XQ);
      return SCPE_OK;
    }

    /* explicit chain buffer? */
    if (xq.rbdl_buf[1] & XQ_DSC_C) {
      xq.rbdl_ba = ((xq.rbdl_buf[1] & 0x3F) << 16) | xq.rbdl_buf[2];
      continue;
    }

    /* stop processing if nothing in read queue */
    if (!xq.ReadQ.count) break;

    /* get status words */
    rstatus = Map_ReadW(xq.rbdl_ba + 8, 4, &xq.rbdl_buf[4], NOMAP);
    if (rstatus) return xq_nxm_error();

    /* get host memory address */
    address = ((xq.rbdl_buf[1] & 0x3F) << 16) | xq.rbdl_buf[2];

    /* decode buffer length - two's complement (in words) */
    w_length = ~xq.rbdl_buf[3] + 1;
    b_length = w_length * 2;
    if (xq.rbdl_buf[1] & XQ_DSC_H) b_length -= 1;
    if (xq.rbdl_buf[1] & XQ_DSC_L) b_length -= 1;

    item = &xq.ReadQ.item[xq.ReadQ.head];
    rbl = item->packet.len;

    /* make sure entire packet fits in buffer */
    assert(rbl <= b_length);

    /* send data to host */
    wstatus = Map_WriteB(address, item->packet.len, item->packet.msg, NOMAP);
    if (wstatus) return xq_nxm_error();

    /* set receive size into RBL - RBL<10:8> maps into Status1<10:8>,
       RBL<7:0> maps into Status2<7:0>, and Status2<15:8> (copy) */

    xq.rbdl_buf[4] = 0;
    switch (item->type) {
      case 0: /* setup packet */
        xq.rbdl_buf[4] = 0x2700; /* set esetup and RBL 10:8 */
        break;
      case 1: /* loopback packet */
        xq.rbdl_buf[4] = 0x2000;         /* loopback flag */
        xq.rbdl_buf[4] |= (rbl & 0x0700); /* high bits of rbl */
        break;
      case 2: /* normal packet */
        rbl -= 60;    /* keeps max packet size in 11 bits */
        xq.rbdl_buf[4] = (rbl & 0x0700); /* high bits of rbl */
        break;
    }
    xq.rbdl_buf[5] = ((rbl & 0x00FF) << 8) | (rbl & 0x00FF);
    if (xq.ReadQ.loss) {
#ifdef XQ_DEBUG
        fprintf(stderr, "XQ: ReadQ overflow\n");
#endif
      xq.rbdl_buf[4] |= 0x0001;   /* set overflow bit */
      xq.ReadQ.loss = 0;          /* reset loss counter */
    }

    /* update read status words*/
    wstatus = Map_WriteW(xq.rbdl_ba + 8, 4, &xq.rbdl_buf[4], NOMAP);
    if (wstatus) return xq_nxm_error();

    /* remove packet from queue */
    xq_remove_queue(&xq.ReadQ);

    /* reset sanity timer */
    xq_reset_santmr();

    /* mark transmission complete */
    xq.csr |= XQ_CSR_RI;
    if (xq.csr & XQ_CSR_IE)
      SET_INT(XQ);

    /* set to next bdl (implicit chain) */
    xq.rbdl_ba += 12;

 } /* while */

  return SCPE_OK;
}

t_stat xq_process_mop(void)
{
  t_addr address;
  uint16 size;
  int32 wstatus;
  struct xq_meb* meb = (struct xq_meb*) &xq.write_buffer.msg[0200];
  const struct xq_meb* limit = (struct xq_meb*) &xq.write_buffer.msg[0400];

#ifdef XQ_DEBUG
  fprintf(stderr, "XQ: Processing MOP data\n");
#endif
  if (xq.type == XQ_T_DEQNA)  /* DEQNA's don't MOP */
    return SCPE_NOFNC;

  while ((meb->type != 0) && (meb < limit)) {
    address = (meb->add_hi << 16) || (meb->add_mi << 8) || meb->add_lo;
    size    = (meb->siz_hi << 8) || meb->siz_lo;

    /* MOP stuff here - NOT YET FULLY IMPLEMENTED */

#ifdef XQ_DEBUG
    printf("XQ: Processing MEB type: %d\n", meb->type);
#endif
    switch (meb->type) {
      case 0:   /* MOP Termination */
        break;
      case 1:   /* MOP Read Ethernet Address */
        wstatus = Map_WriteB(address, sizeof(ETH_MAC), (uint8*) &xq.setup.macs[0], NOMAP);
        if (wstatus) return xq_nxm_error();
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

t_stat xq_process_setup(void)
{
  int i,j;
  int count = 0;
  float secs;
  t_stat status;
  ETH_MAC zeros = {0, 0, 0, 0, 0, 0};
  ETH_MAC filters[XQ_FILTER_MAX + 1];

  /* extract filter addresses from setup packet */
  memset(xq.setup.macs, '\0', sizeof(xq.setup.macs));
  for (i = 0; i < 7; i++)
    for (j = 0; j < 6; j++) {
      xq.setup.macs[i]  [j] = xq.write_buffer.msg[(i +   01) + (j * 8)];
      if (xq.write_buffer.len > 112)
        xq.setup.macs[i+7][j] = xq.write_buffer.msg[(i + 0101) + (j * 8)];
    }

  /* process high byte count */
  if (xq.write_buffer.len > 128) {
    uint16 len = xq.write_buffer.len;
    uint16 led, san;

    if (len & XQ_SETUP_MC)
      xq.setup.multicast = 1;
    if (len & XQ_SETUP_PM)
      xq.setup.promiscuous = 1;
    if (led = (len & XQ_SETUP_LD) >> 2) {
      switch (led) {
        case 1: xq.setup.l1 = 0; break;
        case 2: xq.setup.l2 = 0; break;
        case 3: xq.setup.l3 = 0; break;
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
    xq.sanity.quarter_secs = (int) (secs * 4);

    /* if sanity timer enabled, start sanity timer */
    if (xq.csr & XQ_CSR_SE || xq.sanity.enabled)
      xq_start_santmr();
    else
      xq_cancel_santmr();
  }

  /* set ethernet filter */
  /* memcpy (filters[count++], xq.mac, sizeof(ETH_MAC)); */
  for (i = 0; i < XQ_FILTER_MAX; i++)
    if (memcmp(zeros, &xq.setup.macs[i], sizeof(ETH_MAC)))
      memcpy (filters[count++], xq.setup.macs[i], sizeof(ETH_MAC));
  status = eth_filter (xq.etherface, count, filters, xq.setup.multicast, xq.setup.promiscuous);

  /* process MOP information */
  if (xq.write_buffer.msg[0])
    status = xq_process_mop();

  /* mark setup block valid */
  xq.setup.valid = 1;

#ifdef XQ_DEBUG
  xq_debug_setup();
#endif
  return SCPE_OK;
}

/*
  Dispatch Write Operation

  The DELQA manual does not explicitly state whether or not multiple packets
  can be written in one transmit operation, so a maximum of 1 packet is assumed.

*/
t_stat xq_process_xbdl()
{
  const uint16  implicit_chain_status[2] = {XQ_DSC_V | XQ_DSC_C, 1};
  const uint16  write_success[2] = {0, 1 /*Non-Zero TDR*/};

  uint16 b_length, w_length;
  int32 rstatus, wstatus;
  t_addr address;
  t_stat status;

#ifdef XQ_DEBUG
  fprintf(stderr,"XQ: xq_process_xbdl - Processing write\n");
#endif
  /* clear write buffer */
  xq.write_buffer.len = 0;

  /* process buffer descriptors until not valid */
  while (1) {

    /* Get transmit bdl from memory */
    rstatus = Map_ReadW (xq.xbdl_ba,    12, &xq.xbdl_buf[0], NOMAP);
    xq.xbdl_buf[0] = 0xFFFF;
    wstatus = Map_WriteW(xq.xbdl_ba,     2, &xq.xbdl_buf[0], NOMAP);
    if (rstatus || wstatus) return xq_nxm_error();

    /* invalid buffer? */
    if (~xq.xbdl_buf[1] & XQ_DSC_V) {
      xq.csr |= XQ_CSR_XL;
      if (xq.csr & XQ_CSR_IE)
        SET_INT(XQ);
#ifdef XQ_DEBUG
        fprintf(stderr,"XQ: xq_process_xbdl - List Empty - Done Processing write\n");
#endif
      return SCPE_OK;
    }

#ifdef XQ_DEBUG
  fprintf(stderr,"XQ: xq_process_xbdl: Buffer Descriptor Information: %04X %04X %04X %04X %04X \n",
          xq.xbdl_buf[1], xq.xbdl_buf[2], xq.xbdl_buf[3], xq.xbdl_buf[4], xq.xbdl_buf[5]);
#endif
    /* compute host memory address */
    address = ((xq.xbdl_buf[1] & 0x3F) << 16) | xq.xbdl_buf[2];

    /* decode buffer length - two's complement (in words) */
    w_length = ~xq.xbdl_buf[3] + 1;
    b_length = w_length * 2;
    if (xq.xbdl_buf[1] & XQ_DSC_H) b_length -= 1;
    if (xq.xbdl_buf[1] & XQ_DSC_L) b_length -= 1;

    /* explicit chain buffer? */
    if (xq.xbdl_buf[1] & XQ_DSC_C) {
      xq.xbdl_ba = address;
#ifdef XQ_DEBUG
      fprintf(stderr,"XQ: xq_process_xbdl: Chained Buffer Encountered: %d\n", b_length);
#endif
      continue;
    }

    /* add to transmit buffer, making sure it's not too big */
    if ((xq.write_buffer.len + b_length) > sizeof(xq.write_buffer.msg))
      b_length = sizeof(xq.write_buffer.msg) - xq.write_buffer.len;
    rstatus = Map_ReadB(address, b_length, &xq.write_buffer.msg[xq.write_buffer.len], NOMAP);
    if (rstatus) return xq_nxm_error();
    xq.write_buffer.len += b_length;

    /* end of message? */
    if (xq.xbdl_buf[1] & XQ_DSC_E) {
      if (((~xq.csr & XQ_CSR_RE) && ((~xq.csr & XQ_CSR_IL) || (xq.csr & XQ_CSR_EL))) ||  /* loopback */
           (xq.xbdl_buf[1] & XQ_DSC_S)) { /* or setup packet (forces loopback regardless of state) */
        if (xq.xbdl_buf[1] & XQ_DSC_S) { /* setup packet */
          status = xq_process_setup();

          /* put packet in read buffer */
          xq_insert_queue (&xq.ReadQ, 0, &xq.write_buffer, status);
        } else { /* loopback */
          /* put packet in read buffer */
          xq_insert_queue (&xq.ReadQ, 1, &xq.write_buffer, 0);
        }

        /* update write status */
        wstatus = Map_WriteW(xq.xbdl_ba + 8, 4, (uint16*) write_success, NOMAP);
        if (wstatus) return xq_nxm_error();

        /* clear write buffer */
        xq.write_buffer.len = 0;

        /* reset sanity timer */
        xq_reset_santmr();

        /* mark transmission complete */
        xq.csr |= XQ_CSR_XI;
        if (xq.csr & XQ_CSR_IE)
          SET_INT(XQ);

        /* now trigger "read" of setup or loopback packet */
        if (~xq.csr & XQ_CSR_RL)
          status = xq_process_rbdl();

      } else { /* not loopback */

        status = eth_write(xq.etherface, &xq.write_buffer, &xq_write_callback);
        if (status != SCPE_OK)           /* not implemented or unattached */
          xq_write_callback(1);          /* fake failure */
        else
          xq_svc(&xq_unit[0]);           /* service any received data */
#ifdef XQ_DEBUG
        fprintf(stderr,"XQ: xq_process_xbdl: Completed Processing write\n");
#endif
        return SCPE_OK;

      } /* loopback/non-loopback */
    } else { /* not at end-of-message */

#ifdef XQ_DEBUG
      fprintf(stderr,"XQ: xq_process_xbdl: Processing Implicit Chained Buffer Segment\n");
#endif
      /* update bdl status words */
      wstatus = Map_WriteW(xq.xbdl_ba + 8, 4, (uint16*) implicit_chain_status, NOMAP);
      if(wstatus) return xq_nxm_error();
    }

    /* set to next bdl (implicit chain) */
    xq.xbdl_ba += 12;

  } /* while */
}

t_stat xq_dispatch_rbdl(void)
{
  int i;
  int32 rstatus, wstatus;
  t_stat status;

#ifdef XQ_DEBUG
  fprintf(stderr,"XQ: CSR - Dispatching read\n");
#endif
  /* mark receive bdl valid */
  xq.csr &= ~XQ_CSR_RL;

  /* init receive bdl buffer */
  for (i=0; i<6; i++)
    xq.rbdl_buf[i] = 0;

  /* get address of first receive buffer */
  xq.rbdl_ba = ((xq.rbdl[1] & 0x3F) << 16) | (xq.rbdl[0] & ~01);

  /* get first receive buffer */
  xq.rbdl_buf[0] = 0xFFFF;
  wstatus = Map_WriteW(xq.rbdl_ba,     2, &xq.rbdl_buf[0], NOMAP);
  rstatus = Map_ReadW (xq.rbdl_ba + 2, 6, &xq.rbdl_buf[1], NOMAP);
  if (rstatus || wstatus) return xq_nxm_error();

  /* is buffer valid? */
  if (~xq.rbdl_buf[1] & XQ_DSC_V) {
    xq.csr |= XQ_CSR_RL;
    if (xq.csr & XQ_CSR_IE)
      SET_INT(XQ);
    return SCPE_OK;
  }

  /* process any waiting packets in receive queue */
  if (xq.ReadQ.count)
    status = xq_process_rbdl();

  return SCPE_OK;
}

t_stat xq_dispatch_xbdl()
{
  int i;
  t_stat status;
#ifdef XQ_DEBUG
  fprintf(stderr,"XQ: CSR - Dispatching write\n");
#endif
  /* mark transmit bdl valid */
  xq.csr &= ~XQ_CSR_XL;

  /* initialize transmit bdl buffers */
  for (i=0; i<6; i++)
    xq.xbdl_buf[i] = 0;

  /* clear transmit buffer */
  xq.write_buffer.len = 0;

  /* get base address of first transmit descriptor */
  xq.xbdl_ba = ((xq.xbdl[1] & 0x3F) << 16) | (xq.xbdl[0] & ~01);

  /* process xbdl */
  status = xq_process_xbdl();

  return status;
}

t_stat xq_process_loopback(ETH_PACK* pack)
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
  memcpy (physical_address, xq.setup.valid ? xq.setup.macs[0] : xq.mac, sizeof(ETH_MAC));
  memcpy (&reply.msg[0], &reply.msg[offset+2], sizeof(ETH_MAC));
  memcpy (&reply.msg[6], physical_address, sizeof(ETH_MAC));
  memcpy (&reply.msg[offset+2], physical_address, sizeof(ETH_MAC));
  reply.msg[offset] = 0x01;
  offset += 8;
  reply.msg[14] = offset & 0xFF;
  reply.msg[15] = (offset >> 8) & 0xFF;

  /* send reply packet */
  status = eth_write(xq.etherface, &reply, NULL);

  return status;
}

t_stat xq_process_remote_console (ETH_PACK* pack)
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
      status = xq_system_id (source, receipt);
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

t_stat xq_process_local (ETH_PACK* pack)
{
  /* returns SCPE_OK if local processing occurred,
     otherwise returns SCPE_NOFNC or some other code */
  int protocol;

  /* DEQNA's have no local processing capability */
  if (xq.type == XQ_T_DEQNA)
    return SCPE_NOFNC;

  protocol = pack->msg[12] | (pack->msg[13] << 8);
  switch (protocol) {
    case 0x0090:  /* ethernet loopback */
      return xq_process_loopback(pack);
      break;
    case 0x0260:  /* MOP remote console */
      return xq_process_remote_console(pack);
      break;
  }
  return SCPE_NOFNC;
}

void xq_read_callback(int status)
{
  if (xq.csr & XQ_CSR_RE) { /* receiver enabled */

    /* process any packets locally that can be */
    t_stat status = xq_process_local (&xq.read_buffer);

    /* add packet to read queue */
    if (status != SCPE_OK)
      xq_insert_queue(&xq.ReadQ, 2, &xq.read_buffer, status);
  }
#ifdef XQ_DEBUG
  else
    fprintf(stderr, "XQ: packet received with receiver disabled\n");
#endif
}

void xq_sw_reset(void)
{
  /* cancel sanity timer */
  xq_cancel_santmr();

  /* disconnect ethernet reception */
  sim_cancel(&xq_unit[0]);

  /* stop system_id timer */
  sim_cancel(&xq_unit[2]);

  /* reset csr bits */
  xq.csr = XQ_CSR_XL | XQ_CSR_RL;

  if (xq.etherface)
    xq.csr |= XQ_CSR_OK;

  /* flush read queue */
  xq_clear_queue(&xq.ReadQ);

  /* clear setup info */
  memset (&xq.setup, 0, sizeof(xq.setup));

}

/* write registers: */

t_stat xq_wr_var(int32 data)
{
#ifdef XQ_DEBUG
  xq_var_changes(data);
#endif

  switch (xq.type) {
    case XQ_T_DEQNA:
      xq.var = (data & XQ_VEC_IV);
      break;
    case XQ_T_DELQA:
      xq.var = (xq.var & XQ_VEC_RO) | (data & XQ_VEC_RW);

      /* if switching to DEQNA-LOCK mode clear VAR<14:10> */
      if (~xq.var & XQ_VEC_MS)
        xq.var &= ~(XQ_VEC_OS | XQ_VEC_RS | XQ_VEC_ST);
      break;
  }

  /* set vector of SIMH device */
  if (data & XQ_VEC_IV)
    xq_dib.vec = (data & XQ_VEC_IV) + VEC_Q;
  else
    xq_dib.vec = 0;

  return SCPE_OK;
}

t_stat xq_wr_csr(int32 data)
{
  uint16 saved_csr = xq.csr;
#ifdef XQ_DEBUG
  xq_csr_changes(data);
#endif

  /* reset controller when SR transitions to cleared */
  if (xq.csr & XQ_CSR_SR & ~data) {
    xq_sw_reset();
    return SCPE_OK;
  }

  /* write the writeable bits */
  xq.csr = (xq.csr & XQ_CSR_RO) | (data & XQ_CSR_RW);

  /* clear write-one-to-clear bits */
  xq.csr &= ~(data & XQ_CSR_W1);
  if (data & XQ_CSR_XI)         /* clearing XI clears NI too */
    xq.csr &= ~XQ_CSR_NI;

  /* start receiver timer when RE transitions to set */
  if (~saved_csr & XQ_CSR_RE & data) {
    sim_activate(&xq_unit[0], (clk_tps * tmr_poll)/100);
  }

  /* stop receiver timer when RE transitions to clear */
  if (saved_csr & XQ_CSR_RE & ~data) {
    sim_cancel(&xq_unit[0]);
  }

  return SCPE_OK;
}

t_stat xq_wr(int32 data, int32 PA, int32 access)
{
  t_stat status;
  int index = (PA >> 1) & 07;   /* word index */

#ifdef XQ_DEBUG
  if (index != 7)
#if defined(VM_VAX)
    fprintf (stderr,"XQ: %s %08X %08X write: %X\n", xq_xmit_regnames[index], fault_PC, PSL, data);
#else
    fprintf (stderr,"XQ: %s write: %X\n", xq_xmit_regnames[index], data);
#endif /* VM_VAX */
#endif

  switch (index) {
    case 0:   /* these should not be written */
    case 1:
      break;
    case 2:   /* receive bdl low bits */
      xq.rbdl[0] = data;
      break;
    case 3:   /* receive bdl high bits */
      xq.rbdl[1] = data;
      status = xq_dispatch_rbdl(); /* start receive operation */
      break;
    case 4:   /* transmit bdl low bits */
      xq.xbdl[0] = data;
      break;
    case 5:   /* transmit bdl high bits */
      xq.xbdl[1] = data;
      status = xq_dispatch_xbdl(); /* start transmit operation */
      break;
    case 6:   /* vector address register */
      status = xq_wr_var(data);
      break;
    case 7:   /* control and status register */
      status = xq_wr_csr(data);
      break;
  }
  return SCPE_OK;
}


/* reset device */
t_stat xq_reset(DEVICE* dptr)
{
  t_stat status;

  /* calculate MAC checksum */
  make_checksum();

  /* init vector address register */
  switch (xq.type) {
    case XQ_T_DEQNA:
      xq.var = 0;
      break;
    case XQ_T_DELQA:
      xq.var = XQ_VEC_MS | XQ_VEC_OS;
      break;
  }
  xq_dib.vec = 0;

  /* init control status register */
  xq.csr = XQ_CSR_RL | XQ_CSR_XL;

  /* reset ethernet interface */
  if (xq.etherface) {
    status = eth_filter (xq.etherface, 1, &xq.mac, 0, 0);
    xq.csr |= XQ_CSR_OK;
  }

  /* clear read queue */
  xq_clear_queue(&xq.ReadQ);

  /* start sanity timer if power-on SANITY is set */
  switch (xq.type) {
    case XQ_T_DEQNA:
      if (xq.sanity.enabled) {
        xq.sanity.quarter_secs = 4 * (4 * 60);   /* default is 4 minutes */;
        xq_start_santmr();
      }
      break;
    case XQ_T_DELQA:
      /* note that the DELQA in NORMAL mode has no power-on SANITY state! */
      xq_start_idtmr();
      break;
  };

  return SCPE_OK;
}

void xq_start_santmr(void)
{
  /* must be recalculated each time since tmr_poll is a dynamic number */
  const int32 quarter_sec = (clk_tps * tmr_poll) / 4;

#if 0
#ifdef XQ_DEBUG
  fprintf(stderr,"XQ: SANITY TIMER: enabled, qsecs: %d, poll:%d\n", xq.sanity.quarter_secs, tmr_poll);
#endif
#endif
  if (sim_is_active(&xq_unit[1]))   /* cancel timer, just in case */
    sim_cancel(&xq_unit[1]);
  xq_reset_santmr();
  sim_activate(&xq_unit[1], quarter_sec);
}

void xq_cancel_santmr(void)
{
  /* can't cancel hardware switch sanity timer */
  if (sim_is_active(&xq_unit[1]) && !xq.sanity.enabled) {
#if 0
#ifdef XQ_DEBUG
    fprintf(stderr,"XQ: SANITY TIMER: cancelled, qsecs: %d\n", xq.sanity.quarter_secs);
#endif
#endif
    sim_cancel(&xq_unit[1]);
  }
}

void xq_reset_santmr(void)
{
#if 0
#ifdef XQ_DEBUG
  ftime(&start);
  fprintf(stderr,"XQ: SANITY TIMER: resetting, qsecs: %d\n", xq.sanity.quarter_secs);
#endif
#endif
  xq.sanity.countdown = xq.sanity.quarter_secs;
}

t_stat xq_sansvc(UNIT* uptr)
{
  if (--xq.sanity.countdown) {
    /* must be recalculated each time since tmr_poll is a dynamic number */
    const int32 quarter_sec = (clk_tps * tmr_poll) / 4;

    /* haven't hit the end of the countdown timer yet, resubmit */
    sim_activate(&xq_unit[1], quarter_sec);
  } else {
    /*
    If this section is entered, it means that the sanity timer has expired
    without being reset, and the controller must reboot the processor.
    */
#if 0
#ifdef XQ_DEBUG
    ftime(&finish);
    fprintf(stderr,"XQ: SANITY TIMER: EXPIRED, qsecs: %d, poll: %d, elapsed: %d\n",
           xq.sanity.quarter_secs, tmr_poll, finish.time - start.time);
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

void xq_start_idtmr(void)
{
  /* must be recalculated each time since tmr_poll is a dynamic number */
  const int32 one_sec = clk_tps * tmr_poll;

  if (sim_is_active(&xq_unit[2]))   /* cancel timer, just in case */
    sim_cancel(&xq_unit[2]);
  xq.id.enabled = 1;
  /* every 8-10 minutes (9 in this case) the DELQA broadcasts a system id message */
  xq.id.countdown = 9 * 60;
  sim_activate(&xq_unit[2], one_sec);
}

t_stat xq_system_id (const ETH_MAC dest, uint16 receipt_id)
{
  static uint16 receipt = 0;
  ETH_PACK system_id;
  uint8* const msg = &system_id.msg[0];
  t_stat status;

  memset (&system_id, 0, sizeof(system_id));
  memcpy (&msg[0], dest, sizeof(ETH_MAC));
  memcpy (&msg[6], xq.setup.valid ? xq.setup.macs[0] : xq.mac, sizeof(ETH_MAC));
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
  memcpy (&msg[34], xq.mac, sizeof(ETH_MAC)); /* ROM address */

                                          /* DEVICE TYPE */
  msg[40] = 37;                           /* type */
  msg[41] = 0x00;                         /* type */
  msg[42] = 0x01;                         /* length */
  msg[43] = 0x11;                         /* value (0x11=DELQA) */

  /* write system id */
  system_id.len = 60;
  status = eth_write(xq.etherface, &system_id, NULL);

  return status;
}

t_stat xq_idsvc(UNIT* uptr)
{
/* must be recalculated each time since tmr_poll is a dynamic number */
const int32 one_sec = clk_tps * tmr_poll;
const ETH_MAC mop_multicast = {0xAB, 0x00, 0x00, 0x02, 0x00, 0x00};

  /* DEQNAs don't issue system id messages */
  if (xq.type == XQ_T_DEQNA)
    return SCPE_NOFNC;

  if (--xq.id.countdown <= 0) {
    /*
    If this section is entered, it means that the 9 minute interval has elapsed
    so broadcast system id to MOP multicast address
    */
    xq_system_id(mop_multicast, 0);
    /* every 8-10 minutes (9 in this case) the DELQA broadcasts a system id message */
    xq.id.countdown = 9 * 60;
  }

  /* resubmit - for one second to get a well calibrated value of tmr_poll */
  sim_activate(&xq_unit[2], one_sec);
  return SCPE_OK;
}

/*
** service routine - used for ethernet reading loop
*/
t_stat xq_svc(UNIT* uptr)
{
  t_stat status;
  int queue_size;

  /* Don't try a read if the receiver is disabled */
  if (!(xq.csr & XQ_CSR_RE)) return SCPE_OK;

  /* First pump any queued packets into the system */
  if ((xq.ReadQ.count > 0) && (~xq.csr & XQ_CSR_RL))
    status = xq_process_rbdl();

  /* Now read and queue packets that have arrived */
  /* This is repeated as long as they are available and we have room */
  do
    {
    queue_size = xq.ReadQ.count;
    /* read a packet from the ethernet - processing is via the callback */
    status = eth_read (xq.etherface, &xq.read_buffer, &xq_read_callback);
  } while (queue_size != xq.ReadQ.count);

  /* Now pump any still queued packets into the system */
  if ((xq.ReadQ.count > 0) && (~xq.csr & XQ_CSR_RL))
    status = xq_process_rbdl();

  /* resubmit if still receive enabled */
  if (xq.csr & XQ_CSR_RE)
    sim_activate(&xq_unit[0], (clk_tps * tmr_poll)/100);

  return SCPE_OK;
}



/* attach device: */
t_stat xq_attach(UNIT* uptr, char* cptr)
{
  t_stat status;
  char* tptr;

  tptr = malloc(strlen(cptr) + 1);
  if (tptr == NULL) return SCPE_MEM;
  strcpy(tptr, cptr);

  xq.etherface = malloc(sizeof(ETH_DEV));
  if (!xq.etherface) return SCPE_MEM;

  status = eth_open(xq.etherface, cptr);
  if (status != SCPE_OK) {
    free(tptr);
    free(xq.etherface);
    xq.etherface = 0;
    return status;
  }
  uptr->filename = tptr;
  uptr->flags |= UNIT_ATT;

  /* turn on transceiver power indicator */
  xq.csr |= XQ_CSR_OK;

  return SCPE_OK;
}

/* detach device: */

t_stat xq_detach(UNIT* uptr)
{
  t_stat status;

  if (uptr->flags & UNIT_ATT) {
    status = eth_close (xq.etherface);
    free(xq.etherface);
    xq.etherface = 0;
    free(uptr->filename);
    uptr->filename = NULL;
    uptr->flags &= ~UNIT_ATT;
  }

  /* turn off transceiver power indicator */
  xq.csr &= ~XQ_CSR_OK;

  return SCPE_OK;
}

int32 xq_inta (void)
{
  return xq_dib.vec;
}

/*==============================================================================
/                               debugging routines
/=============================================================================*/

#ifdef XQ_DEBUG

void xq_dump_csr (void)
{
 static int cnt  = 0;
  /* tell user what is changing in register */
  int i;
  int mask = 1;
  uint16 csr = xq.csr;
  char hi[256] = "Set: ";
  char lo[256] = "Reset: ";
  for (i=0; i<16; i++, mask <<= 1) {
    if ((csr & mask))  strcat (hi, xq_csr_bits[i]);
    if ((~csr & mask)) strcat (lo, xq_csr_bits[i]);
  }
#if defined (VM_VAX)
  printf ("CSR %08X %08X read: %s %s\n", fault_PC, PSL, hi, lo);
#else
if (cnt < 20)
  printf ("CSR read[%d]: %s %s\n", cnt++, hi, lo);
#endif /* VM_VAX */
}

void xq_dump_var (void)
{
  /* tell user what is changing in register */
  uint16 var = xq.var;
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
  printf ("VAR read: %s %s - Vec: %d \n", hi, lo, vec);
}

void xq_csr_changes (uint16 data)
{
  /* tell user what is changing in register */
  int i;
  int mask = 1;
  uint16 csr = xq.csr;
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
  printf ("CSR %08X %08X write: %s %s\n", fault_PC, PSL, hi, lo);
#else
  printf ("CSR write: %s %s\n", hi, lo);
#endif /* VM_VAX */
}

void xq_var_changes (uint16 data)
{
  /* tell user what is changing in register */
  uint16 vec;
  uint16 var = xq.var;
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
    printf ("VAR write: %s %s - Vec: %d\n", hi, lo, vec);
  } else 
    printf ("VAR write: %s %s\n", hi, lo);
}

void xq_debug_setup(void)
{
  int   i;
  char  buffer[20];
  if (xq.write_buffer.msg[0])
    printf ("Setup: MOP info present!\n");

  for (i = 0; i < XQ_FILTER_MAX; i++) {
    eth_mac_fmt(&xq.setup.macs[i], buffer);
    printf ("Setup: set addr[%d]: %s\n", i, buffer);
  }

  if (xq.write_buffer.len > 128) {
    char buffer[20] = {0};
    uint16 len = xq.write_buffer.len;
    if (len & XQ_SETUP_MC) strcat(buffer, "MC ");
    if (len & XQ_SETUP_PM) strcat(buffer, "PM ");
    if (len & XQ_SETUP_LD) strcat(buffer, "LD ");
    if (len & XQ_SETUP_ST) strcat(buffer, "ST ");
    printf ("Setup: Length [%d =0x%X, LD:%d, ST:%d] info: %s\n", len, len, (len & XQ_SETUP_LD) >> 2, (len & XQ_SETUP_ST) >> 4, buffer);
  }
}
#endif
