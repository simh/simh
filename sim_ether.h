/* sim_ether.h: OS-dependent network information
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

  Modification history:

  22-Oct-02  DTH  Added all_multicast and promiscuous support
  21-Oct-02  DTH  Corrected copyright again
  16-Oct-02  DTH  Fixed copyright
  08-Oct-02  DTH  Integrated with 2.10-0p4, added variable vector and copyrights
  03-Oct-02  DTH  Beta version of xq/sim_ether released for SIMH 2.09-11
  15-Aug-02  DTH  Started XQ simulation

  ------------------------------------------------------------------------------
*/

#ifndef _SIM_ETHER_H
#define _SIM_ETHER_H

#include "sim_defs.h"

/* structure declarations */

#define ETH_PROMISC        1          /* promiscuous mode = true */
#define ETH_TIMEOUT       -1          /* read timeout in milliseconds (immediate) */
#define ETH_FILTER_MAX    20          /* maximum address filters */
#define ETH_BPF_INS_MAX  500          /* maximum bpf instructions */
#define ETH_DEV_NAME_MAX 256          /* maximum device name size */
#define ETH_DEV_DESC_MAX 256          /* maximum device description size */
#define ETH_MIN_PACKET    60          /* minimum ethernet packet size */
#define ETH_MAX_PACKET  1514          /* maximum ethernet packet size */

struct eth_packet {
  uint8   msg[1518];
  int     len;
};

struct eth_list {
  int     num;
  uint8   name[ETH_DEV_NAME_MAX];
  uint8   desc[ETH_DEV_DESC_MAX];
};

typedef int ETH_BOOL;
typedef unsigned char ETH_MAC[6];
typedef struct eth_packet  ETH_PACK;
typedef void (*ETH_PCALLBACK)(int status);
typedef struct eth_list ETH_LIST;

struct eth_device {
  uint8*        name;                           /* name of ethernet device */
  void*         handle;                         /* handle of implementation-specific device */
  ETH_PCALLBACK read_callback;                  /* read callback function */
  ETH_PCALLBACK write_callback;                 /* write callback function */
  ETH_PACK*     read_packet;                    /* read packet */
  ETH_PACK*     write_packet;                   /* write packet */
  ETH_MAC       filter_address[ETH_FILTER_MAX]; /* filtering addresses */
  ETH_BOOL      promiscuous;                    /* promiscuous mode flag */
  ETH_BOOL      all_multicast;                  /* receive all multicast messages */
};

typedef struct eth_device  ETH_DEV;

/* prototype declarations*/

t_stat eth_open   (ETH_DEV* dev, char* name);     /* open ethernet interface */
t_stat eth_close  (ETH_DEV* dev);                 /* close ethernet interface */
t_stat eth_write  (ETH_DEV* dev, ETH_PACK* packet,/* write sychronous packet; */
                   ETH_PCALLBACK routine);        /*  callback when done */
t_stat eth_read   (ETH_DEV* dev, ETH_PACK* packet,/* read single packet; */
                   ETH_PCALLBACK routine);        /*  callback when done*/
t_stat eth_filter (ETH_DEV* dev, int addr_count,  /* set filter on incoming packets */
                   ETH_MAC* addresses,
                   ETH_BOOL all_multicast,
                   ETH_BOOL promiscuous);
int eth_devices   (int max, ETH_LIST* dev);       /* get ethernet devices on host */

void eth_mac_fmt      (ETH_MAC* add, char* buffer);  /* format ethernet mac address */
void eth_packet_trace (ETH_PACK* packet, char* msg); /* trace ethernet packet */

#endif  /* _SIM_ETHER_H */
