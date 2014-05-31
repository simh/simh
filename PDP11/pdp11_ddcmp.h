/* pdp11_ddcmp.h: Digital Data Communications Message Protocol support

   Copyright (c) 2013, Mark Pizzolato

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
   ROBERT M SUPNIK BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of Robert M Supnik shall not be
   used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from Robert M Supnik.

   Digital Data Communications Message Protocol - DDCMP support routines 

   29-May-13    MP      Initial implementation
*/

#ifndef PDP11_DDCMP_H_
#define PDP11_DDCMP_H_    0

#include "sim_tmxr.h"

/* DDCMP packet types */

#define DDCMP_SYN  0226u    /* Sync character on synchronous links */
#define DDCMP_DEL  0377u    /* Sync character on asynchronous links */
#define DDCMP_SOH  0201u    /* Numbered Data Message Identifier */
#define DDCMP_ENQ  0005u    /* Control Message Identifier */
#define DDCMP_DLE  0220u    /* Maintenance Message Identifier */

#define DDCMP_CTL_ACK    1  /* Control Message ACK Type */
#define DDCMP_CTL_NAK    2  /* Control Message NAK Type */
#define DDCMP_CTL_REP    3  /* Control Message REP Type */
#define DDCMP_CTL_STRT   6  /* Control Message STRT Type */
#define DDCMP_CTL_STACK  7  /* Control Message STACK Type */

#define DDCMP_FLAG_SELECT 0x2  /* Link Select */
#define DDCMP_FLAG_QSYNC  0x1  /* Quick Sync (next message won't abut this message) */

#define DDCMP_CRC_SIZE    2 /* Bytes in DDCMP CRC fields */
#define DDCMP_HEADER_SIZE 8 /* Bytes in DDCMP Control and Data Message headers (including header CRC) */

#define DDCMP_RESP_OFFSET 3 /* Byte offset of response (ack) number field */
#define DDCMP_NUM_OFFSET  4 /* Byte offset of packet number field */

#define DDCMP_PACKET_TIMEOUT 4  /* Seconds before sending REP command for unacknowledged packets */

#define DDCMP_DBG_PXMT  TMXR_DBG_PXMT   /* Debug Transmitted Packet Header Contents */
#define DDCMP_DBG_PRCV  TMXR_DBG_PRCV   /* Debug Received Packet Header Contents */
#define DDCMP_DBG_PDAT  0x4000000       /* Debug Packet Data */

/* Support routines */

/* crc16 polynomial x^16 + x^15 + x^2 + 1 (0xA001) CCITT LSB */
static uint16 crc16_nibble[16] = {
    0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
    0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400,
    };

static uint16 ddcmp_crc16(uint16 crc, const void* vbuf, size_t len)
{
const unsigned char* buf = (const unsigned char*)vbuf;

while(0 != len--) {
    crc = (crc>>4) ^ crc16_nibble[(*buf ^ crc) & 0xF];
    crc = (crc>>4) ^ crc16_nibble[((*buf++)>>4 ^ crc) & 0xF];
    };
return(crc);
}

/* Debug routines */

#include <ctype.h>

static void ddcmp_packet_trace (uint32 reason, DEVICE *dptr, const char *txt, const uint8 *msg, int32 len)
{
if (sim_deb && dptr && (reason & dptr->dctrl)) {
    int i, same, group, sidx, oidx;
    char outbuf[80], strbuf[18];
    static const char hex[] = "0123456789ABCDEF";
    static const char *const flags [4] = { "..", ".Q", "S.", "SQ" };
    static const char *const nak[18] = { "", " (HCRC)", " (DCRC)", " (REPREPLY)", /* 0-3 */
                                         "", "", "", "",                          /* 4-7 */
                                         " (NOBUF)", " (RXOVR)", "", "",          /* 8-11 */
                                         "", "", "", "",                          /* 12-15 */
                                         " (TOOLONG)", " (HDRFMT)" };             /* 16-17 */
    const char *flag = flags[msg[2]>>6];
    int msg2 = msg[2] & 0x3F;

    sim_debug(reason, dptr, "%s  len: %d\n", txt, len);
    switch (msg[0]) {
        case DDCMP_SOH:   /* Data Message */
            sim_debug (reason, dptr, "Data Message, Count: %d, Num: %d, Flags: %s, Resp: %d, HDRCRC: %s, DATACRC: %s\n", (msg2 << 8)|msg[1], msg[4], flag, msg[3], 
                                        (0 == ddcmp_crc16 (0, msg, 8)) ? "OK" : "BAD", (0 == ddcmp_crc16 (0, msg+8, 2+((msg2 << 8)|msg[1]))) ? "OK" : "BAD");
            break;
        case DDCMP_ENQ:   /* Control Message */
            sim_debug (reason, dptr, "Control: Type: %d ", msg[1]);
            switch (msg[1]) {
                case DDCMP_CTL_ACK: /* ACK */
                    sim_debug (reason, dptr, "(ACK) ACKSUB: %d, Flags: %s, Resp: %d\n", msg2, flag, msg[3]);
                    break;
                case DDCMP_CTL_NAK: /* NAK */
                    sim_debug (reason, dptr, "(NAK) Reason: %d%s, Flags: %s, Resp: %d\n", msg2, ((msg2 > 17)? "": nak[msg2]), flag, msg[3]);
                    break;
                case DDCMP_CTL_REP: /* REP */
                    sim_debug (reason, dptr, "(REP) REPSUB: %d, Num: %d, Flags: %s\n", msg2, msg[4], flag);
                    break;
                case DDCMP_CTL_STRT: /* STRT */
                    sim_debug (reason, dptr, "(STRT) STRTSUB: %d, Flags: %s\n", msg2, flag);
                    break;
                case DDCMP_CTL_STACK: /* STACK */
                    sim_debug (reason, dptr, "(STACK) STCKSUB: %d, Flags: %s\n", msg2, flag);
                    break;
                default: /* Unknown */
                    sim_debug (reason, dptr, "(Unknown=0%o)\n", msg[1]);
                    break;
                }
            if (len != DDCMP_HEADER_SIZE) {
                sim_debug (reason, dptr, "Unexpected Control Message Length: %d expected %d\n", len, DDCMP_HEADER_SIZE);
                }
            if (0 != ddcmp_crc16 (0, msg, len)) {
                sim_debug (reason, dptr, "Unexpected Message CRC\n");
                }
            break;
        case DDCMP_DLE:   /* Maintenance Message */
            sim_debug (reason, dptr, "Maintenance Message, Count: %d, Flags: %s, HDRCRC: %s, DATACRC: %s\n", (msg2 << 8)| msg[1], flag, 
                                        (0 == ddcmp_crc16 (0, msg, DDCMP_HEADER_SIZE)) ? "OK" : "BAD", (0 == ddcmp_crc16 (0, msg+DDCMP_HEADER_SIZE, 2+((msg2 << 8)| msg[1]))) ? "OK" : "BAD");
            break;
        }
    if (DDCMP_DBG_PDAT & dptr->dctrl) {
        for (i=same=0; i<len; i += 16) {
            if ((i > 0) && (0 == memcmp(&msg[i], &msg[i-16], 16))) {
                ++same;
                continue;
                }
            if (same > 0) {
                sim_debug(reason, dptr, "%04X thru %04X same as above\n", i-(16*same), i-1);
                same = 0;
                }
            group = (((len - i) > 16) ? 16 : (len - i));
            for (sidx=oidx=0; sidx<group; ++sidx) {
                outbuf[oidx++] = ' ';
                outbuf[oidx++] = hex[(msg[i+sidx]>>4)&0xf];
                outbuf[oidx++] = hex[msg[i+sidx]&0xf];
                if (isprint(msg[i+sidx]))
                    strbuf[sidx] = msg[i+sidx];
                else
                    strbuf[sidx] = '.';
                }
            outbuf[oidx] = '\0';
            strbuf[sidx] = '\0';
            sim_debug(reason, dptr, "%04X%-48s %s\n", i, outbuf, strbuf);
            }
        if (same > 0) {
            sim_debug(reason, dptr, "%04X thru %04X same as above\n", i-(16*same), len-1);
            }
        }
    }
}

uint16 ddcmp_crc16(uint16 crc, const void* vbuf, size_t len);

/* Data corruption troll, which simulates imperfect links.
 */

/* Evaluate the corruption troll's appetite.
 *
 * A message can be eaten (dropped), nibbled (corrupted) or spared.
 *
 * The probability of a message not being spared is trollHungerLevel,
 * expressed in milli-gulps - 0.1%.  The troll selects which action
 * to taken on selected messages with equal probability.
 *
 * Nibbled messages' CRCs are changed when possible to simplify
 * identifying them when debugging.  When it's too much work to
 * find the CRC, the first byte of data is changed.  The change
 * is an XOR to make it possible to reconstruct the original data.
 *
 * A particularly unfortunate message can be nibbled by both
 * the transmitter and receiver; thus the troll applies a direction-
 * dependent pattern.
 *
 * Return TRUE if the troll ate the message.
 * Return FALSE if the message was nibbled or spared.
 */
static t_bool ddcmp_feedCorruptionTroll (TMLN *lp, uint8 *msg, t_bool rx, int32 trollHungerLevel)
{
double r, rmax;
char msgbuf[80];

if (trollHungerLevel == 0)
    return FALSE;
#if defined(_POSIX_VERSION) || defined (_XOPEN_VERSION)
r = (double)random();
rmax = (double)0x7fffffff;
#else
r = rand();
rmax = (double)RAND_MAX;
#endif
if (msg[0] == DDCMP_ENQ) {
    int eat =  0 + (int) (2000.0 * (r / (rmax + 1.0)));
    
    if (eat <= (trollHungerLevel * 2)) {    /* Hungry? */
        if (eat <= trollHungerLevel) {      /* Eat the packet */
            sprintf (msgbuf, "troll ate a %s control message\n", rx ? "RCV" : "XMT");
            tmxr_debug_msg (rx ? DDCMP_DBG_PRCV : DDCMP_DBG_PXMT, lp, msgbuf);
            return TRUE;
            }
        sprintf (msgbuf, "troll bit a %s control message\n", rx ? "RCV" : "XMT");
        tmxr_debug_msg (rx ? DDCMP_DBG_PRCV : DDCMP_DBG_PXMT, lp, msgbuf);
        msg[6] ^= rx? 0114: 0154;           /* Eat the CRC */
        }
    } 
else {
    int eat =  0 + (int) (3000.0 * (r / (rmax + 1.0)));

    if (eat <= (trollHungerLevel * 3)) {    /* Hungry? */
        if (eat <= trollHungerLevel) {      /* Eat the packet */
            sprintf (msgbuf, "troll ate a %s %s message\n", rx ? "RCV" : "XMT", (msg[0] == DDCMP_SOH)? "data" : "maintenance");
            tmxr_debug_msg (rx ? DDCMP_DBG_PRCV : DDCMP_DBG_PXMT, lp, msgbuf);
            return TRUE;
            }
        if (eat <= (trollHungerLevel * 2)) { /* HCRC */
            sprintf (msgbuf, "troll bit a %s %s message\n", rx ? "RCV" : "XMT", (msg[0] == DDCMP_SOH)? "data" : "maintenance");
            tmxr_debug_msg (rx ? DDCMP_DBG_PRCV : DDCMP_DBG_PXMT, lp, msgbuf);
            msg[6] ^= rx? 0124: 0164;
            } 
        else {                            /* DCRC */
            sprintf (msgbuf, "troll bit %s %s DCRC\n", (rx? "RCV" : "XMT"), ((msg[0] == DDCMP_SOH)? "data" : "maintenance"));
            tmxr_debug_msg (rx ? DDCMP_DBG_PRCV : DDCMP_DBG_PXMT, lp, msgbuf);
            msg[8] ^= rx? 0114: 0154;       /* Rather than find the CRC, the first data byte will do */
            }
        }
    }
return FALSE;
}

/* Get packet from specific line

   Inputs:
        *lp     =       pointer to terminal line descriptor
        **pbuf  =       pointer to pointer of packet contents
        *psize  =       pointer to packet size

   Output:
        SCPE_LOST       link state lost
        SCPE_OK         Packet returned OR no packet available

   Implementation notes:

    1. If a packet is not yet available, then the pbuf address returned is
       NULL, but success (SCPE_OK) is returned
*/

static t_stat ddcmp_tmxr_get_packet_ln (TMLN *lp, const uint8 **pbuf, uint16 *psize, int32 corruptrate)
{
int32 c;
size_t payloadsize;
char msg[32];

while (TMXR_VALID & (c = tmxr_getc_ln (lp))) {
    c &= ~TMXR_VALID;
    if (lp->rxpboffset + 1 > lp->rxpbsize) {
        lp->rxpbsize += 512;
        lp->rxpb = (uint8 *)realloc (lp->rxpb, lp->rxpbsize);
        }
    lp->rxpb[lp->rxpboffset] = (uint8)c;
    if ((lp->rxpboffset == 0) && ((c == DDCMP_SYN) || (c == DDCMP_DEL))) {
        tmxr_debug (DDCMP_DBG_PRCV, lp, "Ignoring Interframe Sync Character", (char *)&lp->rxpb[0], 1);
        continue;
        }
    lp->rxpboffset += 1;
    if (lp->rxpboffset == 1) {
        switch (c) {
            default:
                tmxr_debug (DDCMP_DBG_PRCV, lp, "Ignoring unexpected byte in DDCMP mode", (char *)&lp->rxpb[0], 1);
                lp->rxpboffset = 0;
            case DDCMP_SOH:
            case DDCMP_ENQ:
            case DDCMP_DLE:
                continue;
            }
        }
    if (lp->rxpboffset >= DDCMP_HEADER_SIZE) {
        if (lp->rxpb[0] == DDCMP_ENQ) { /* Control Message? */
            ++lp->rxpcnt;
            *pbuf = lp->rxpb;
            *psize = DDCMP_HEADER_SIZE;
            lp->rxpboffset = 0;
            if (lp->mp->lines > 1)
                sprintf (msg, "Line%d: <<< RCV Packet", (int)(lp-lp->mp->ldsc));
            else
                strcpy (msg, "<<< RCV Packet");
            ddcmp_packet_trace (DDCMP_DBG_PRCV, lp->mp->dptr, msg, lp->rxpb, *psize);
            if (ddcmp_feedCorruptionTroll (lp, lp->rxpb, TRUE, corruptrate))
                break;
            return SCPE_OK;
            }
        payloadsize  = ((lp->rxpb[2] & 0x3F) << 8)| lp->rxpb[1];
        if (lp->rxpboffset >= 10 + payloadsize) {
            ++lp->rxpcnt;
            *pbuf = lp->rxpb;
            *psize = (uint16)(10 + payloadsize);
            if (lp->mp->lines > 1)
                sprintf (msg, "Line%d: <<< RCV Packet", (int)(lp-lp->mp->ldsc));
            else
                strcpy (msg, "<<< RCV Packet");
            ddcmp_packet_trace (DDCMP_DBG_PRCV, lp->mp->dptr, msg, lp->rxpb, *psize);
            lp->rxpboffset = 0;
            if (ddcmp_feedCorruptionTroll (lp, lp->rxpb, TRUE, corruptrate))
                break;
            return SCPE_OK;
            }
        }
    }
*pbuf = NULL;
*psize = 0;
if (lp->conn)
    return SCPE_OK;
return SCPE_LOST;
}

/* Store packet in line buffer (or store packet in line buffer and add needed CRCs)

   Inputs:
        *lp     =       pointer to line descriptor
        *buf    =       pointer to packet data
        size    =       size of packet

   Outputs:
        status  =       ok, connection lost, or stall

   Implementation notea:

    1. If the line is not connected, SCPE_LOST is returned.
    2. If prior packet transmission still in progress, SCPE_STALL is 
       returned and no packet data is stored.  The caller must retry later.
*/
static t_stat ddcmp_tmxr_put_packet_ln (TMLN *lp, const uint8 *buf, size_t size, int32 corruptrate)
{
t_stat r;
char msg[32];

if (!lp->conn)
    return SCPE_LOST;
if (lp->txppoffset < lp->txppsize) {
    tmxr_debug (DDCMP_DBG_PXMT, lp, "Skipped Sending Packet - Transmit Busy", (char *)&lp->txpb[3], size);
    return SCPE_STALL;
    }
if (lp->txpbsize < size) {
    lp->txpbsize = size;
    lp->txpb = (uint8 *)realloc (lp->txpb, lp->txpbsize);
    }
memcpy (lp->txpb, buf, size);
lp->txppsize = size;
lp->txppoffset = 0;
if (lp->mp->lines > 1)
    sprintf (msg, "Line%d: >>> XMT Packet", (int)(lp-lp->mp->ldsc));
else
    strcpy (msg, ">>> XMT Packet");
ddcmp_packet_trace (DDCMP_DBG_PXMT, lp->mp->dptr, msg, lp->txpb, lp->txppsize);
if (!ddcmp_feedCorruptionTroll (lp, lp->txpb, FALSE, corruptrate)) {
    ++lp->txpcnt;
    while ((lp->txppoffset < lp->txppsize) && 
           (SCPE_OK == (r = tmxr_putc_ln (lp, lp->txpb[lp->txppoffset]))))
       ++lp->txppoffset;
    tmxr_send_buffered_data (lp);
    }
else {/* Packet eaten, so discard it */
    lp->txppoffset = lp->txppsize; /* Act like all data was sent */
    }
return lp->conn ? SCPE_OK : SCPE_LOST;
}

static t_stat ddcmp_tmxr_put_packet_crc_ln (TMLN *lp, uint8 *buf, size_t size, int32 corruptrate)
{
uint16 hdr_crc16 = ddcmp_crc16(0, buf, DDCMP_HEADER_SIZE-DDCMP_CRC_SIZE);

buf[DDCMP_HEADER_SIZE-DDCMP_CRC_SIZE] = hdr_crc16 & 0xFF;
buf[DDCMP_HEADER_SIZE-DDCMP_CRC_SIZE+1] = (hdr_crc16>>8) & 0xFF;
if (size > DDCMP_HEADER_SIZE) {
    uint16 data_crc16 = ddcmp_crc16(0, buf+DDCMP_HEADER_SIZE, size-(DDCMP_HEADER_SIZE+DDCMP_CRC_SIZE));
    buf[size-DDCMP_CRC_SIZE] = data_crc16 & 0xFF;
    buf[size-DDCMP_CRC_SIZE+1] = (data_crc16>>8) & 0xFF;
    }
return ddcmp_tmxr_put_packet_ln (lp, buf, size, corruptrate);
}

static void ddcmp_build_data_packet (uint8 *buf, size_t size, uint8 flags, uint8 sequence, uint8 ack)
{
buf[0] = DDCMP_SOH;
buf[1] = size & 0xFF;
buf[2] = ((size >> 8) & 0x3F) | (flags << 6);
buf[3] = ack;
buf[4] = sequence;
buf[5] = 1;
}

static void ddcmp_build_maintenance_packet (uint8 *buf, size_t size)
{
buf[0] = DDCMP_DLE;
buf[1] = size & 0xFF;
buf[2] = ((size >> 8) & 0x3F) | (DDCMP_FLAG_SELECT|DDCMP_FLAG_QSYNC << 6);
buf[3] = 0;
buf[4] = 0;
buf[5] = 1;
}

static t_stat ddcmp_tmxr_put_data_packet_ln (TMLN *lp, uint8 *buf, size_t size, uint8 flags, uint8 sequence, uint8 ack)
{
ddcmp_build_data_packet (buf, size, flags, sequence, ack);
return ddcmp_tmxr_put_packet_crc_ln (lp, buf, size, 0);
}

static void ddcmp_build_control_packet (uint8 *buf, uint8 type, uint8 subtype, uint8 flags, uint8 sndr, uint8 rcvr)
{
buf[0] = DDCMP_ENQ;                 /* Control Message */
buf[1] = type;                      /* STACK type */
buf[2] = (subtype & 0x3f) | (flags << 6);
                                    /* STACKSUB type and flags */
buf[3] = rcvr;                      /* RCVR */
buf[4] = sndr;                      /* SNDR */
buf[5] = 1;                         /* ADDR */
}

static t_stat ddcmp_tmxr_put_control_packet_ln (TMLN *lp, uint8 *buf, uint8 type, uint8 subtype, uint8 flags, uint8 sndr, uint8 rcvr)
{
ddcmp_build_control_packet (buf, type, subtype, flags, sndr, rcvr);
return ddcmp_tmxr_put_packet_crc_ln (lp, buf, DDCMP_HEADER_SIZE, 0);
}

static void ddcmp_build_ack_packet (uint8 *buf, uint8 ack, uint8 flags)
{
ddcmp_build_control_packet (buf, DDCMP_CTL_ACK, 0, flags, 0, ack);
}

static t_stat ddcmp_tmxr_put_ack_packet_ln (TMLN *lp, uint8 *buf, uint8 ack, uint8 flags)
{
ddcmp_build_ack_packet (buf, ack, flags);
return ddcmp_tmxr_put_packet_crc_ln (lp, buf, DDCMP_HEADER_SIZE, 0);
}

static void ddcmp_build_nak_packet (uint8 *buf, uint8 reason, uint8 nack, uint8 flags)
{
ddcmp_build_control_packet (buf, DDCMP_CTL_NAK, reason, flags, 0, nack);
}

static t_stat ddcmp_tmxr_put_nak_packet_ln (TMLN *lp, uint8 *buf, uint8 reason, uint8 nack, uint8 flags)
{
return ddcmp_tmxr_put_control_packet_ln (lp, buf, DDCMP_CTL_NAK, reason, flags, 0, nack);
}

static void ddcmp_build_rep_packet (uint8 *buf, uint8 ack, uint8 flags)
{
ddcmp_build_control_packet (buf, DDCMP_CTL_REP, 0, flags, ack, 0);
}

static t_stat ddcmp_tmxr_put_rep_packet_ln (TMLN *lp, uint8 *buf, uint8 ack, uint8 flags)
{
return ddcmp_tmxr_put_control_packet_ln (lp, buf, DDCMP_CTL_REP, 0, flags, ack, 0);
}

static void ddcmp_build_start_packet (uint8 *buf)
{
ddcmp_build_control_packet (buf, DDCMP_CTL_STRT, 0, DDCMP_FLAG_SELECT|DDCMP_FLAG_QSYNC, 0, 0);
}

static t_stat ddcmp_tmxr_put_start_packet_ln (TMLN *lp, uint8 *buf)
{
ddcmp_build_start_packet (buf);
return ddcmp_tmxr_put_packet_crc_ln (lp, buf, DDCMP_HEADER_SIZE, 0);
}

static void ddcmp_build_start_ack_packet (uint8 *buf)
{
ddcmp_build_control_packet (buf, DDCMP_CTL_STACK, 0, DDCMP_FLAG_SELECT|DDCMP_FLAG_QSYNC, 0, 0);
}

static t_stat ddcmp_tmxr_put_start_ack_packet_ln (TMLN *lp, uint8 *buf)
{
ddcmp_build_start_ack_packet (buf);
return ddcmp_tmxr_put_packet_crc_ln (lp, buf, DDCMP_HEADER_SIZE, 0);
}

#endif /* PDP11_DDCMP_H_ */
