/* ka10_ch.c: CH10 Chaosnet interface.
  ------------------------------------------------------------------------------

   Copyright (c) 2019, Richard Cornwell, original by Lars Brinkhoff.

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

#ifndef NUM_DEVS_CH10
#define NUM_DEVS_CH10 0
#endif

#if (NUM_DEVS_CH10 > 0)
#define CH_DEVNUM      0470

/* CSR bits */
#define PIA   00000007LL /* Interrupt channel */
#define TXIE  00000010LL /* Transmit interrupt enable */
#define RXIE  00000020LL /* Receive interrupt enable */
#define SPY   00000040LL /* Spy */
#define LOOP  00000100LL /* Loop back */
#define SWAP  00000200LL /* Swap bytes */
#define HALF  00000400LL /* Halfword output */
#define TXD   00001000LL /* Transmit done */
#define RXD   00002000LL /* Receive done */
#define TXA   00004000LL /* Transmit abort */
#define CTX   00004000LL /* Clear transmitter */
#define LOST  00170000LL /* Lost count */
#define RESET 00010000LL /* Reset */
#define CRC   00200000LL /* CRC error */
#define WLE   00400000LL /* Word length error */
#define PLE   01000000LL /* Packet length error */
#define OVER  02000000LL /* Overrun */

#define STATUS_BITS (PIA|TXIE|RXIE|SPY|LOOP|SWAP|HALF|TXA|LOST|CRC|WLE|PLE|OVER)

BITFIELD ch10_csr_bits[] = {
  BIT(PIA),
  BIT(TXIE),
  BIT(RXIE),
  BIT(SPY),
  BIT(LOOP),
  BIT(SWAP),
  BIT(HALF),
  BIT(TXD),
  BIT(RXD),
  BIT(TXA),
  BIT(CTX),
  BITF(LOST,4),
  BIT(CRC),
  BIT(WLE),
  BIT(PLE),
  BIT(OVER),
  ENDBITS
};

#define CHUDP_HEADER 4
#define IOLN_CH 020
#define DBG_TRC  0x0001
#define DBG_REG  0x0002
#define DBG_PKT  0x0004
#define DBG_DAT  0x0008
#define DBG_INT  0x0010
#define DBG_ERR  0x0020

t_stat ch10_svc(UNIT *);
t_stat ch10_reset (DEVICE *);
t_stat ch10_attach (UNIT *, CONST char *);
t_stat ch10_detach (UNIT *);
t_stat ch10_devio(uint32 dev, uint64 *data);
t_stat ch10_show_peer (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat ch10_set_peer (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat ch10_show_node (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat ch10_set_node (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat ch10_help (FILE *, DEVICE *, UNIT *, int32, const char *);
t_stat ch10_help_attach (FILE *, DEVICE *, UNIT *, int32, const char *);
const char *ch10_description (DEVICE *);

static char peer[256];
int address;
static uint64 ch10_status;
static int rx_count;
static int tx_count;
static uint8 rx_buffer[512+100];
static uint8 tx_buffer[512+100];

TMLN ch10_lines[1] = { {0} };
TMXR ch10_tmxr = { 1, NULL, 0, ch10_lines};

UNIT ch10_unit[] = {
  {UDATA (&ch10_svc, UNIT_IDLE|UNIT_ATTABLE, 0) },
};

REG ch10_reg[] = {
  { GRDATADF(CSR,   ch10_status,     16, 16, 0, "Control and status", ch10_csr_bits), REG_FIT },
  { GRDATAD(RXCNT,  rx_count,   16, 16, 0, "Receive word count"), REG_FIT|REG_RO},
  { GRDATAD(TXCNT,  tx_count,   16, 16, 0, "Transmit word count"), REG_FIT|REG_RO},
  { BRDATAD(RXBUF,  rx_buffer,  16,  8, sizeof rx_buffer, "Receive packet buffer"), REG_FIT},
  { BRDATAD(TXBUF,  tx_buffer,  16,  8, sizeof tx_buffer, "Transmit packet buffer"), REG_FIT},
  { BRDATAD(PEER,   peer,       16,  8, sizeof peer, "Network peer"), REG_HRO},
  { GRDATAD(NODE,   address,    16, 16, 0, "Node address"), REG_HRO},
  { NULL }  };

MTAB ch10_mod[] = {
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "PEER", "PEER",
    &ch10_set_peer, &ch10_show_peer, NULL, "Remote host name and port" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "NODE", "NODE",
    &ch10_set_node, &ch10_show_node, NULL, "Chaosnet node address" },
  { 0 },
};

DIB ch10_dib = {CH_DEVNUM, 1, &ch10_devio, NULL};

DEBTAB ch10_debug[] = {
    { "TRC",       DBG_TRC,   "Detailed trace" },
    { "REG",       DBG_REG,   "Hardware registers" },
    { "PKT",       DBG_PKT,   "Packets" },
    { "DAT",       DBG_DAT,   "Packet data" },
    { "INT",       DBG_INT,   "Interrupts" },
    { "ERR",       DBG_ERR,   "Error conditions" },
    { 0 }
};

DEVICE ch10_dev = {
    "CH", ch10_unit, ch10_reg, ch10_mod,
    1, 8, 16, 1, 8, 16,
    NULL, NULL, &ch10_reset,
    NULL, &ch10_attach, &ch10_detach,
    &ch10_dib, DEV_DISABLE | DEV_DIS | DEV_DEBUG | DEV_MUX,
    0, ch10_debug, NULL, NULL, &ch10_help, &ch10_help_attach, NULL,
    &ch10_description
  };

uint16 ch10_checksum (const uint8 *p, int count)
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


int ch10_test_int (void)
{
  if ((ch10_status & (RXD|RXIE)) == (RXD|RXIE) ||
      (ch10_status & (TXD|TXIE)) == (TXD|TXIE)) {
    sim_debug (DBG_INT, &ch10_dev, "%s %s Interrupt\n",
               ch10_status & RXD ? "RX" : "",
               ch10_status & TXD ? "TX" : "");
    set_interrupt(CH_DEVNUM, ch10_status & PIA);
    return 1;
  } else {
    clr_interrupt(CH_DEVNUM);
    return 0;
  }
}

void ch10_validate (const uint8 *p, int count)
{
  uint16 chksum;
  int size;

  sim_debug (DBG_TRC, &ch10_dev, "Packet opcode: %02x\n", p[0]);
  sim_debug (DBG_TRC, &ch10_dev, "MBZ: %02x\n", p[1]);
  sim_debug (DBG_TRC, &ch10_dev, "Forwarding count: %02x\n", p[2] >> 4);
  size = ((p[2] & 0xF) << 8) + p[3];
  sim_debug (DBG_TRC, &ch10_dev, "Packet size: %03x\n", size);
  sim_debug (DBG_TRC, &ch10_dev, "Destination address: %o\n", (p[4] << 8) + p[5]);
  sim_debug (DBG_TRC, &ch10_dev, "Destination index: %02x\n", (p[6] << 8) + p[7]);
  sim_debug (DBG_TRC, &ch10_dev, "Source address: %o\n", (p[8] << 8) + p[9]);
  sim_debug (DBG_TRC, &ch10_dev, "Source index: %02x\n", (p[10] << 8) + p[11]);
  sim_debug (DBG_TRC, &ch10_dev, "Packet number: %02x\n", (p[12] << 8) + p[13]);
  sim_debug (DBG_TRC, &ch10_dev, "Acknowledgement: %02x\n", (p[14] << 8) + p[15]);

  if (p[1] != 0)
    sim_debug (DBG_ERR, &ch10_dev, "Bad packet\n");

  chksum = ch10_checksum (p, count);
  if (chksum != 0) {
    sim_debug (DBG_ERR, &ch10_dev, "Checksum error: %04x\n", chksum);
    ch10_status |= CRC;
  } else
    sim_debug (DBG_TRC, &ch10_dev, "Checksum: %05o\n", chksum);
}

t_stat ch10_transmit ()
{
  size_t len;
  t_stat r;
  int i = CHUDP_HEADER + tx_count;
  uint16 chk;

  if (tx_count > (512 - CHUDP_HEADER)) {
    sim_debug (DBG_PKT, &ch10_dev, "Pack size failed, %d bytes.\n", (int)tx_count);
    ch10_status |= PLE;
    return SCPE_INCOMP;
  }
  tx_buffer[i] = tx_buffer[8+CHUDP_HEADER];
  tx_buffer[i+1] = tx_buffer[9+CHUDP_HEADER];
  tx_count += 2;
  chk = ch10_checksum(tx_buffer + CHUDP_HEADER, tx_count);
  tx_buffer[i+2] = (chk >> 8) & 0xff;
  tx_buffer[i+3] = chk & 0xff;
  tx_count += 2;

  tmxr_poll_tx (&ch10_tmxr);
  len = CHUDP_HEADER + (size_t)tx_count;
  r = tmxr_put_packet_ln (&ch10_lines[0], (const uint8 *)&tx_buffer, len);
  if (r == SCPE_OK) {
    sim_debug (DBG_PKT, &ch10_dev, "Sent UDP packet, %d bytes.\n", (int)len);
    tmxr_poll_tx (&ch10_tmxr);
  } else {
    sim_debug (DBG_ERR, &ch10_dev, "Sending UDP failed: %d.\n", r);
    ch10_status |= OVER;
  }
  tx_count = 0;
  ch10_test_int ();
  return SCPE_OK;
}

void ch10_receive (void)
{
  size_t count;
  const uint8 *p;
  uint16 dest;

  tmxr_poll_rx (&ch10_tmxr);
  if (tmxr_get_packet_ln (&ch10_lines[0], &p, &count) != SCPE_OK) {
    sim_debug (DBG_ERR, &ch10_dev, "TMXR error receiving packet\n");
    return;
  }
  if (p == NULL)
    return;
  dest = ((p[4+CHUDP_HEADER] & 0xff) << 8) + (p[5+CHUDP_HEADER] & 0xff);

  sim_debug (DBG_PKT, &ch10_dev, "Received UDP packet, %d bytes for: %o\n", (int)count, dest);
  /* Check if packet for us. */
  if (dest != address && dest != 0 && (ch10_status & SPY) == 0)
    return;

  if ((RXD & ch10_status) == 0) {
    count = (count + 1) & 0776;
    memcpy (rx_buffer + (512 - count), p, count);
    rx_count = count;
    sim_debug (DBG_TRC, &ch10_dev, "Rx count, %d\n", rx_count);
    ch10_validate (p + CHUDP_HEADER, count - CHUDP_HEADER);
    ch10_status |= RXD;
    ch10_lines[0].rcve = FALSE;
    sim_debug (DBG_TRC, &ch10_dev, "Rx off\n");
    ch10_test_int ();
  } else {
    sim_debug (DBG_ERR, &ch10_dev, "Lost packet\n");
    if ((ch10_status & LOST) < LOST)
      ch10_status += 01000;
  }
}

void ch10_clear (void)
{
  ch10_status = TXD;
  rx_count = 0;
  tx_count = 0;

  tx_buffer[0] = 1; /* CHUDP header */
  tx_buffer[1] = 1;
  tx_buffer[2] = 0;
  tx_buffer[3] = 0;
  ch10_lines[0].rcve = TRUE;

  ch10_test_int ();
}

void ch10_command (uint32 data)
{
  if (data & RXD) {
     sim_debug (DBG_REG, &ch10_dev, "Clear RX\n");
     ch10_status &= ~RXD;
     rx_count = 0;
     ch10_lines[0].rcve = TRUE;
     rx_count = 0;
  }
  if (data & RESET) {
    /* Do this first so other bits can do their things. */
    sim_debug (DBG_REG, &ch10_dev, "Reset\n");
    ch10_clear ();
  }
  if (data & CTX) {
    sim_debug (DBG_REG, &ch10_dev, "Clear TX\n");
    tx_count = 0;
    ch10_status |= TXD;
    ch10_status &= ~TXA;
  }
  if (data & TXD) {
    sim_debug (DBG_REG, &ch10_dev, "XMIT TX\n");
    ch10_transmit();
    ch10_status &= ~TXA;
  }
}

t_stat ch10_devio(uint32 dev, uint64 *data)
{
    DEVICE *dptr = &imx_dev;

    switch(dev & 07) {
    case CONO:
        sim_debug (DBG_REG, &ch10_dev, "CONO %012llo %012llo \n", *data, ch10_status);
        ch10_command ((uint32)(*data & RMASK));
        ch10_status &= ~STATUS_BITS;
        ch10_status |= *data & STATUS_BITS;
        ch10_test_int ();
        break;
    case CONI:
        *data = ch10_status & (STATUS_BITS|TXD|RXD);
        *data |= (uint64)address << 20;
        break;
    case DATAO:
        ch10_status &= ~TXD;
        if (tx_count < 512) {
          int i = CHUDP_HEADER + tx_count;
          if (ch10_status & SWAP) {
             tx_buffer[i] = (*data >> 20) & 0xff;
             tx_buffer[i+1] = (*data >> 28) & 0xff;
          } else {
             tx_buffer[i] = (*data >> 28) & 0xff;
             tx_buffer[i+1] = (*data >> 20) & 0xff;
          }
          tx_count+=2;
          if ((ch10_status & HALF) == 0) {
              if (ch10_status & SWAP) {
                  tx_buffer[i+2] = (*data >> 4) & 0xff;
                  tx_buffer[i+3] = (*data >> 12) & 0xff;
              } else {
                  tx_buffer[i+2] = (*data >> 12) & 0xff;
                  tx_buffer[i+3] = (*data >> 4) & 0xff;
              }
              tx_count+=2;
          }
          sim_debug (DBG_DAT, &ch10_dev, "Write buffer word %d:%02x %02x %02x %02x %012llo %012llo\n",
                     tx_count, tx_buffer[i], tx_buffer[i+1], tx_buffer[i+2], tx_buffer[i+3], *data, ch10_status);
          return SCPE_OK;
        } else {
          sim_debug (DBG_ERR, &ch10_dev, "Write buffer overflow\n");
          ch10_status |= PLE;
          return SCPE_OK;
        }
    case DATAI:
        if (rx_count == 0) {
          *data = 0;
          sim_debug (DBG_ERR, &ch10_dev, "Read empty buffer\n");
        } else {
          int i = 512-rx_count;
          ch10_status &= ~RXD;
          if (ch10_status & SWAP) {
              *data = ((uint64)(rx_buffer[i]) & 0xff) << 20;
              *data |= ((uint64)(rx_buffer[i+1]) & 0xff) << 28;
              *data |= ((uint64)(rx_buffer[i+2]) & 0xff) << 4;
              *data |= ((uint64)(rx_buffer[i+3]) & 0xff) << 12;
          } else {
              *data = ((uint64)(rx_buffer[i]) & 0xff) << 28;
              *data |= ((uint64)(rx_buffer[i+1]) & 0xff) << 20;
              *data |= ((uint64)(rx_buffer[i+2]) & 0xff) << 12;
              *data |= ((uint64)(rx_buffer[i+3]) & 0xff) << 4;
          }
          rx_count-=4;
          sim_debug (DBG_DAT, &ch10_dev, "Read buffer word %d:%02x %02x %02x %02x %012llo %012llo\n",
                     rx_count, rx_buffer[i], rx_buffer[i+1], rx_buffer[i+2], rx_buffer[i+3], *data, ch10_status);
        }
    }

    return SCPE_OK;
}

t_stat ch10_svc(UNIT *uptr)
{
  sim_clock_coschedule (uptr, 1000);
  (void)tmxr_poll_conn (&ch10_tmxr);
  if (ch10_lines[0].conn) {
    ch10_receive ();
  }
  if (tx_count == 0)
    ch10_status |= TXD;
  ch10_test_int ();
  return SCPE_OK;
}

t_stat ch10_attach (UNIT *uptr, CONST char *cptr)
{
  char linkinfo[256];
  t_stat r;

  ch10_dev.dctrl |= 0xF77F0000;
  if (address == -1)
    return sim_messagef (SCPE_2FARG, "Must set Chaosnet NODE address first \"SET CH NODE=val\"\n");
  if (peer[0] == '\0')
    return sim_messagef (SCPE_2FARG, "Must set Chaosnet PEER \"SET CH PEER=host:port\"\n");

  snprintf (linkinfo, sizeof(linkinfo), "Buffer=%d,UDP,%s,PACKET,Connect=%.*s,Line=0",
           (int)sizeof tx_buffer, cptr, (int)(sizeof(linkinfo) - (45 + strlen(cptr))), peer);
  r = tmxr_attach (&ch10_tmxr, uptr, linkinfo);
  if (r != SCPE_OK) {
    sim_debug (DBG_ERR, &ch10_dev, "TMXR error opening master\n");
    return sim_messagef (r, "Error Opening: %s\n", peer);
  }

  uptr->filename = (char *)realloc (uptr->filename, 1 + strlen (cptr));
  strcpy (uptr->filename, cptr);
  sim_activate (uptr, 1000);
  return SCPE_OK;
}

t_stat ch10_detach (UNIT *uptr)
{
  sim_cancel (uptr);
  tmxr_detach (&ch10_tmxr, uptr);
  return SCPE_OK;
}

t_stat ch10_reset (DEVICE *dptr)
{
  ch10_clear ();
  if (ch10_unit[0].flags & UNIT_ATT)
      sim_activate (&ch10_unit[0], 100);
  return SCPE_OK;
}

t_stat ch10_show_peer (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  fprintf (st, "peer=%s", peer[0] ? peer : "unspecified");
  return SCPE_OK;
}

t_stat ch10_set_peer (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
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

t_stat ch10_show_node (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  if (address == -1)
    fprintf (st, "node=unspecified");
  else
    fprintf (st, "node=%o", address);
  return SCPE_OK;
}

t_stat ch10_set_node (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
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

const char *ch10_description (DEVICE *dptr)
{
  return "CH11 Chaosnet interface";
}

t_stat ch10_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  fprintf (st, "CH10 Chaosnet interface\n\n");
  fprintf (st, "It's a network interface for MIT's Chaosnet.  Options allow\n");
  fprintf (st, "control of the node address and network peer.  The node address must\n");
  fprintf (st, "be a 16-bit octal number.\n");
  fprint_set_help (st, dptr);
  fprintf (st, "\nConfigured options and controller state can be displayed with:\n");
  fprint_show_help (st, dptr);
  fprintf (st, "\nThe CH10 simulation will encapsulate Chaosnet packets in UDP or TCP.\n");
  fprintf (st, "To access the network, the simulated Chaosnet interface must be attached\n");
  fprintf (st, "to a network peer.\n\n");
  ch10_help_attach (st, dptr, uptr, flag, cptr);
  fprintf (st, "Software that runs on SIMH that supports this device include:\n");
  fprintf (st, " - ITS, the PDP-10 Incompatible Timesharing System\n");
  fprintf (st, "Outside SIMH, there's KLH10 and Lisp machine simulators.  Various\n");
  fprintf (st, "encapsulating transport mechanisms exist: UDP, IP, Ethernet.\n\n");
  fprintf (st, "Documentation:\n");
  fprintf (st, "https://lm-3.github.io/amber.html#Hardware-Programming-Documentation\n\n");
  return SCPE_OK;
}

t_stat ch10_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  fprintf (st, "To configure CH10, first set the local Chaosnet node address, and\n");
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
