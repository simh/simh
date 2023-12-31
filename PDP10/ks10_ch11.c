/* ks10_ch11.c: CH11 Chaosnet interface.
  ------------------------------------------------------------------------------

   Copyright (c) 2022, Richard Cornwell, original by Lars Brinkhoff.

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

*/

#include "kx10_defs.h"
#include "sim_tmxr.h"

#ifndef NUM_DEVS_CH11
#define NUM_DEVS_CH11 0
#endif

#if (NUM_DEVS_CH11 > 0)

/* CSR 076410 */

#define CSR_BSY     0000001   /* Xmit busy RO */
#define CSR_LUP     0000002   /* Loop back R/W */
#define CSR_SPY     0000004   /* Receive msgs from any destination R/W */
#define CSR_RCL     0000010   /* Clear receiver WO */
#define CSR_REN     0000020   /* Receiver int enable R/W */
#define CSR_TEN     0000040   /* Transmitter int enable R/W */
#define CSR_TAB     0000100   /* Transmit abort by conflict RO */
#define CSR_TDN     0000200   /* Transmit Done */
#define CSR_TCL     0000400   /* Clear transmitter WO */
#define CSR_LOS     0017000   /* Lost count RO */
#define CSR_RST     0020000   /* I/O Reset WO */
#define CSR_ERR     0040000   /* CRC Error RO */
#define CSR_RDN     0100000   /* Receive done */


/* MY # 764142      Host number  RO */

/* WBF  764142      Write bufffer WO */

/* RBF  764144      Read buffer RO */

/* RBC  764146      Receive bit counter RO */

/* XMT  764152      Read initiates transmission */


#define CHUDP_HEADER 4
#define IOLN_CH 020
#define DBG_TRC  0x0001
#define DBG_REG  0x0002
#define DBG_PKT  0x0004
#define DBG_DAT  0x0008
#define DBG_INT  0x0010
#define DBG_ERR  0x0020

int    ch11_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access);
int    ch11_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access);
uint16 ch11_checksum (const uint8 *p, int count);
void   ch11_validate (const uint8 *p, int count);
t_stat ch11_transmit (struct pdp_dib *dibp);
int    ch11_receive (struct pdp_dib *dibp);
void   ch11_clear (struct pdp_dib *dibp);
t_stat ch11_svc(UNIT *);
t_stat ch11_reset (DEVICE *);
t_stat ch11_attach (UNIT *, CONST char *);
t_stat ch11_detach (UNIT *);
t_stat ch11_show_peer (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat ch11_set_peer (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat ch11_show_node (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat ch11_set_node (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat ch11_help (FILE *, DEVICE *, UNIT *, int32, const char *);
t_stat ch11_help_attach (FILE *, DEVICE *, UNIT *, int32, const char *);
const char *ch11_description (DEVICE *);

static char peer[256];
static int address;
static uint16 ch11_csr;
static int rx_count;
static int rx_pos;
static int tx_count;
static uint8 rx_buffer[514+100];
static uint8 tx_buffer[514+100];

TMLN ch11_lines[1] = { {0} };
TMXR ch11_tmxr = { 1, NULL, 0, ch11_lines};

UNIT ch11_unit[] = {
  {UDATA (&ch11_svc, UNIT_IDLE|UNIT_ATTABLE, 0) },
};

REG ch11_reg[] = {
  { ORDATA(CSR,   ch11_csr,     16)},
  { GRDATAD(RXCNT,  rx_count,   16, 16, 0, "Receive word count"), REG_FIT|REG_RO},
  { GRDATAD(RXPOS,  rx_pos,     16, 16, 0, "Receive Position"), REG_FIT|REG_RO},
  { GRDATAD(TXCNT,  tx_count,   16, 16, 0, "Transmit word count"), REG_FIT|REG_RO},
  { BRDATAD(RXBUF,  rx_buffer,  16,  8, sizeof rx_buffer, "Receive packet buffer"), REG_FIT},
  { BRDATAD(TXBUF,  tx_buffer,  16,  8, sizeof tx_buffer, "Transmit packet buffer"), REG_FIT},
  { BRDATAD(PEER,   peer,       16,  8, sizeof peer, "Network peer"), REG_HRO},
  { GRDATAD(NODE,   address,    16, 16, 0, "Node address"), REG_HRO},
  { NULL }  };

MTAB ch11_mod[] = {
  {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "addr", "addr",  &uba_set_addr, uba_show_addr,
            NULL, "Sets address of CH11" },
  {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "vect", "vect",  &uba_set_vect, uba_show_vect,
            NULL, "Sets vect of CH11" },
  {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "br", "br",  &uba_set_br, uba_show_br,
            NULL, "Sets br of CH11" },
  {MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "ctl", "ctl",  &uba_set_ctl, uba_show_ctl,
            NULL, "Sets uba of CH11" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "PEER", "PEER",
    &ch11_set_peer, &ch11_show_peer, NULL, "Remote host name and port" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "NODE", "NODE",
    &ch11_set_node, &ch11_show_node, NULL, "Chaosnet node address" },
  { 0 },
};

DIB ch11_dib = { 0764140, 017, 0270, 5, 3, &ch11_read, &ch11_write, NULL, 0, 0 };

DEBTAB ch11_debug[] = {
    { "DETAIL",    DEBUG_DETAIL,"I/O operations"},
    { "TRC",       DBG_TRC,   "Detailed trace" },
    { "REG",       DBG_REG,   "Hardware registers" },
    { "PKT",       DBG_PKT,   "Packets" },
    { "DAT",       DBG_DAT,   "Packet data" },
    { "INT",       DBG_INT,   "Interrupts" },
    { "ERR",       DBG_ERR,   "Error conditions" },
    { 0 }
};

DEVICE ch11_dev = {
    "CH", ch11_unit, ch11_reg, ch11_mod,
    1, 8, 16, 1, 8, 16,
    NULL, NULL, &ch11_reset,
    NULL, &ch11_attach, &ch11_detach,
    &ch11_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX,
    0, ch11_debug, NULL, NULL, &ch11_help, &ch11_help_attach, NULL,
    &ch11_description
  };

int
ch11_write(DEVICE *dptr, t_addr addr, uint16 data, int32 access)
{
    struct pdp_dib   *dibp = (DIB *)dptr->ctxt;
    int               i;

    addr &= dibp->uba_mask;
    sim_debug(DEBUG_DETAIL, dptr, "CH11 write %06o %06o %o\n",
             addr, data, access);

    switch (addr & 016) {
    case 000:  /* CSR */
        if (data & CSR_RST) {
            sim_debug (DBG_REG, &ch11_dev, "Reset\n");
            ch11_clear (dibp);
        }
        ch11_csr &= ~(CSR_REN|CSR_TEN|CSR_SPY);
        ch11_csr |= data & (CSR_REN|CSR_TEN|CSR_SPY);
        if (data & CSR_RCL) {
            sim_debug (DBG_REG, &ch11_dev, "Clear RX\n");
            ch11_csr &= ~CSR_RDN;
            rx_count = 0;
            rx_pos = 0;
            ch11_lines[0].rcve = TRUE;
            uba_clr_irq(dibp, dibp->uba_vect);
        }
        if (data & CSR_TCL) {
          sim_debug (DBG_REG, &ch11_dev, "Clear TX\n");
          tx_count = 0;
          ch11_csr |= CSR_TDN;
          if (ch11_csr & CSR_TEN)
            uba_set_irq(dibp, dibp->uba_vect);
        }
        break;

    case 002:  /* Write buffer */
        ch11_csr &= ~CSR_TDN;
        if (tx_count < 512) {
          i = CHUDP_HEADER + tx_count;
          tx_buffer[i] = (data >> 8) & 0xff;
          tx_buffer[i+1] = data & 0xff;
          tx_count+=2;
          sim_debug (DBG_DAT, &ch11_dev, "Write buffer word %d:%02x %02x %06o %06o\n",
                     tx_count, tx_buffer[i], tx_buffer[i+1], data, ch11_csr);
        } else {
          sim_debug (DBG_ERR, &ch11_dev, "Write buffer overflow\n");
        }
        break;
    case 004:  /* Read buffer */
    case 006:  /* Bit count */
    case 012:  /* Start transmission */
    case 010:  /* Empty */
    case 014:  /* Empty */
    case 016:  /* Empty */
        break;
    }
    return 0;
}

int
ch11_read(DEVICE *dptr, t_addr addr, uint16 *data, int32 access)
{
    struct pdp_dib   *dibp = (DIB *)dptr->ctxt;

    addr &= dibp->uba_mask;
    *data = 0;
    switch (addr & 016) {
    case 000:  /* CSR */
        *data = ch11_csr;
        break;

    case 002:  /* Host number */
        *data = address;
        break;

    case 004:  /* Read buffer */
        if (rx_count == 0) {
          *data = 0;
          sim_debug (DBG_ERR, &ch11_dev, "Read empty buffer\n");
        } else {
          ch11_csr &= ~CSR_RDN;
          uba_clr_irq(dibp, dibp->uba_vect);
          *data = ((uint64)(rx_buffer[rx_pos]) & 0xff) << 8;
          *data |= ((uint64)(rx_buffer[rx_pos+1]) & 0xff);
          sim_debug (DBG_DAT, &ch11_dev, "Read buffer word %d:%02x %02x %06o %06o\n",
                     rx_count, rx_buffer[rx_pos], rx_buffer[rx_pos+1], *data, ch11_csr);
          rx_count-=2;
          rx_pos+=2;
        }
        break;

    case 006:  /* Bit count */
        *data = ((rx_count * 8) - 1) & 07777;
        break;
    case 012:  /* Start transmission */
        sim_debug (DBG_REG, &ch11_dev, "XMIT TX\n");
        ch11_transmit(dibp);
        break;

    case 010:  /* Empty */
    case 014:  /* Empty */
    case 016:  /* Empty */
        break;
    }
    sim_debug(DEBUG_DETAIL, dptr, "CH11 read %06o %06o %o\n",
             addr, *data, access);
    return 0;
}

uint16
ch11_checksum (const uint8 *p, int count)
{
  int32 sum = 0;

  while (count > 1) {
    sum += (p[0]<<8) | p[1];
    p += 2;
    count -= 2;
  }

  if ( count > 0)
    sum += p[0];

  while (sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);

  return (~sum) & 0xffff;
}

void
ch11_validate (const uint8 *p, int count)
{
  uint16 chksum;
  int size;

  sim_debug (DBG_TRC, &ch11_dev, "Packet opcode: %02x\n", p[0]);
  sim_debug (DBG_TRC, &ch11_dev, "MBZ: %02x\n", p[1]);
  sim_debug (DBG_TRC, &ch11_dev, "Forwarding count: %02x\n", p[2] >> 4);
  size = ((p[2] & 0xF) << 8) + p[3];
  sim_debug (DBG_TRC, &ch11_dev, "Packet size: %03x\n", size);
  sim_debug (DBG_TRC, &ch11_dev, "Destination address: %o\n", (p[4] << 8) + p[5]);
  sim_debug (DBG_TRC, &ch11_dev, "Destination index: %02x\n", (p[6] << 8) + p[7]);
  sim_debug (DBG_TRC, &ch11_dev, "Source address: %o\n", (p[8] << 8) + p[9]);
  sim_debug (DBG_TRC, &ch11_dev, "Source index: %02x\n", (p[10] << 8) + p[11]);
  sim_debug (DBG_TRC, &ch11_dev, "Packet number: %02x\n", (p[12] << 8) + p[13]);
  sim_debug (DBG_TRC, &ch11_dev, "Acknowledgement: %02x\n", (p[14] << 8) + p[15]);

  if (p[1] != 0)
    sim_debug (DBG_ERR, &ch11_dev, "Bad packet\n");

  chksum = ch11_checksum (p, count);
  if (chksum != 0) {
    sim_debug (DBG_ERR, &ch11_dev, "Checksum error: %04x\n", chksum);
    ch11_csr |= CSR_ERR;
  } else
    sim_debug (DBG_TRC, &ch11_dev, "Checksum: %05o\n", chksum);
}

t_stat
ch11_transmit (struct pdp_dib *dibp)
{
  size_t len;
  t_stat r;
  int i = CHUDP_HEADER + tx_count;
  uint16 chk;

  if (tx_count > (512 - CHUDP_HEADER)) {
    sim_debug (DBG_PKT, &ch11_dev, "Pack size failed, %d bytes.\n", (int)tx_count);
    ch11_csr |= CSR_ERR;
    return SCPE_INCOMP;
  }
  tx_buffer[i] = tx_buffer[8+CHUDP_HEADER];
  tx_buffer[i+1] = tx_buffer[9+CHUDP_HEADER];
  tx_count += 2;
  chk = ch11_checksum(tx_buffer + CHUDP_HEADER, tx_count);
  tx_buffer[i+2] = (chk >> 8) & 0xff;
  tx_buffer[i+3] = chk & 0xff;
  tx_count += 2;

  tmxr_poll_tx (&ch11_tmxr);
  len = CHUDP_HEADER + (size_t)tx_count;
  r = tmxr_put_packet_ln (&ch11_lines[0], (const uint8 *)&tx_buffer, len);
  if (r == SCPE_OK) {
    sim_debug (DBG_PKT, &ch11_dev, "Sent UDP packet, %d bytes. %04x checksum.\n", (int)len, chk);
    tmxr_poll_tx (&ch11_tmxr);
  } else {
    sim_debug (DBG_ERR, &ch11_dev, "Sending UDP failed: %d.\n", r);
    ch11_csr |= CSR_TAB;
  }
  tx_count = 0;
  ch11_csr |= CSR_TDN;
  return SCPE_OK;
}

int
ch11_receive (struct pdp_dib *dibp)
{
  size_t count;
  const uint8 *p;
  uint16 dest;

  tmxr_poll_rx (&ch11_tmxr);
  if (tmxr_get_packet_ln (&ch11_lines[0], &p, &count) != SCPE_OK) {
    sim_debug (DBG_ERR, &ch11_dev, "TMXR error receiving packet\n");
    return 0;
  }
  if (p == NULL)
    return 0;
  dest = ((p[4+CHUDP_HEADER] & 0xff) << 8) + (p[5+CHUDP_HEADER] & 0xff);

  sim_debug (DBG_PKT, &ch11_dev, "Received UDP packet, %d bytes for: %o\n", (int)count, dest);
  /* Check if packet for us. */
  if (dest != address && dest != 0 && (ch11_csr & CSR_SPY) == 0)
    return 1;

  if ((CSR_RDN & ch11_csr) == 0) {
    count = (count + 1) & 01776;
    memcpy (rx_buffer, p + CHUDP_HEADER, count);
    rx_count = count - CHUDP_HEADER;
    rx_pos = 0;
    sim_debug (DBG_TRC, &ch11_dev, "Rx count, %d\n", rx_count);
    ch11_validate (p + CHUDP_HEADER, count - CHUDP_HEADER);
    ch11_csr |= CSR_RDN;
    if (ch11_csr & CSR_REN) {
        sim_debug (DBG_INT, &ch11_dev, "RX Interrupt\n");
        uba_set_irq(dibp, dibp->uba_vect);
    }
    ch11_lines[0].rcve = FALSE;
    sim_debug (DBG_TRC, &ch11_dev, "Rx off\n");
  } else {
    sim_debug (DBG_ERR, &ch11_dev, "Lost packet\n");
    if ((ch11_csr & CSR_LOS) != CSR_LOS)
        ch11_csr = (ch11_csr & ~CSR_LOS) | (CSR_LOS & (ch11_csr + 01000));
  }
  return 1;
}

void
ch11_clear (struct pdp_dib *dibp)
{
  ch11_csr = CSR_TDN;
  rx_count = 0;
  tx_count = 0;
  rx_pos = 0;

  tx_buffer[0] = 1; /* CHUDP header */
  tx_buffer[1] = 1;
  tx_buffer[2] = 0;
  tx_buffer[3] = 0;
  ch11_lines[0].rcve = TRUE;

  uba_clr_irq(dibp, dibp->uba_vect);
}

t_stat
ch11_svc(UNIT *uptr)
{
   DEVICE           *dptr = find_dev_from_unit (uptr);
   struct pdp_dib   *dibp = (DIB *)dptr->ctxt;

  if (ch11_lines[0].conn) {
    if (ch11_receive (dibp))
      sim_activate_after (uptr, 300);
    else
      sim_clock_coschedule (uptr, 1000);
  } else {
    (void)tmxr_poll_conn (&ch11_tmxr);
    sim_clock_coschedule (uptr, 1000);
  }
  if (tx_count == 0) {
    ch11_csr |= CSR_TDN;
    if (ch11_csr & CSR_TEN) {
        sim_debug (DBG_INT, &ch11_dev, "RX Interrupt\n");
        uba_set_irq(dibp, dibp->uba_vect);
    }
  }
  return SCPE_OK;
}

t_stat ch11_attach (UNIT *uptr, CONST char *cptr)
{
  char linkinfo[256];
  t_stat r;

  ch11_dev.dctrl |= 0xF77F0000;
  if (address == -1)
    return sim_messagef (SCPE_2FARG, "Must set Chaosnet NODE address first \"SET CH NODE=val\"\n");
  if (peer[0] == '\0')
    return sim_messagef (SCPE_2FARG, "Must set Chaosnet PEER \"SET CH PEER=host:port\"\n");

  snprintf (linkinfo, sizeof(linkinfo), "Buffer=%d,UDP,%s,PACKET,Connect=%.*s,Line=0",
           (int)sizeof tx_buffer, cptr, (int)(sizeof(linkinfo) - (45 + strlen(cptr))), peer);
  r = tmxr_attach (&ch11_tmxr, uptr, linkinfo);
  if (r != SCPE_OK) {
    sim_debug (DBG_ERR, &ch11_dev, "TMXR error opening master\n");
    return sim_messagef (r, "Error Opening: %s\n", peer);
  }

  uptr->filename = (char *)realloc (uptr->filename, 1 + strlen (cptr));
  strcpy (uptr->filename, cptr);
  sim_activate (uptr, 1000);
  return SCPE_OK;
}

t_stat ch11_detach (UNIT *uptr)
{
  sim_cancel (uptr);
  tmxr_detach (&ch11_tmxr, uptr);
  return SCPE_OK;
}

t_stat ch11_reset (DEVICE *dptr)
{
  struct pdp_dib   *dibp = (DIB *)dptr->ctxt;

  ch11_clear (dibp);
  if (ch11_unit[0].flags & UNIT_ATT)
      sim_activate (&ch11_unit[0], 100);
  return SCPE_OK;
}

t_stat ch11_show_peer (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  fprintf (st, "peer=%s", peer[0] ? peer : "unspecified");
  return SCPE_OK;
}

t_stat ch11_set_peer (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
  char host[256], port[256];

  if ((cptr == NULL) || (*cptr == 0))
    return SCPE_ARG;
  if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;
  if (sim_parse_addr (cptr, host, sizeof host, NULL, port, sizeof port, NULL, NULL))
    return SCPE_ARG;
  if (host[0] == '\0')
    return SCPE_ARG;

  strncpy (peer, cptr, sizeof(peer) - 1);
  return SCPE_OK;
}

t_stat ch11_show_node (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  if (address == -1)
    fprintf (st, "node=unspecified");
  else
    fprintf (st, "node=%o", address);
  return SCPE_OK;
}

t_stat ch11_set_node (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
  t_stat r;
  int x;

  if ((cptr == NULL) || (*cptr == 0))
    return SCPE_ARG;
  if (uptr->flags & UNIT_ATT)
    return SCPE_ALATT;

  x = (int)get_uint (cptr, 8, 0177777, &r);
  if (r != SCPE_OK)
    return SCPE_ARG;

  address = x;
  return SCPE_OK;
}

const char *ch11_description (DEVICE *dptr)
{
  return "CH11 Chaosnet interface";
}

t_stat ch11_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  fprintf (st, "CH11 Chaosnet interface\n\n");
  fprintf (st, "It's a network interface for MIT's Chaosnet.  Options allow\n");
  fprintf (st, "control of the node address and network peer.  The node address must\n");
  fprintf (st, "be a 16-bit octal number.\n");
  fprint_set_help (st, dptr);
  fprintf (st, "\nConfigured options and controller state can be displayed with:\n");
  fprint_show_help (st, dptr);
  fprintf (st, "\nThe CH11 simulation will encapsulate Chaosnet packets in UDP or TCP.\n");
  fprintf (st, "To access the network, the simulated Chaosnet interface must be attached\n");
  fprintf (st, "to a network peer.\n\n");
  ch11_help_attach (st, dptr, uptr, flag, cptr);
  fprintf (st, "Software that runs on SIMH that supports this device include:\n");
  fprintf (st, " - ITS, the PDP-10 Incompatible Timesharing System\n");
  fprintf (st, "Outside SIMH, there's KLH10 and Lisp machine simulators.  Various\n");
  fprintf (st, "encapsulating transport mechanisms exist: UDP, IP, Ethernet.\n\n");
  fprintf (st, "Documentation:\n");
  fprintf (st, "https://lm-3.github.io/amber.html#Hardware-Programming-Documentation\n\n");
  return SCPE_OK;
}

t_stat ch11_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  fprintf (st, "To configure CH11, first set the local Chaosnet node address, and\n");
  fprintf (st, "the peer:\n\n");
  fprintf (st, "  sim> SET CH NODE=<octal address>\n");
  fprintf (st, "  sim> SET CH PEER=<remote host>:<remote port>\n\n");
  fprintf (st, "Then, attach a local port.  By default UDP is used:\n\n");
  fprintf (st, "  sim> ATTACH CH <local port>\n\n");
  fprintf (st, "If TCP is desired, add \"TCP\":\n\n");
  fprintf (st, "  sim> ATTACH CH <local port>,TCP\n\n");
  return SCPE_OK;
}
#endif
