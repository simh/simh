/* pdp11_ch.c: CH11 Chaosnet interface.
  ------------------------------------------------------------------------------

   Copyright (c) 2018, Lars Brinkhoff.

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

#if defined (VM_PDP11)
#include "pdp11_defs.h"

#elif defined (VM_PDP10)
#include "pdp10_defs.h"

#elif defined (VM_VAX)
#include "vax_defs.h"

#else
#error The CH11 device only works with Unibus machines.
#endif

#include "sim_tmxr.h"

/* CSR bits */
#define TIE   0000001 /* Timer interrupt enable */
#define LOOP  0000002 /* Loop back */
#define SPY   0000004 /* Loop back */
#define CRX   0000010 /* Clear receiver */
#define RXIE  0000020 /* Receive interrupt enable */
#define TXIE  0000040 /* Transmit interrupt enable */
#define TXA   0000100 /* Transmit abort */
#define TXD   0000200 /* Transmit done */
#define CTX   0000400 /* Clear transmitter */
#define LOST  0017000 /* Lost count */
#define RESET 0020000 /* Reset */
#define CRC   0040000 /* CRC error */
#define RXD   0100000 /* Receive done */

#define STATUS_BITS (TIE|LOOP|SPY|RXIE|TXIE|TXA|TXD|LOST|CRC|RXD)
#define COMMAND_BITS (TIE|LOOP|SPY|RXIE|TXIE)

BITFIELD ch_csr_bits[] = {
  BIT(TIE),
  BIT(LOOP),
  BIT(SPY),
  BIT(CRX),
  BIT(RXIE),
  BIT(TXIE),
  BIT(TXA),
  BIT(TXD),
  BIT(CTX),
  BITF(LOST,4),
  BIT(RESET),
  BIT(CRC),
  BIT(RXD),
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

t_stat ch_svc(UNIT *);
t_stat ch_reset (DEVICE *);
t_stat ch_attach (UNIT *, CONST char *);
t_stat ch_detach (UNIT *);
t_stat ch_rd(int32 *, int32, int32);
t_stat ch_wr(int32, int32, int32);
t_stat ch_show_peer (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat ch_set_peer (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat ch_show_node (FILE* st, UNIT* uptr, int32 val, CONST void* desc);
t_stat ch_set_node (UNIT* uptr, int32 val, CONST char* cptr, void* desc);
t_stat ch_help (FILE *, DEVICE *, UNIT *, int32, const char *);
t_stat ch_help_attach (FILE *, DEVICE *, UNIT *, int32, const char *);
const char *ch_description (DEVICE *);

static char peer[256];
static int status;
static int address = -1;
static int rx_count;
static int tx_count;
static uint8 rx_buffer[512+100];
static uint8 tx_buffer[512+100];

TMLN ch_lines[1] = { {0} };
TMXR ch_tmxr = { 1, NULL, 0, ch_lines};

UNIT ch_unit[] = {
  { UDATA (&ch_svc, UNIT_IDLE|UNIT_ATTABLE, 0) },
};

REG ch_reg[] = {
  { GRDATADF(CSR,   status,     16, 16, 0, "Control and status", ch_csr_bits), REG_FIT },
  { GRDATAD(RXCNT,  rx_count,   16, 16, 0, "Receive word count"), REG_FIT|REG_RO},
  { GRDATAD(TXCNT,  tx_count,   16, 16, 0, "Transmit word count"), REG_FIT|REG_RO},
  { BRDATAD(RXBUF,  rx_buffer,  16,  8, sizeof rx_buffer, "Receive packet buffer"), REG_FIT},
  { BRDATAD(TXBUF,  tx_buffer,  16,  8, sizeof tx_buffer, "Transmit packet buffer"), REG_FIT},
  { BRDATAD(PEER,   peer,       16,  8, sizeof peer, "Network peer"), REG_HRO},
  { GRDATAD(NODE,   address,    16, 16, 0, "Node address"), REG_HRO},
  { NULL }  };

MTAB ch_mod[] = {
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 010, "ADDRESS", "ADDRESS",
    &set_addr, &show_addr, NULL, "Unibus address" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "VECTOR", "VECTOR",
    &set_vec, &show_vec, NULL, "Interrupt vector" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "PEER", "PEER",
    &ch_set_peer, &ch_show_peer, NULL, "Remote host name and port" },
  { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "NODE", "NODE",
    &ch_set_node, &ch_show_node, NULL, "Chaosnet node address" },
  { 0 },
};

DIB ch_dib = {
  IOBA_AUTO, IOLN_CH, &ch_rd, &ch_wr,
  1, IVCL (CH), VEC_AUTO
};

DEBTAB ch_debug[] = {
    { "TRC",       DBG_TRC,   "Detailed trace" },
    { "REG",       DBG_REG,   "Hardware registers" },
    { "PKT",       DBG_PKT,   "Packets" },
    { "DAT",       DBG_DAT,   "Packet data" },
    { "INT",       DBG_INT,   "Interrupts" },
    { "ERR",       DBG_ERR,   "Error conditions" },
    { 0 }
};

DEVICE ch_dev = {
    "CH", ch_unit, ch_reg, ch_mod,
    1, 8, 16, 1, 8, 16,
    NULL, NULL, &ch_reset,
    NULL, &ch_attach, &ch_detach,
    &ch_dib, DEV_DISABLE | DEV_DIS | DEV_UBUS | DEV_DEBUG | DEV_MUX,
    0, ch_debug, NULL, NULL, &ch_help, &ch_help_attach, NULL,
    &ch_description
  };

int ch_checksum (const uint8 *p, int length)
{
  int i, sum = 0;
  for (i = 0; i < length; i += 2)
    sum += (p[i] << 8) + p[i+1];
  while (sum > 0xffff)
    sum = (sum & 0xffff) + (sum >> 16);
  return sum ^ 0xffff;
}

t_stat ch_rx_word (int32 *data)
{
  if (rx_count == 0) {
    *data = 0;
    sim_debug (DBG_ERR, &ch_dev, "Read empty buffer\n");
  } else {
    int i = 512-2*rx_count;
    *data = (rx_buffer[i] << 8) + rx_buffer[i+1];
    sim_debug (DBG_DAT, &ch_dev, "Read buffer word %d: %06o\n",
               rx_count, *data);
    rx_count--;
  }
  return SCPE_OK;
}

t_stat ch_tx_word (int data)
{
  if (tx_count < 126) {
    int i = CHUDP_HEADER + 2*tx_count;
    sim_debug (DBG_DAT, &ch_dev, "Write buffer word %d: %06o\n",
               tx_count, data);
    status &= ~TXD;
    tx_buffer[i] = data >> 8;
    tx_buffer[i+1] = data;
    tx_count++;
    return SCPE_OK;
  } else {
    sim_debug (DBG_ERR, &ch_dev, "Write buffer overflow\n");
    return SCPE_INCOMP;
  }
}

int ch_test_int (void)
{
  if ((status & (RXD|RXIE)) == (RXD|RXIE) ||
      (status & (TXD|TXIE)) == (TXD|TXIE)) {
    sim_debug (DBG_INT, &ch_dev, "%s %s Interrupt\n",
               status & RXD ? "RX" : "",
               status & TXD ? "TX" : "");
    SET_INT(CH);
    return 1;
  } else {
    CLR_INT(CH);
    return 0;
  }
}

t_stat ch_transmit ()
{
  size_t len;
  t_stat r;

  if (ch_tx_word (address) != SCPE_OK ||
      ch_tx_word (ch_checksum (tx_buffer + CHUDP_HEADER, 2*tx_count)) != SCPE_OK)
    return SCPE_INCOMP;

  tmxr_poll_tx (&ch_tmxr);
  len = CHUDP_HEADER + 2 * (size_t)tx_count;
  r = tmxr_put_packet_ln (&ch_lines[0], (const uint8 *)&tx_buffer, len);
  if (r == SCPE_OK) {
    sim_debug (DBG_PKT, &ch_dev, "Sent UDP packet, %d bytes.\n", (int)len);
    tmxr_poll_tx (&ch_tmxr);
    status |= TXD;
    ch_test_int ();
  } else
    sim_debug (DBG_ERR, &ch_dev, "Sending UDP failed: %d.\n", r);
  return SCPE_OK;
}

void ch_validate (const uint8 *p, int count)
{
  int chksum;
  int size;

  sim_debug (DBG_TRC, &ch_dev, "Packet opcode: %02x\n", p[0]);
  sim_debug (DBG_TRC, &ch_dev, "MBZ: %02x\n", p[1]);
  sim_debug (DBG_TRC, &ch_dev, "Forwarding count: %02x\n", p[2] >> 4);
  size = ((p[2] & 0xF) << 8) + p[3];
  sim_debug (DBG_TRC, &ch_dev, "Packet size: %03x\n", size);
  sim_debug (DBG_TRC, &ch_dev, "Destination address: %02x\n", (p[4] << 8) + p[5]);
  sim_debug (DBG_TRC, &ch_dev, "Destination index: %02x\n", (p[6] << 8) + p[7]);
  sim_debug (DBG_TRC, &ch_dev, "Source address: %02x\n", (p[8] << 8) + p[9]);
  sim_debug (DBG_TRC, &ch_dev, "Source index: %02x\n", (p[10] << 8) + p[11]);
  sim_debug (DBG_TRC, &ch_dev, "Packet number: %02x\n", (p[12] << 8) + p[13]);
  sim_debug (DBG_TRC, &ch_dev, "Acknowledgement: %02x\n", (p[14] << 8) + p[15]);
  
  if (p[1] != 0)
    sim_debug (DBG_ERR, &ch_dev, "Bad packet\n");

  chksum = ch_checksum (p, count);
  if (chksum != 0) {
    sim_debug (DBG_ERR, &ch_dev, "Checksum error: %05o\n", chksum);
    status |= CRC;
  } else
    sim_debug (DBG_TRC, &ch_dev, "Checksum: %05o\n", chksum);
}

void ch_receive (void)
{
  size_t count;
  const uint8 *p;

  tmxr_poll_rx (&ch_tmxr);
  if (tmxr_get_packet_ln (&ch_lines[0], &p, &count) != SCPE_OK) {
    sim_debug (DBG_ERR, &ch_dev, "TMXR error receiving packet\n");
    return;
  }
  if (p == NULL)
    return;

  sim_debug (DBG_PKT, &ch_dev, "Received UDP packet, %d bytes\n", (int)count);
  if ((status & RXD) == 0) {
    count -= CHUDP_HEADER;
    count = (count + 1) & 0776;
    memcpy (rx_buffer + (512 - count), p + CHUDP_HEADER, count);
    rx_count = count >> 1;
    sim_debug (DBG_TRC, &ch_dev, "Rx count, %d\n", rx_count);
    ch_validate (p + CHUDP_HEADER, count);
    status |= RXD;
    ch_lines[0].rcve = FALSE;
    sim_debug (DBG_TRC, &ch_dev, "Rx off\n");
    ch_test_int ();
  } else {
    sim_debug (DBG_ERR, &ch_dev, "Lost packet\n");
    if ((status & LOST) < LOST)
      status += 01000;
  }
}

t_stat ch_rd (int32 *data, int32 PA, int32 access)
{
  int reg = (PA >> 1) & 07;

  switch (reg) {
  case 00: /* Status */
    *data = status & STATUS_BITS;
    sim_debug (DBG_REG, &ch_dev, "Read status: %06o\n", *data);
    sim_debug_bits (DBG_TRC, &ch_dev, ch_csr_bits, *data, *data, TRUE);
    break;
  case 01: /* Address */
    *data = address;
    sim_debug (DBG_REG, &ch_dev, "Read address: %06o\n", *data);
    break;
  case 02: /* Read buffer */
    return ch_rx_word (data);
  case 03: /* Count */
    *data = ((16 * rx_count) - 1) & 07777;
    sim_debug (DBG_REG, &ch_dev, "Read bit count: %d\n", *data);
    break;
  case 05: /* Start */
    sim_debug (DBG_REG, &ch_dev, "Start transmission\n");
    *data = address;
    return ch_transmit ();
  default:
    *data = 0;
    break;
  }

  return SCPE_OK;
}

void ch_clear (void)
{
  status = TXD;
  rx_count = 0;
  tx_count = 0;

  tx_buffer[0] = 1; /* CHUDP header */
  tx_buffer[1] = 1;
  tx_buffer[2] = 0;
  tx_buffer[3] = 0;

  ch_test_int ();
}

void ch_command (int32 data)
{
  if (data & RESET) {
    /* Do this first so other bits can do their things. */
    sim_debug (DBG_REG, &ch_dev, "Reset\n");
    ch_clear ();
  }
  if (data & CRX) {
    sim_debug (DBG_REG, &ch_dev, "Clear RX\n");
    rx_count = 0;
    status &= ~(RXD|CRC|LOST);
    ch_lines[0].rcve = TRUE;
    sim_debug (DBG_TRC, &ch_dev, "Rx on\n");
    sim_activate_abs (ch_unit, 100);   /* Force next packet read attempt */
  }
  if (data & CTX) {
    sim_debug (DBG_REG, &ch_dev, "Clear TX\n");
    tx_count = 0;
    status |= TXD;
    status &= ~TXA;
  }
  ch_test_int ();
}

t_stat ch_wr (int32 data, int32 PA, int32 access)
{
  int reg = (PA >> 1) & 07;

  switch (reg) {
  case 00: /* Command */
    ch_command (data); /* Do this first in case of reset. */
    if (data & TIE)
      sim_debug (DBG_REG, &ch_dev, "Timer interrupt enable\n");
    if (data & LOOP)
      sim_debug (DBG_REG, &ch_dev, "Loopback\n");
    if (data & SPY)
      sim_debug (DBG_REG, &ch_dev, "Spy mode\n");
    if (data & RXIE)
      sim_debug (DBG_REG, &ch_dev, "RX interrupt enable\n");
    if (data & TXIE)
      sim_debug (DBG_REG, &ch_dev, "TX interrupt enable\n");
    status = (status & ~COMMAND_BITS) | (data & COMMAND_BITS);
    sim_debug_bits (DBG_TRC, &ch_dev, ch_csr_bits, status, status, TRUE);
    ch_test_int ();
    break;
  case 01: /* Write */
    return ch_tx_word (data);
  }

  return SCPE_OK;
}

t_stat ch_svc(UNIT *uptr)
{
  sim_clock_coschedule (uptr, 1000);
  (void)tmxr_poll_conn (&ch_tmxr);
  if (ch_lines[0].conn)
    ch_receive ();
  return SCPE_OK;
}

t_stat ch_attach (UNIT *uptr, CONST char *cptr)
{
  char linkinfo[256];
  t_stat r;

  if (address == -1)
    return sim_messagef (SCPE_2FARG, "Must set Chaosnet NODE address first \"SET CH NODE=val\"\n");
  if (peer[0] == '\0')
    return sim_messagef (SCPE_2FARG, "Must set Chaosnet PEER \"SET CH PEER=host:port\"\n");

  sprintf (linkinfo, "Buffer=%d,Line=%d,UDP,%s,PACKET,Connect=%s",
           (int)sizeof tx_buffer, 0, cptr, peer);
  r = tmxr_attach (&ch_tmxr, uptr, linkinfo);
  if (r != SCPE_OK) {
    sim_debug (DBG_ERR, &ch_dev, "TMXR error opening master\n");
    return sim_messagef (r, "Error Opening: %s\n", peer);
  }

  sim_clock_coschedule (uptr, 1000);        /* make sure polling starts */
  uptr->filename = (char *)realloc (uptr->filename, 1 + strlen (cptr));
  strcpy (uptr->filename, cptr);
  return SCPE_OK;
}

t_stat ch_detach (UNIT *uptr)
{
  sim_cancel (uptr);
  tmxr_detach (&ch_tmxr, uptr);
  return SCPE_OK;
}

t_stat ch_reset (DEVICE *dptr)
{
  DEVICE *ng_dptr = find_dev ("NG");

  if ((ng_dptr != NULL) && 
      !(ng_dptr->flags & DEV_DIS) &&
      !(dptr->flags & DEV_DIS)) {
    dptr->flags |= DEV_DIS;
    return sim_messagef (SCPE_ALATT, "CH device in conflict with NG.\n");
  }

  ch_clear ();

  if (dptr->units->flags & UNIT_ATT)
    sim_clock_coschedule (dptr->units, 1000);   /* poll for connections */

  return auto_config (dptr->name, (dptr->flags & DEV_DIS)? 0 : 1);  /* auto config */
}

t_stat ch_show_peer (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  fprintf (st, "peer=%s", peer[0] ? peer : "unspecified");
  return SCPE_OK;
}

t_stat ch_set_peer (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
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

  strlcpy (peer, cptr, sizeof peer);
  return SCPE_OK;
}

t_stat ch_show_node (FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
  if (address == -1)
    fprintf (st, "node=unspecified");
  else
    fprintf (st, "node=%o", address);
  return SCPE_OK;
}

t_stat ch_set_node (UNIT* uptr, int32 val, CONST char* cptr, void* desc)
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

const char *ch_description (DEVICE *dptr)
{
  return "CH11 Chaosnet interface";
}

t_stat ch_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
  fprintf (st, "CH11 Chaosnet interface\n\n");
  fprintf (st, "The CH11 is a Unibus device which can be used with PDP-11, VAX, and\n");
  fprintf (st, "KS10.  It's a network interface for MIT's Chaosnet.  Options allow\n");
  fprintf (st, "control of the node address and network peer.  The node address must\n");
  fprintf (st, "be a 16-bit octal number.\n");
  fprint_set_help (st, dptr);
  fprintf (st, "\nConfigured options and controller state can be displayed with:\n");
  fprint_show_help (st, dptr);
  fprintf (st, "\nThe CH11 simulation will encapsulate Chaosnet packets in UDP or TCP.\n");
  fprintf (st, "To access the network, the simulated Chaosnet interface must be attached\n");
  fprintf (st, "to a network peer.\n\n");
  ch_help_attach (st, dptr, uptr, flag, cptr);
  fprintf (st, "Software that runs on SIMH that supports this device include:\n");
  fprintf (st, " - ITS, the PDP-10 Incompatible Timesharing System\n");
  fprintf (st, " - Berkeley Unix with MIT patches\n");
  fprintf (st, " - MINITS, a PDP-11 Chaosnet router/terminal concentrator\n\n");
  fprintf (st, "Outside SIMH, there's KLH10 and Lisp machine simulators.  Various\n");
  fprintf (st, "encapsulating transport mechanisms exist: UDP, IP, Ethernet.\n\n");
  fprintf (st, "Documentation:\n");
  fprintf (st, "https://lm-3.github.io/amber.html#Hardware-Programming-Documentation\n\n");
  return SCPE_OK;
}

t_stat ch_help_attach (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
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
