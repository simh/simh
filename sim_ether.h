/* sim_ether.h: OS-dependent network information
  ------------------------------------------------------------------------------

   Copyright (c) 2002-2005, David T. Hittner

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

  01-Mar-12  AGN  Cygwin doesn't have non-blocking pcap I/O pcap (it uses WinPcap)
  17-Nov-11  MP   Added dynamic loading of libpcap on *nix platforms
  30-Oct-11  MP   Added support for vde (Virtual Distributed Ethernet) networking
  18-Apr-11  MP   Fixed race condition with self loopback packets in 
                  multithreaded environments
  09-Dec-10  MP   Added support to determine if network address conflicts exist
  07-Dec-10  MP   Reworked DECnet self detection to the more general approach
                  of loopback self when any Physical Address is being set.
  04-Dec-10  MP   Changed eth_write to do nonblocking writes when 
                  USE_READER_THREAD is defined.
  07-Feb-08  MP   Added eth_show_dev to display ethernet state
  28-Jan-08  MP   Added eth_set_async
  23-Jan-08  MP   Added eth_packet_trace_ex and ethq_destroy
  30-Nov-05  DTH  Added CRC length to packet and more field comments
  04-Feb-04  DTH  Added debugging information
  14-Jan-04  MP   Generalized BSD support issues
  05-Jan-04  DTH  Added eth_mac_scan
  26-Dec-03  DTH  Added ethernet show and queue functions from pdp11_xq
  23-Dec-03  DTH  Added status to packet
  01-Dec-03  DTH  Added reflections, tweaked decnet fix items
  25-Nov-03  DTH  Verified DECNET_FIX, reversed ifdef to mainstream code
  14-Nov-03  DTH  Added #ifdef DECNET_FIX for problematic duplicate detection code
  07-Jun-03  MP   Added WIN32 support for DECNET duplicate address detection.
  05-Jun-03  DTH  Added used to struct eth_packet
  01-Feb-03  MP   Changed some uint8 strings to char* to reflect usage 
  22-Oct-02  DTH  Added all_multicast and promiscuous support
  21-Oct-02  DTH  Corrected copyright again
  16-Oct-02  DTH  Fixed copyright
  08-Oct-02  DTH  Integrated with 2.10-0p4, added variable vector and copyrights
  03-Oct-02  DTH  Beta version of xq/sim_ether released for SIMH 2.09-11
  15-Aug-02  DTH  Started XQ simulation

  ------------------------------------------------------------------------------
*/

#ifndef SIM_ETHER_H
#define SIM_ETHER_H

#include "sim_defs.h"
#include "sim_sock.h"

/* make common BSD code a bit easier to read in this file */
/* OS/X seems to define and compile using one of these BSD types */
#if defined(__NetBSD__) || defined (__OpenBSD__) || defined (__FreeBSD__)
#define xBSD 1
#endif
#if !defined(__FreeBSD__) && !defined(_WIN32) && !defined(VMS) && !defined(__CYGWIN__) && !defined(__APPLE__)
#define USE_SETNONBLOCK 1
#endif

/* cygwin dowsn't have the right features to use the threaded network I/O */
#if defined(__CYGWIN__) || defined(__ZAURUS__) // psco added check for Zaurus platform
#define DONT_USE_READER_THREAD
#endif

#if ((((defined(__sun) || defined(__sun__)) && defined(__i386__)) || defined(__linux)) && !defined(DONT_USE_READER_THREAD))
#define USE_READER_THREAD 1
#endif

#if defined(DONT_USE_READER_THREAD)
#undef USE_READER_THREAD
#endif

/* make common winpcap code a bit easier to read in this file */
#if defined(_WIN32) || defined(VMS) || defined(__CYGWIN__)
#define PCAP_READ_TIMEOUT -1
#else
#define PCAP_READ_TIMEOUT  1
#endif

/* set related values to have correct relationships */
#if defined (USE_READER_THREAD)
#if defined (USE_SETNONBLOCK)
#undef USE_SETNONBLOCK
#endif /* USE_SETNONBLOCK */
#undef PCAP_READ_TIMEOUT
#define PCAP_READ_TIMEOUT 15
#if (!defined (xBSD) && !defined(_WIN32) && !defined(VMS) && !defined(__CYGWIN__)) || defined (HAVE_TAP_NETWORK) || defined (HAVE_VDE_NETWORK)
#define MUST_DO_SELECT 1
#endif
#endif /* USE_READER_THREAD */

/* give priority to USE_NETWORK over USE_SHARED */
#if defined(USE_NETWORK) && defined(USE_SHARED)
#undef USE_SHARED
#endif
/* USE_SHARED only works on Windows or if HAVE_DLOPEN */
#if defined(USE_SHARED) && !defined(_WIN32) && !defined(HAVE_DLOPEN)
#undef USE_SHARED
#endif

/* USE_SHARED implies shared pcap, so force HAVE_PCAP_NETWORK */
#if defined(USE_SHARED) && !defined(HAVE_PCAP_NETWORK)
#define HAVE_PCAP_NETWORK 1
#endif

/*
  USE_BPF is defined to let this code leverage the libpcap/OS kernel provided 
  BPF packet filtering.  This generally will enhance performance.  It may not 
  be available in some environments and/or it may not work correctly, so 
  undefining this will still provide working code here.
*/
#if defined(HAVE_PCAP_NETWORK)
#define USE_BPF 1
#if defined (_WIN32) && !defined (BPF_CONST_STRING)
#define BPF_CONST_STRING 1
#endif
#else
#define DONT_USE_PCAP_FINDALLDEVS 1
#endif

#if defined (USE_READER_THREAD)
#include <pthread.h>
#endif

/* structure declarations */

#define ETH_PROMISC            1                        /* promiscuous mode = true */
#define ETH_TIMEOUT           -1                        /* read timeout in milliseconds (immediate) */
#define ETH_FILTER_MAX        20                        /* maximum address filters */
#define ETH_DEV_NAME_MAX     256                        /* maximum device name size */
#define ETH_DEV_DESC_MAX     256                        /* maximum device description size */
#define ETH_MIN_PACKET        60                        /* minimum ethernet packet size */
#define ETH_MAX_PACKET      1514                        /* maximum ethernet packet size */
#define ETH_MAX_JUMBO_FRAME 65536                       /* maximum ethernet jumbo frame size (or Offload Segment Size) */
#define ETH_MAX_DEVICE        20                        /* maximum ethernet devices */
#define ETH_CRC_SIZE           4                        /* ethernet CRC size */
#define ETH_FRAME_SIZE (ETH_MAX_PACKET+ETH_CRC_SIZE)    /* ethernet maximum frame size */
#define ETH_MIN_JUMBO_FRAME ETH_MAX_PACKET              /* Threshold size for Jumbo Frame Processing */

#define LOOPBACK_SELF_FRAME(phy_mac, msg)                                                     \
    (((msg)[12] == 0x90) && ((msg)[13] == 0x00) &&              /* Ethernet Loopback */       \
     ((msg)[16] == 0x02) && ((msg)[17] == 0x00) &&              /* Forward Function */        \
     ((msg)[24] == 0x01) && ((msg)[25] == 0x00) &&              /* Next Function - Reply */   \
     (memcmp(phy_mac, (msg),    6) == 0) &&                     /* Ethernet Destination */    \
     (memcmp(phy_mac, (msg)+6,  6) == 0) &&                     /* Ethernet Source */         \
     (memcmp(phy_mac, (msg)+18, 6) == 0))                       /* Forward Address */

#define LOOPBACK_PHYSICAL_RESPONSE(dev, msg)                                                    \
    ((dev->have_host_nic_phy_addr) &&                                                           \
     ((msg)[12] == 0x90) && ((msg)[13] == 0x00) &&              /* Ethernet Loopback */         \
     ((msg)[14] == 0x08) && ((msg)[15] == 0x00) &&              /* Skipcount - 8 */             \
     ((msg)[16] == 0x02) && ((msg)[17] == 0x00) &&              /* Last Function - Forward */   \
     ((msg)[24] == 0x01) && ((msg)[25] == 0x00) &&              /* Function - Reply */          \
     (memcmp(dev->host_nic_phy_hw_addr, (msg)+18, 6) == 0) &&   /* Forward Address - Host MAC */\
     (memcmp(dev->host_nic_phy_hw_addr, (msg),    6) == 0) &&   /* Ethernet Source - Host MAC */\
     (memcmp(dev->physical_addr,  (msg)+6,  6) == 0))           /* Ethernet Source */

#define LOOPBACK_PHYSICAL_REFLECTION(dev, msg)                                                  \
    ((dev->have_host_nic_phy_addr) &&                                                           \
     ((msg)[12] == 0x90) && ((msg)[13] == 0x00) &&              /* Ethernet Loopback */         \
     ((msg)[16] == 0x02) && ((msg)[17] == 0x00) &&              /* Forward Function */          \
     ((msg)[24] == 0x01) && ((msg)[25] == 0x00) &&              /* Next Function - Reply */     \
     (memcmp(dev->host_nic_phy_hw_addr, (msg)+6,  6) == 0) &&   /* Ethernet Source - Host MAC */\
     (memcmp(dev->host_nic_phy_hw_addr, (msg)+18, 6) == 0))     /* Forward Address - Host MAC */

#define LOOPBACK_REFLECTION_TEST_PACKET(dev, msg)                                                \
    ((dev->have_host_nic_phy_addr) &&                                                            \
     ((msg)[12] == 0x90) && ((msg)[13] == 0x00) &&             /* Ethernet Loopback */           \
     ((msg)[14] == 0x00) && ((msg)[15] == 0x00) &&             /* Skipcount - 0 */               \
     ((msg)[16] == 0x02) && ((msg)[17] == 0x00) &&             /* Forward Function */            \
     ((msg)[24] == 0x01) && ((msg)[25] == 0x00) &&             /* Next Function - Reply */       \
     ((msg)[00] == 0xFE) && ((msg)[01] == 0xFF) &&             /* Ethernet Destination - Reflection Test MAC */\
     ((msg)[02] == 0xFF) && ((msg)[03] == 0xFF) &&                                               \
     ((msg)[04] == 0xFF) && ((msg)[05] == 0xFE) &&                                               \
     (memcmp(dev->host_nic_phy_hw_addr, (msg)+6,  6) == 0))    /* Ethernet Source - Host MAC */

struct eth_packet {
  uint8   msg[ETH_FRAME_SIZE];                          /* ethernet frame (message) */
  uint8   *oversize;                                    /* oversized frame (message) */
  uint32  len;                                          /* packet length without CRC */
  uint32  used;                                         /* bytes processed (used in packet chaining) */
  int     status;                                       /* transmit/receive status */
  uint32  crc_len;                                      /* packet length with CRC */
};

struct eth_item {
  int                 type;                             /* receive (0=setup, 1=loopback, 2=normal) */
#define ETH_ITM_SETUP    0
#define ETH_ITM_LOOPBACK 1
#define ETH_ITM_NORMAL   2
  struct eth_packet   packet;
};

struct eth_queue {
  int                 max;
  int                 count;
  int                 head;
  int                 tail;
  int                 loss;
  int                 high;
  struct eth_item*    item;
};

struct eth_list {
  char    name[ETH_DEV_NAME_MAX];
  char    desc[ETH_DEV_DESC_MAX];
};

typedef int ETH_BOOL;
typedef unsigned char ETH_MAC[6];
typedef unsigned char ETH_MULTIHASH[8];
typedef struct eth_packet  ETH_PACK;
typedef void (*ETH_PCALLBACK)(int status);
typedef struct eth_list ETH_LIST;
typedef struct eth_queue ETH_QUE;
typedef struct eth_item ETH_ITEM;

struct eth_device {
  char*         name;                                   /* name of ethernet device */
  void*         handle;                                 /* handle of implementation-specific device */
  SOCKET        fd_handle;                              /* fd to kernel device (where needed) */
  char*         bpf_filter;                             /* bpf filter currently in effect */
  int           eth_api;                                /* Designator for which API is being used to move packets */
#define ETH_API_NONE 0                                  /* No API in use yet */
#define ETH_API_PCAP 1                                  /* Pcap API in use */
#define ETH_API_TAP  2                                  /* tun/tap API in use */
#define ETH_API_VDE  3                                  /* VDE API in use */
#define ETH_API_UDP  4                                  /* UDP API in use */
#define ETH_API_NAT  5                                  /* NAT (SLiRP) API in use */
  ETH_PCALLBACK read_callback;                          /* read callback function */
  ETH_PCALLBACK write_callback;                         /* write callback function */
  ETH_PACK*     read_packet;                            /* read packet */
  ETH_MAC       filter_address[ETH_FILTER_MAX];         /* filtering addresses */
  int           addr_count;                             /* count of filtering addresses */
  ETH_BOOL      promiscuous;                            /* promiscuous mode flag */
  ETH_BOOL      all_multicast;                          /* receive all multicast messages */
  ETH_BOOL      hash_filter;                            /* filter using AUTODIN II multicast hash */
  ETH_MULTIHASH hash;                                   /* AUTODIN II multicast hash */
  int32         loopback_self_sent;                     /* loopback packets sent but not seen */
  int32         loopback_self_sent_total;               /* total loopback packets sent */
  int32         loopback_self_rcvd_total;               /* total loopback packets seen */
  ETH_MAC       physical_addr;                          /* physical address of interface */
  int32         have_host_nic_phy_addr;                 /* flag indicating that the host_nic_phy_hw_addr is valid */
  ETH_MAC       host_nic_phy_hw_addr;                   /* MAC address of the attached NIC */
  uint32        jumbo_fragmented;                       /* Giant IPv4 Frames Fragmented */
  uint32        jumbo_dropped;                          /* Giant Frames Dropped */
  uint32        jumbo_truncated;                        /* Giant Frames too big for capture buffer - Dropped */
  uint32        packets_sent;                           /* Total Packets Sent */
  uint32        packets_received;                       /* Total Packets Received */
  uint32        loopback_packets_processed;             /* Total Loopback Packets Processed */
  uint32        transmit_packet_errors;                 /* Total Send Packet Errors */
  uint32        receive_packet_errors;                  /* Total Read Packet Errors */
  int32         error_waiting_threads;                  /* Count of threads currently waiting after an error */
  ETH_BOOL      error_needs_reset;                      /* Flag indicating to force reset */
#define ETH_ERROR_REOPEN_THRESHOLD 10                   /* Attempt ReOpen after 20 send/receive errors */
#define ETH_ERROR_REOPEN_PAUSE 4                        /* Seconds to pause between closing and reopening LAN */
  uint32        error_reopen_count;                     /* Count of ReOpen Attempts */
  DEVICE*       dptr;                                   /* device ethernet is attached to */
  uint32        dbit;                                   /* debugging bit */
  int           reflections;                            /* packet reflections on interface */
  int           need_crc;                               /* device needs CRC (Cyclic Redundancy Check) */
  /* Throttling control parameters: */
  uint32        throttle_time;                          /* ms burst time window */
#define ETH_THROT_DEFAULT_TIME 5                        /* 5ms Default burst time window */
  uint32        throttle_burst;                         /* packets passed with throttle_time which trigger throttling */
#define ETH_THROT_DEFAULT_BURST 4                       /* 4 Packet burst in time window */
  uint32        throttle_delay;                         /* ms to delay when throttling.  0 disables throttling */
#define ETH_THROT_DISABLED_DELAY 0                      /* 0 Delay disables throttling */
#define ETH_THROT_DEFAULT_DELAY 10                      /* 10ms Delay during burst */
  /* Throttling state variables: */
  uint32        throttle_mask;                          /* match test for threshold detection (1 << throttle_burst) - 1 */
  uint32        throttle_events;                        /* keeps track of packet arrival values */
  uint32        throttle_packet_time;                   /* time last packet was transmitted */
  uint32        throttle_count;                         /* Total Throttle Delays */
#if defined (USE_READER_THREAD)
  int           asynch_io;                              /* Asynchronous Interrupt scheduling enabled */
  int           asynch_io_latency;                      /* instructions to delay pending interrupt */
  ETH_QUE       read_queue;
  pthread_mutex_t     lock;
  pthread_t     reader_thread;                          /* Reader Thread Id */
  pthread_t     writer_thread;                          /* Writer Thread Id */
  pthread_mutex_t     writer_lock;
  pthread_mutex_t     self_lock;
  pthread_cond_t      writer_cond;
  struct write_request {
      struct write_request *next;
      ETH_PACK packet;
      } *write_requests;
  int write_queue_peak;
  struct write_request *write_buffers;
  t_stat write_status;
#endif
};

typedef struct eth_device  ETH_DEV;

/* prototype declarations*/

t_stat eth_open   (ETH_DEV* dev, char* name,            /* open ethernet interface */
                   DEVICE* dptr, uint32 dbit);
t_stat eth_close  (ETH_DEV* dev);                       /* close ethernet interface */
t_stat eth_attach_help(FILE *st, DEVICE *dptr, UNIT *uptr, int32 flag, char *cptr);
t_stat eth_write  (ETH_DEV* dev, ETH_PACK* packet,      /* write sychronous packet; */
                   ETH_PCALLBACK routine);              /*  callback when done */
int eth_read      (ETH_DEV* dev, ETH_PACK* packet,      /* read single packet; */
                   ETH_PCALLBACK routine);              /*  callback when done*/
t_stat eth_filter (ETH_DEV* dev, int addr_count,        /* set filter on incoming packets */
                   ETH_MAC* const addresses,
                   ETH_BOOL all_multicast,
                   ETH_BOOL promiscuous);
t_stat eth_filter_hash (ETH_DEV* dev, int addr_count,   /* set filter on incoming packets with AUTODIN II based hash */
                        ETH_MAC* const addresses,
                        ETH_BOOL all_multicast,
                        ETH_BOOL promiscuous,
                        ETH_MULTIHASH* const hash);
t_stat eth_check_address_conflict (ETH_DEV* dev, 
                                   ETH_MAC* const address);
int eth_devices   (int max, ETH_LIST* dev);             /* get ethernet devices on host */
void eth_setcrc   (ETH_DEV* dev, int need_crc);         /* enable/disable CRC mode */
t_stat eth_set_async (ETH_DEV* dev, int latency);       /* set read behavior to be async */
t_stat eth_clr_async (ETH_DEV* dev);                    /* set read behavior to be not async */
t_stat eth_set_throttle (ETH_DEV* dev, uint32 time, uint32 burst, uint32 delay); /* set transmit throttle parameters */
uint32 eth_crc32(uint32 crc, const void* vbuf, size_t len); /* Compute Ethernet Autodin II CRC for buffer */

void eth_packet_trace (ETH_DEV* dev, const uint8 *msg, int len, char* txt); /* trace ethernet packet header+crc */
void eth_packet_trace_ex (ETH_DEV* dev, const uint8 *msg, int len, char* txt, int detail, uint32 reason); /* trace ethernet packet */
t_stat eth_show (FILE* st, UNIT* uptr,                  /* show ethernet devices */
                 int32 val, void* desc);
t_stat eth_show_devices (FILE* st, DEVICE *dptr,        /* show ethernet devices */
                         UNIT* uptr, int32 val, char* desc);
void eth_show_dev (FILE*st, ETH_DEV* dev);              /* show ethernet device state */

void eth_mac_fmt      (ETH_MAC* add, char* buffer);     /* format ethernet mac address */
t_stat eth_mac_scan (ETH_MAC* mac, char* strmac);       /* scan string for mac, put in mac */

t_stat ethq_init (ETH_QUE* que, int max);               /* initialize FIFO queue */
void ethq_clear  (ETH_QUE* que);                        /* clear FIFO queue */
void ethq_remove (ETH_QUE* que);                        /* remove item from FIFO queue */
void ethq_insert (ETH_QUE* que, int32 type,             /* insert item into FIFO queue */
                  ETH_PACK* packet, int32 status);
void ethq_insert_data(ETH_QUE* que, int32 type,         /* insert item into FIFO queue */
                  const uint8 *data, int used, size_t len, 
                  size_t crc_len, const uint8 *crc_data, int32 status);
t_stat ethq_destroy(ETH_QUE* que);                      /* release FIFO queue */

const char *eth_capabilities(void);

#endif                                                  /* _SIM_ETHER_H */
