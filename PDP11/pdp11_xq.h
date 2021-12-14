/* pdp11_xq.h: DEQNA/DELQA ethernet controller information
  ------------------------------------------------------------------------------

   Copyright (c) 2002-2008, David T. Hittner

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

  03-Mar-08  MP   Added DELQA-T (aka DELQA Plus) device emulation support.
  06-Feb-08  MP   Added dropped frame statistics to record when the receiver discards
                  received packets due to the receiver being disabled, or due to the
                  XQ device's packet receive queue being full.  Also removed the 
                  filter statistic counter since there was no code which ever set it.
  29-Jan-08  MP   Dynamically determine the timer polling rate based on the 
                  calibrated tmr_poll and clk_tps values of the simulator.
  23-Jan-08  MP   Added debugging support to display packet headers and packet data
  07-Jul-05  RMS  Removed extraneous externs
  20-Jan-04  DTH  Added new sanity timer and system id timer
  19-Jan-04  DTH  Added XQ_SERVICE_INTERVAL, poll
  09-Jan-04  DTH  Added Boot PDP diagnostic definition, XI/RI combination
  26-Dec-03  DTH  Moved ethernet queue definitions to sim_ether
  25-Nov-03  DTH  Added interrupt request flag
  02-Jun-03  DTH  Added struct xq_stats
  28-May-03  DTH  Made xq_msg_que.item dynamic
  28-May-03  MP   Optimized structures, removed rtime variable
  06-May-03  DTH  Changed 32-bit t_addr to uint32 for v3.0
  28-Apr-03  DTH  Added callbacks for multicontroller identification
  25-Mar-03  DTH  Removed bootrom field - no longer needed; Updated copyright
  15-Jan-03  DTH  Merged Mark Pizzolato's changes into main source
  13-Jan-03  MP   Added countdown for System Id multicast packets
  10-Jan-03  DTH  Added bootrom field
  30-Dec-02  DTH  Added setup valid field
  21-Oct-02  DTH  Corrected copyright again
  15-Oct-02  DTH  Fixed copyright, added sanity timer support
  10-Oct-02  DTH  Added more setup fields and bitmasks
  08-Oct-02  DTH  Integrated with 2.10-0p4, added variable vector and copyrights
  03-Oct-02  DTH  Beta version of xq/sim_ether released for SIMH 2.09-11
  15-Aug-02  DTH  Started XQ simulation

  ------------------------------------------------------------------------------
*/

#ifndef PDP11_XQ_H
#define PDP11_XQ_H

#if defined (VM_PDP10)                                  /* PDP10 version */
#error "DEQNA/DELQA not supported on PDP10!"

#elif defined (VM_VAX)                                  /* VAX version */
#include "vax_defs.h"
#define XQ_RDX          16
#define XQ_WID          32
#define ULTRIX1X ((cpu_idle_mask&VAX_IDLE_ULT1X) && ((cpu_idle_mask & ~VAX_IDLE_ULT1X) == 0))
#else                                                   /* PDP-11 version */
#include "pdp11_defs.h"
#define XQ_RDX          8
#define XQ_WID          16
#define ULTRIX1X 0
#endif

#include "sim_ether.h"

#define XQ_QUE_MAX           500                        /* read queue size in packets */
#define XQ_FILTER_MAX         14                        /* number of filters allowed */
#if defined(SIM_ASYNCH_IO) && defined(USE_READER_THREAD)
#define XQ_SERVICE_INTERVAL  0                          /* polling interval - No Polling with Asynch I/O */
#else
#define XQ_SERVICE_INTERVAL  100                        /* polling interval - X per second */
#endif
#define XQ_SYSTEM_ID_SECS    540                        /* seconds before system ID timer expires */
#define XQ_STARTUP_DELAY      20                        /* instruction delay before receiver starts */
#define XQ_HW_SANITY_SECS    240                        /* seconds before HW sanity timer expires */
#define XQ_MAX_CONTROLLERS     2                        /* maximum controllers allowed */

#define XQ_MAX_RCV_PACKET   1600                        /* Maximum receive packet data */

enum xq_type {XQ_T_DEQNA, XQ_T_DELQA, XQ_T_DELQA_PLUS};

struct xq_sanity {
  int       enabled;                                    /* sanity timer enabled? 2=HW, 1=SW, 0=off */
#define XQ_SAN_HW_SW  2
#define XQ_SAN_ENABLE 1
  int       quarter_secs;                               /* sanity timer value in 1/4 seconds */
  int       timer;                                      /* countdown timer */
};

struct xq_setup {
  int               valid;                              /* is the setup block valid? */
  int               promiscuous;                        /* promiscuous mode enabled */
  int               multicast;                          /* enable all multicast addresses */
  int               l1;                                 /* first  diagnostic led state */
  int               l2;                                 /* second diagnostic led state */
  int               l3;                                 /* third  diagnostic led state */
  int               sanity_timer;                       /* sanity timer value (encoded) */
  ETH_MAC           macs[XQ_FILTER_MAX];                /* MAC addresses to respond to */
};

struct xq_turbo_init_block { /* DELQA-T Initialization Block */
  uint16            mode;
#define XQ_IN_MO_PRO 0x8000               /* Promiscuous Mode */
#define XQ_IN_MO_INT 0x0040               /* Internal Loopback Mode */
#define XQ_IN_MO_DRT 0x0020               /* Disable Retry */
#define XQ_IN_MO_DTC 0x0008               /* Disable Transmit CRC */
#define XQ_IN_MO_LOP 0x0004               /* Loopback */
  ETH_MAC           phys;                 /* Physical MAC Address */
  ETH_MULTIHASH     hash_filter;          /* 64bit LANCE Hash Filter for Multicast Address selection */
  uint16            rdra_l;
  uint16            rdra_h;
  uint16            tdra_l;
  uint16            tdra_h;
  uint16            options;
#define XQ_IN_OP_HIT 0x0002               /* Host Inactivity Timer Enable Flag */
#define XQ_IN_OP_INT 0x0001               /* Interrupt Enable Flag*/
  uint16            vector;               /* Interrupt Vector */
  uint16            hit_timeout;          /* Host Inactivity Timer Timeout Value */
  uint8             bootpassword[6];      /* MOP Console Boot Password */
};

/* DELQA-T Mode - Transmit Buffer Descriptor */
struct transmit_buffer_descriptor {
    uint16         tmd0;
#define XQ_TMD0_ERR1  0x4000    /* Error Summary. The OR of TMD1 (LC0, LCA, and RTR) */
#define XQ_TMD0_MOR   0x1000    /* More than one retry on transmit */
#define XQ_TMD0_ONE   0x0800    /* One retry on transmit */
#define XQ_TMD0_DEF   0x0400    /* Deferral during transmit */
    uint16         tmd1;
#define XQ_TMD1_LCO   0x1000    /* Late collision on transmit - packet not transmitted */
#define XQ_TMD1_LCA   0x0800    /* Loss of carrier on transmit - packet not transmitted */
#define XQ_TMD1_RTR   0x0400    /* Retry error on transmit - packet not transmitted */
#define XQ_TMD1_TDR   0x03FF    /* Time Domain Reflectometry value */
    uint16         tmd2;
#define XQ_TMD2_ERR2  0x8000    /* Error Summary. The OR of TMD2 (BBL, CER, and MIS)  */
#define XQ_TMD2_BBL   0x4000    /* Babble error on transmit */
#define XQ_TMD2_CER   0x2000    /* Collision error on transmit */
#define XQ_TMD2_MIS   0x1000    /* Packet lost on receive */
#define XQ_TMD2_EOR   0x0800    /* End Of Receive Ring Reached */
#define XQ_TMD2_RON   0x0020    /* Receiver On */
#define XQ_TMD2_TON   0x0010    /* Transmitter On */
    uint16         tmd3;
#define XQ_TMD3_OWN   0x8000    /* Ownership field. 0 = DELQA-T, 1 = Host Driver */
#define XQ_TMD3_FOT   0x4000    /* First Of Two flag. 1 = first in chained, 0 = no chain or last in chain */
#define XQ_TMD3_BCT   0x0FFF    /* Byte Count */
    uint16         ladr;        /* Low 16bits of Buffer Address */
    uint16         hadr;        /* Most significant bits of the Buffer Address */
    uint16         hostuse1;
    uint16         hostuse2;
};
#define XQ_TURBO_XM_BCNT  12    /* Transmit Buffer Descriptor Count */

struct receive_buffer_descriptor {
    uint16         rmd0;
#define XQ_RMD0_ERR3  0x4000    /* Error Summary. The OR of FRA, CRC, OFL and BUF */
#define XQ_RMD0_FRA   0x2000    /* Framing error on receive */
#define XQ_RMD0_OFL   0x1000    /* Overflow error on receive (Giant packet) */
#define XQ_RMD0_CRC   0x0800    /* CRC error on receive */
#define XQ_RMD0_BUF   0x0400    /* Internal device buffer error. Part of Giant packet lost */
#define XQ_RMD0_STP   0x0200    /* Start of Packet Flag */
#define XQ_RMD0_ENP   0x0100    /* End of Packet Flag */
    uint16         rmd1;
#define XQ_RMD1_MCNT  0x0FFF    /* Message byte count (including CRC) */
    uint16         rmd2;
#define XQ_RMD2_ERR4  0x8000    /* Error Summary. The OR of RMD2 (RBL, CER, and MIS)  */
#define XQ_RMD2_BBL   0x4000    /* Babble error on transmit */
#define XQ_RMD2_CER   0x2000    /* Collision error on transmit */
#define XQ_RMD2_MIS   0x1000    /* Packet lost on receive */
#define XQ_RMD2_EOR   0x0800    /* End Of Receive Ring Reached */
#define XQ_RMD2_RON   0x0020    /* Receiver On */
#define XQ_RMD2_TON   0x0010    /* Transmitter On */
    uint16         rmd3;
#define XQ_RMD3_OWN   0x8000    /* Ownership field. 0 = DELQA-T, 1 = Host Driver */
    uint16         ladr;        /* Low 16bits of Buffer Address */
    uint16         hadr;        /* Most significant bits of the Buffer Address */
    uint16         hostuse1;
    uint16         hostuse2;
};
#define XQ_TURBO_RC_BCNT  32    /* Receive Buffer Descriptor Count */

struct xq_stats {
  int               recv;                               /* received packets */
  int               dropped;                            /* received packets dropped */
  int               xmit;                               /* transmitted packets */
  int               fail;                               /* transmit failed */
  int               runt;                               /* runts */
  int               reset;                              /* reset count */
  int               giant;                              /* oversize packets */
  int               setup;                              /* setup packets */
  int               loop;                               /* loopback packets */
  int               recv_overrun;                       /* receiver overruns */
};

#pragma pack(2)
struct xq_mop_counters {
  uint16            seconds;            /* Seconds since last zeroed */
  uint32            b_rcvd;             /* Bytes Received */
  uint32            b_xmit;             /* Bytes Transmitted */
  uint32            p_rcvd;             /* Packets Received */
  uint32            p_xmit;             /* Packets Transmitted */
  uint32            mb_rcvd;            /* Multicast Bytes Received */
  uint32            mp_rcvd;            /* Multicast Packets Received */
  uint32            p_x_col1;           /* Packets Transmitted Initially Deferred */
  uint32            p_x_col2;           /* Packets Transmitted after 2 attempts */
  uint32            p_x_col3;           /* Packets Transmitted after 3+ attempts */
  uint16            p_x_fail;           /* Transmit Packets Aborted (Send Failure) */
  uint16            p_x_f_bitmap;       /* Transmit Packets Aborted (Send Failure) Bitmap */
#define XQ_XF_RTRY  0x0001              /* Excessive Collisions */
#define XQ_XF_LCAR  0x0002              /* Loss of Carrier */
#define XQ_XF_MLEN  0x0010              /* Data Block Too Long */
#define XQ_XF_LCOL  0x0020              /* Late Collision */
  uint16            p_r_fail;           /* Packets received with Error (Receive Failure) */
  uint16            p_r_f_bitmap;       /* Packets received with Error (Receive Failure) Bitmap */
#define XQ_RF_CRC   0x0001              /* Block Check Error */
#define XQ_RF_FRAM  0x0002              /* Framing Error */
#define XQ_RF_MLEN  0x0004              /* Message Length Error */
  uint16            h_dest_err;         /* Host Counter - Unrecognized Frame Destination Error */
  uint16            r_p_lost_i;         /* Receive Packet Lost: Internal Buffer Error */
  uint16            r_p_lost_s;         /* Receive Packet Lost: System Buffer Error (Unavailable or Truncated) */
  uint16            h_no_buf;           /* Host Counter - User Buffer Unavailable */
  uint32            mb_xmit;            /* Multicast Bytes Tramsmitted */
  uint16            reserved1;          /*  */
  uint16            reserved2;          /*  */
  uint16            babble;             /* Babble Counter */
};
#pragma pack()

struct xq_meb {                                         /* MEB block */
  uint8   type;
  uint8   add_lo;
  uint8   add_mi;
  uint8   add_hi;
  uint8   siz_lo;
  uint8   siz_hi;
};

struct xq_device {
                                                        /*+ initialized values - DO NOT MOVE */
  ETH_PCALLBACK     rcallback;                          /* read callback routine */
  ETH_PCALLBACK     wcallback;                          /* write callback routine */
  ETH_MAC           mac;                                /* Hardware MAC address */
  enum xq_type      type;                               /* controller type */
  enum xq_type      mode;                               /* controller operating mode */
  uint32            poll;                               /* configured poll ethernet times/sec for receive */
  uint32            coalesce_latency;                   /* microseconds to hold-off interrupts when not polling */
  uint32            coalesce_latency_ticks;             /* instructions in coalesce_latency microseconds */
  struct xq_sanity  sanity;                             /* sanity timer information */
  t_bool            lockmode;                           /* DEQNA-Lock mode */
  uint32            throttle_time;                      /* ms burst time window */
  uint32            throttle_burst;                     /* packets passed with throttle_time which trigger throttling */
  uint32            throttle_delay;                     /* ms to delay when throttling.  0 disables throttling */
  uint32            startup_delay;                      /* instructions to delay when starting the receiver */
                                                        /*- initialized values - DO NOT MOVE */

                                                        /* I/O register storage */

  uint16            rbdl[2];                            /* Receive Buffer Descriptor List */
  uint16            xbdl[2];                            /* Transmit Buffer Descriptor List */
  uint16            var;                                /* Vector Address Register */
  uint16            csr;                                /* Control and Status Register */

  uint16            srr;                                /* Status and Response Register - DELQA-T only */
  uint16            srqr;                               /* Synchronous Request Register - DELQA-T only */
  uint32            iba;                                /* Init Block Address Register - DELQA-T only */
  uint16            icr;                                /* Interrupt Request Register - DELQA-T only */
  uint16            pending_interrupt;                  /* Pending Interrupt - DELQA-T only */
  struct xq_turbo_init_block
                    init;
  struct transmit_buffer_descriptor
                    xring[XQ_TURBO_XM_BCNT];            /* Transmit Buffer Ring */
  uint32            tbindx;                             /* Transmit Buffer Ring Index */
  struct receive_buffer_descriptor
                    rring[XQ_TURBO_RC_BCNT];            /* Receive Buffer Ring */
  uint32            rbindx;                             /* Receive Buffer Ring Index */

  uint32            irq;                                /* interrupt request flag */

                                                        /* buffers, etc. */
  struct xq_setup   setup;
  struct xq_stats   stats;
  uint8             mac_checksum[2];
  uint16            rbdl_buf[6];
  uint16            xbdl_buf[6];
  uint32            rbdl_ba;
  uint32            xbdl_ba;
  ETH_DEV*          etherface;
  ETH_PACK          read_buffer;
  ETH_PACK          write_buffer;
  ETH_QUE           ReadQ;
  int32             idtmr;                              /* countdown for ID Timer */
  uint32            must_poll;                          /* receiver must poll instead of counting on asynch polls */
  t_bool            initialized;                        /* flag for one time initializations */
};

struct xq_controller {
  DEVICE*           dev;                                /* device block */
  UNIT*             unit;                               /* unit block */
  DIB*              dib;                                /* device interface block */
  struct xq_device* var;                                /* controller-specific variables */
};

typedef struct xq_controller CTLR;


#define XQ_CSR_RI 0x8000                                /* Receive Interrupt Request     (RI) [RO/W1] */
#define XQ_CSR_PE 0x4000                                /* Parity Error in Host Memory   (PE) [RO] */
#define XQ_CSR_CA 0x2000                                /* Carrier from Receiver Enabled (CA) [RO] */
#define XQ_CSR_OK 0x1000                                /* Ethernet Transceiver Power    (OK) [RO] */
#define XQ_CSR_RR 0x0800                                /* Reserved : Set to Zero        (RR) [RO] */
#define XQ_CSR_SE 0x0400                                /* Sanity Timer Enable           (SE) [RW] */
#define XQ_CSR_EL 0x0200                                /* External Loopback             (EL) [RW] */
#define XQ_CSR_IL 0x0100                                /* Internal Loopback             (IL) [RW] */
#define XQ_CSR_XI 0x0080                                /* Transmit Interrupt Request    (XI) [RO/W1] */
#define XQ_CSR_IE 0x0040                                /* Interrupt Enable              (IE) [RW] */
#define XQ_CSR_RL 0x0020                                /* Receive List Invalid/Empty    (RL) [RO] */
#define XQ_CSR_XL 0x0010                                /* Transmit List Invalid/Empty   (XL) [RO] */
#define XQ_CSR_BD 0x0008                                /* Boot/Diagnostic ROM Load      (BD) [RW] */
#define XQ_CSR_NI 0x0004                                /* NonExistant Memory Timeout   (NXM) [RO] */
#define XQ_CSR_SR 0x0002                                /* Software Reset                (SR) [RW] */
#define XQ_CSR_RE 0x0001                                /* Receiver Enable               (RE) [RW] */

/* special access bitmaps */
#define XQ_CSR_RO   0xF8B4                              /* Read-Only bits */
#define XQ_CSR_RW   0x074B                              /* Read/Write bits */
#define XQ_CSR_W1   0x8080                              /* Write-one-to-clear bits */
#define XQ_CSR_BP   0x0208                              /* Boot PDP diagnostic ROM */
#define XQ_CSR_XIRI 0X8080                              /* Transmit & Receive Interrupts */

#define XQ_VEC_MS 0x8000                                /* Mode Select                   (MO) [RW]  */
#define XQ_VEC_OS 0x4000                                /* Option Switch Setting         (OS) [RO]  */
#define XQ_VEC_RS 0x2000                                /* Request Self-Test             (RS) [RW]  */
#define XQ_VEC_S3 0x1000                                /* Self-Test Status              (S3) [RO]  */
#define XQ_VEC_S2 0x0800                                /* Self-Test Status              (S2) [RO]  */
#define XQ_VEC_S1 0x0400                                /* Self-Test Status              (S1) [RO]  */
#define XQ_VEC_ST 0x1C00                                /* Self-Test (S1 + S2 + S3)           [RO]  */
#define XQ_VEC_IV 0x03FC                                /* Interrupt Vector              (IV) [RW]  */
#define XQ_VEC_RR 0x0002                                /* Reserved                      (RR) [RO]  */
#define XQ_VEC_ID 0x0001                                /* Identity Test Bit             (ID) [RW]  */

/* special access bitmaps */
#define XQ_VEC_RO 0x5C02                                /* Read-Only bits */
#define XQ_VEC_RW 0xA3FD                                /* Read/Write bits */

/* DEQNA - DELQA Normal Mode Buffer Descriptors */
#define XQ_DSC_V  0x8000                                /* Valid bit */
#define XQ_DSC_C  0x4000                                /* Chain bit */
#define XQ_DSC_E  0x2000                                /* End of Message bit       [Transmit only] */
#define XQ_DSC_S  0x1000                                /* Setup bit                [Transmit only] */
#define XQ_DSC_L  0x0080                                /* Low Byte Termination bit [Transmit only] */
#define XQ_DSC_H  0x0040                                /* High Byte Start bit      [Transmit only] */

/* DEQNA - DELQA Receive Status Word 1 */
#define XQ_RST_UNUSED    0x8000                         /* Unused buffer */
#define XQ_RST_LASTNOT   0xC000                         /* Used but Not Last segment */
#define XQ_RST_LASTERR   0x4000                         /* Used, Last segment, with errors */
#define XQ_RST_LASTNOERR 0x0000                         /* Used, Last segment, without errors */
#define XQ_RST_RUNT      0x4800                         /* Runt packet, internal loopback unsuccessful */
#define XQ_RST_ESETUP    0x2000                         /* Setup packet, internal loopback or external loopback packet */
#define XQ_RST_DISCARD   0x1000                         /* Runt packet, internal loopback unsuccessful */
#define XQ_RST_RUNT      0x4800                         /* Runt packet, internal loopback unsuccessful */
#define XQ_RST_FRAMEERR  0x5006                         /* Framing Error in packet */
#define XQ_RST_CRCERR    0x5002                         /* CRC Error in packet */
#define XQ_RST_OVERFLOW  0x0001                         /* Receiver overflowed, packet(s) lost */

/* DEQNA - DELQA Transmit Status Word 1 */
#define XQ_XMT_UNUSED    0x8000                         /* Unused buffer */
#define XQ_XMT_LASTNOT   0xC000                         /* Used but Not Last segment */
#define XQ_XMT_LASTERR   0x4000                         /* Used, Last segment, with errors */
#define XQ_XMT_LASTNOERR 0x0000                         /* Used, Last segment, without errors */
#define XQ_XMT_LOSS      0x5000                         /* Carrier Loss during transmission error */
#define XQ_XMT_NOCARRIER 0x4800                         /* No Carrier during transmission error */
#define XQ_XMT_STE16     0x0400                         /* Sanity timer enabled with timeout of 4 minutes */
#define XQ_XMT_ABORT     0x4200                         /* Transmission aborted due to excessive collisions */
#define XQ_XMT_FAIL      0x0100                         /* Heartbeat collision check failure */

#define XQ_LONG_PACKET   0x0600                         /* DEQNA Long Packet Limit (1536 bytes) */

/* DEQNA - DELQA Normal Mode Setup Packet Flags */
#define XQ_SETUP_MC 0x0001                              /* multicast bit */
#define XQ_SETUP_PM 0x0002                              /* promiscuous bit */
#define XQ_SETUP_LD 0x000C                              /* led bits */
#define XQ_SETUP_ST 0x0070                              /* sanity timer bits */

/* DELQA-T Mode - Status and Response Register (SRR) */
#define XQ_SRR_FES  0x8000                              /* Fatal Error Summary                [RO]  */
#define XQ_SRR_CHN  0x4000                              /* Chaining Error                     [RO]  */
#define XQ_SRR_NXM  0x1000                              /* Non-Existant Memory Error          [RO]  */
#define XQ_SRR_PAR  0x0800                              /* Parity Error (Qbus)                [RO]  */
#define XQ_SRR_IME  0x0400                              /* Internal Memory Error              [RO]  */
#define XQ_SRR_TBL  0x0200                              /* Transmit Buffer Too Long Error     [RO]  */
#define XQ_SRR_RESP 0x0003                              /* Synchronous Response Field         [RO]  */
#define XQ_SRR_TRBO 0x0001                              /* Select Turbo Response              [RO]  */
#define XQ_SRR_STRT 0x0002                              /* Start Device Response              [RO]  */
#define XQ_SRR_STOP 0x0003                              /* Stop Device Response               [RO]  */

/* DELQA-T Mode - Synchronous Request Register (SRQR) */
#define XQ_SRQR_STRT 0x0002                             /* Start Device Request               [WO]  */
#define XQ_SRQR_STOP 0x0003                             /* Stop Device Request                [WO]  */
#define XQ_SRQR_RW   0x0003                             /* Writable Bits in SRQR              [WO]  */

/* DELQA-T Mode - Asynchronous Request Register (ARQR) */
#define XQ_ARQR_TRQ 0x8000                             /* Transmit Request                    [WO]  */
#define XQ_ARQR_RRQ 0x0080                             /* Receieve Request                    [WO]  */
#define XQ_ARQR_SR  0x0002                             /* Software Reset Request              [WO]  */

/* DELQA-T Mode - Interrupt Control Register (ICR) */
#define XQ_ICR_ENA 0x0001                              /* Interrupt Enabled                   [WO]  */


/* debugging bitmaps */
#define DBG_TRC  0x0001                                 /* trace routine calls */
#define DBG_REG  0x0002                                 /* trace read/write registers */
#define DBG_CSR  0x0004                                 /* watch CSR */
#define DBG_VAR  0x0008                                 /* watch VAR */
#define DBG_WRN  0x0010                                 /* display warnings */
#define DBG_RBL  0x0020                                 /* RBDL issues */
#define DBG_XBL  0x0040                                 /* XBDL issues */
#define DBG_SAN  0x0080                                 /* display sanity timer info */
#define DBG_SET  0x0100                                 /* display setup info */
#define DBG_PCK  0x0200                                 /* display packet headers */
#define DBG_DAT  0x0400                                 /* display packet data */
#define DBG_ETH  0x8000                                 /* debug ethernet device */

#endif                                                  /* _PDP11_XQ_H */
