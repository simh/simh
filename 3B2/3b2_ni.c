/* 3b2_ni.c: AT&T 3B2 Model 400 "NI" feature card

   Copyright (c) 2018, Seth J. Morabito

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Except as contained in this notice, the name of the author shall
   not be used in advertising or otherwise to promote the sale, use or
   other dealings in this Software without prior written authorization
   from the author.
*/

/*
 * NI is an intelligent feature card for the 3B2 that provides a
 * 10BASE5 Ethernet interface.

 * Overview
 * --------
 *
 * The NI board is based on the Common I/O (CIO) platform. Like other
 * CIO boards, it uses an 80186 embedded processor. The board and the
 * 3B2 host communicate by reading and writing to the 3B2's main
 * memory at locations established by the host via a series of job
 * request and job completion queues. Only three interrupts are used:
 * Two interrupts (80186 interrupts INT0 and INT1) are triggered by
 * the 3B2 and tell the card when work is available in the request
 * queue. One WE32100 interrupt (at a negotiated vector and predefined
 * IPL) is used by the CIO board to tell the 3B2 that a new entry is
 * available in the completion queue.
 *
 * The on-board ROM does not contain the full firmware required to
 * perform all application-specific work. Rather, it is used only to
 * bootstrap the 80186 and provide essential communication between the
 * 3B2 host and the board's internal RAM. During initialization, the
 * host must upload application-specific code to the board's RAM and
 * cause the board to start running it. This is known as
 * "pumping". The 80186 binary code for the NI board under System V
 * Release 3 is stored in the file `/lib/pump/ni`
 *
 * Implementation Details
 * ----------------------
 *
 * The 10BASE5 interface on the NI board is driven by an Intel 82586
 * IEEE 802.3 LAN Coprocessor, controlled by the board's 80186
 * CPU. The 82586 is completely opaque to the host due to the nature
 * of the CIO protocol. Nevertheless, an attempt is made to simulate
 * the behavior of the 82586 where appropriate and possible.
 *
 * The NI board uses a sanity timer to occasionally write a watchdog
 * or heartbeat entry into the completion queue, indicating that the
 * Ethernet interface is still alive and that all is well. If the UNIX
 * driver has not seen this heartbeat after approximately 10 seconds,
 * it will consider the board to be in an "DOWN" state and send it a
 * TERM ioctl.
 *
 * The NI board does behave differently from the other CIO boards in
 * one respect: Unlike other CIO boards, the NI board takes jobs from
 * its two Packet Receive CIO request queues by polling them, and then
 * stores the taken jobs in a small 4-entry internal cache. It polls
 * these queues quite rapidly in the real NI so it always has a full
 * cache available for future incoming packets. To prevent performance
 * issues, this simulation polls rapidly ("fast polling mode") only
 * when absolutely necessary. Typically, that means only after the
 * card has been reset, but before the request queues have finished
 * being built by the 3B2 host. The UNIX NI driver expects and
 * requires this behavior!
 *
 * Open Issues
 * -----------
 *
 * 1. The simulated card does not yet support setting or removing
 *    multicast Ethernet addresses. ioctl operations that attempt to
 *    set or remove multicast Ethernet addresses should silently
 *    fail. This will be supported in a future release.
 *
 */

#include "3b2_defs.h"
#include "3b2_ni.h"

#include <math.h>

/* State container for the card */
NI_STATE  ni;

/* Static Function Declarations */
static void dump_packet(const char *direction, ETH_PACK *pkt);
static void ni_enable();
static void ni_disable();
static void ni_cmd(uint8 cid, cio_entry *rentry, uint8 *rapp_data, t_bool is_exp);
static t_stat ni_show_queue_common(FILE *st, UNIT *uptr, int32 val,
                                   CONST void *desc, t_bool rq);
static t_stat ni_show_rqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
static t_stat ni_show_cqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc);

/*
 * When the NI card is pumped, its CRC depends on what slot it is
 * installed in and what version of driver has been installed.
 */
#define NI_DIAG_CRCS_LEN 7
static const uint32 NI_DIAG_CRCS[] = {
    0x795268a4,
    0xfab1057c,
    0x10ca00cd,
    0x9b3ddeda,
    0x267b19a0,
    0x123f36c0,
    0xc04ca0ab,
};

/*
 * Unit 0: Packet reception.
 * Unit 1: Sanity timer.
 * Unit 2: Request Queue poller.
 * Unit 3: CIO requests.
 */
UNIT ni_unit[] = {
    { UDATA(&ni_rcv_svc, UNIT_IDLE|UNIT_ATTABLE, 0) },
    { UDATA(&ni_sanity_svc, UNIT_IDLE|UNIT_DIS, 0) },
    { UDATA(&ni_rq_svc, UNIT_IDLE|UNIT_DIS, 0) },
    { UDATA(&ni_cio_svc, UNIT_DIS, 0) },
    { 0 }
};

static UNIT *rcv_unit = &ni_unit[0];
static UNIT *sanity_unit = &ni_unit[1];
static UNIT *rq_unit = &ni_unit[2];
static UNIT *cio_unit = &ni_unit[3];

MTAB ni_mod[] = {
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "STATS", "STATS",
      &ni_set_stats, &ni_show_stats, NULL, "Display or reset statistics" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "POLL", NULL,
      NULL, &ni_show_poll, NULL, "Display the current polling mode" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "RQUEUE=n", NULL,
      NULL, &ni_show_rqueue, NULL, "Display Request Queue for card n" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR, 0, "CQUEUE=n", NULL,
      NULL, &ni_show_cqueue, NULL, "Display Completion Queue for card n" },
    { MTAB_XTD|MTAB_VDV|MTAB_VALR|MTAB_NC, 0, "MAC", "MAC=xx:xx:xx:xx:xx:xx",
      &ni_setmac, &ni_showmac, NULL, "MAC address" },
    { MTAB_XTD|MTAB_VDV|MTAB_NMO, 0, "FILTERS", NULL,
      NULL, &ni_show_filters, NULL, "Display address filters" },
    { 0 }
};

static DEBTAB ni_debug[] = {
    { "TRACE",  DBG_TRACE,     "trace routine calls" },
    { "IO",     DBG_IO,        "debug i/o" },
    { "CACHE",  DBG_CACHE,     "debug job cache" },
    { "PACKET", DBG_DAT,       "display packet data" },
    { "ERR",    DBG_ERR,       "display errors" },
    { "ETH",    DBG_ETH,       "debug ethernet device" },
    { 0 }
};

DEVICE ni_dev = {
    "NI",                   /* name */
    ni_unit,                /* units */
    NULL,                   /* registers */
    ni_mod,                 /* modifiers */
    4,                      /* #units */
    16,                     /* address radix */
    32,                     /* address width */
    1,                      /* address incr. */
    16,                     /* data radix */
    8,                      /* data width */
    NULL,                   /* examine routine */
    NULL,                   /* deposit routine */
    &ni_reset,              /* reset routine */
    NULL,                   /* boot routine */
    &ni_attach,             /* attach routine */
    &ni_detach,             /* detach routine */
    NULL,                   /* context */
    DEV_DISABLE|DEV_DIS|
    DEV_DEBUG|DEV_ETHER,    /* flags */
    0,                      /* debug control flags */
    ni_debug,               /* debug flag names */
    NULL,                   /* memory size change */
    NULL,                   /* logical name */
    &ni_help,               /* help routine */
    NULL,                   /* attach help routine */
    NULL,                   /* help context */
    &ni_description,        /* device description */
    NULL,
};

static void dump_packet(const char *direction, ETH_PACK *pkt)
{
    char dumpline[82];
    char *p;
    uint32 char_offset, i;

    if (!direction) {
        return;
    }

    snprintf(dumpline, 10, "%08x ", 0);
    char_offset = 9;

    for (i = 0; i < pkt->len; i++) {
        snprintf(dumpline + char_offset, 4, "%02x ", pkt->msg[i]);
        snprintf(dumpline + 61 + (i % 16), 2, "%c", CHAR(pkt->msg[i]));
        char_offset += 3;

        if ((i + 1) % 16 == 0) {

            snprintf(dumpline + 56, 5, "   |");
            snprintf(dumpline + 78, 2, "|");

            for (p = dumpline; p < (dumpline + 80); p++) {
                if (*p == '\0') {
                    *p = ' ';
                }
            }
            *p = '\0';
            sim_debug(DBG_DAT, &ni_dev,
                      "[%s packet]: %s\n", direction, dumpline);
            memset(dumpline, 0, 80);
            snprintf(dumpline, 10, "%08x ", i + 1);
            char_offset = 9;
        }
    }

    /* Finish any leftover bits */
    if ((i + 1) % 16 != 0) {
        snprintf(dumpline + 56, 5, "   |");
        snprintf(dumpline + 78, 2, "|");

        for (p = dumpline; p < (dumpline + 80); p++) {
            if (*p == '\0') {
                *p = ' ';
            }
        }
        *p = '\0';

        sim_debug(DBG_DAT, &ni_dev,
                  "[%s packet]: %s\n", direction, dumpline);
    }
}

static void ni_enable()
{
    sim_debug(DBG_TRACE, &ni_dev,
              "[ni_enable] Enabling the interface.\n");

    /* Reset Statistics */
    memset(&ni.stats, 0, sizeof(ni_stat_info));

    /* Clear out job cache */
    memset(&ni.job_cache, 0, sizeof(ni_job_cache) * 2);

    /* Enter fast polling mode */
    ni.poll_rate = NI_QPOLL_FAST;

    /* Start the queue poller in fast poll mode */
    sim_activate_abs(rq_unit, NI_QPOLL_FAST);

    /* Start the sanity timer */
    sim_activate_after(sanity_unit, NI_SANITY_INTERVAL_US);

    /* Enable the interface */
    ni.enabled = TRUE;
}

static void ni_disable()
{
    sim_debug(DBG_TRACE, &ni_dev,
              "[ni_disable] Disabling the interface.\n");
    ni.enabled = FALSE;
    cio[ni.cid].intr = FALSE;
    sim_cancel(ni_unit);
    sim_cancel(rcv_unit);
    sim_cancel(rq_unit);
    sim_cancel(cio_unit);
    sim_cancel(sanity_unit);
}

static void ni_cmd(uint8 cid, cio_entry *rentry, uint8 *rapp_data, t_bool is_exp)
{
    int i, j;
    int32 delay;
    uint16 hdrsize;
    t_stat status;
    int prot_info_offset;
    cio_entry centry = {0};
    uint8 app_data[4] = {rapp_data[0], rapp_data[1], rapp_data[2], rapp_data[3]};

    /* Assume some default values, but let the handlers below
     * override these where appropriate */
    centry.opcode = CIO_SUCCESS;
    centry.subdevice = rentry->subdevice;
    centry.address = rentry->address;

    cio[cid].op = rentry->opcode;

    delay = NI_INT_DELAY;

    switch(rentry->opcode) {
    case CIO_DLM:
        for (i = 0; i < rentry->byte_count; i++) {
            ni.crc = cio_crc32_shift(ni.crc, pread_b(rentry->address + i));
        }

        centry.address = rentry->address + rentry->byte_count;
        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_cmd] CIO Download Memory: bytecnt=%04x "
                  "addr=%08x return_addr=%08x subdev=%02x (CRC=%08x)\n",
                  rentry->byte_count, rentry->address,
                  centry.address, centry.subdevice, ni.crc);

        if (is_exp) {
            cio_cexpress(cid, NIQESIZE, &centry, app_data);
        } else {
            cio_cqueue(cid, CIO_STAT, NIQESIZE, &centry, app_data);
        }

        break;
    case CIO_FCF:
        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_cmd] CIO Force Function Call (CRC=%08x)\n",
                  ni.crc);

        /* If the currently running program is a diagnostics program,
         * we are expected to write results into memory at address
         * 0x200f000 */
        for (i = 0; i < NI_DIAG_CRCS_LEN; i++) {
            if (ni.crc == NI_DIAG_CRCS[i]) {
                pwrite_h(0x200f000, 0x1);   /* Test success */
                pwrite_h(0x200f002, 0x0);   /* Test Number */
                pwrite_h(0x200f004, 0x0);   /* Actual */
                pwrite_h(0x200f006, 0x0);   /* Expected */
                pwrite_b(0x200f008, 0x1);   /* Success flag again */
                break;
            }
        }

        /* Store the sequence byte we were sent for later reply. */
        ni.fcf_seq = rapp_data[3];

        /* "Force Function Call" causes the CIO card to start running
         * pumped code as a new process, taking over from its firmware
         * ROM. As a result, a new sysgen is necessary to get the card
         * in the right state. */

        ni_disable();
        cio[cid].sysgen_s = 0;

        if (cio[cid].ivec == 0 || cio[cid].ivec == 3) {
            cio_cexpress(cid, NIQESIZE, &centry, app_data);
        } else {
            cio_cqueue(cid, CIO_STAT, NIQESIZE, &centry, app_data);
        }

        break;
    case CIO_DSD:
        /* Determine Sub-Devices. We have none. */
        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_cmd] Determine Sub-Devices.\n");

        /* The system wants us to write sub-device structures at the
         * supplied address */
        pwrite_h(rentry->address, 0x0);

        if (is_exp) {
            cio_cexpress(cid, NIQESIZE, &centry, app_data);
        } else {
            cio_cqueue(cid, CIO_STAT, NIQESIZE, &centry, app_data);
        }

        break;
    case NI_SETID:
        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_cmd] NI SETID Operation\n");

        /* Try to read the mac from memory */
        for (i = 0; i < MAC_SIZE_BYTES; i++) {
            ni.mac_bytes[i] = pread_b(rentry->address + i);
        }

        snprintf(ni.mac_str, MAC_SIZE_CHARS, "%02x:%02x:%02x:%02x:%02x:%02x",
                 ni.mac_bytes[0], ni.mac_bytes[1], ni.mac_bytes[2],
                 ni.mac_bytes[3], ni.mac_bytes[4], ni.mac_bytes[5]);

        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_cmd] NI SETID: New MAC: %s\n",
                  ni.mac_str);

        status = ni_setmac(ni_dev.units, 0, ni.mac_str, NULL);

        cio_cqueue(cid, CIO_STAT, NIQESIZE, &centry, app_data);

        break;
    case NI_TURNOFF:
        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_cmd] NI TURNOFF Operation\n");

        ni_disable();

        cio_cqueue(cid, CIO_STAT, NIQESIZE, &centry, app_data);

        break;
    case NI_TURNON:
        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_cmd] NI TURNON Operation\n");

        ni_enable();

        cio_cqueue(cid, CIO_STAT, NIQESIZE, &centry, app_data);

        break;
    case NI_STATS:
        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_cmd] NI STATS Operation\n");

        cio_cqueue(cid, CIO_STAT, NIQESIZE, &centry, app_data);

        break;
    case NI_SEND:
    case NI_SEND_A:
        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_cmd] NI SEND Operation (opcode=%d)\n",
                  rentry->opcode);

        /* TODO: Why is this always 4 for a send? */
        centry.subdevice = 4;

        /* TODO: On the real 3B2, this appears to be some sort of
         * checksum. Perhaps the packet checksum? I'm not sure. I need
         * to run Wireshark on the real 3B2 and investigate.
         *
         * However, we're in luck: The driver code doesn't seem to
         * validate it or check against it in any way, so we can
         * put anything in there.
         */
        centry.address = rentry->address;
        centry.byte_count = rentry->byte_count;


        /* If the interface is not attached, we can't actually send
         * any packets. */
        if (!(rcv_unit->flags & UNIT_ATT)) {
            ni.stats.tx_fail++;
            centry.opcode = CIO_FAILURE;
            sim_debug(DBG_TRACE, &ni_dev,
                      "[ni_cmd] NI SEND failure. Not attached. tx_fail=%d\n",
                      ni.stats.tx_fail);
            break;
        }

        /* Reset the write packet */
        ni.wr_buf.len = 0;
        ni.wr_buf.oversize = NULL;

        /* Read the size of the header */
        hdrsize = pread_h(rentry->address + EIG_TABLE_SIZE);

        /* Read out the packet frame */
        for (i = 0; i < rentry->byte_count; i++) {
            ni.wr_buf.msg[i] = pread_b(rentry->address + PKT_START_OFFSET + i);
        }

        /* Get a pointer to the buffer containing the protocol data */
        prot_info_offset = 0;
        i = 0;
        do {
            ni.prot.addr = pread_w(rentry->address + prot_info_offset);
            ni.prot.size = pread_h(rentry->address + prot_info_offset + 4);
            ni.prot.last = pread_h(rentry->address + prot_info_offset + 6);
            prot_info_offset += 8;

            /* Fill in the frame from this buffer */
            for (j=0; j < ni.prot.size; i++, j++) {
                ni.wr_buf.msg[hdrsize + i] = pread_b(ni.prot.addr + j);
            }
        } while (!ni.prot.last);

        /* Fill in packet details */
        ni.wr_buf.len = rentry->byte_count;

        sim_debug(DBG_IO, &ni_dev,
                  "[XMT] Transmitting a packet of size %d (0x%x)\n",
                  ni.wr_buf.len, ni.wr_buf.len);

        /* Send it */
        status = eth_write(ni.eth, &ni.wr_buf, NULL);

        if (status == SCPE_OK) {
            if (ni_dev.dctrl & DBG_DAT) {
                dump_packet("XMT", &ni.wr_buf);
            }
            ni.stats.tx_bytes += ni.wr_buf.len;
            ni.stats.tx_pkt++;
        } else {
            ni.stats.tx_fail++;
            centry.opcode = CIO_FAILURE;
        }

        /* Weird behavior seen on the real 3B2's completion queue: If
         * the byte count value is < 0xff, shift it! I really wish I
         * understood this card... */
        if (centry.byte_count < 0xff) {
            centry.byte_count <<= 8;
        }

        cio_cqueue(cid, CIO_STAT, NIQESIZE, &centry, app_data);

        delay = 0;

        break;
    default:
        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_cmd] Opcode %d Not Handled Yet\n",
                  rentry->opcode);

        cio_cqueue(cid, CIO_STAT, NIQESIZE, &centry, app_data);

        break;
    }

    sim_activate_abs(cio_unit, delay);
}

t_stat ni_setmac(UNIT *uptr, int32 val, CONST char* cptr, void* desc)
{
    t_stat status;

    UNUSED(val);
    UNUSED(desc);

    status = SCPE_OK;
    status = eth_mac_scan_ex(&ni.macs[NI_NIC_MAC], cptr, uptr);

    if (status == SCPE_OK) {
        eth_filter(ni.eth, ni.filter_count, ni.macs, 0, 0);
    } else {
        sim_debug(DBG_ERR, &ni_dev,
                  "[ni_setmac] Error in eth_mac_scan_ex. status=%d\n", status);
    }

    return status;
}

t_stat ni_showmac(FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
    char buffer[20];

    UNUSED(uptr);
    UNUSED(val);
    UNUSED(desc);

    eth_mac_fmt(&ni.macs[NI_NIC_MAC], buffer);
    fprintf(st, "MAC=%s", buffer);
    return SCPE_OK;
}

t_stat ni_show_filters(FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
    char  buffer[20];
    int i;

    UNUSED(uptr);
    UNUSED(val);
    UNUSED(desc);

    eth_mac_fmt(&ni.macs[NI_NIC_MAC], buffer);
    fprintf(st, "Physical Address=%s\n", buffer);
    if (ni.filter_count > 0) {
        fprintf(st, "Filters:\n");
        for (i=0; i < ni.filter_count; i++) {
            eth_mac_fmt((ETH_MAC *) ni.macs[i], buffer);
            fprintf(st, "[%2d]: %s\n", i, buffer);
        }
        fprintf(st, "\n");
    }

    return SCPE_OK;
}

void ni_sysgen(uint8 cid)
{
    cio_entry cqe = {0};
    uint8 app_data[4] = {0};

    ni_disable();

    app_data[3] = 0x64;
    cqe.opcode = CIO_SYSGEN_OK;

    sim_debug(DBG_TRACE, &ni_dev,
              "[ni_sysgen]   CIO SYSGEN. rqp=%08x, cqp=%08x, nrq=%d, rqs=%d cqs=%d\n",
              cio[cid].rqp, cio[cid].cqp, cio[cid].no_rque, cio[cid].rqs, cio[cid].cqs);

    /* If the card has been successfully pumped, then we respond with
     * a full completion queue entry.  Otherwise, an express entry is
     * used. */
    if (ni.crc == NI_PUMP_CRC1 ||
        ni.crc == NI_PUMP_CRC2) {
        cio_cqueue(cid, CIO_STAT, NIQESIZE, &cqe, app_data);
    } else {
        cio_cexpress(cid, NIQESIZE, &cqe, app_data);
    }

    /* Now clear out the old CRC value, in case the card needs to be
     * sysgen'ed again later. */
    ni.crc = 0;

    sim_activate_abs(cio_unit, NI_INT_DELAY);
}

/*
 * Handler for CIO INT0 (express job) requests.
 */
void ni_express(uint8 cid)
{
    cio_entry rqe = {0};
    uint8 app_data[4] = {0};

    sim_debug(DBG_TRACE, &ni_dev,
              "[ni_express] Handling express CIO request.\n");

    cio_rexpress(cid, NIQESIZE, &rqe, app_data);
    ni_cmd(cid, &rqe, app_data, TRUE);
}

/*
 * Handler for CIO INT1 (full job) requests.
 */
void ni_full(uint8 cid)
{
    cio_entry rqe = {0};
    uint8 app_data[4] = {0};

    sim_debug(DBG_TRACE, &ni_dev,
              "[ni_full] INT1 received. Handling full CIO request.\n");

    while (cio_cqueue_avail(cid, NIQESIZE) &&
           cio_rqueue(cid, GE_QUEUE, NIQESIZE, &rqe, app_data) == SCPE_OK) {
        ni_cmd(cid, &rqe, app_data, FALSE);
    }
}

/*
 * Handler for CIO RESET requests.
 */
void ni_cio_reset(uint8 cid)
{
    UNUSED(cid);

    ni_disable();
}

t_stat ni_autoconfig()
{
    uint8 cid;

    /* Clear the CIO table of NI cards */
    for (cid = 0; cid < CIO_SLOTS; cid++) {
        if (cio[cid].id == NI_ID) {
            cio[cid].id = 0;
            cio[cid].ipl = 0;
            cio[cid].ivec = 0;
            cio[cid].exp_handler = NULL;
            cio[cid].full_handler = NULL;
            cio[cid].reset_handler = NULL;
            cio[cid].sysgen = NULL;
        }
    }

    /* Find the first avaialable slot */
    for (cid = 0; cid < CIO_SLOTS; cid++) {
        if (cio[cid].id == 0) {
            break;
        }
    }

    /* Do we have room? */
    if (cid >= CIO_SLOTS) {
        return SCPE_NXM;
    }

    /* Remember the card slot */
    ni.cid = cid;

    /* Set up the ni structure */
    cio[cid].id = NI_ID;
    cio[cid].ipl = NI_IPL;
    cio[cid].exp_handler = &ni_express;
    cio[cid].full_handler = &ni_full;
    cio[cid].reset_handler = &ni_cio_reset;
    cio[cid].sysgen = &ni_sysgen;

    return SCPE_OK;
}

t_stat ni_reset(DEVICE *dptr)
{
    t_stat status;
    char uname[16];

    sim_debug(DBG_TRACE, &ni_dev,
              "[ni_reset] Resetting NI device\n");

    /* Initial setup that should only ever be done once. */
    if (!(dptr->flags & DEV_DIS) && !ni.initialized) {
        /* Autoconfiguration will select the correct backplane slot
         * for the device, and enable CIO routines. This should only
         * be done once. */
        status = ni_autoconfig();
        if (status != SCPE_OK) {
            return status;
        }

        /* Set an initial MAC address in the AT&T NI range */
        ni_setmac(ni_dev.units, 0, "80:00:10:03:00:00/32", NULL);

        ni.initialized = TRUE;
    }

    /* Set up unit names */
    snprintf(uname, 16, "%s-RCV", dptr->name);
    sim_set_uname(rcv_unit, uname);
    snprintf(uname, 16, "%s-SANITY", dptr->name);
    sim_set_uname(sanity_unit, uname);
    snprintf(uname, 16, "%s-RQ", dptr->name);
    sim_set_uname(rq_unit, uname);
    snprintf(uname, 16, "%s-CIO", dptr->name);
    sim_set_uname(cio_unit, uname);

    /* Ensure that the broadcast address is configured, and that we
     * have a minmimum of two filters set. */
    memset(&ni.macs[NI_BCST_MAC], 0xff, sizeof(ETH_MAC));
    ni.filter_count = NI_FILTER_MIN;

    ni.poll_rate = NI_QPOLL_FAST;

    /* Make sure the transceiver is disabled and all
     * polling activity and interrupts are disabled. */
    ni_disable();

    /* We make no attempt to autoconfig until the device
     * is attached. */

    return SCPE_OK;
}

t_stat ni_rcv_svc(UNIT *uptr)
{
    t_stat read_succ;

    UNUSED(uptr);

    /* Since we cannot know which queue (large packet or small packet
     * queue) will have room for the next packet that we read, for
     * safety reasons we will not call eth_read() until we're certain
     * there's room available in BOTH queues. */
    while (ni.enabled && NI_BUFFERS_AVAIL) {
        read_succ = eth_read(ni.eth, &ni.rd_buf, NULL);
        if (!read_succ) {
            break;
        }
        /* Attempt to process the packet that was received. */
        ni_process_packet();
    }

    return SCPE_OK;
}

/*
 * Service used by the card to poll for available request queue
 * entries.
 */
t_stat ni_rq_svc(UNIT *uptr)
{
    t_bool rq_taken;
    int i, wp, no_rque;
    cio_entry rqe = {0};
    uint8 slot[4] = {0};

    UNUSED(uptr);

    rq_taken = FALSE;
    no_rque = cio[ni.cid].no_rque - 1;

    for (i = 0; i < no_rque; i++) {
        while (NI_CACHE_HAS_SPACE(i) && cio_rqueue(ni.cid, i+1, NIQESIZE, &rqe, slot) == SCPE_OK) {
            sim_debug(DBG_CACHE, &ni_dev,
                      "[cache -  FILL] %s packet entry. lp=%02x ulp=%02x "
                      "slot=%d addr=0x%08x\n",
                      i == 0 ? "Small" : "Large",
                      cio_r_lp(ni.cid, i+1, NIQESIZE),
                      cio_r_ulp(ni.cid, i+1, NIQESIZE),
                      slot[3], rqe.address);
            wp = ni.job_cache[i].wp;
            ni.job_cache[i].req[wp].addr = rqe.address;
            ni.job_cache[i].req[wp].slot = slot[3];
            ni.job_cache[i].wp = (wp + 1) % NI_CACHE_LEN;
            ni.stats.rq_taken++;
            rq_taken = TRUE;
        }
    }

    /* Somewhat of a kludge, unfortunately. */
    if (ni.poll_rate == NI_QPOLL_FAST && ni.stats.rq_taken >= 6) {
        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_rq_svc] Switching to slow poll mode.\n");
        ni.poll_rate = NI_QPOLL_SLOW;
    }

    /* If any receive jobs were found, schedule a packet read right
       away */
    if (rq_taken) {
        sim_activate_abs(rcv_unit, 0);
    }

    /* Reactivate the poller. */
    if (ni.poll_rate == NI_QPOLL_FAST) {
        sim_activate_abs(rq_unit, NI_QPOLL_FAST);
    } else {
        if (sim_idle_enab) {
            sim_clock_coschedule(rq_unit, tmxr_poll);
        } else {
            sim_activate_abs(rq_unit, NI_QPOLL_SLOW);
        }
    }

    return SCPE_OK;
}

/*
 * The NI card uses a sanity timer to poke the host every few seconds
 * and let it know that it is still alive. This service handling
 * routine is responsible for scheduling these notifications.
 *
 * The NI driver expects these notifications to happen no more than 15
 * seconds apart. Unfortunately, I do not yet know the exact frequency
 * with which the real hardware sends these updates, but it appears
 * not to happen very frequently, so we'll have to settle for an
 * educated guess of 10 seconds.
 */
t_stat ni_sanity_svc(UNIT *uptr)
{
    cio_entry cqe = {0};
    uint8 app_data[4] = {0};

    UNUSED(uptr);

    sim_debug(DBG_TRACE, &ni_dev,
              "[ni_sanity_svc] Firing sanity timer.\n");

    cqe.opcode = NI_SANITY;
    cio_cqueue(ni.cid, CIO_STAT, NIQESIZE, &cqe, app_data);

    if (cio[ni.cid].ivec > 0) {
        cio[ni.cid].intr = TRUE;
    }

    sim_activate_after(sanity_unit, NI_SANITY_INTERVAL_US);

    return SCPE_OK;
}

t_stat ni_cio_svc(UNIT *uptr)
{
    UNUSED(uptr);

    if (cio[ni.cid].ivec > 0) {
        sim_debug(DBG_TRACE, &ni_dev,
                  "[ni_cio_svc] Handling a CIO service (Setting Interrupt) for board %d\n", ni.cid);
        cio[ni.cid].intr = TRUE;
    }

    return SCPE_OK;
}

/*
 * Do the work of trying to process the most recently received packet
 */
void ni_process_packet()
{
    int i, rp;
    uint32 addr;
    uint8 slot;
    cio_entry centry = {0};
    uint8 capp_data[4] = {0};
    int len = 0;
    int que_num = 0;
    uint8 *rbuf;

    len = ni.rd_buf.len;
    rbuf = ni.rd_buf.msg;
    que_num = len > SM_PKT_MAX ? LG_QUEUE : SM_QUEUE;

    sim_debug(DBG_IO, &ni_dev,
              "[ni_process_packet] Receiving a packet of size %d (0x%x)\n",
              len, len);

    /* Availability of a job in the job cache was checked before
     * calling ni_process_packet(), so there is no need to check it
     * again. */
    rp = ni.job_cache[que_num].rp;
    addr = ni.job_cache[que_num].req[rp].addr;
    slot = ni.job_cache[que_num].req[rp].slot;
    ni.job_cache[que_num].rp = (rp + 1) % NI_CACHE_LEN;
    sim_debug(DBG_CACHE, &ni_dev,
              "[cache - DRAIN] %s packet entry. lp=%02x ulp=%02x "
              "slot=%d addr=0x%08x\n",
              que_num == 0 ? "Small" : "Large",
              cio_r_lp(ni.cid, que_num+1, NIQESIZE),
              cio_r_ulp(ni.cid, que_num+1, NIQESIZE),
              slot, addr);

    /* Store the packet into main memory */
    for (i = 0; i < len; i++) {
        pwrite_b(addr + i, rbuf[i]);
    }

    if (ni_dev.dctrl & DBG_DAT) {
        dump_packet("RCV", &ni.rd_buf);
    }

    ni.stats.rx_pkt++;
    ni.stats.rx_bytes += len;

    /* Build a reply CIO message */
    centry.subdevice = 4; /* TODO: Why is it always 4? */
    centry.opcode = 0;
    centry.address = addr + len;
    centry.byte_count = len;
    capp_data[3] = slot;

    /* TODO: We should probably also check status here. */
    cio_cqueue(ni.cid, CIO_STAT, NIQESIZE, &centry, capp_data);

    /* Trigger an interrupt */
    if (cio[ni.cid].ivec > 0) {
        cio[ni.cid].intr = TRUE;
    }
}

t_stat ni_attach(UNIT *uptr, CONST char *cptr)
{
    t_stat status;
    char *tptr;

    sim_debug(DBG_TRACE, &ni_dev, "ni_attach()\n");

    tptr = (char *) malloc(strlen(cptr) + 1);
    if (tptr == NULL) {
        return SCPE_MEM;
    }
    strcpy(tptr, cptr);

    ni.eth = (ETH_DEV *) malloc(sizeof(ETH_DEV));
    if (!ni.eth) {
        free(tptr);
        return SCPE_MEM;
    }

    status = eth_open(ni.eth, cptr, &ni_dev, DBG_ETH);
    if (status != SCPE_OK) {
        sim_debug(DBG_ERR, &ni_dev, "ni_attach failure: open\n");
        free(tptr);
        free(ni.eth);
        ni.eth = NULL;
        return status;
    }

    status = eth_check_address_conflict(ni.eth, &ni.macs[NI_NIC_MAC]);
    if (status != SCPE_OK) {
        sim_debug(DBG_ERR, &ni_dev, "ni_attach failure: mac check\n");
        eth_close(ni.eth);
        free(tptr);
        free(ni.eth);
        ni.eth = NULL;
        return status;
    }

    /* Ensure the ethernet device is in async mode */
    /* TODO: Determine best latency */
    status = eth_set_async(ni.eth, 1000);
    if (status != SCPE_OK) {
        sim_debug(DBG_ERR, &ni_dev, "ni_attach failure: eth_set_async\n");
        eth_close(ni.eth);
        free(tptr);
        free(ni.eth);
        ni.eth = NULL;
        return status;
    }

    uptr->filename = tptr;
    uptr->flags |= UNIT_ATT;

    eth_filter(ni.eth, ni.filter_count, ni.macs, 0, 0);

    return SCPE_OK;
}

t_stat ni_detach(UNIT *uptr)
{
    sim_debug(DBG_TRACE, &ni_dev, "ni_detach()\n");

    if (uptr->flags & UNIT_ATT) {
        /* TODO: Do we really want to disable here? Or is that ONLY FCF's job? */
        /* ni_disable(); */
        eth_close(ni.eth);
        free(ni.eth);
        ni.eth = NULL;
        free(uptr->filename);
        uptr->filename = NULL;
        uptr->flags &= ~UNIT_ATT;
    }

    return SCPE_OK;
}

t_stat ni_set_stats(UNIT* uptr, int32 val, CONST char* cptr, void* desc)
{
    int init, elements, i;
    uint32 *stats_array;

    UNUSED(uptr);
    UNUSED(val);
    UNUSED(cptr);
    UNUSED(desc);

    if (cptr) {
        init = atoi(cptr);
        stats_array = (uint32 *)&ni.stats;
        elements = sizeof(ni_stat_info) / sizeof(uint32);

        for (i = 0 ; i < elements; i++) {
            stats_array[i] = init;
        }
    } else {
        memset(&ni.stats, 0, sizeof(ni_stat_info));
    }


    return SCPE_OK;
}

t_stat ni_show_stats(FILE* st, UNIT* uptr, int32 val, CONST void* desc)
{
    const char *fmt = "  %-15s%d\n";

    UNUSED(uptr);
    UNUSED(val);
    UNUSED(desc);

    fprintf(st, "NI Ethernet statistics:\n");
    fprintf(st, fmt, "Recv:",          ni.stats.rx_pkt);
    fprintf(st, fmt, "Recv Bytes:",    ni.stats.rx_bytes);
    fprintf(st, fmt, "Xmit:",          ni.stats.tx_pkt);
    fprintf(st, fmt, "Xmit Bytes:",    ni.stats.tx_bytes);
    fprintf(st, fmt, "Xmit Fail:",     ni.stats.tx_fail);

    eth_show_dev(st, ni.eth);

    return SCPE_OK;
}

t_stat ni_show_poll(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    UNUSED(uptr);
    UNUSED(val);
    UNUSED(desc);

    if (ni.poll_rate == NI_QPOLL_FAST) {
        fprintf(st, "polling=fast");
    } else {
        fprintf(st, "polling=slow");
    }

    return SCPE_OK;
}


t_stat ni_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr)
{
    const char help_string[] =
        /****************************************************************************/
        " The Network Interface (NI) 10BASE5 controller is a Common I/O card for\n"
        " the AT&T 3B2 that allows the 3B2 to connect to an Ethernet Local Area\n"
        " Network (LAN).\n"
        "1 Enabling\n"
        " The simulator allows a single NI card to be configured in the first\n"
        " available slot. The NI card is disabled at startup. To use the card\n"
        " you must first enable it with the command:\n"
        "\n"
        "+sim> SET %D ENABLE\n"
        "1 Configuration\n"
        " By default, the card uses a self-assigned MAC address in the AT&T address\n"
        " range (beginning with 80:00:10:03), however, another MAC may be set by\n"
        " using the SET %D MAC command, e.g.:\n"
        "\n"
        "+sim> SET %D MAC=<mac-address>\n"
        "\n"
        " Please note, however, that the %D driver for AT&T System V R3 UNIX\n"
        " always sets a MAC in the AT&T range through a software command.\n"
        "1 Attaching\n"
        " The %D card must be attached to a LAN device to communicate with systems\n"
        " on the LAN.\n"
        "\n"
        " To get a list of available devices on your host platform, use the command:\n"
        "\n"
        "+sim> SHOW ETH\n"
        "\n"
        " After enabling the card, it can be attached to one of the host's\n"
        " Ethernet devices with the ATTACH command. For example, depending on your\n"
        " platform:\n"
        "\n"
        "+sim> ATTACH %D eth0\n"
        "+sim> ATTACH %D en0\n"
        "1 Dependencies\n"
#if defined(_WIN32)
        " The WinPcap package must be installed in order to enable\n"
        " communication with other computers on the local LAN.\n"
        "\n"
        " The WinPcap package is available from http://www.winpcap.org/\n"
#else
        " To build simulators with the ability to communicate to other computers\n"
        " on the local LAN, the libpcap development package must be installed on\n"
        " the system which builds the simulator.\n"
#endif
        "1 Performance\n"
        " The simulated NI device is capable of much faster transfer speeds than\n"
        " the real NI card in a 3B2, which was limited to a 10 Mbit pipe shared\n"
        " between all hosts on the LAN.\n";

    return scp_help(st, dptr, uptr, flag, help_string, cptr);
}

const char *ni_description(DEVICE *dptr)
{
    UNUSED(dptr);

    return "NI 10BASE5 Ethernet controller";
}

/*
 * Useful routines for debugging request and completion queues
 */

static t_stat ni_show_rqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    return ni_show_queue_common(st, uptr, val, desc, TRUE);
}

static t_stat ni_show_cqueue(FILE *st, UNIT *uptr, int32 val, CONST void *desc)
{
    return ni_show_queue_common(st, uptr, val, desc, FALSE);
}

static t_stat ni_show_queue_common(FILE *st, UNIT *uptr, int32 val,
                                   CONST void *desc, t_bool rq)
{
    uint8 cid;
    char *cptr = (char *) desc;
    t_stat result;
    uint32 ptr, size, no_rque, i, j;
    uint16 lp, ulp;
    uint8  op, dev, seq, cmdstat;

    UNUSED(uptr);
    UNUSED(val);

    if (cptr) {
        cid = (uint8) get_uint(cptr, 10, 12, &result);
        if (result != SCPE_OK) {
            return SCPE_ARG;
        }
    } else {
        return SCPE_ARG;
    }

    /* If the card is not sysgen'ed, give up */
    if (cio[cid].sysgen_s != CIO_SYSGEN) {
        fprintf(st, "No card in slot %d, or card has not completed sysgen\n", cid);
        return SCPE_ARG;
    }

    fprintf(st, "---------------------------------------------------------\n");
    fprintf(st, "Sysgen Block:\n");
    fprintf(st, "---------------------------------------------------------\n");
    fprintf(st, "    Request Queue Pointer: 0x%08x\n", cio[cid].rqp);
    fprintf(st, " Completion Queue Pointer: 0x%08x\n", cio[cid].cqp);
    fprintf(st, "       Request Queue Size: 0x%02x\n", cio[cid].rqs);
    fprintf(st, "    Completion Queue Size: 0x%02x\n", cio[cid].cqs);
    fprintf(st, "         Interrupt Vector: %d (0x%02x)\n", cio[cid].ivec, cio[cid].ivec);
    fprintf(st, " Number of Request Queues: %d\n", cio[cid].no_rque);
    fprintf(st, "---------------------------------------------------------\n");

    /* Get the top of the queue */
    if (rq) {
        ptr = cio[cid].rqp;
        size = cio[cid].rqs;
        no_rque = cio[cid].no_rque;
    } else {
        ptr = cio[cid].cqp;
        size = cio[cid].cqs;
        no_rque = 0; /* Not used */
    }

    if (rq) {
        fprintf(st, "Dumping %d Request Queues\n", no_rque);
    } else {
        fprintf(st, "Dumping Completion Queue\n");
    }

    fprintf(st, "---------------------------------------------------------\n");
    fprintf(st, "EXPRESS ENTRY:\n");
    fprintf(st, "    Byte Count: %d\n",     pread_h(ptr));
    fprintf(st, "    Subdevice:  %d\n",     pread_b(ptr + 2));
    fprintf(st, "    Opcode:     0x%02x\n", pread_b(ptr + 3));
    fprintf(st, "    Addr/Data:  0x%08x\n", pread_w(ptr + 4));
    fprintf(st, "    App Data:   0x%08x\n", pread_w(ptr + 8));
    ptr += 12;

    if (rq) {
        for (i = 0; i < no_rque; i++) {
            lp = pread_h(ptr);
            ulp = pread_h(ptr + 2);
            ptr += 4;
            fprintf(st, "---------------------------------------------------------\n");
            fprintf(st, "REQUEST QUEUE %d\n", i);
            fprintf(st, "---------------------------------------------------------\n");
            fprintf(st, "Load Pointer:   0x%04x (%d)\n", lp, (lp / NIQESIZE) + 1);
            fprintf(st, "Unload Pointer: 0x%04x (%d)\n", ulp, (ulp / NIQESIZE) + 1);
            fprintf(st, "---------------------------------------------------------\n");
            for (j = 0; j < size; j++) {
                dev = pread_b(ptr + 2);
                op = pread_b(ptr + 3);
                seq = (dev & 0x40) >> 6;
                cmdstat = (dev & 0x80) >> 7;
                fprintf(st, "REQUEST ENTRY %d (@ 0x%08x)\n", j + 1, ptr);
                fprintf(st, "    Byte Count: 0x%04x\n",      pread_h(ptr));
                fprintf(st, "    Subdevice:  %d\n",          dev & 0x3f);
                fprintf(st, "    Cmd/Stat:   %d\n",          cmdstat);
                fprintf(st, "    Seqbit:     %d\n",          seq);
                fprintf(st, "    Opcode:     0x%02x (%d)\n", op, op);
                fprintf(st, "    Addr/Data:  0x%08x\n",      pread_w(ptr + 4));
                fprintf(st, "    App Data:   0x%08x\n",      pread_w(ptr + 8));
                ptr += 12;
            }
        }
    } else {
        lp = pread_h(ptr);
        ulp = pread_h(ptr + 2);
        ptr += 4;
        fprintf(st, "---------------------------------------------------------\n");
        fprintf(st, "Load Pointer:   0x%04x (%d)\n", lp, (lp / NIQESIZE) + 1);
        fprintf(st, "Unload Pointer: 0x%04x (%d)\n", ulp, (ulp / NIQESIZE) + 1);
        fprintf(st, "---------------------------------------------------------\n");
        for (i = 0; i < size; i++) {
            dev = pread_b(ptr + 2);
            op = pread_b(ptr + 3);
            seq = (dev & 0x40) >> 6;
            cmdstat = (dev & 0x80) >> 7;
            fprintf(st, "COMPLETION ENTRY %d (@ 0x%08x)\n", i + 1, ptr);
            fprintf(st, "    Byte Count: 0x%04x\n",      pread_h(ptr));
            fprintf(st, "    Subdevice:  %d\n",          dev & 0x3f);
            fprintf(st, "    Cmd/Stat:   %d\n",          cmdstat);
            fprintf(st, "    Seqbit:     %d\n",          seq);
            fprintf(st, "    Opcode:     0x%02x (%d)\n", op, op);
            fprintf(st, "    Addr/Data:  0x%08x\n",      pread_w(ptr + 4));
            fprintf(st, "    App Data:   0x%08x\n",      pread_w(ptr + 8));
            ptr += 12;
        }
    }

    return SCPE_OK;
}
