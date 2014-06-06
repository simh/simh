/* pdp11_xu.h: DEUNA/DELUA ethernet controller information
  ------------------------------------------------------------------------------

   Copyright (c) 2003-2005, David T. Hittner

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

  25-Jan-13  RJ   SELFTEST needs to report the READY state otherwise VMS 3.7 gets fatal controller error
  23-Jan-08  MP   Added debugging support to display packet headers and packet data
  08-Dec-05  DTH  Added load_server, increased UDBSIZE for system ID parameters
  07-Jul-05  RMS  Removed extraneous externs
  05-Jan-04  DTH  Added network statistics
  31-Dec-03  DTH  Added reserved states
  28-Dec-03  DTH  Corrected MODE bitmasks
  23-Dec-03  DTH  Corrected TXR and RXR bitmasks
  03-Dec-03  DTH  Refitted to SIMH v3.0 platform
  05-May-03  DTH  Started XU simulation

  ------------------------------------------------------------------------------
*/

#ifndef PDP11_XU_H
#define PDP11_XU_H


#if defined (VM_PDP10)                                                  /* PDP10 version */
#include "pdp10_defs.h"
#define XU_RDX                    8
#define XU_WID                   16
extern int32 int_req;

#elif defined (VM_VAX)                                                  /* VAX version */
#include "vax_defs.h"
#define XU_RDX                    16
#define XU_WID                    32
extern int32 int_req[IPL_HLVL];

#else                                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#define XU_RDX                     8
#define XU_WID                    16
extern int32 int_req[IPL_HLVL];
#endif                                                  /* VM_PDP10 */

#include "sim_ether.h"

#define XU_QUE_MAX           500                        /* message queue array */
#define XU_FILTER_MAX         12                        /* mac + broadcast + 10 multicast addrs */
#define XU_SERVICE_INTERVAL  100                        /* times per second */
#define XU_ID_TIMER_VAL      540                        /* 9 min * 60 sec */
#define UDBSIZE              200                        /* max size of UDB (in words) */

enum xu_type {XU_T_DEUNA, XU_T_DELUA};

struct xu_setup {
  int               valid;                              /* is the setup block valid? */
  int               promiscuous;                        /* promiscuous mode enabled */
  int               multicast;                          /* enable all multicast addresses */
  int               mac_count;                          /* number of multicast mac addresses */
  ETH_MAC           macs[XU_FILTER_MAX];                /* MAC addresses to respond to */
};

/* Network Statistics -
   some of these will always be zero in the simulated environment,
   since there is no ability for the sim_ether network driver to see
   things like incoming runts, collision tests, babbling, etc.
 */
struct xu_stats {
  uint16 secs;                                          /* seconds since last clear */
  uint32 frecv;                                         /* frames received */
  uint32 mfrecv;                                        /* multicast frames received */
  uint16 rxerf;                                         /* receive error flags */
  uint32 frecve;                                        /* frames received with errors */
  uint32 rbytes;                                        /* data bytes received */
  uint32 mrbytes;                                       /* multicast data bytes received */
  uint16 rlossi;                                        /* received frames lost - internal err */
  uint16 rlossl;                                        /* received frames lost - local buffers */
  uint32 ftrans;                                        /* frames transmitted */
  uint32 mftrans;                                       /* multicast frames transmitted */
  uint32 ftrans3;                                       /* frames transmitted with 3+ tries */
  uint32 ftrans2;                                       /* frames transmitted - two tries */
  uint32 ftransd;                                       /* frames transmitted - deferred */
  uint32 tbytes;                                        /* data bytes transmitted */
  uint32 mtbytes;                                       /* multicast data bytes transmitted */
  uint16 txerf;                                         /* transmit error flags summary */
  uint16 ftransa;                                       /* transmit frames aborted */
  uint16 txccf;                                         /* transmit collision test failure */
  uint16 porterr;                                       /* port driver errors */
  uint16 bablcnt;                                       /* babble counter */
  uint32 loopf;                                         /* loopback frames processed */
};

struct xu_device {
                                                        /*+ initialized values - DO NOT MOVE */
  ETH_PCALLBACK     rcallback;                          /* read callback routine */
  ETH_PCALLBACK     wcallback;                          /* write callback routine */
  ETH_MAC           mac;                                /* MAC address */
  enum xu_type      type;                               /* controller type */
  uint32            throttle_time;                      /* ms burst time window */
  uint32            throttle_burst;                     /* packets passed with throttle_time which trigger throttling */
  uint32            throttle_delay;                     /* ms to delay when throttling.  0 disables throttling */
                                                        /*- initialized values - DO NOT MOVE */

                                                        /* I/O register storage */
  uint32            irq;                                /* interrupt request flag */

                                                        /* buffers, etc. */
  ETH_DEV*          etherface;
  ETH_PACK          read_buffer;
  ETH_PACK          write_buffer;
  ETH_QUE           ReadQ;
  ETH_MAC           load_server;                        /* load server address */
  int               idtmr;                              /* countdown for ID Timer */
  struct xu_setup   setup;
  struct xu_stats   stats;                              /* reportable network statistics */

                                                        /* copied from dec_deuna.h */
  uint16          pcsr0;                                /* primary DEUNA registers */
  uint16          pcsr1;
  uint16          pcsr2;
  uint16          pcsr3;
  uint32          mode;                                 /* mode register */
  uint32          pcbb;                                 /* port command block base */
  uint16          stat;                                 /* extended port status */

  uint32          tdrb;                                 /* transmit desc ring base */
  uint32          telen;                                /* transmit desc ring entry len */
  uint32          trlen;                                /* transmit desc ring length */
  uint32          txnext;                               /* transmit buffer pointer */
  uint32          rdrb;                                 /* receive desc ring base */
  uint32          relen;                                /* receive desc ring entry len */
  uint32          rrlen;                                /* receive desc ring length */
  uint32          rxnext;                               /* receive buffer pointer */

  uint16          pcb[4];                               /* copy of Port Command Block */
  uint16          udb[UDBSIZE];                         /* copy of Unibus Data Block */
  uint16          rxhdr[4];                             /* content of RX ring entry, during wait */
  uint16          txhdr[4];                             /* content of TX ring entry, during xmit */
};

struct xu_controller {
  DEVICE*           dev;                                /* device block */
  UNIT*             unit;                               /* unit block */
  DIB*              dib;                                /* device interface block */
  struct xu_device* var;                                /* controller-specific variables */
};

typedef struct xu_controller CTLR;

/* PCSR0 register definitions */
#define PCSR0_SERI  0100000                             /* <15> Status Error Intr */
#define PCSR0_PCEI  0040000                             /* <14> Port Command Error Intr */
#define PCSR0_RXI   0020000                             /* <13> Receive Interrupt */
#define PCSR0_TXI   0010000                             /* <12> Transmit Interrupt */
#define PCSR0_DNI   0004000                             /* <11> Done Interrupt */
#define PCSR0_RCBI  0002000                             /* <10> Recv Buffer Unavail Intr */
#define PCSR0_FATL  0001000                             /* <09> Fatal Internal Error */
#define PCSR0_USCI  0000400                             /* <08> Unsolicited State Chg Inter */
#define PCSR0_INTR  0000200                             /* <07> Interrupt Summary */
#define PCSR0_INTE  0000100                             /* <06> Interrupt Enable */
#define PCSR0_RSET  0000040                             /* <05> Reset */
#define PCSR0_PCMD  0000017                             /* <03:00> Port Command field */

/* PCSR0 Port Commands */
#define CMD_NOOP       000                              /* No-op */
#define CMD_GETPCBB    001                              /* Get PCB base */
#define CMD_GETCMD     002                              /* Get Command */
#define CMD_SELFTEST   003                              /* Self-test init */
#define CMD_START      004                              /* Start xmit/recv */
#define CMD_BOOT       005                              /* Boot */
#define CMD_RSV06      006                              /* Reserved */
#define CMD_RSV07      007                              /* Reserved */
#define CMD_PDMD       010                              /* Polling Demand */
#define CMD_RSV11      011                              /* Reserved */
#define CMD_RSV12      012                              /* Reserved */
#define CMD_RSV13      013                              /* Reserved */
#define CMD_RSV14      014                              /* Reserved */
#define CMD_RSV15      015                              /* Reserved */
#define CMD_HALT       016                              /* Halt */
#define CMD_STOP       017                              /* Stop */

/* PCSR1 register definitions */
#define PCSR1_XPWR      0100000                         /* <15> Tranceiver power failure */
#define PCSR1_ICAB      0040000                         /* <14> Port/Link cable failure */
#define PCSR1_ECOD      0037400                         /* <13:08> Self-test error code */
#define PCSR1_PCTO      0000200                         /* <07> Port Command Timeout */
#define PCSR1_TYPE      0000160                         /* <06:04> Interface type */
#define PCSR1_STATE     0000017                         /* <03:00> State: */

/* PCSR1 Types */
#define TYPE_DEUNA      (0 << 4)                        /* Controller is a DEUNA */
#define TYPE_DELUA      (1 << 4)                        /* Controller is a DELUA */

/* PCSR1 States */
#define STATE_RESET   000                               /* Reset */
#define STATE_PLOAD   001                               /* Primary Load */
#define STATE_READY   002                               /* Ready */
#define STATE_RUNNING 003                               /* Running */
#define STATE_UHALT   005                               /* UNIBUS Halted */
#define STATE_NHALT   006                               /* NI Halted */
#define STATE_NUHALT  007                               /* NI and UNIBUS Halted */
#define STATE_HALT    010                               /* Halted */
#define STATE_SLOAD       017                           /* Secondary Load */

/* Status register definitions */
#define STAT_ERRS   0100000                             /* <15> error summary */
#define STAT_MERR   0040000                             /* <14> multiple errors */
#define STAT_BABL   0020000                             /* <13> Transmitter on too long [DELUA only] */
#define STAT_CERR   0010000                             /* <12> collision test error */
#define STAT_TMOT   0004000                             /* <11> UNIBUS timeout */
#define STAT_RRNG   0001000                             /* <09> receive ring error */
#define STAT_TRNG   0000400                             /* <08> transmit ring error */
#define STAT_PTCH   0000200                             /* <07> ROM patch */
#define STAT_RRAM   0000100                             /* <06> running from RAM */
#define STAT_RREV   0000077                             /* <05:00> ROM version */

/* Mode definitions */
#define MODE_PROM   0100000                             /* <15> Promiscuous Mode */
#define MODE_ENAL   0040000                             /* <14> Enable All Multicasts */
#define MODE_DRDC   0020000                             /* <13> Disable Data Chaining */
#define MODE_TPAD   0010000                             /* <12> Transmit Msg Pad Enable */
#define MODE_ECT    0004000                             /* <11> Enable Collision Test */
#define MODE_DMNT   0001000                             /* <09> Disable Maint Message */
#define MODE_INTL   0000200                             /* <07> Internal Loopback [DELUA only] */
#define MODE_DTCR   0000010                             /* <03> Disable Transmit CRC */
#define MODE_LOOP   0000004                             /* <02> Internal Loopback Mode */
#define MODE_HDPX   0000001                             /* <00> Half-Duplex Mode */

/* Function Code definitions */
#define FC_NOOP     0000000                             /* no-op */
#define FC_LSM      0000001                             /* Load and Start Microaddress */
#define FC_RDPA     0000002                             /* Read Default Physical Address */
#define FC_RPA      0000004                             /* Read Physical Address */
#define FC_WPA      0000005                             /* Write Physical Address */
#define FC_RMAL     0000006                             /* Read Multicast Address List */
#define FC_WMAL     0000007                             /* Write Multicast Address List */
#define FC_RRF      0000010                             /* Read Ring Format */
#define FC_WRF      0000011                             /* Write Ring Format */
#define FC_RDCTR    0000012                             /* Read Counters */
#define FC_RDCLCTR  0000013                             /* Read and Clear Counters */
#define FC_RMODE    0000014                             /* Read Mode */
#define FC_WMODE    0000015                             /* Write Mode */
#define FC_RSTAT    0000016                             /* Read Status */
#define FC_RCSTAT   0000017                             /* Read and Clear Status */
#define FC_DIM      0000020                             /* Dump Internal Memory */
#define FC_LIM      0000021                             /* Load Internal Memory */
#define FC_RSID     0000022                             /* Read System ID parameters */
#define FC_WSID     0000023                             /* Write System ID parameters */
#define FC_RLSA     0000024                             /* Read Load Server Address */
#define FC_WLSA     0000025                             /* Write Load Server Address */

/* Transmitter Ring definitions */
#define TXR_OWN   0100000                               /* <15> we own it (1) */
#define TXR_ERRS  0040000                               /* <14> error summary */
#define TXR_MTCH  0020000                               /* <13> Station Match */
#define TXR_MORE  0010000                               /* <12> Mult Retries Needed */
#define TXR_ONE   0004000                               /* <11> One Collision */
#define TXR_DEF   0002000                               /* <10> Deferred */
#define TXR_STF   0001000                               /* <09> Start Of Frame */
#define TXR_ENF   0000400                               /* <08> End Of Frame */
#define TXR_BUFL  0100000                               /* <15> Buffer Length Error */
#define TXR_UBTO  0040000                               /* <14> UNIBUS TimeOut */
#define TXR_UFLO  0020000                               /* <13> Underflow Error */
#define TXR_LCOL  0010000                               /* <12> Late Collision */
#define TXR_LCAR  0004000                               /* <11> Lost Carrier */
#define TXR_RTRY  0002000                               /* <10> Retry Failure (16x) */
#define TXR_TDR   0001777                               /* <9:0> TDR value if RTRY=1 */

/* Receiver Ring definitions */
#define RXR_OWN   0100000                               /* <15> we own it (1) */
#define RXR_ERRS  0040000                               /* <14> Error Summary */
#define RXR_FRAM  0020000                               /* <13> Frame Error */
#define RXR_OFLO  0010000                               /* <12> Message Overflow */
#define RXR_CRC   0004000                               /* <11> CRC Check Error */
#define RXR_STF   0001000                               /* <09> Start Of Frame */
#define RXR_ENF   0000400                               /* <08> End Of Frame */
#define RXR_BUFL  0100000                               /* <15> Buffer Length error */
#define RXR_UBTO  0040000                               /* <14> UNIBUS TimeOut */
#define RXR_NCHN  0020000                               /* <13> No Data Chaining */
#define RXR_OVRN  0010000                               /* <12> Overrun Error [DELUA only] */
#define RXR_MLEN  0007777                               /* <11:0> Message Length */

/* debugging bitmaps */
#define DBG_TRC  0x0001                                 /* trace routine calls */
#define DBG_REG  0x0002                                 /* trace read/write registers */
#define DBG_WRN  0x0004                                 /* display warnings */
#define DBG_PCK  0x0080                                 /* display packet headers */
#define DBG_DAT  0x0100                                 /* display packet data */
#define DBG_ETH  0x8000                                 /* debug ethernet device */

#endif                                                  /* _PDP11_XU_H */
