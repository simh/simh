/* vax_xs.c: LANCE ethernet simulator

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

#include "vax_xs.h"

#if defined(VAX_410)
#include "vax_ka410_xs_bin.h"
#else
#define BOOT_CODE_ARRAY NULL
#define BOOT_CODE_SIZE  0
#endif

#define GETB(p,x)         (p[x])
#define GETW(p,x)         ((p[(x + 1)] << 8) | p[x])
#define GETL(p,x)         ((p[(x + 3)] << 24) | (p[(x + 2)] << 16) | (p[(x + 1)] << 8) | p[x])

extern int32 tmxr_poll;

t_stat xs_svc (UNIT *uptr);
void xs_process_receive(CTLR* xs);
void xs_process_transmit (CTLR* xs);
void xs_dump_rxring(CTLR* xs);
void xs_dump_txring(CTLR* xs);
t_stat xs_init (CTLR* xs);
void xs_updateint(CTLR* xs);
void xs_setint (CTLR* xs);
void xs_clrint (CTLR* xs);
void xs_read_callback (int status);
void xs_write_callback (int status);
t_stat xs_reset (DEVICE *dptr);
t_stat xs_attach (UNIT *uptr, CONST char *cptr);
t_stat xs_detach (UNIT* uptr);
t_stat xs_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
const char *xs_description (DEVICE *dptr);

/* XS data structures

   xs_dev       XS device descriptor
   xs_unit      XS unit list
   xs_reg       XS register list
   xs_mod       XS modifier list
*/

struct xs_device  xs_var = {
    xs_read_callback,                      /* read callback routine */
    xs_write_callback,                     /* write callback routine */
    };

DIB xs_dib = {
    XS_ROM_INDEX, BOOT_CODE_ARRAY, BOOT_CODE_SIZE
    };

UNIT xs_unit = {
   UDATA (&xs_svc, UNIT_IDLE | UNIT_ATTABLE | UNIT_DISABLE, 0)
   };

REG xs_reg[] = {
    { GRDATA  ( SA0,    xs_var.mac[0],        16,  8, 0), REG_RO|REG_FIT },
    { GRDATA  ( SA1,    xs_var.mac[1],        16,  8, 0), REG_RO|REG_FIT },
    { GRDATA  ( SA2,    xs_var.mac[2],        16,  8, 0), REG_RO|REG_FIT },
    { GRDATA  ( SA3,    xs_var.mac[3],        16,  8, 0), REG_RO|REG_FIT },
    { GRDATA  ( SA4,    xs_var.mac[4],        16,  8, 0), REG_RO|REG_FIT },
    { GRDATA  ( SA5,    xs_var.mac[5],        16,  8, 0), REG_RO|REG_FIT },
    { FLDATA  ( INT,    xs_var.irq, 0) },
    { BRDATA  ( SETUP,  &xs_var.setup,   DEV_RDX,  8, sizeof(xs_var.setup)), REG_HRO },
    { GRDATA  ( CSR0,   xs_var.csr0,     DEV_RDX, 16, 0), REG_FIT },
    { GRDATA  ( CSR1,   xs_var.csr1,     DEV_RDX, 16, 0), REG_FIT },
    { GRDATA  ( CSR2,   xs_var.csr2,     DEV_RDX, 16, 0), REG_FIT },
    { GRDATA  ( CSR3,   xs_var.csr3,     DEV_RDX, 16, 0), REG_FIT },
    { GRDATA  ( MODE,   xs_var.mode,     DEV_RDX, 16, 0), REG_FIT },
    { GRDATA  ( RPTR,   xs_var.rptr,     DEV_RDX, 16, 0), REG_FIT },
    { GRDATA  ( INBB,   xs_var.inbb,     DEV_RDX, 32, 0), REG_FIT },
    { GRDATA  ( TDRB,   xs_var.tdrb,     DEV_RDX, 32, 0), REG_FIT },
    { GRDATA  ( TELEN,  xs_var.telen,    DEV_RDX, 32, 0), REG_FIT },
    { GRDATA  ( TRLEN,  xs_var.trlen,    DEV_RDX, 32, 0), REG_FIT },
    { GRDATA  ( TXNEXT, xs_var.txnext,   DEV_RDX, 32, 0), REG_FIT },
    { GRDATA  ( RDRB,   xs_var.rdrb,     DEV_RDX, 32, 0), REG_FIT },
    { GRDATA  ( RELEN,  xs_var.relen,    DEV_RDX, 32, 0), REG_FIT },
    { GRDATA  ( RRLEN,  xs_var.rrlen,    DEV_RDX, 32, 0), REG_FIT },
    { GRDATA  ( RXNEXT, xs_var.rxnext,   DEV_RDX, 32, 0), REG_FIT },
    { BRDATA  ( RXHDR,  xs_var.rxhdr,    DEV_RDX, 16, 4), REG_HRO },
    { BRDATA  ( TXHDR,  xs_var.txhdr,    DEV_RDX, 16, 4), REG_HRO },
    { FLDATAD ( INT,    int_req[IPL_XS1], INT_V_XS1, "interrupt pending flag") },
    { NULL }
    };

DEBTAB xs_debug[] = {
    {"TRACE",  DBG_TRC, "trace routine calls"},
    {"REG",    DBG_REG, "read/write registers"},
    {"PACKET", DBG_PCK, "packet headers"},
    {"DATA",   DBG_DAT, "packet data"},
    {"ETH",    DBG_ETH, "ethernet device"},
    { 0 }
};

MTAB xs_mod[] = {
    { MTAB_XTD | MTAB_VDV | MTAB_NMO, 0, "ETH", NULL,
      NULL, &eth_show, NULL, "Display attachable devices" },
    { 0 }
    };

DEVICE xs_dev = {
    "XS", &xs_unit, xs_reg, xs_mod,
    1, DEV_RDX, 20, 1, DEV_RDX, 8,
    NULL, NULL, &xs_reset,
    NULL, &xs_attach, &xs_detach,
    &xs_dib, DEV_DEBUG | XS_FLAGS, 0,
    xs_debug, NULL, NULL, &xs_help, NULL, NULL,
    &xs_description
    };

CTLR xs_ctrl[] = {
   {&xs_dev, &xs_unit, NULL, &xs_var}       /* XS controller */
   };

/* XS read

   200E0000             register data port
   200E0004             register address port
*/

int32 xs_rd (int32 pa)
{
CTLR *xs = &xs_ctrl[0];
int32 rg = (pa >> 2) & 3;
int32 data = 0;

switch (rg) {

    case 0:                                             /* NI_RDP */
        switch (xs->var->rptr) {

            case 0:                                     /* NI_CSR0 */
                data = xs->var->csr0;
                if (data & CSR0_ERR)
                    data |= CSR0_ESUM;                  /* error bits set */
                break;

            case 1:                                     /* NI_CSR1 */
                data = xs->var->csr1;
                break;

            case 2:                                     /* NI_CSR2 */
                data = xs->var->csr2;
                break;

            case 3:                                     /* NI_CSR3 */
                data = xs->var->csr3;
                break;
                }
        sim_debug(DBG_REG, &xs_dev, "reg %d read, value = %X, PC = %08X\n", xs->var->rptr, data, fault_PC);
        break;

    case 1:                                             /* NI_RAP */
        data = xs->var->rptr;
        /* sim_debug(DBG_REG, &xs_dev, "reg ptr read, value = %X\n", data); */
        break;
        }

return data;
}

/* XS write

   200E0000             register data port
   200E0004             register address port
*/

void xs_wr (int32 pa, int32 data, int32 access)
{
CTLR *xs = &xs_ctrl[0];
int32 rg = (pa >> 2) & 3;

switch (rg) {

    case 0:                                             /* NI_RDP */
        switch (xs->var->rptr) {

            case 0:                                     /* NI_CSR0 */
                xs->var->csr0 = (xs->var->csr0 & ~CSR0_RW) | (data & CSR0_RW);
                xs->var->csr0 = xs->var->csr0 & ~(data & CSR0_W1C);

                if (data & CSR0_STOP) {                 /* STOP */
                    xs->var->csr0 = xs->var->csr0 | CSR0_STOP;
                    xs->var->csr0 = xs->var->csr0 & ~(CSR0_STRT | CSR0_INIT | CSR0_IDON | CSR0_TXON | CSR0_RXON);
                    xs->var->csr0 = xs->var->csr0 & ~(CSR0_ERR | CSR0_ESUM);
                    sim_cancel (&xs_unit);
                    }
                else if ((data & CSR0_INIT) && (!(xs->var->csr0 & CSR0_INIT)))   /* INIT */
                    xs_init(xs);
                else if ((data & CSR0_STRT) && (!(xs->var->csr0 & CSR0_STRT))) { /* START */
                    xs->var->csr0 = xs->var->csr0 | CSR0_STRT;
                    xs->var->csr0 = xs->var->csr0 & ~CSR0_STOP;
                    if ((xs->var->mode & MODE_DRX) == 0)
                        xs->var->csr0 = xs->var->csr0 | CSR0_RXON;
                    if ((xs->var->mode & MODE_DTX) == 0)
                        xs->var->csr0 = xs->var->csr0 | CSR0_TXON;
                    sim_clock_coschedule (&xs_unit, tmxr_poll);
                    }
                else if (data & CSR0_TDMD) {            /* TDMD */
                    xs_process_transmit(xs);
                    }

                xs_updateint (xs);

                if ((data & CSR_IE) == 0)
                    CLR_INT (XS1);
                else if ((xs->var->csr0 & (CSR0_INTR + CSR_IE)) == CSR0_INTR)
                    SET_INT (XS1);
                break;

            case 1:                                     /* NI_CSR1 */
                xs->var->csr1 = (data & 0xFFFF);
                break;

            case 2:                                     /* NI_CSR2 */
                xs->var->csr2 = (data & 0xFFFF);
                break;

            case 3:                                     /* NI_CSR3 */
                xs->var->csr3 = data;
                break;
                }
        sim_debug(DBG_REG, &xs_dev, "reg %d write, value = %X, PC = %08X\n", xs->var->rptr, data, fault_PC);
        break;

    case 1:                                             /* NI_RAP */
        xs->var->rptr = data;
        /* sim_debug(DBG_REG, &xs_dev, "reg ptr write, value = %X\n", data); */
        break;
        }

return;
}

/* Unit service */

t_stat xs_svc (UNIT *uptr)
{
int32 queue_size;
CTLR *xs = &xs_ctrl[0];

if ((xs->var->csr0 & (CSR0_STRT | CSR0_INIT)) == CSR0_INIT) {
    /* Init done */
    xs->var->csr0 = xs->var->csr0 | (CSR0_IDON | CSR0_INTR);
    if (xs->var->csr0 & CSR_IE)
        SET_INT (XS1);
    return SCPE_OK;
    }

if (!(xs->var->mode & MODE_DRX)) {
     /* First pump any queued packets into the system */
    if (xs->var->ReadQ.count > 0)
        xs_process_receive(xs);

    /* Now read and queue packets that have arrived */
    /* This is repeated as long as they are available and we have room */
    do {
        queue_size = xs->var->ReadQ.count;
        /* read a packet from the ethernet - processing is via the callback */
        eth_read (xs->var->etherface, &xs->var->read_buffer, xs->var->rcallback);
    } while (queue_size != xs->var->ReadQ.count);

    /* Now pump any still queued packets into the system */
    if (xs->var->ReadQ.count > 0)
        xs_process_receive(xs);
    }

if (!(xs->var->mode & MODE_DTX))
    xs_process_transmit(xs);

sim_clock_coschedule (uptr, tmxr_poll);
return SCPE_OK;
}

/* Transfer received packets into receive ring. */
void xs_process_receive(CTLR* xs)
{
uint8 b0, b1, b2, b3;
uint32 segb, ba;
int slen, wlen, off = 0;
t_stat rstatus, wstatus;
ETH_ITEM* item = 0;
int no_buffers = xs->var->csr0 & CSR0_MISS;

sim_debug(DBG_TRC, xs->dev, "xs_process_receive(), buffers: %d\n", xs->var->rrlen);

// xs_dump_rxring(xs); /* debug receive ring */

/* process only when in the running state, and host buffers are available */
if (no_buffers)
    return;

/* check read queue for buffer loss */
if (xs->var->ReadQ.loss) {
    xs->var->ReadQ.loss = 0;
    }

/* while there are still packets left to process in the queue */
while (xs->var->ReadQ.count > 0) {

    /* get next receive buffer */
    ba = xs->var->rdrb + (xs->var->relen * 2) * xs->var->rxnext;
    rstatus = XS_READW (ba, 8, xs->var->rxhdr);
    if (rstatus) {
        /* tell host bus read failed */
        xs->var->csr0 |= CSR0_MERR;
        break;
        }

    /* if buffer not owned by controller, exit [at end of ring] */
    if (!(xs->var->rxhdr[1] & RXR_OWN)) {
        /* tell the host there are no more buffers */
        /* xs->var->csr0 |= CSR0_MISS; */ /* I don't think this is correct 08-dec-2005 dth */
        sim_debug(DBG_TRC, xs->dev, "Stopping input processing - Not Owned receive descriptor=0x%X, ", ba);
        sim_debug_bits(DBG_TRC, xs->dev, xs_rdes_w2, xs->var->rxhdr[2], xs->var->rxhdr[2], 0);
        sim_debug_bits(DBG_TRC, xs->dev, xs_rdes_w3, xs->var->rxhdr[3], xs->var->rxhdr[3], 1);
        break;
        }

    /* set buffer length and address */
    slen = (uint16)(xs->var->rxhdr[2] * -1);            /* 2s Complement */
    segb = xs->var->rxhdr[0] + ((xs->var->rxhdr[1] & RXR_HADR) << 16);
    segb |= XS_ADRMBO;                                  /* set system specific bits */

    /* get first packet from receive queue */
    if (!item) {
        item = &xs->var->ReadQ.item[xs->var->ReadQ.head];
        /* Pad the packet to minimum size */
        if (item->packet.len < ETH_MIN_PACKET) {
            int len = item->packet.len;
            memset (&item->packet.msg[len], 0, ETH_MIN_PACKET - len);
            item->packet.len = ETH_MIN_PACKET;
            }
        }

    /* is this the start of frame? */
    if (item->packet.used == 0) {
        xs->var->rxhdr[1] |= RXR_STF;
        off = 0;
        }

    /* figure out chained packet size */
    wlen = item->packet.crc_len - item->packet.used;
    if (wlen > slen)
        wlen = slen;

    sim_debug(DBG_TRC, xs->dev, "Using receive descriptor=0x%X, slen=0x%04X(%d), segb=0x%04X, ", ba, slen, slen, segb);
    sim_debug_bits(DBG_TRC, xs->dev, xs_rdes_w1, xs->var->rxhdr[1], xs->var->rxhdr[1], 0);
    sim_debug_bits(DBG_TRC, xs->dev, xs_rdes_w2, xs->var->rxhdr[2], xs->var->rxhdr[2], 0);
    sim_debug_bits(DBG_TRC, xs->dev, xs_rdes_w3, xs->var->rxhdr[3], xs->var->rxhdr[3], 0);
    sim_debug(DBG_TRC, xs->dev, ", pktlen=0x%X(%d), used=0x%X, wlen=0x%X\n", item->packet.len, item->packet.len, item->packet.used, wlen);

    /* Is this the end-of-frame? */
    if ((item->packet.used + wlen) == item->packet.crc_len) {
        b0 = item->packet.msg[item->packet.crc_len - 4];
        b1 = item->packet.msg[item->packet.crc_len - 3];
        b2 = item->packet.msg[item->packet.crc_len - 2];
        b3 = item->packet.msg[item->packet.crc_len - 1];
        item->packet.msg[item->packet.crc_len - 4] = b3;
        item->packet.msg[item->packet.crc_len - 3] = b2;
        item->packet.msg[item->packet.crc_len - 2] = b1;
        item->packet.msg[item->packet.crc_len - 1] = b0;
        }

    /* transfer chained packet to host buffer */
    wstatus = XS_WRITEB (segb, wlen, &item->packet.msg[off]);
    if (wstatus) {
        /* error during write */
        xs->var->csr0 |= CSR0_MERR;
        break;
        }

    /* update chained counts */
    item->packet.used += wlen;
    off += wlen;

    /* Is this the end-of-frame? */
    if (item->packet.used == item->packet.crc_len) {
        /* mark end-of-frame */
        xs->var->rxhdr[1] |= RXR_ENF;

        /* Fill in the Received Message Length field */
        xs->var->rxhdr[3] &= ~RXR_MLEN;
        xs->var->rxhdr[3] |= (item->packet.crc_len);

        /* remove processed packet from the receive queue */
        ethq_remove (&xs->var->ReadQ);
        item = 0;

        /* tell host we received a packet */
        xs->var->csr0 |= CSR0_RINT;
        } /* if end-of-frame */

    /* give buffer back to host */
    xs->var->rxhdr[1] &= ~RXR_OWN;              /* clear ownership flag */

    sim_debug(DBG_TRC, xs->dev, "Updating receive descriptor=0x%X, slen=0x%04X, segb=0x%04X, ", ba, slen, segb);
    sim_debug_bits(DBG_TRC, xs->dev, xs_rdes_w1, xs->var->rxhdr[1], xs->var->rxhdr[1], 0);
    sim_debug_bits(DBG_TRC, xs->dev, xs_rdes_w2, xs->var->rxhdr[2], xs->var->rxhdr[2], 0);
    sim_debug_bits(DBG_TRC, xs->dev, xs_rdes_w3, xs->var->rxhdr[3], xs->var->rxhdr[3], 1);

    /* update the ring entry in host memory. */
    wstatus = XS_WRITEW (ba, 8, xs->var->rxhdr);
    if (wstatus) {
        /* tell host bus write failed */
        xs->var->csr0 |= CSR0_MERR;
        }
    /* set to next receive ring buffer */
    xs->var->rxnext += 1;
    if (xs->var->rxnext == xs->var->rrlen)
        xs->var->rxnext = 0;

    } /* while */

    /* if we failed to finish receiving the frame, flush the packet */
    if (item) {
        ethq_remove(&xs->var->ReadQ);
        xs->var->csr0 |= CSR0_MISS;
        }

    /* set or clear interrupt, depending on what happened */
    xs_updateint (xs);
    // xs_dump_rxring(xs); /* debug receive ring */
}

void xs_process_transmit (CTLR* xs)
{
uint32 segb, ba;
int slen, wlen, off, giant, runt;
t_stat rstatus, wstatus;

/* sim_debug(DBG_TRC, xs->dev, "xs_process_transmit()\n"); */

off = giant = runt = 0;
for (;;) {

    /* get next transmit buffer */
    ba = xs->var->tdrb + (xs->var->telen * 2) * xs->var->txnext;
    rstatus = XS_READW (ba, 8, xs->var->txhdr);
    if (rstatus) {
        /* tell host bus read failed */
        xs->var->csr0 |= CSR0_MERR;
        break;
        }

    /* if buffer not owned by controller, exit [at end of ring] */
    if (!(xs->var->txhdr[1] & TXR_OWN))
        break;

    /* set buffer length and address */
    slen = (uint16)(xs->var->txhdr[2] * -1);            /* 2s complement */
    segb = xs->var->txhdr[0] + ((xs->var->txhdr[1] & TXR_HADR) << 16);
    segb |= XS_ADRMBO;                                  /* set system specific bits */
    wlen = slen;

    sim_debug(DBG_TRC, xs->dev, "Using transmit descriptor=0x%X, slen=0x%04X(%d), segb=0x%04X, ", ba, slen, slen, segb);
    sim_debug_bits(DBG_TRC, xs->dev, xs_tdes_w1, xs->var->txhdr[1], xs->var->txhdr[1], 0);
    sim_debug_bits(DBG_TRC, xs->dev, xs_tdes_w2, xs->var->txhdr[2], xs->var->txhdr[2], 0);
    sim_debug(DBG_TRC, xs->dev, ", pktlen=0x%X(%d), used=0x%X, wlen=0x%X\n", 0, 0, 0, wlen);

    /* prepare to accumulate transmit information if start of frame */
    if (xs->var->txhdr[1] & TXR_STF) {
        memset(&xs->var->write_buffer, 0, sizeof(ETH_PACK));
        off = giant = runt = 0;
        }

    /* get packet data from host */
    if (xs->var->write_buffer.len + slen > ETH_MAX_PACKET) {
        wlen = ETH_MAX_PACKET - xs->var->write_buffer.len;
        giant = 1;
        }
    if (wlen > 0) {
        rstatus = XS_READB(segb, wlen, &xs->var->write_buffer.msg[off]);
        if (rstatus) {
            /* tell host bus read failed */
            xs->var->csr0 |= CSR0_MERR;
            break;
            }
        }
    off += wlen;
    xs->var->write_buffer.len += wlen;

    /* transmit packet when end-of-frame is reached */
    if (xs->var->txhdr[1] & TXR_ENF) {

        /* make sure packet is minimum length */
        if (xs->var->write_buffer.len < ETH_MIN_PACKET) {
            xs->var->write_buffer.len = ETH_MIN_PACKET;  /* pad packet to minimum length */
            runt = 1;
            }

        /* are we in internal loopback mode ? */
        if ((xs->var->mode & MODE_LOOP) && (xs->var->mode & MODE_INTL)) {
            /* just put packet in  receive buffer */
            ethq_insert (&xs->var->ReadQ, 1, &xs->var->write_buffer, 0);
            sim_debug(DBG_TRC, xs->dev, "loopback packet\n");
            }
        else {
            /* transmit packet synchronously - write callback sets status */
            wstatus = eth_write(xs->var->etherface, &xs->var->write_buffer, xs->var->wcallback);
            if (wstatus)
                xs->var->csr0 |= CSR0_BABL;
            else if (DEBUG_PRI (xs_dev, DBG_PCK))
                eth_packet_trace_ex (xs->var->etherface, xs->var->write_buffer.msg, xs->var->write_buffer.len, "xs-write", DEBUG_PRI (xs_dev, DBG_DAT), DBG_PCK);
            }

        /* update transmit status in transmit buffer */
        if (xs->var->write_buffer.status != 0) {
            /* failure */
            const uint16 tdr = 100 + wlen * 8; /* arbitrary value */
            xs->var->txhdr[3] |= TXR_RTRY;
            xs->var->txhdr[3] |= tdr & TXR_TDR;
            xs->var->txhdr[1] |= TXR_ERRS;
            }

        /* was packet too big or too small? */
        if (giant || runt) {
            xs->var->txhdr[3] |= TXR_BUFL;
            xs->var->txhdr[1] |= TXR_ERRS;
            }

        /* tell host we transmitted a packet */
        xs->var->csr0 |= CSR0_TINT;

    } /* if end-of-frame */

    /* give buffer ownership back to host */
    xs->var->txhdr[1] &= ~TXR_OWN;

    sim_debug(DBG_TRC, xs->dev, "Updating transmit descriptor=0x%X, slen=0x%04X, segb=0x%04X, ", ba, slen, segb);
    sim_debug_bits(DBG_TRC, xs->dev, xs_tdes_w1, xs->var->txhdr[1], xs->var->txhdr[1], 0);
    sim_debug_bits(DBG_TRC, xs->dev, xs_tdes_w2, xs->var->txhdr[2], xs->var->txhdr[2], 1);

    /* update transmit buffer */
    wstatus = XS_WRITEW (ba, 8, xs->var->txhdr);
    if (wstatus) {
        /* tell host bus write failed */
        xs->var->csr0 |= CSR0_MERR;
        break;
        }

    /* set to next transmit ring buffer */
    xs->var->txnext += 1;
    if (xs->var->txnext == xs->var->trlen)
        xs->var->txnext = 0;

    }
xs_updateint (xs);
}

t_stat xs_init (CTLR* xs)
{
uint16 w1, w2;
uint8 inb[0x18];

sim_debug (DBG_TRC, &xs_dev, "xs_init() at %08X\n", fault_PC);

sim_cancel (&xs_unit);

/* clear read queue */
ethq_clear (&xs->var->ReadQ);

/* clear setup info */
memset (&xs->var->setup, 0, sizeof(struct xs_setup));

xs->var->inbb = ((xs->var->csr2 & 0xFF) << 16) | (xs->var->csr1 & 0xFFFE);
xs->var->inbb |= XS_ADRMBO;                             /* set system specific bits */
sim_debug (DBG_REG, &xs_dev, "xs_inbb = %04X\n", xs->var->inbb);

if (XS_READB (xs->var->inbb, 0x18, &inb[0])) {
    /* memory read error */
    xs->var->csr0 |= (CSR0_MERR | CSR0_IDON | CSR0_INTR);
    xs->var->csr0 &= ~(CSR0_RXON | CSR0_TXON);
    return SCPE_OK;
    }

xs->var->mode = GETW (inb, 0);
sim_debug(DBG_REG, &xs_dev, "xs_mode = %04X\n", xs->var->mode);

xs->var->mac[0] = GETB (inb, 0x2);
xs->var->mac[1] = GETB (inb, 0x3);
xs->var->mac[2] = GETB (inb, 0x4);
xs->var->mac[3] = GETB (inb, 0x5);
xs->var->mac[4] = GETB (inb, 0x6);
xs->var->mac[5] = GETB (inb, 0x7);

w1 = GETW (inb, 0x10);
w2 = GETW (inb, 0x12);

xs->var->rdrb = ((w2 << 16) | w1) & 0xFFFFF8;
xs->var->rdrb |= XS_ADRMBO;                             /* set system specific bits */
xs->var->rrlen = (w2 >> 13) & 0x7;
xs->var->rrlen = (1u << xs->var->rrlen);
xs->var->relen = 4;
xs->var->rxnext = 0;
sim_debug (DBG_REG, &xs_dev, "xs_rdrb = %08X\n", xs->var->rdrb);
sim_debug (DBG_REG, &xs_dev, "xs_rrlen = %04X\n", xs->var->rrlen);

w1 = GETW (inb, 0x14);
w2 = GETW (inb, 0x16);

xs->var->tdrb = ((w2 << 16) | w1) & 0xFFFFF8;
xs->var->tdrb |= XS_ADRMBO;                             /* set system specific bits */
xs->var->trlen = (w2 >> 13) & 0x7;
xs->var->trlen = (1u << xs->var->trlen);
xs->var->telen = 4;
xs->var->txnext = 0;
sim_debug (DBG_REG, &xs_dev, "xs_tdrb = %08X\n", xs->var->tdrb);
sim_debug (DBG_REG, &xs_dev, "xs_trlen = %04X\n", xs->var->trlen);

xs->var->setup.mult0 = GETL (inb, 0x8);
xs->var->setup.mult1 = GETL (inb, 0xC);

xs->var->setup.promiscuous = (xs->var->mode & MODE_PROM) ? 1 : 0;
xs->var->setup.multicast   = ((xs->var->setup.mult0 | xs->var->setup.mult1) > 0) ? 1 : 0;

xs->var->csr0 = xs->var->csr0 | CSR0_INIT;
xs->var->csr0 = xs->var->csr0 & ~CSR0_STOP;

/* reset ethernet interface */
memcpy (xs->var->setup.macs[0], xs->var->mac, sizeof(ETH_MAC));
xs->var->setup.mac_count = 1;
if (xs->var->etherface)
    eth_filter (xs->var->etherface, xs->var->setup.mac_count,
                &xs->var->mac, xs->var->setup.multicast,
                xs->var->setup.promiscuous);

sim_activate (&xs_unit, 50);

return SCPE_OK;
}

void xs_updateint(CTLR* xs)
{
if (xs->var->csr0 & 0x5F00)                             /* if any interrupt bits on, */
    xs_setint (xs);
else
    xs_clrint (xs);
}

void xs_setint (CTLR* xs)
{
if (xs->var->csr0 & CSR0_INTR)
    return;
xs->var->csr0 |= CSR0_INTR;
if (xs->var->csr0 & CSR_IE)
    SET_INT (XS1);
}

void xs_clrint (CTLR* xs)
{
xs->var->csr0 &= ~CSR0_INTR;
CLR_INT (XS1);
}

void xs_read_callback(int status)
{
CTLR *xs = &xs_ctrl[0];

if (DEBUG_PRI (xs_dev, DBG_PCK))
    eth_packet_trace_ex (xs->var->etherface, xs->var->read_buffer.msg, xs->var->read_buffer.len, "xs-recvd", DEBUG_PRI (xs_dev, DBG_DAT), DBG_PCK);

/* add packet to read queue */
ethq_insert(&xs->var->ReadQ, 2, &xs->var->read_buffer, 0);
}

void xs_write_callback (int status)
{
CTLR *xs = &xs_ctrl[0];
xs->var->write_buffer.status = status;
}

/* Device initialization */

t_stat xs_reset (DEVICE *dptr)
{
t_stat status;
CTLR *xs = &xs_ctrl[0];

xs->var->csr0 = 0;
xs->var->csr1 = 0;
xs->var->csr2 = 0;
xs->var->csr3 = 0;
xs->var->rptr = 0;
xs->var->mode = 0;
xs->var->inbb = 0;
CLR_INT (XS1);                                           /* clear int req */
/* init read queue (first time only) */
status = ethq_init (&xs->var->ReadQ, XS_QUE_MAX);
if (status != SCPE_OK)
    return status;

sim_cancel (&xs_unit);                                   /* cancel unit */
return SCPE_OK;
}

/* Attach routine */

t_stat xs_attach (UNIT *uptr, CONST char *cptr)
{
t_stat status;
char* tptr;
CTLR *xs = &xs_ctrl[0];

tptr = (char *) malloc(strlen(cptr) + 1);
if (tptr == NULL)
    return SCPE_MEM;
strcpy(tptr, cptr);

xs->var->etherface = (ETH_DEV *) malloc(sizeof(ETH_DEV));
if (!xs->var->etherface) {
    free(tptr);
    return SCPE_MEM;
    }

status = eth_open(xs->var->etherface, cptr, xs->dev, DBG_ETH);
if (status != SCPE_OK) {
    free(tptr);
    free(xs->var->etherface);
    xs->var->etherface = 0;
    return status;
    }
uptr->filename = tptr;
uptr->flags |= UNIT_ATT;
eth_setcrc(xs->var->etherface, 1); /* enable CRC */

/* reset the device with the new attach info */
xs_reset(xs->dev);

return SCPE_OK;
}

t_stat xs_detach (UNIT* uptr)
{
CTLR *xs = &xs_ctrl[0];

if (uptr->flags & UNIT_ATT) {
    eth_close (xs->var->etherface);
    free(xs->var->etherface);
    xs->var->etherface = 0;
    free(uptr->filename);
    uptr->filename = NULL;
    uptr->flags &= ~UNIT_ATT;
    }
return SCPE_OK;
}

void xs_dump_rxring (CTLR* xs)
{
int i;
int rrlen = xs->var->rrlen;
sim_printf ("receive ring[%s]: base address: %08x  headers: %d, header size: %d, current: %d\n",
            xs->dev->name, xs->var->rdrb, xs->var->rrlen, xs->var->relen, xs->var->rxnext);
for (i=0; i<rrlen; i++) {
    uint16 rxhdr[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    uint32 ba = xs->var->rdrb + (xs->var->relen * 2) * i;
    t_stat rstatus = XS_READW (ba, 8, rxhdr);  /* get rxring entry[i] */
    int own = (rxhdr[2] & RXR_OWN) >> 15;
    int len = rxhdr[0];
    uint32 addr = rxhdr[1] + ((rxhdr[2] & 3) << 16);
    if (rstatus == 0)
        sim_printf ("  header[%d]: own:%d, len:%d, address:%08x data:{%04x,%04x,%04x,%04x}\n",
                    i, own, len, addr, rxhdr[0], rxhdr[1], rxhdr[2], rxhdr[3]);
    }
}

void xs_dump_txring (CTLR* xs)
{
int i;
int trlen = xs->var->trlen;
sim_printf ("transmit ring[%s]: base address: %08x  headers: %d, header size: %d, current: %d\n",
            xs->dev->name, xs->var->tdrb, xs->var->trlen, xs->var->telen, xs->var->txnext);
for (i=0; i<trlen; i++) {
    uint16 txhdr[4];
    uint32 ba = xs->var->tdrb + (xs->var->telen * 2) * i;
    t_stat tstatus = XS_READW (ba, 8, txhdr);  /* get rxring entry[i] */
    int own = (txhdr[2] & RXR_OWN) >> 15;
    int len = txhdr[0];
    uint32 addr = txhdr[1] + ((txhdr[2] & 3) << 16);
    if (tstatus == 0)
        sim_printf ("  header[%d]: own:%d, len:%d, address:%08x data:{%04x,%04x,%04x,%04x}\n",
                    i, own, len, addr, txhdr[0], txhdr[1], txhdr[2], txhdr[3]);
    }
}

t_stat xs_help (FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
fprintf (st, "LANCE Ethernet Controller (XS)\n\n");
fprintf (st, "The simulator implements one LANCE Ethernet controller (XS).\n");
if (dptr->flags & DEV_DISABLE)
    fprintf (st, "Initially the XS controller is disabled.\n");
else
    fprintf (st, "The XS controller cannot be disabled.\n");
fprintf (st, "There are no configurable options. The MAC address is controlled through\n");
fprintf (st, "the network address ROM device (NAR).\n\n");
fprint_set_help (st, dptr);
fprintf (st, "\nConfigured options and controller state can be displayed with:\n\n");
fprint_show_help (st, dptr);
fprintf (st, "To access the network, the simulated Ethernet controller must be attached to a\n");
fprintf (st, "real Ethernet interface.\n\n");
eth_attach_help(st, dptr, uptr, flag, cptr);
return SCPE_OK;
}

const char *xs_description (DEVICE *dptr)
{
return "LANCE Ethernet controller";
}
