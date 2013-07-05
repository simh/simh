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

static void ddcmp_packet_trace (uint32 reason, DEVICE *dptr, const char *txt, const uint8 *msg, int32 len, t_bool detail)
{
if (sim_deb && dptr && (reason & dptr->dctrl)) {
    sim_debug(reason, dptr, "%s  len: %d\n", txt, len);
    if (detail) {
        int i, same, group, sidx, oidx;
        char outbuf[80], strbuf[18];
        static char hex[] = "0123456789ABCDEF";

        switch (msg[0]) {
            case DDCMP_SOH:   /* Data Message */
                sim_debug (reason, dptr, "Data Message, Link: %d, Count: %d, Resp: %d, Num: %d, HDRCRC: %s, DATACRC: %s\n", msg[2]>>6, ((msg[2] & 0x3F) << 8)|msg[1], msg[3], msg[4], 
                                            (0 == ddcmp_crc16 (0, msg, 8)) ? "OK" : "BAD", (0 == ddcmp_crc16 (0, msg+8, 2+(((msg[2] & 0x3F) << 8)|msg[1]))) ? "OK" : "BAD");
                break;
            case DDCMP_ENQ:   /* Control Message */
                sim_debug (reason, dptr, "Control: Type: %d ", msg[1]);
                switch (msg[1]) {
                    case 1: /* ACK */
                        sim_debug (reason, dptr, "(ACK) ACKSUB: %d, Link: %d, Resp: %d\n", msg[2] & 0x3F, msg[2]>>6, msg[3]);
                        break;
                    case 2: /* NAK */
                        sim_debug (reason, dptr, "(NAK) Reason: %d, Link: %d, Resp: %d\n", msg[2] & 0x3F, msg[2]>>6, msg[3]);
                        break;
                    case 3: /* REP */
                        sim_debug (reason, dptr, "(REP) REPSUB: %d, Link: %d, Num: %d\n", msg[2] & 0x3F, msg[2]>>6, msg[4]);
                        break;
                    case 6: /* STRT */
                        sim_debug (reason, dptr, "(STRT) STRTSUB: %d, Link: %d\n", msg[2] & 0x3F, msg[2]>>6);
                        break;
                    case 7: /* STACK */
                        sim_debug (reason, dptr, "(STACK) STCKSUB: %d, Link: %d\n", msg[2] & 0x3F, msg[2]>>6);
                        break;
                    default: /* Unknown */
                        sim_debug (reason, dptr, "(Unknown=0%o)\n", msg[1]);
                        break;
                    }
                if (len != 8) {
                    sim_debug (reason, dptr, "Unexpected Control Message Length: %d expected 8\n", len);
                    }
                if (0 != ddcmp_crc16 (0, msg, len)) {
                    sim_debug (reason, dptr, "Unexpected Message CRC\n");
                    }
                break;
            case DDCMP_DLE:   /* Maintenance Message */
                sim_debug (reason, dptr, "Maintenance Message, Link: %d, Count: %d, HDRCRC: %s, DATACRC: %s\n", msg[2]>>6, ((msg[2] & 0x3F) << 8)| msg[1], 
                                            (0 == ddcmp_crc16 (0, msg, 8)) ? "OK" : "BAD", (0 == ddcmp_crc16 (0, msg+8, 2+(((msg[2] & 0x3F) << 8)| msg[1]))) ? "OK" : "BAD");
                break;
            }
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
       receive break status associated with the character is cleared, and
       SCPE_BREAK is ORed into the return value.
*/

static t_stat ddcmp_tmxr_get_packet_ln (TMLN *lp, const uint8 **pbuf, uint16 *psize)
{
int32 c;
size_t payloadsize;

while (TMXR_VALID & (c = tmxr_getc_ln (lp))) {
    c &= ~TMXR_VALID;
    if (lp->rxpboffset + 1 > lp->rxpbsize) {
        lp->rxpbsize += 512;
        lp->rxpb = (uint8 *)realloc (lp->rxpb, lp->rxpbsize);
        }
    lp->rxpb[lp->rxpboffset] = c;
    if ((lp->rxpboffset == 0) && ((c == DDCMP_SYN) || (c == DDCMP_DEL))) {
        tmxr_debug (TMXR_DBG_PRCV, lp, "Ignoring Interframe Sync Character", (char *)&lp->rxpb[0], 1);
        continue;
        }
    lp->rxpboffset += 1;
    if (lp->rxpboffset == 1) {
        switch (c) {
            default:
                tmxr_debug (TMXR_DBG_PRCV, lp, "Ignoring unexpected byte in DDCMP mode", (char *)&lp->rxpb[0], 1);
                lp->rxpboffset = 0;
            case DDCMP_SOH:
            case DDCMP_ENQ:
            case DDCMP_DLE:
                continue;
            }
        }
    if (lp->rxpboffset >= 8) {
        if (lp->rxpb[0] == DDCMP_ENQ) { /* Control Message? */
            ++lp->rxpcnt;
            *pbuf = lp->rxpb;
            *psize = 8;
            lp->rxpboffset = 0;
            ddcmp_packet_trace (TMXR_DBG_PRCV, lp->mp->dptr, "<<< RCV Packet", lp->rxpb, *psize, TRUE);
            return SCPE_OK;
            }
        payloadsize  = ((lp->rxpb[2] & 0x3F) << 8)| lp->rxpb[1];
        if (lp->rxpboffset >= 10 + payloadsize) {
            ++lp->rxpcnt;
            *pbuf = lp->rxpb;
            *psize = 10 + payloadsize;
            ddcmp_packet_trace (TMXR_DBG_PRCV, lp->mp->dptr, "<<< RCV Packet", lp->rxpb, *psize, TRUE);
            lp->rxpboffset = 0;
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

/* Store packet in line buffer

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
static t_stat ddcmp_tmxr_put_packet_ln (TMLN *lp, const uint8 *buf, size_t size)
{
t_stat r;

if (!lp->conn)
    return SCPE_LOST;
if (lp->txppoffset < lp->txppsize) {
    tmxr_debug (TMXR_DBG_PXMT, lp, "Skipped Sending Packet - Transmit Busy", (char *)&lp->txpb[3], size);
    return SCPE_STALL;
    }
if (lp->txpbsize < size) {
    lp->txpbsize = size;
    lp->txpb = (uint8 *)realloc (lp->txpb, lp->txpbsize);
    }
memcpy (lp->txpb, buf, size);
lp->txppsize = size;
lp->txppoffset = 0;
ddcmp_packet_trace (TMXR_DBG_PXMT, lp->mp->dptr, ">>> XMT Packet", lp->txpb, lp->txppsize, TRUE);
++lp->txpcnt;
while ((lp->txppoffset < lp->txppsize) && 
       (SCPE_OK == (r = tmxr_putc_ln (lp, lp->txpb[lp->txppoffset]))))
   ++lp->txppoffset;
tmxr_send_buffered_data (lp);
return lp->conn ? SCPE_OK : SCPE_LOST;
}


#endif /* PDP11_DDCMP_H_ */
