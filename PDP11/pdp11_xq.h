/* pdp11_xq.h: DEQNA/DELQA ethernet controller information
  ------------------------------------------------------------------------------

   Copyright (c) 2002-2003, David T. Hittner

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

#ifndef _PDP11_XQ_H
#define _PDP11_XQ_H

#if defined (VM_PDP10)					/* PDP10 version */
#error "DEQNA/DELQA not supported on PDP10!"

#elif defined (VM_VAX)					/* VAX version */
#include "vax_defs.h"
#define XQ_RDX		16
#define XQ_WID		32
extern int32 PSL;           /* PSL */
extern int32 fault_PC;      /* fault PC */
extern int32 int_req[IPL_HLVL];
extern int32 int_vec[IPL_HLVL][32];

#else							/* PDP-11 version */
#include "pdp11_defs.h"
#define XQ_RDX		8
#define XQ_WID		16
extern int32 int_req[IPL_HLVL];
extern int32 int_vec[IPL_HLVL][32];
#endif

#include "sim_ether.h"

/* message queue arrays */
#define XQ_QUE_MAX      500
#define XQ_FILTER_MAX    14

enum xq_type {XQ_T_DEQNA, XQ_T_DELQA};

struct xq_sanity {
  int       enabled;        /* sanity timer enabled ? */
  int       quarter_secs;   /* sanity timer value in 1/4 seconds */
  int       countdown;      /* sanity timer countdown in 1/4 seconds */
};

struct xq_id {
  int       enabled;        /* System ID timer enabled ? */
  int       countdown;      /* System ID timer countdown in seconds */
};

struct xq_msg_itm {
  int       type;     /* receive (0=setup, 1=loopback, 2=normal) */
  int32     status;   /* message size */
  ETH_PACK  packet;   /* packet */
};

struct xq_msg_que {
  int                 count;
  int                 head;
  int                 tail;
  int                 loss;
  int                 high;
  struct xq_msg_itm*  item;
};

struct xq_setup {
  int               valid;                /* is the setup block valid? */
  int               promiscuous;          /* promiscuous mode enabled */
  int               multicast;            /* enable all multicast addresses */
  int               l1;                   /* first  diagnostic led state */
  int               l2;                   /* second diagnostic led state */
  int               l3;                   /* third  diagnostic led state */
  int               sanity_timer;         /* sanity timer value (encoded) */
  ETH_MAC           macs[XQ_FILTER_MAX];  /* MAC addresses to respond to */
};

struct xq_stats {
  int               recv;                 /* received packets */
  int               filter;               /* filtered packets */
  int               xmit;                 /* transmitted packets */
  int               fail;                 /* transmit failed */
  int               runt;                 /* runts */
  int               giant;                /* oversize packets */
  int               setup;                /* setup packets */
  int               loop;                 /* loopback packets */
};

struct xq_meb {                           /* MEB block */
  uint8   type;
  uint8   add_lo;
  uint8   add_mi;
  uint8   add_hi;
  uint8   siz_lo;
  uint8   siz_hi;
};

struct xq_device {
  /*+ initialized values - DO NOT MOVE */
  ETH_PCALLBACK     rcallback;             /* read callback routine */
  ETH_PCALLBACK     wcallback;            /* write callback routine */
  ETH_MAC           mac;                  /* MAC address */
  enum xq_type      type;                 /* controller type */
  struct xq_sanity  sanity;               /* sanity timer information */
  struct xq_id      id;                   /* System ID timer information */
  /*- initialized values - DO NOT MOVE */

  /* I/O register storage */
  uint16            addr[6];
  uint16            rbdl[2];
  uint16            xbdl[2];
  uint16            var;
  uint16            csr;

  /* buffers, etc. */
  struct xq_setup   setup;
  struct xq_stats   stats;
  uint8             mac_checksum[2];
  uint16            rbdl_buf[6];
  uint16            xbdl_buf[6];
  uint32            rbdl_ba;
  uint32            xbdl_ba;
  ETH_DEV*          etherface;
  int               receiving;
  ETH_PACK          read_buffer;
  ETH_PACK          write_buffer;
  struct xq_msg_que ReadQ;
};

struct xq_controller {
  DEVICE*           dev;          /* device block */
  UNIT*             unit;         /* unit block */
  DIB*              dib;          /* device interface block */
  struct xq_device* var;          /* controller-specific variables */
};

typedef struct xq_controller CTLR;


#define XQ_CSR_RI 0x8000   /* Receive Interrupt Request     (RI) [RO/W1] */
#define XQ_CSR_PE 0x4000   /* Parity Error in Host Memory   (PE) [RO] */
#define XQ_CSR_CA 0x2000   /* Carrier from Receiver Enabled (CA) [RO] */
#define XQ_CSR_OK 0x1000   /* Ethernet Transceiver Power    (OK) [RO] */
#define XQ_CSR_RR 0x0800   /* Reserved : Set to Zero        (RR) [RO] */
#define XQ_CSR_SE 0x0400   /* Sanity Timer Enable           (SE) [RW] */
#define XQ_CSR_EL 0x0200   /* External Loopback             (EL) [RW] */
#define XQ_CSR_IL 0x0100   /* Internal Loopback             (IL) [RW] */
#define XQ_CSR_XI 0x0080   /* Transmit Interrupt Request    (XI) [RO/W1] */
#define XQ_CSR_IE 0x0040   /* Interrupt Enable              (IE) [RW] */
#define XQ_CSR_RL 0x0020   /* Receive List Invalid/Empty    (RL) [RO] */
#define XQ_CSR_XL 0x0010   /* Transmit List Invalid/Empty   (XL) [RO] */
#define XQ_CSR_BD 0x0008   /* Boot/Diagnostic ROM Load      (BD) [RW] */
#define XQ_CSR_NI 0x0004   /* NonExistant Memory Timeout   (NXM) [RO] */
#define XQ_CSR_SR 0x0002   /* Software Reset                (SR) [RW] */
#define XQ_CSR_RE 0x0001   /* Receiver Enable               (RE) [RW] */

/* special access bitmaps */
#define XQ_CSR_RO 0xF8B4   /* Read-Only bits */
#define XQ_CSR_RW 0x074B   /* Read/Write bits */
#define XQ_CSR_W1 0x8080   /* Write-one-to-clear bits */

#define XQ_VEC_MS 0x8000   /* Mode Select                   (MO) [RW]  */
#define XQ_VEC_OS 0x4000   /* Option Switch Setting         (OS) [RO]  */
#define XQ_VEC_RS 0x2000   /* Request Self-Test             (RS) [RW]  */
#define XQ_VEC_S3 0x1000   /* Self-Test Status              (S3) [RO]  */
#define XQ_VEC_S2 0x0800   /* Self-Test Status              (S2) [RO]  */
#define XQ_VEC_S1 0x0400   /* Self-Test Status              (S1) [RO]  */
#define XQ_VEC_ST 0x1C00   /* Self-Test (S1 + S2 + S3)           [RO]  */
#define XQ_VEC_IV 0x03FC   /* Interrupt Vector              (IV) [RW]  */
#define XQ_VEC_RR 0x0002   /* Reserved                      (RR) [RO]  */
#define XQ_VEC_ID 0x0001   /* Identity Test Bit             (ID) [RW]  */

/* special access bitmaps */
#define XQ_VEC_RO 0x5C02   /* Read-Only bits */
#define XQ_VEC_RW 0xA3FD   /* Read/Write bits */

#define XQ_DSC_V  0x8000    /* Valid bit */
#define XQ_DSC_C  0x4000    /* Chain bit */
#define XQ_DSC_E  0x2000    /* End of Message bit       [Transmit only] */
#define XQ_DSC_S  0x1000    /* Setup bit                [Transmit only] */
#define XQ_DSC_L  0x0080    /* Low Byte Termination bit [Transmit only] */
#define XQ_DSC_H  0x0040    /* High Byte Start bit      [Transmit only] */

#define XQ_SETUP_MC 0x0001  /* multicast bit */
#define XQ_SETUP_PM 0x0002  /* promiscuous bit */
#define XQ_SETUP_LD 0x000C  /* led bits */
#define XQ_SETUP_ST 0x0070  /* sanity timer bits */

#endif /* _PDP11_XQ_H */
