/* 3b2_ni.h: AT&T 3B2 Model 400 "NI" feature card

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

#ifndef _3B2_NI_H_
#define _3B2_NI_H_

#include "3b2_defs.h"
#include "3b2_io.h"
#include "sim_ether.h"

#define NI_ID                  0x0002
#define NI_IPL                 12

/* Opcodes for NI card */

#define NI_SETID               6
#define NI_TURNOFF             7
#define NI_TURNON              8
#define NI_SEND                11
#define NI_RECV                12
#define NI_STATS               13
#define NI_SANITY              15
#define NI_SEND_A              22

#define MAC_SIZE_BYTES         6
#define MAC_SIZE_CHARS         20

#define NIQESIZE               12
#define NI_QUE_MAX             1024
#define NI_INT_DELAY           10000
#define NI_SANITY_INTERVAL_US  5000000

/* Maximum allowed number of multicast addresses */
#define NI_MULTI_MAX           64

/* At least two filter addresses are always configured:
 * 1. The host MAC
 * 2. The broadcast address */
#define NI_FILTER_MIN          2

/* Maximum total allowed number of filter addresses, including the
 * host's MAC and the broadcast address. */
#define NI_FILTER_MAX          NI_MULTI_MAX + NI_FILTER_MIN

/* Indexes in the internal filter address table of the
 * host's MAC and the broadcast address */
#define NI_NIC_MAC             0
#define NI_BCST_MAC            1

/*
 * For performance reasons, there are two modes of polling the receive
 * queues. Initially, polling is VERY aggressive as we race the
 * filling of the receive queues. Once we've taken three jobs from
 * each of the two receive queues, we switch to slow polling,
 * which uses coscheduling.
 */

#define NI_QPOLL_FAST          100
#define NI_QPOLL_SLOW          50000

#define NI_PUMP_CRC1           0xfab1057c
#define NI_PUMP_CRC2           0xf6744bed

#define EIG_TABLE_SIZE         40
#define PKT_HEADER_LEN_OFFSET  EIG_TABLE_SIZE
#define PKT_START_OFFSET       (PKT_HEADER_LEN_OFFSET + 4)

/*
 * The NI card has two request queues for packet receive: One for
 * small packets, and one for large packets. The small queue is meant
 * for packets smaller than 128 bytes. The large queue is meant for
 * packets up to 1500 bytes (no jumbo frames allowed)
 */

#define GE_QUEUE       0         /* General request CIO queue */
#define SM_QUEUE       0         /* Small packet receive queue number */
#define LG_QUEUE       1         /* Large packet receive queue number */
#define SM_PKT_MAX     106       /* Max size of small packets (excluding CRC) */
#define LG_PKT_MAX     1514      /* Max size of large packets (excluding CRC) */

/*
 * NI-specific debugging flags
 */
#define DBG_TRACE 0x01
#define DBG_IO    0x02
#define DBG_CACHE 0x04
#define DBG_DAT   0x08
#define DBG_ERR   0x10
#define DBG_ETH   0x20

#define CHAR(c)   ((((c) >= 0x20) && ((c) < 0x7f)) ? (c) : '.')

#define NI_CACHE_HAS_SPACE(i) (((ni.job_cache[(i)].wp + 1) % NI_CACHE_LEN) != ni.job_cache[(i)].rp)
/* Determine whether both job caches have available slots */
#define NI_BUFFERS_AVAIL      ((ni.job_cache[0].wp != ni.job_cache[0].rp) && \
                               (ni.job_cache[1].wp != ni.job_cache[1].rp))

/*
 * The NI card caches up to three jobs taken from each of the two
 * packet receive queues so that they are available immediately after
 * receipt of a packet. These jobs are kept in small circular buffers.
 * Each job is represented by an ni_rec_job structure, containing a
 * buffer pointer and a slot number. The slot number is used by both
 * the driver and the firmware to correlate a packet receive buffer
 * with a completion queue event.
 */
typedef struct {
    uint32 addr;                   /* address of job's buffer */
    uint8  slot;                   /* slot number of the job  */
} ni_rec_job;

#define NI_CACHE_LEN 4

typedef struct {
    ni_rec_job req[NI_CACHE_LEN];  /* the cache      */
    int        wp;                 /* write pointer  */
    int        rp;                 /* read pointer   */
} ni_job_cache;

/*
 * When the NI driver submits a packet send request to the general
 * request queue, it constructs one or more ni_prot_info structs in
 * main memory that point to the protocol-specific byte data of the
 * packet (minus the Ethernet frame). These structs are packed one
 * after the other following the Ethernet frame header in the job's
 * request buffer. The last entry has its "last" bit set to non-zero.
 */
typedef struct {
    uint32 addr;  /* Physical address of the buffer in system RAM */
    uint16 size;  /* Length of the buffer */
    uint16 last;  /* Is this the last entry in the list? */
} ni_prot_info;

typedef struct {
    uint32 rq_taken;
    uint32 tx_fail;
    uint32 rx_dropped;
    uint32 rx_pkt;
    uint32 tx_pkt;
    uint32 rx_bytes;
    uint32 tx_bytes;
} ni_stat_info;

typedef struct {
    uint8           cid;
    t_bool          initialized;
    t_bool          enabled;
    uint32          crc;
    uint32          poll_rate;
    char            mac_str[MAC_SIZE_CHARS];
    uint8           mac_bytes[MAC_SIZE_BYTES];
    ni_job_cache    job_cache[2];
    ni_prot_info    prot;
    ni_stat_info    stats;
    uint8           fcf_seq;
    ETH_DEV*        eth;
    ETH_PACK        rd_buf;
    ETH_PACK        wr_buf;
    ETH_MAC         macs[NI_FILTER_MAX];    /* List of all filter addresses */
    int             filter_count;           /* Number of filters available */
    ETH_PCALLBACK   callback;
} NI_STATE;

extern DEVICE ni_dev;

void ni_recv_callback(int status);
t_stat ni_reset(DEVICE *dptr);
t_stat ni_rcv_svc(UNIT *uptr);
t_stat ni_sanity_svc(UNIT *uptr);
t_stat ni_rq_svc(UNIT *uptr);
t_stat ni_cio_svc(UNIT *uptr);
t_stat ni_attach(UNIT *uptr, CONST char *cptr);
t_stat ni_detach(UNIT *uptr);
t_stat ni_setmac(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ni_showmac(FILE* st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ni_try_job(uint8 cid);
t_stat ni_show_stats(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ni_set_stats(UNIT *uptr, int32 val, CONST char *cptr, void *desc);
t_stat ni_show_poll(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
t_stat ni_show_filters(FILE *st, UNIT *uptr, int32 val, CONST void *desc);
const char *ni_description(DEVICE *dptr);
t_stat ni_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, const char *cptr);
void ni_cio_reset(uint8 cid);
void ni_process_packet();
void ni_int_ack(uint8 cid);
void ni_sysgen(uint8 cid);
void ni_express(uint8 cid);
void ni_full(uint8 cid);

#endif /* _3B2_NI_H_ */
