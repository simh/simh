/* vax_xs.h: LANCE ethernet simulator

   Copyright (c) 2019, Matt Burke
   This module is partly based on the DEUNA simulator, Copyright (c) 2003-2011, David T. Hittner

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
   THE AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name(s) of the author(s) shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from the author(s).

   xs           LANCE Ethernet Controller
*/

#include "vax_defs.h"
#include "sim_ether.h"

#define XS_QUE_MAX       500                            /* message queue array */
#define XS_FILTER_MAX    11                             /* mac + 10 multicast addrs */

struct xs_setup {
    int32               promiscuous;                    /* promiscuous mode enabled */
    int32               multicast;                      /* enable all multicast addresses */
    uint32              mult0;
    uint32              mult1;
    int32               mac_count;                      /* number of multicast mac addresses */
    ETH_MAC             macs[XS_FILTER_MAX];            /* MAC addresses to respond to */
};

struct xs_device {
                                                        /*+ initialized values - DO NOT MOVE */
    ETH_PCALLBACK       rcallback;                      /* read callback routine */
    ETH_PCALLBACK       wcallback;                      /* write callback routine */
                                                        /*- initialized values - DO NOT MOVE */

                                                        /* I/O register storage */
    uint32              irq;                            /* interrupt request flag */

    ETH_MAC             mac;                            /* MAC address */
    ETH_DEV*            etherface;                      /* buffers, etc. */
    ETH_PACK            read_buffer;
    ETH_PACK            write_buffer;
    ETH_QUE             ReadQ;
    struct xs_setup     setup;

    uint16              csr0;                           /* LANCE registers */
    uint16              csr1;
    uint16              csr2;
    uint16              csr3;
    uint16              rptr;                           /* register pointer */
    uint16              mode;                           /* mode register */
    uint32              inbb;                           /* initialisation block base */

    uint32              tdrb;                           /* transmit desc ring base */
    uint32              telen;                          /* transmit desc ring entry len */
    uint32              trlen;                          /* transmit desc ring length */
    uint32              txnext;                         /* transmit buffer pointer */
    uint32              rdrb;                           /* receive desc ring base */
    uint32              relen;                          /* receive desc ring entry len */
    uint32              rrlen;                          /* receive desc ring length */
    uint32              rxnext;                         /* receive buffer pointer */

    uint16              rxhdr[4];                       /* content of RX ring entry, during wait */
    uint16              txhdr[4];                       /* content of TX ring entry, during xmit */
};

struct xs_controller {
    DEVICE*             dev;                            /* device block */
    UNIT*               unit;                           /* unit block */
    DIB*                dib;                            /* device interface block */
    struct xs_device*   var;                            /* controller-specific variables */
};

typedef struct xs_controller CTLR;

/* CSR definitions */
#define CSR0_ESUM       0x8000                          /* <15> error summary */
#define CSR0_BABL       0x4000                          /* <14> transmitter timeout */
#define CSR0_CERR       0x2000                          /* <13> collision error */
#define CSR0_MISS       0x1000                          /* <12> missed packet */
#define CSR0_MERR       0x0800                          /* <11> memory error */
#define CSR0_RINT       0x0400                          /* <10> receive interrupt */
#define CSR0_TINT       0x0200                          /* <09> transmit interrupt */
#define CSR0_IDON       0x0100                          /* <08> initialisation done */
#define CSR0_INTR       0x0080                          /* <07> interrupt reqest */
#define CSR0_RXON       0x0020                          /* <05> receiver on */
#define CSR0_TXON       0x0010                          /* <04> transmitter on */
#define CSR0_TDMD       0x0008                          /* <03> transmitter demand */
#define CSR0_STOP       0x0004                          /* <02> stop */
#define CSR0_STRT       0x0002                          /* <01> start */
#define CSR0_INIT       0x0001                          /* <00> initialise */
#define CSR0_RW         (CSR_IE)
#define CSR0_W1C        (CSR0_IDON | CSR0_TINT | CSR0_RINT | \
                         CSR0_MERR | CSR0_MISS | CSR0_CERR | \
                         CSR0_BABL)
#define CSR0_ERR        (CSR0_BABL | CSR0_CERR | CSR0_MISS | \
                         CSR0_MERR)

/* Mode definitions */
#define MODE_PROM       0x8000                          /* <15> Promiscuous Mode */
#define MODE_INTL       0x0040                          /* <06> Internal Loopback */
#define MODE_DRTY       0x0020                          /* <05> Disable Retry */
#define MODE_COLL       0x0010                          /* <04> Force Collision */
#define MODE_DTCR       0x0008                          /* <03> Disable Transmit CRC */
#define MODE_LOOP       0x0004                          /* <02> Loopback */
#define MODE_DTX        0x0002                          /* <01> Disable Transmitter */
#define MODE_DRX        0x0001                          /* <00> Disable Receiver */

/* Transmitter Ring definitions */
#define TXR_OWN         0x8000                          /* <15> we own it (1) */
#define TXR_ERRS        0x4000                          /* <14> error summary */
#define TXR_MORE        0x1000                          /* <12> Mult Retries Needed */
#define TXR_ONE         0x0800                          /* <11> One Collision */
#define TXR_DEF         0x0400                          /* <10> Deferred */
#define TXR_STF         0x0200                          /* <09> Start Of Frame */
#define TXR_ENF         0x0100                          /* <08> End Of Frame */
#define TXR_HADR        0x00FF                          /* <7:0> High order buffer address */
#define TXR_BUFL        0x8000                          /* <15> Buffer Length Error */
#define TXR_UFLO        0x4000                          /* <14> Underflow Error */
#define TXR_LCOL        0x1000                          /* <12> Late Collision */
#define TXR_LCAR        0x0800                          /* <11> Lost Carrier */
#define TXR_RTRY        0x0400                          /* <10> Retry Failure (16x) */
#define TXR_TDR         0x01FF                          /* <9:0> TDR value if RTRY=1 */

/* Receiver Ring definitions */
#define RXR_OWN         0x8000                          /* <15> we own it (1) */
#define RXR_ERRS        0x4000                          /* <14> Error Summary */
#define RXR_FRAM        0x2000                          /* <13> Frame Error */
#define RXR_OFLO        0x1000                          /* <12> Message Overflow */
#define RXR_CRC         0x0800                          /* <11> CRC Check Error */
#define RXR_BUFL        0x0400                          /* <10> Buffer Length error */
#define RXR_STF         0x0200                          /* <09> Start Of Frame */
#define RXR_ENF         0x0100                          /* <08> End Of Frame */
#define RXR_HADR        0x00FF                          /* <7:0> High order buffer address */
#define RXR_MLEN        0x0FFF                          /* <11:0> Message Length */

BITFIELD xs_tdes_w1[] = {
  BITNCF(8), BIT(ENP), BIT(STP), BIT(DEF), BIT(ONE), BIT(MORE), BIT(FCS), BIT(ERR), BIT(OWN),
  ENDBITS
};
BITFIELD xs_tdes_w2[] = {
  BITFFMT(mlen,12,"0x%X"),
  ENDBITS
};

BITFIELD xs_rdes_w1[] = {
  BITNCF(8), BIT(ENP), BIT(STP), BIT(BUFL), BIT(CRC), BIT(OFLO), BIT(FRAM), BIT(ERRS), BIT(OWN),
  ENDBITS
};
BITFIELD xs_rdes_w2[] = {
  BITFFMT(blen,12,"0x%X"),
  ENDBITS
};
BITFIELD xs_rdes_w3[] = {
  BITFFMT(mlen,12,"0x%X"),
  ENDBITS
};

/* Debug definitions */
#define DBG_TRC         0x0001
#define DBG_REG         0x0002
#define DBG_PCK         0x0004
#define DBG_DAT         0x0008
#define DBG_ETH         0x0010
